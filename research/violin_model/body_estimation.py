"""Estimating the violin body response from recordings of bowed notes.

Idea
----
During Helmholtz motion the string drives the bridge with a sawtooth-like
force whose n-th harmonic has amplitude close to S(n) = 1/n. A recording of
the note is that source filtered by the body (and the room and microphone),
so each harmonic gives one sample of the body's magnitude response:

    |recorded harmonic n of note k|  =  B(n * f0_k) * S(n) * g_k

where g_k is an unknown per-note level (dynamics, bow speed). Many notes
across the range, especially with vibrato sweeping each harmonic back and
forth, sample B densely. B (in dB, on a log-frequency grid) and all g_k are
then solved together by regularised least squares, with iterative
reweighting to ignore outliers such as harmonics the bow position happens
to suppress (n * beta close to an integer).

The estimate is the response from bridge force to the recorded sound: body,
radiation towards the microphone and microphone, which is what the plugin's
body filter should reproduce. Only the magnitude is recovered; use
`minimum_phase_fir` or `fit_modes` to turn it into a filter.

Pipeline
--------
    segments = segment_notes(audio, fs)              # split a recording into notes
    obs = harmonic_observations(segment, fs, f0)     # per-frame harmonic levels
    est = estimate_body([obs, ...])                  # joint solve for B and g_k
    modes = fit_modes(est.freqs, est.response_db)    # parametric modal bank
    fir = minimum_phase_fir(est.freqs, est.response_db, fs)
"""

from __future__ import annotations

import math
from dataclasses import dataclass

import numpy as np
from scipy import sparse
from scipy.optimize import least_squares
from scipy.signal import find_peaks, freqz
from scipy.sparse.linalg import lsqr

from .body import Mode, resonator_coefficients

# --------------------------------------------------------------------------
# Pitch
# --------------------------------------------------------------------------


