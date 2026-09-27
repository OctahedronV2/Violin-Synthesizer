"""Measurements used to evaluate the model."""

from __future__ import annotations

import math
from dataclasses import asdict, dataclass

import numpy as np
from scipy.signal import resample_poly


def cents(f: float, ref: float) -> float:
    return 1200.0 * math.log2(f / ref)


def estimate_f0(x: np.ndarray, fs: float, f_expected: float, search: float = 0.1) -> float:
    """Fundamental frequency by normalised autocorrelation near the expected period.

    Searches lags within +/- `search` of fs / f_expected and refines the peak
    with parabolic interpolation. Short periods are upsampled first so there
    are at least ~200 samples per period, because parabolic interpolation on
    a coarse autocorrelation is biased by several cents. Accurate to well
    under a cent on steady, periodic signals of a few hundred periods.
    """
    x = np.asarray(x, dtype=np.float64)
    upsample = int(math.ceil(200.0 / (fs / f_expected)))
    if upsample > 1:
        x = resample_poly(x, upsample, 1)
        fs = fs * upsample
    x = x - np.mean(x)
    p = fs / f_expected
    lo = max(2, int(math.floor(p * (1.0 - search))))
    hi = int(math.ceil(p * (1.0 + search))) + 1
    n = len(x) - (hi + 2)
    if n <= hi:
        raise ValueError("signal too short for pitch estimation")

    ref = x[:n]
    ref_energy = np.dot(ref, ref)
    lags = np.arange(lo - 1, hi + 2)
    r = np.empty(len(lags))
    for k, lag in enumerate(lags):
        seg = x[lag : lag + n]
        r[k] = np.dot(ref, seg) / math.sqrt(ref_energy * np.dot(seg, seg) + 1e-30)

    k = int(np.argmax(r[1:-1])) + 1
    denom = r[k - 1] - 2.0 * r[k] + r[k + 1]
    offset = 0.5 * (r[k - 1] - r[k + 1]) / denom if denom != 0.0 else 0.0
    return fs / (lags[k] + offset)


def periodicity(x: np.ndarray, fs: float, f0: float) -> float:
    """Peak normalised autocorrelation near one period (1 = perfectly periodic)."""
    x = np.asarray(x, dtype=np.float64) - np.mean(x)
    p = fs / f0
    best = -1.0
    n = len(x) - int(p * 1.1) - 2
    ref = x[:n]
    for lag in range(max(1, int(p * 0.9)), int(p * 1.1) + 2):
        seg = x[lag : lag + n]
        r = np.dot(ref, seg) / math.sqrt(np.dot(ref, ref) * np.dot(seg, seg) + 1e-30)
        best = max(best, r)
    return float(best)


@dataclass
class MotionStats:
    label: str
    slips_per_period: float  # main slips only
    secondary_slips_per_period: float
    stick_fraction: float
    slip_interval_cv: float
    periodicity: float
    f0_measured: float
    cents_error: float

    def to_dict(self):
        return asdict(self)


def slip_episodes(sticking: np.ndarray):
    """(start, length) of each run of slipping samples that begins after sticking."""
    starts = np.flatnonzero(sticking[:-1] & ~sticking[1:]) + 1
    ends = np.flatnonzero(~sticking[:-1] & sticking[1:]) + 1
    episodes = []
    for s in starts:
        later = ends[ends > s]
        if len(later) == 0:
            break
        episodes.append((int(s), int(later[0] - s)))
    return episodes


def motion_statistics(render, f0: float, beta: float | None = None, start_s: float = 0.25,
                      end_s: float | None = None) -> MotionStats:
    """Classify the string motion at the bow point in the steady part of a render.

    A slip is "main" if it lasts at least half the nominal Helmholtz slip time
    (beta * period); shorter ones are counted as secondary slips. Without
    `beta`, every slip counts as a main slip.

    Labels:
      helmholtz      one regular main slip per period (secondary slips allowed)
      multiple_slip  more than one main slip per period, periodic (typical of too little force)
      subharmonic    fewer than one main slip per period (period doubling / ALF)
      raucous        aperiodic motion, or irregular slipping (typical of too much force)
      silent         no slipping at all
    """
    fs = render.internal_fs
    start = int(start_s * fs)
    end = len(render.sticking) if end_s is None else int(end_s * fs)
    stick = render.sticking[start:end]
    vel = render.string_velocity[start:end]
    period = fs / f0
    if len(stick) < period * 20:
        raise ValueError("steady segment too short")

    episodes = slip_episodes(stick)
    min_main = 0.5 * beta * period if beta is not None else 0.0
    main = [s for s, length in episodes if length >= min_main]
    n_periods = len(stick) / period
    main_rate = len(main) / n_periods
    secondary_rate = (len(episodes) - len(main)) / n_periods
    stick_fraction = float(np.mean(stick))

    if len(main) > 2:
        intervals = np.diff(main)
        cv = float(np.std(intervals) / np.mean(intervals))
    else:
        cv = float("inf")

    per = periodicity(vel, fs, f0)
    try:
        f_meas = estimate_f0(vel, fs, f0)
        err = cents(f_meas, f0)
    except ValueError:
        f_meas, err = float("nan"), float("nan")

    if len(episodes) == 0:
        label = "silent"
    elif per < 0.95:
        label = "raucous"
    elif 0.9 <= main_rate <= 1.1 and cv < 0.1:
        label = "helmholtz"
    elif main_rate > 1.1:
        label = "multiple_slip"
    elif main_rate < 0.9:
        label = "subharmonic"
    else:
        label = "raucous"

    return MotionStats(label, main_rate, secondary_rate, stick_fraction, cv, per, f_meas, err)


def helmholtz_onset_time(render, f0: float, periods_required: int = 10, tolerance: float = 0.05) -> float:
    """Time (s) at which regular one-slip-per-period motion starts, or NaN.

    Onset is the first slip that begins a run of `periods_required` slip
    intervals each within `tolerance` of the nominal period.
    """
    fs = render.internal_fs
    period = fs / f0
    stick = render.sticking
    onsets = np.flatnonzero(stick[:-1] & ~stick[1:]) + 1
    if len(onsets) <= periods_required:
        return float("nan")
    good = np.abs(np.diff(onsets) / period - 1.0) < tolerance
    run = 0
    for i, ok in enumerate(good):
        run = run + 1 if ok else 0
        if run >= periods_required:
            return float(onsets[i - periods_required + 1] / fs)
    return float("nan")


def spectral_centroid(x: np.ndarray, fs: float, f_max: float | None = None) -> float:
    x = np.asarray(x, dtype=np.float64)
    spectrum = np.abs(np.fft.rfft(x * np.hanning(len(x))))
    freqs = np.fft.rfftfreq(len(x), 1.0 / fs)
    if f_max is not None:
        keep = freqs <= f_max
        spectrum, freqs = spectrum[keep], freqs[keep]
    return float(np.sum(freqs * spectrum) / (np.sum(spectrum) + 1e-30))
