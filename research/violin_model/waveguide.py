"""Digital waveguide bowed string.

Structure (velocity waves, one sample per step, internal rate fs * oversample)

            bridge side                      nut side
   bridge <-- line_b (round trip beta*P) -- BOW -- line_n (round trip (1-beta)*P) --> nut
   -H(z)                                  junction                                    -1

  * P = fs / f0 is the loop delay in samples.
  * Each delay line holds the round trip between the bow and one end, so the
    wave written towards the bridge comes back beta*P samples later.
  * The bridge reflection -H(z) is a one-pole low-pass with DC gain g, which
    lumps all string and bridge losses:  H(z) = g (1 - a) / (1 - a z^-1).
    g and a are derived from two physical decay times, T60 at DC and T60 at
    f_high, so the string sounds the same at any sample rate.
  * The nut (or stopping finger) is a rigid -1 reflection.
  * Delays use 3rd-order Lagrange interpolation, and the bridge-side delay is
    shortened by the loss filter's phase delay so the loop stays in tune.
    Helmholtz motion is a sawtooth whose period is set by all its harmonics,
    so the compensation uses the filter's phase delay averaged over the
    harmonics with the sawtooth's 1/n weights, not just the delay at f0.
    Compensating at f0 alone leaves bowed notes up to ~4 cents sharp at A6.

The output is the transverse force on the bridge, Z * (v_in - v_reflected),
which drives the body model.
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field

import numpy as np
from numba import njit
from scipy.signal import resample_poly

from .friction import solve_junction

MIN_SEGMENT_DELAY = 2.0  # samples; the Lagrange read needs at least 2

# Bowed tuning stays within +/-2 cents up to E7 only when the string runs at
# ~176 kHz or more (see docs/PHASE1_FINDINGS.md).
MIN_INTERNAL_RATE = 176400.0


def recommended_oversample(fs: float) -> int:
    """Smallest integer factor that runs the string at >= MIN_INTERNAL_RATE."""
    return max(1, int(math.ceil(MIN_INTERNAL_RATE / fs - 1e-9)))


@dataclass
class BowedStringParams:
    fs: float = 48000.0
    oversample: int = 4  # 48 kHz x 4 = 192 kHz, above MIN_INTERNAL_RATE
    impedance: float = 0.197  # kg/s, A string
    mu_s: float = 0.8
    mu_d: float = 0.3
    v0: float = 0.1  # m/s
    t60: float = 1.5  # s, decay time at low frequencies without bowing
    t60_high: float = 0.25  # s, decay time at f_high
    f_high: float = 4000.0  # Hz
    # "harmonic" tunes bowed (sawtooth) motion; "fundamental" tunes free
    # vibration such as pizzicato, where each partial keeps its own pitch.
    tuning: str = "harmonic"

    @property
    def internal_fs(self) -> float:
        return self.fs * self.oversample


@dataclass
class StringRender:
    """Result of a simulation."""

    bridge_force: np.ndarray  # at the output rate fs
    string_velocity: np.ndarray  # at the bow point, internal rate
    sticking: np.ndarray  # bool per internal sample
    internal_fs: float
    fs: float
    params: BowedStringParams = field(repr=False)


@njit(cache=True)
def _lagrange_read(buf: np.ndarray, write_pos: int, delay: float) -> float:
    """Read `delay` samples back from the last written sample + 1.

    A value written at step n is returned at step n + delay (delay >= 2).
    """
    mask = buf.shape[0] - 1
    n0 = int(math.floor(delay)) - 1
    x = delay - n0  # in [1, 2)
    h0 = -(x - 1.0) * (x - 2.0) * (x - 3.0) / 6.0
    h1 = x * (x - 2.0) * (x - 3.0) / 2.0
    h2 = -x * (x - 1.0) * (x - 3.0) / 2.0
    h3 = x * (x - 1.0) * (x - 2.0) / 6.0
    # write_pos is where the next sample will be written; age k lives at write_pos - k.
    s0 = buf[(write_pos - n0) & mask]
    s1 = buf[(write_pos - n0 - 1) & mask]
    s2 = buf[(write_pos - n0 - 2) & mask]
    s3 = buf[(write_pos - n0 - 3) & mask]
    return h0 * s0 + h1 * s1 + h2 * s2 + h3 * s3


@njit(cache=True)
def one_pole_phase_delay(a: float, omega: float) -> float:
    """Phase delay in samples of (1 - a) / (1 - a z^-1) at omega (rad/sample)."""
    if omega <= 0.0:
        return a / (1.0 - a)
    return math.atan2(a * math.sin(omega), 1.0 - a * math.cos(omega)) / omega


@njit(cache=True)
def loss_filter_coefficients(f0: float, fs: float, t60: float, t60_high: float, f_high: float):
    """(g, a) of the one-pole loop filter from two decay times.

    Per trip round the loop (one period) partials must lose
    G = 10^(-3 / (f0 * T60)). g is the DC gain, and a is chosen so that
    |H| at f_high is G_high / G_dc times the DC gain.
    """
    g = 10.0 ** (-3.0 / (f0 * t60))
    if t60_high >= t60:
        return g, 0.0
    r = 10.0 ** (-3.0 / (f0 * t60_high)) / g
    c = math.cos(2.0 * math.pi * min(f_high, 0.45 * fs) / fs)
    # |(1 - a) / (1 - a e^{-jw})| = r  <=>  (r^2 - 1) a^2 + (2 - 2 r^2 c) a + (r^2 - 1) = 0
    qa = r * r - 1.0
    qb = 2.0 - 2.0 * r * r * c
    disc = qb * qb - 4.0 * qa * qa
    if disc < 0.0:
        return g, 0.0
    a = (-qb + math.sqrt(disc)) / (2.0 * qa)
    if a < 0.0 or a >= 1.0:
        a = (-qb - math.sqrt(disc)) / (2.0 * qa)
    return g, min(max(a, 0.0), 0.99)


@njit(cache=True)
def harmonic_phase_delay(a: float, f0: float, fs: float, max_harmonics: int = 40) -> float:
    """Loss-filter phase delay averaged over harmonics below 0.45 fs, weighted 1/n."""
    n_max = min(max_harmonics, max(1, int(0.45 * fs / f0)))
    num = 0.0
    den = 0.0
    for n in range(1, n_max + 1):
        w = 1.0 / n
        num += w * one_pole_phase_delay(a, 2.0 * math.pi * n * f0 / fs)
        den += w
    return num / den


@njit(cache=True)
def _simulate(
    f0: np.ndarray,
    beta: np.ndarray,
    v_bow: np.ndarray,
    f_bow: np.ndarray,
    excitation: np.ndarray,
    fs: float,
    z: float,
    mu_s: float,
    mu_d: float,
    v0: float,
    t60: float,
    t60_high: float,
    f_high: float,
    harmonic_tuning: bool,
    buf_size: int,
):
    n = f0.shape[0]
    buf_b = np.zeros(buf_size)
    buf_n = np.zeros(buf_size)
    mask = buf_size - 1
    wp = 0
    lp = 0.0
    sticking = False
    last_f0 = -1.0
    g = 1.0
    a = 0.0
    tau = 0.0

    out_force = np.zeros(n)
    out_v = np.zeros(n)
    out_stick = np.zeros(n, dtype=np.bool_)

    for i in range(n):
        period = fs / f0[i]
        if f0[i] != last_f0:
            g, a = loss_filter_coefficients(f0[i], fs, t60, t60_high, f_high)
            if harmonic_tuning:
                tau = harmonic_phase_delay(a, f0[i], fs)
            else:
                tau = one_pole_phase_delay(a, 2.0 * math.pi * f0[i] / fs)
            last_f0 = f0[i]

        d_bridge = beta[i] * period - tau
        d_nut = (1.0 - beta[i]) * period
        if d_bridge < 2.0:
            d_bridge = 2.0
        if d_nut < 2.0:
            d_nut = 2.0

        y_b = _lagrange_read(buf_b, wp, d_bridge)
        y_n = _lagrange_read(buf_n, wp, d_nut)

        lp = a * lp + (1.0 - a) * g * y_b
        from_bridge = -lp
        from_nut = -y_n

        v_h = from_bridge + from_nut
        v, sticking = solve_junction(v_bow[i], v_h, f_bow[i], z, mu_s, mu_d, v0, sticking)
        injected = (v - v_h) + excitation[i]

        buf_b[wp] = from_nut + injected
        buf_n[wp] = from_bridge + injected
        wp = (wp + 1) & mask

        out_force[i] = z * (y_b + lp)
        out_v[i] = v + excitation[i]
        out_stick[i] = sticking

    return out_force, out_v, out_stick


def _as_signal(value, n: int) -> np.ndarray:
    arr = np.asarray(value, dtype=np.float64)
    if arr.ndim == 0:
        return np.full(n, float(arr))
    if arr.shape[0] != n:
        raise ValueError(f"control signal has {arr.shape[0]} samples, expected {n}")
    return arr


def _upsample_control(value, n_out: int, oversample: int) -> np.ndarray:
    arr = np.asarray(value, dtype=np.float64)
    if arr.ndim == 0:
        return np.full(n_out * oversample, float(arr))
    return np.repeat(arr, oversample)


def min_beta(f0: float, params: BowedStringParams) -> float:
    """Smallest bow position (fraction from the bridge) the delay lines support at f0."""
    period = params.internal_fs / f0
    _, a = loss_filter_coefficients(f0, params.internal_fs, params.t60, params.t60_high, params.f_high)
    tau = harmonic_phase_delay(a, f0, params.internal_fs)
    return (MIN_SEGMENT_DELAY + tau) / period


def simulate(
    f0,
    beta,
    v_bow,
    f_bow,
    num_samples: int,
    params: BowedStringParams | None = None,
    excitation: np.ndarray | None = None,
) -> StringRender:
    """Run the bowed string.

    f0, beta, v_bow (m/s) and f_bow (N) are scalars or arrays of
    `num_samples` values at the output rate `params.fs`. `excitation` is an
    optional velocity injection at the bow point, given at the internal rate.
    """
    params = params or BowedStringParams()
    os_factor = int(params.oversample)
    n_int = num_samples * os_factor

    f0_i = _upsample_control(f0, num_samples, os_factor)
    beta_i = _upsample_control(beta, num_samples, os_factor)
    v_i = _upsample_control(v_bow, num_samples, os_factor)
    f_i = _upsample_control(f_bow, num_samples, os_factor)
    exc = np.zeros(n_int) if excitation is None else _as_signal(excitation, n_int)

    if params.tuning not in ("harmonic", "fundamental"):
        raise ValueError("tuning must be 'harmonic' or 'fundamental'")
    if np.any(f0_i <= 0.0):
        raise ValueError("f0 must be positive")
    if np.any((beta_i <= 0.0) | (beta_i >= 1.0)):
        raise ValueError("beta must be in (0, 1)")

    max_delay = params.internal_fs / float(np.min(f0_i)) + 8
    buf_size = 1 << int(math.ceil(math.log2(max_delay)))

    force, vel, stick = _simulate(
        f0_i,
        beta_i,
        v_i,
        f_i,
        exc,
        params.internal_fs,
        params.impedance,
        params.mu_s,
        params.mu_d,
        params.v0,
        params.t60,
        params.t60_high,
        params.f_high,
        params.tuning == "harmonic",
        buf_size,
    )

    out = resample_poly(force, 1, os_factor) if os_factor > 1 else force
    return StringRender(out[:num_samples], vel, stick, params.internal_fs, params.fs, params)


def pluck_excitation(num_samples_internal: int, internal_fs: float, amplitude: float = 0.5,
                     width_s: float = 0.0005, start: int = 0) -> np.ndarray:
    """Raised-cosine velocity pulse injected at the pluck point.

    A first approximation of a finger pluck: a short, smooth velocity
    impulse. The width sets the brightness (shorter = brighter).
    """
    width = max(2, int(round(width_s * internal_fs)))
    pulse = amplitude * 0.5 * (1.0 - np.cos(2.0 * np.pi * np.arange(width) / width))
    exc = np.zeros(num_samples_internal)
    end = min(num_samples_internal, start + width)
    exc[start:end] = pulse[: end - start]
    return exc
