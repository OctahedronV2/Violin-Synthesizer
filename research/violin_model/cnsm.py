"""Bridge admittance measurements from the CNSM dataset.

Pauget Ballesteros, H. (2026). CNSM Dataset (1.0.0) [Data set]. Zenodo.
https://doi.org/10.5281/zenodo.18696786 - licensed CC BY 4.0.

The dataset has driving-point bridge admittance Y = v / F (m/s per N) for
three violins (makers Levaggi, Klimke, Stoppani), measured several times in
two phases, sampled up to 25.6 kHz. `scripts/fetch_cnsm.py` downloads it to
research/external/cnsm (not committed).

The body filter used by the synthesiser maps bridge force to radiated sound.
Radiated pressure is approximated here as proportional to bridge
acceleration, j*omega*Y*F. That is exact for the low-frequency monopole
radiation of the body and a reasonable first approximation above it; the
recordings in the same dataset can refine it.
"""

from __future__ import annotations

import math
import os
from dataclasses import dataclass
from pathlib import Path

import numpy as np
from scipy.signal import resample_poly

DEFAULT_ROOT = Path(__file__).resolve().parent.parent / "external" / "cnsm"
VIOLINS = ("Levaggi", "Klimke", "Stoppani")


def dataset_root() -> Path:
    return Path(os.environ.get("CNSM_ROOT", DEFAULT_ROOT))


@dataclass
class Admittance:
    freqs: np.ndarray  # Hz, uniform from 0
    y: np.ndarray  # complex admittance, m/s per N
    violin: str
    phase: int
    measurements: int

    @property
    def sample_rate(self) -> float:
        """Rate of the impulse response implied by the frequency grid."""
        return 2.0 * self.freqs[-1]


def load_measurement(path: Path):
    data = np.loadtxt(path, delimiter=",", skiprows=1)
    return data[:, 0], data[:, 1] + 1j * data[:, 2]


def load_admittance(violin: str, phase: int = 2, root: Path | None = None) -> Admittance:
    """Complex mean of all measurements of one violin in one phase, on a 1.5625 Hz grid."""
    folder = (root or dataset_root()) / "admittances" / f"Phase {phase}" / violin
    paths = sorted(folder.glob("measurement_*.csv"))
    if not paths:
        raise FileNotFoundError(f"no measurements in {folder}; run scripts/fetch_cnsm.py")
    grid = None
    total = None
    for path in paths:
        f, y = load_measurement(path)
        if grid is None:
            grid = np.arange(0.0, f[-1] + 1e-9, 1.5625)
        y_grid = np.interp(grid, f, y.real) + 1j * np.interp(grid, f, y.imag)
        total = y_grid if total is None else total + y_grid
    return Admittance(grid, total / len(paths), violin, phase, len(paths))


def radiation_response(adm: Admittance, f_low: float = 150.0, f_high: float = 10000.0) -> np.ndarray:
    """j*omega*Y, band-limited with raised-cosine tapers outside [f_low, f_high].

    Below ~150 Hz the measurements are noisy and no violin note has energy
    there anyway. Above ~10 kHz the admittance flattens into a measurement
    noise floor, which j*omega would turn into a rising hiss, so the
    response is tapered to zero between f_high and 1.25 * f_high.
    """
    f = adm.freqs
    h = 1j * 2.0 * math.pi * f * adm.y
    taper = np.ones_like(f)
    lo = f < f_low
    taper[lo] = 0.5 - 0.5 * np.cos(np.pi * np.clip((f[lo] - 0.5 * f_low) / (0.5 * f_low), 0.0, 1.0))
    hi = f > f_high
    top = min(f[-1], 1.25 * f_high)
    taper[hi] = 0.5 + 0.5 * np.cos(np.pi * np.clip((f[hi] - f_high) / (top - f_high), 0.0, 1.0))
    return h * taper


def apply_correction(adm: Admittance, h: np.ndarray, correction_freqs: np.ndarray,
                     correction_db: np.ndarray) -> np.ndarray:
    """Multiply a response on the admittance grid by a smooth real gain curve given in dB.

    The curve is interpolated in log frequency and held at its end values.
    """
    f = np.maximum(adm.freqs, 1.0)
    gain_db = np.interp(np.log(f), np.log(correction_freqs), correction_db)
    return h * 10 ** (gain_db / 20)


def body_response(adm: Admittance, correction: tuple[np.ndarray, np.ndarray] | None = None) -> np.ndarray:
    """Force -> radiated sound on the admittance grid.

    Without a correction this is j*omega*Y. With one (frequencies, dB), it is
    Y times the smooth radiation correction derived from recordings of the
    same violin (see scripts/radiation_from_cnsm.py).
    """
    if correction is None:
        return radiation_response(adm)
    band_limited = radiation_response(adm) / np.where(adm.freqs > 0, 1j * 2.0 * math.pi * adm.freqs, 1.0)
    return apply_correction(adm, band_limited, *correction)


def impulse_response(adm: Admittance, fs: float = 48000.0, length_s: float = 0.2,
                     correction: tuple[np.ndarray, np.ndarray] | None = None) -> np.ndarray:
    """Body impulse response (bridge force -> radiated sound) at `fs`, peak-normalised spectrum.

    Built by inverse FFT of the measured, band-limited j*omega*Y, so it keeps
    the measured phase, then resampled from the measurement rate (51.2 kHz)
    to `fs` and faded out. About 95% of the energy arrives in the first
    10 ms; the rest of the raw response is mostly a flat measurement-noise
    floor, which the 0.2 s window (long enough for A0 to decay 60 dB)
    removes.
    """
    h = body_response(adm, correction)
    ir = np.fft.irfft(h)
    rate = adm.sample_rate
    up, down = _ratio(fs, rate)
    ir = resample_poly(ir, up, down)
    n = int(length_s * fs)
    ir = ir[:n]
    fade = int(0.1 * fs)
    ir[-fade:] *= 0.5 + 0.5 * np.cos(np.pi * np.arange(fade) / fade)
    peak = np.max(np.abs(np.fft.rfft(ir, 1 << 16)))
    return ir / peak


def _ratio(fs_out: float, fs_in: float):
    from fractions import Fraction

    frac = Fraction(fs_out / fs_in).limit_denominator(1000)
    return frac.numerator, frac.denominator


class MeasuredBody:
    """Drop-in alternative to BodyModel that convolves with a measured impulse response."""

    def __init__(self, violin: str, phase: int = 2, fs: float = 48000.0, root: Path | None = None,
                 correction: tuple[np.ndarray, np.ndarray] | None = None):
        self.violin = violin
        self.fs = fs
        self.ir = impulse_response(load_admittance(violin, phase, root), fs, correction=correction)

    def process(self, x: np.ndarray, fs: float) -> np.ndarray:
        if fs != self.fs:
            raise ValueError(f"MeasuredBody was built for {self.fs} Hz")
        from scipy.signal import fftconvolve

        return fftconvolve(x, self.ir)[: len(x)]

    def frequency_response(self, fs: float, n: int = 16384):
        spectrum = np.fft.rfft(self.ir, 2 * n)[:n]
        return np.arange(n) * fs / (2 * n), spectrum