def yin_f0(frame: np.ndarray, fs: float, f_min: float = 150.0, f_max: float = 3500.0,
           threshold: float = 0.15) -> float:
    """YIN fundamental estimate (de Cheveigné & Kawahara 2002). NaN if unvoiced."""
    frame = np.asarray(frame, dtype=np.float64)
    tau_min = max(2, int(fs / f_max))
    tau_max = min(len(frame) // 2, int(fs / f_min) + 1)
    if tau_max <= tau_min + 2:
        return float("nan")
    w = len(frame) - tau_max
    x0 = frame[:w]
    energy0 = np.dot(x0, x0)
    # Difference function via energies and cross terms.
    d = np.empty(tau_max + 1)
    d[0] = 0.0
    cums = np.concatenate([[0.0], np.cumsum(frame * frame)])
    for tau in range(1, tau_max + 1):
        seg = frame[tau : tau + w]
        d[tau] = energy0 + (cums[tau + w] - cums[tau]) - 2.0 * np.dot(x0, seg)
    cmnd = np.ones_like(d)
    running = np.cumsum(d[1:])
    cmnd[1:] = d[1:] * np.arange(1, tau_max + 1) / np.maximum(running, 1e-30)

    candidates = np.flatnonzero(cmnd[tau_min:tau_max] < threshold)
    if len(candidates) == 0:
        return float("nan")
    tau = candidates[0] + tau_min
    while tau + 1 < tau_max and cmnd[tau + 1] < cmnd[tau]:
        tau += 1
    # Octave check: a strong 2nd harmonic (e.g. boosted by a body resonance)
    # can make half the true period dip below the threshold first. Prefer
    # the doubled period if it fits clearly better.
    while 2 * tau + 1 < tau_max:
        lo, hi = 2 * tau - 2, 2 * tau + 3
        k = lo + int(np.argmin(cmnd[lo:hi]))
        if cmnd[k] < cmnd[tau] - 0.05:
            tau = k
        else:
            break
    if 1 <= tau < tau_max:
        a, b, c = cmnd[tau - 1], cmnd[tau], cmnd[tau + 1]
        denom = a - 2 * b + c
        offset = 0.5 * (a - c) / denom if denom != 0 else 0.0
    else:
        offset = 0.0
    return fs / (tau + offset)


# --------------------------------------------------------------------------
# Note segmentation
# --------------------------------------------------------------------------


@dataclass
class NoteSegment:
    start: int
    end: int
    f0: float  # median fundamental, Hz


def segment_notes(audio: np.ndarray, fs: float, min_note_s: float = 0.3, gap_db: float = -40.0,
                  hop_s: float = 0.01) -> list[NoteSegment]:
    """Split a recording of separated notes (e.g. a chromatic run) at silences."""
    audio = np.asarray(audio, dtype=np.float64)
    if audio.ndim > 1:
        audio = audio.mean(axis=1)
    hop = max(1, int(hop_s * fs))
    frames = len(audio) // hop
    rms = np.sqrt(np.array([np.mean(audio[i * hop : (i + 1) * hop] ** 2) for i in range(frames)]) + 1e-20)
    level = 20 * np.log10(rms / (rms.max() + 1e-20))
    active = level > gap_db

    segments = []
    i = 0
    while i < frames:
        if not active[i]:
            i += 1
            continue
        j = i
        while j < frames and active[j]:
            j += 1
        start, end = i * hop, j * hop
        if (end - start) / fs >= min_note_s:
            seg = audio[start:end]
            win = int(0.08 * fs)
            f0s = [yin_f0(seg[k : k + win], fs) for k in range(0, len(seg) - win, win // 2)]
            f0s = np.array([f for f in f0s if not math.isnan(f)])
            if len(f0s) >= 3:
                segments.append(NoteSegment(start, end, float(np.median(f0s))))
        i = j
    return segments


def segment_by_pitch(audio: np.ndarray, fs: float, frame_s: float = 0.02, min_note_s: float = 0.08,
                     tolerance_cents: float = 60.0, gate_db: float = -35.0) -> list[NoteSegment]:
    """Split continuous playing (scales, phrases) into stretches of stable pitch.

    Frames are pitch-tracked with YIN; runs of voiced frames that stay within
    `tolerance_cents` of their running median become notes. Vibrato within
    the tolerance stays in one note; slides and note changes split.
    """
    audio = np.asarray(audio, dtype=np.float64)
    if audio.ndim > 1:
        audio = audio.mean(axis=1)
    hop = int(frame_s * fs)
    win = 2 * hop
    starts = list(range(0, len(audio) - win, hop))
    rms = np.array([np.sqrt(np.mean(audio[s : s + win] ** 2)) for s in starts])
    if len(rms) == 0:
        return []
    loud = rms > rms.max() * 10 ** (gate_db / 20)
    f0 = np.array([yin_f0(audio[s : s + win], fs) if ok else np.nan for s, ok in zip(starts, loud)])

    segments = []
    run: list[int] = []

    def close_run():
        if len(run) * hop / fs >= min_note_s:
            segments.append(NoteSegment(starts[run[0]], starts[run[-1]] + win, float(np.median(f0[run]))))

    for i, f in enumerate(f0):
        if np.isnan(f):
            close_run()
            run = []
            continue
        if run and abs(1200 * math.log2(f / np.median(f0[run]))) > tolerance_cents:
            close_run()
            run = []
        run.append(i)
    close_run()
    return segments


# --------------------------------------------------------------------------
# Harmonic levels
# --------------------------------------------------------------------------


@dataclass
class HarmonicObservations:
    freqs: np.ndarray  # Hz, measured harmonic frequency
    level_db: np.ndarray  # dB, harmonic amplitude
    harmonic: np.ndarray  # harmonic number n
    f0: float  # nominal fundamental of the note


def harmonic_observations(audio: np.ndarray, fs: float, f0: float, periods_per_frame: float = 8.0,
                          f_max: float = 12000.0, floor_db: float = -60.0,
                          level_gate_db: float = -15.0) -> HarmonicObservations:
    """Per-frame harmonic amplitudes of one sustained note.

    Uses a Hann window of `periods_per_frame` periods, 4x zero padding, and
    parabolic interpolation of the peak near each n * f0 (in dB). Frames more
    than `level_gate_db` below the loudest frame (attack/release) or with an
    unstable pitch are skipped, as are harmonics more than `floor_db` below
    the strongest harmonic in the frame.
    """
    audio = np.asarray(audio, dtype=np.float64)
    if audio.ndim > 1:
        audio = audio.mean(axis=1)
    n_win = int(periods_per_frame * fs / f0)
    hop = n_win // 4
    n_fft = 1 << int(math.ceil(math.log2(4 * n_win)))
    window = np.hanning(n_win)
    win_gain = window.sum() / 2.0  # amplitude of a unit sinusoid at its peak bin

    starts = range(0, len(audio) - n_win, hop)
    frame_rms = np.array([np.sqrt(np.mean(audio[s : s + n_win] ** 2)) for s in starts])
    if len(frame_rms) == 0:
        return HarmonicObservations(np.array([]), np.array([]), np.array([], dtype=int), f0)
    gate = frame_rms.max() * 10 ** (level_gate_db / 20)

    freqs, levels, harmonics = [], [], []
    bin_hz = fs / n_fft
    for s, rms in zip(starts, frame_rms):
        if rms < gate:
            continue
        frame = audio[s : s + n_win]
        f_frame = yin_f0(frame, fs, f_min=f0 / 1.12, f_max=f0 * 1.12)
        if math.isnan(f_frame) or abs(1200 * math.log2(f_frame / f0)) > 100:
            continue
        spectrum = np.abs(np.fft.rfft(frame * window, n_fft)) / win_gain
        spec_db = 20 * np.log10(spectrum + 1e-12)

        n_harm = int(min(f_max, 0.45 * fs) / f_frame)
        frame_levels = []
        for n in range(1, n_harm + 1):
            centre = n * f_frame / bin_hz
            lo = int(max(1, centre - 0.3 * f_frame / bin_hz))
            hi = int(min(len(spec_db) - 2, centre + 0.3 * f_frame / bin_hz))
            if hi <= lo:
                continue
            k = lo + int(np.argmax(spec_db[lo : hi + 1]))
            a, b, c = spec_db[k - 1], spec_db[k], spec_db[k + 1]
            denom = a - 2 * b + c
            offset = 0.5 * (a - c) / denom if denom != 0 else 0.0
            frame_levels.append(((k + offset) * bin_hz, b - 0.25 * (a - c) * offset, n))
        if not frame_levels:
            continue
        top = max(level for _, level, _ in frame_levels)
        for f, level, n in frame_levels:
            if level >= top + floor_db:
                freqs.append(f)
                levels.append(level)
                harmonics.append(n)

    return HarmonicObservations(np.array(freqs), np.array(levels), np.array(harmonics, dtype=int), f0)


# --------------------------------------------------------------------------
# Joint estimation
# --------------------------------------------------------------------------


def sawtooth_source_db(n: np.ndarray, f0: float) -> np.ndarray:
    """Ideal Helmholtz bridge-force spectrum, 1/n, in dB."""
    del f0
    return -20.0 * np.log10(n)


@dataclass
class BodyEstimate:
    freqs: np.ndarray  # log-spaced grid, Hz
    response_db: np.ndarray  # estimated |B| in dB (arbitrary overall level)
    coverage: np.ndarray  # number of observations supporting each grid node
    note_offsets_db: np.ndarray
    residual_db: float  # weighted RMS misfit
    note_residual_db: np.ndarray  # median absolute misfit per note
    rejected_notes: list[int]  # indices of notes left out of the final solve


def estimate_body(observations: list[HarmonicObservations], outlier_sigmas: float = 3.0,
                  min_reject_db: float = 3.0, **kwargs) -> BodyEstimate:
    """Estimate the body, then re-solve without notes that fit the source model badly.

    A note is an outlier if its median absolute misfit is more than
    `outlier_sigmas` robust standard deviations (1.4826 * MAD) above the
    median note, and at least `min_reject_db`. Typical causes are period
    doubling or multiple slipping in that take, a wrong pitch, or noise, all
    of which break the sawtooth-source assumption. Keyword arguments go to
    `solve_body`.
    """
    first = solve_body(observations, **kwargs)
    r = first.note_residual_db
    centre = np.nanmedian(r)
    spread = 1.4826 * np.nanmedian(np.abs(r - centre))
    limit = max(min_reject_db, centre + outlier_sigmas * spread)
    bad = [k for k, x in enumerate(r) if x > limit]
    if not bad or len(bad) == len(observations):
        return first
    kept = [obs for k, obs in enumerate(observations) if k not in bad]
    second = solve_body(kept, **kwargs)
    residuals = np.full(len(observations), np.nan)
    residuals[[k for k in range(len(observations)) if k not in bad]] = second.note_residual_db
    residuals[bad] = first.note_residual_db[bad]
    second.note_residual_db = residuals
    second.rejected_notes = bad
    return second


def solve_body(observations: list[HarmonicObservations], f_min: float = 180.0, f_max: float = 12000.0,
                  points_per_octave: int = 48, smoothness: float = 3.0, source_db=sawtooth_source_db,
                  iterations: int = 4, huber_db: float = 3.0) -> BodyEstimate:
    """Solve for the body response on a log-frequency grid and one level per note.

    The response is piecewise linear in log frequency. `smoothness` weights a
    second-difference penalty (in dB per grid step squared). Iteratively
    reweighted least squares with a Huber weight of `huber_db` suppresses
    outliers.
    """
    n_nodes = int(math.ceil(points_per_octave * math.log2(f_max / f_min))) + 1
    grid = f_min * 2.0 ** (np.arange(n_nodes) / points_per_octave)
    n_notes = len(observations)

    # Vectorised rows. Observations of the same note falling in the same
    # 1/8-grid-step frequency bin are merged (mean level, weighted by count),
    # which shrinks long recordings ~10x without losing frequency detail.
    all_k, all_pos, all_y = [], [], []
    for k, obs in enumerate(observations):
        keep = (obs.freqs >= f_min) & (obs.freqs < f_max)
        all_k.append(np.full(int(keep.sum()), k))
        all_pos.append(points_per_octave * np.log2(obs.freqs[keep] / f_min))
        all_y.append(obs.level_db[keep] - source_db(obs.harmonic[keep].astype(float), obs.f0))
    note_idx = np.concatenate(all_k) if all_k else np.array([], dtype=int)
    pos = np.concatenate(all_pos) if all_pos else np.array([])
    y_raw = np.concatenate(all_y) if all_y else np.array([])
    if len(y_raw) == 0:
        raise ValueError("no harmonic observations inside the frequency range")

    fine = np.floor(pos * 8).astype(np.int64)
    keys = note_idx.astype(np.int64) * (n_nodes * 8 + 8) + fine
    uniq, inverse, counts = np.unique(keys, return_inverse=True, return_counts=True)
    y = np.bincount(inverse, weights=y_raw) / counts
    pos_m = np.bincount(inverse, weights=pos) / counts
    note_of_row = (uniq // (n_nodes * 8 + 8)).astype(int)
    n_obs = len(uniq)

    i0 = np.minimum(np.floor(pos_m).astype(int), n_nodes - 1)
    frac = pos_m - i0
    rows = np.repeat(np.arange(n_obs), 3)
    cols = np.stack([i0, np.minimum(i0 + 1, n_nodes - 1), n_nodes + note_of_row], axis=1).ravel()
    vals = np.stack([1.0 - frac, frac, np.ones(n_obs)], axis=1).ravel()
    coverage = np.bincount(i0, weights=counts, minlength=n_nodes)[:n_nodes].astype(int)

    data = sparse.csr_matrix((vals, (rows, cols)), shape=(n_obs, n_nodes + n_notes))

    # Second-difference smoothness on B, and a zero-mean constraint on the note offsets
    # (the overall level is arbitrary and is carried by B).
    d2 = sparse.diags([1.0, -2.0, 1.0], [0, 1, 2], shape=(n_nodes - 2, n_nodes))
    reg = sparse.hstack([smoothness * d2, sparse.csr_matrix((n_nodes - 2, n_notes))])
    mean_row = sparse.csr_matrix(np.concatenate([np.zeros(n_nodes), np.full(n_notes, 10.0)])[None, :])
    # Keep poorly observed nodes near their neighbours rather than free.
    tie = sparse.hstack([sparse.diags(np.full(n_nodes, 1e-3)), sparse.csr_matrix((n_nodes, n_notes))])

    base_weights = counts.astype(float)
    weights = base_weights.copy()
    solution = np.zeros(n_nodes + n_notes)
    for _ in range(iterations):
        w = sparse.diags(np.sqrt(weights))
        system = sparse.vstack([w @ data, reg, mean_row, tie]).tocsr()
        rhs = np.concatenate([np.sqrt(weights) * y, np.zeros(n_nodes - 2), [0.0], np.zeros(n_nodes)])
        solution = lsqr(system, rhs, atol=1e-10, btol=1e-10, iter_lim=20000)[0]
        resid = data @ solution - y
        a = np.abs(resid)
        weights = base_weights * np.where(a <= huber_db, 1.0, huber_db / np.maximum(a, 1e-12))

    resid = data @ solution - y
    rms = float(np.sqrt(np.sum(weights * resid**2) / np.sum(weights)))
    # Per-note misfit: median absolute residual weighted by how many frames
    # each merged row represents. Unweighted, the few heavy rows of a steady
    # note would be outvoted by its many light attack/release rows.
    order = np.argsort(note_of_row, kind="stable")
    bounds = np.searchsorted(note_of_row[order], np.arange(n_notes + 1))
    abs_sorted = np.abs(resid[order])
    count_sorted = base_weights[order]
    note_resid = np.array([_weighted_median(abs_sorted[bounds[k] : bounds[k + 1]], count_sorted[bounds[k] : bounds[k + 1]])
                           if bounds[k + 1] > bounds[k] else np.nan for k in range(n_notes)])
    return BodyEstimate(grid, solution[:n_nodes], coverage, solution[n_nodes:], rms, note_resid, [])


def _weighted_median(values: np.ndarray, weights: np.ndarray) -> float:
    order = np.argsort(values)
    cumulative = np.cumsum(weights[order])
    return float(values[order][np.searchsorted(cumulative, 0.5 * cumulative[-1])])


# --------------------------------------------------------------------------
# Turning the estimate into filters
# --------------------------------------------------------------------------


def minimum_phase_fir(freqs: np.ndarray, response_db: np.ndarray, fs: float, n_taps: int = 2048,
                      f_low: float | None = None) -> np.ndarray:
    """Minimum-phase FIR whose magnitude follows the estimate (cepstral method).

    Outside the estimated band the response is held at the edge values, then
    rolled off below `f_low` (default: lowest estimated frequency).
    """
    n_fft = 1 << int(math.ceil(math.log2(8 * n_taps)))
    f = np.fft.rfftfreq(n_fft, 1.0 / fs)
    log_f = np.log(np.maximum(f, 1.0))
    mag_db = np.interp(log_f, np.log(freqs), response_db)
    f_low = freqs[0] if f_low is None else f_low
    below = f < f_low
    mag_db[below] += 40.0 * np.log10(np.maximum(f[below], 1.0) / f_low)  # 2nd-order-like roll-off
    mag = 10 ** (mag_db / 20)

    cepstrum = np.fft.irfft(np.log(np.maximum(mag, 1e-9)), n_fft)
    fold = np.zeros(n_fft)
    fold[0] = cepstrum[0]
    fold[1 : n_fft // 2] = 2.0 * cepstrum[1 : n_fft // 2]
    fold[n_fft // 2] = cepstrum[n_fft // 2]
    h = np.fft.irfft(np.exp(np.fft.rfft(fold)), n_fft)[:n_taps]
    h *= 0.5 + 0.5 * np.cos(np.pi * np.arange(n_taps) / n_taps)  # fade the tail
    return h


def _bank_response_db(params: np.ndarray, freqs: np.ndarray, fs: float, direct_gain: float) -> np.ndarray:
    h = np.full(len(freqs), direct_gain, dtype=complex)
    for f_mode, log_q, gain in params.reshape(-1, 3):
        b, a = resonator_coefficients(f_mode, math.exp(log_q), fs)
        _, hm = freqz(b, a, worN=freqs, fs=fs)
        h += gain * hm
    return 20 * np.log10(np.abs(h) + 1e-9)


def fit_modes(freqs: np.ndarray, response_db: np.ndarray, fs: float = 48000.0, max_modes: int = 40,
              prominence_db: float = 2.0, f_fit_max: float = 10000.0, direct_gain: float = 0.02) -> list[Mode]:
    """Fit a parallel resonator bank (as used by BodyModel) to an estimated magnitude response.

    Peaks of the estimate seed the modes; frequencies, Q values and signed
    gains are then refined by nonlinear least squares on the dB response.
    Returns modes normalised so the largest gain is 1.
    """
    band = freqs <= f_fit_max
    f_band, target = freqs[band], response_db[band]
    peaks, props = find_peaks(target, prominence=prominence_db)
    order = np.argsort(props["prominences"])[::-1][:max_modes]
    peaks = np.sort(peaks[order])
    if len(peaks) == 0:
        raise ValueError("no resonances found in the estimate")

    ref = np.max(target)
    x0 = []
    for p in peaks:
        x0 += [f_band[p], math.log(25.0), 10 ** ((target[p] - ref) / 20)]
    x0 = np.array(x0)
    lower = np.tile([0.0, math.log(3.0), -4.0], len(peaks))
    upper = np.tile([fs / 2, math.log(200.0), 4.0], len(peaks))
    lower[0::3] = x0[0::3] * 0.9
    upper[0::3] = x0[0::3] * 1.1

    def residual(x):
        model = _bank_response_db(x, f_band, fs, direct_gain)
        return (model - np.mean(model)) - (target - np.mean(target))

    fit = least_squares(residual, x0, bounds=(lower, upper), max_nfev=200)
    params = fit.x.reshape(-1, 3)
    scale = np.max(np.abs(params[:, 2]))
    return [Mode(round(float(f), 1), round(float(math.exp(lq)), 1), round(float(g / scale), 4))
            for f, lq, g in params]


def compare_responses(freqs: np.ndarray, estimate_db: np.ndarray, truth_db: np.ndarray, f_lo: float = 200.0,
                      f_hi: float = 8000.0, smooth_octaves: float = 0.0) -> dict:
    """RMS dB difference after removing the best overall level (and optional smoothing)."""
    band = (freqs >= f_lo) & (freqs <= f_hi)
    e, t = estimate_db[band].copy(), truth_db[band].copy()
    if smooth_octaves > 0:
        e, t = _smooth_log(freqs[band], e, smooth_octaves), _smooth_log(freqs[band], t, smooth_octaves)
    diff = e - t
    diff -= np.mean(diff)
    return {"rms_db": float(np.sqrt(np.mean(diff**2))), "max_abs_db": float(np.max(np.abs(diff)))}


def _smooth_log(freqs: np.ndarray, values: np.ndarray, octaves: float) -> np.ndarray:
    logf = np.log2(freqs)
    out = np.empty_like(values)
    for i, lf in enumerate(logf):
        sel = np.abs(logf - lf) <= octaves / 2
        out[i] = np.mean(values[sel])
    return out
