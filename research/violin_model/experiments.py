"""Parameter sweeps and theory curves used in the Phase 1 findings."""

from __future__ import annotations

import math
from dataclasses import dataclass, replace

import numpy as np

from .analysis import estimate_f0, helmholtz_onset_time, motion_statistics, cents, spectral_centroid
from .strings import STRINGS, midi_to_hz, string_for_note
from .waveguide import BowedStringParams, min_beta, recommended_oversample, simulate

MOTION_LABELS = ["helmholtz", "multiple_slip", "subharmonic", "raucous", "silent"]


def bridge_resistance(params: BowedStringParams, f0: float) -> float:
    """Resistive bridge impedance equivalent to the loop's DC reflection gain.

    A string of impedance Z terminated by a resistance R reflects velocity
    waves with -(R - Z)/(R + Z), so R = Z (1 + g) / (1 - g).
    """
    g = 10.0 ** (-3.0 / (f0 * params.t60))
    return params.impedance * (1.0 + g) / (1.0 - g)


def schelleng_limits(beta: np.ndarray, v_bow: float, f0: float, params: BowedStringParams):
    """Schelleng's (1973) maximum and minimum bow force for Helmholtz motion."""
    z = params.impedance
    dmu = params.mu_s - params.mu_d
    f_max = 2.0 * z * v_bow / (beta * dmu)
    f_min = z * z * v_bow / (2.0 * bridge_resistance(params, f0) * beta * beta * dmu)
    return f_max, f_min


@dataclass
class SweepResult:
    betas: np.ndarray
    forces: np.ndarray
    labels: np.ndarray  # [len(betas), len(forces)] of str
    cents: np.ndarray
    stick_fraction: np.ndarray


def schelleng_sweep(
    f0: float = 440.0,
    v_bow: float = 0.2,
    betas: np.ndarray | None = None,
    forces: np.ndarray | None = None,
    duration_s: float = 1.5,
    params: BowedStringParams | None = None,
) -> SweepResult:
    params = params or BowedStringParams()
    betas = np.geomspace(0.03, 0.3, 14) if betas is None else betas
    forces = np.geomspace(0.01, 5.0, 24) if forces is None else forces
    n = int(duration_s * params.fs)

    labels = np.empty((len(betas), len(forces)), dtype=object)
    cents_err = np.full((len(betas), len(forces)), np.nan)
    stick = np.full((len(betas), len(forces)), np.nan)
    for i, beta in enumerate(betas):
        for j, force in enumerate(forces):
            render = simulate(f0, beta, v_bow, force, n, params)
            stats = motion_statistics(render, f0, beta, start_s=duration_s / 2)
            labels[i, j] = stats.label
            cents_err[i, j] = stats.cents_error
            stick[i, j] = stats.stick_fraction
    return SweepResult(betas, forces, labels, cents_err, stick)


def force_sweep(
    f0: float = 440.0,
    beta: float = 0.1,
    v_bow: float = 0.2,
    forces: np.ndarray | None = None,
    duration_s: float = 1.5,
    params: BowedStringParams | None = None,
):
    """Pitch error, onset time and motion label against bow force."""
    params = params or BowedStringParams()
    forces = np.geomspace(0.05, 2.0, 30) if forces is None else forces
    n = int(duration_s * params.fs)
    rows = []
    for force in forces:
        render = simulate(f0, beta, v_bow, force, n, params)
        stats = motion_statistics(render, f0, beta, start_s=duration_s / 2)
        rows.append(
            {
                "force": float(force),
                "label": stats.label,
                "cents": stats.cents_error,
                "onset_s": helmholtz_onset_time(render, f0),
                "stick_fraction": stats.stick_fraction,
            }
        )
    return rows


def playable_force(beta: float, v_bow: float, params: BowedStringParams, fraction: float = 0.3) -> float:
    """A bow force comfortably inside the playable range: a fraction of Schelleng's F_max.

    The measured lower limit is far above Schelleng's F_min estimate (see the
    Phase 1 findings), so the default force is set relative to F_max only.
    """
    return fraction * 2.0 * params.impedance * v_bow / (beta * (params.mu_s - params.mu_d))


def brightness_sweep(
    f0: float = 440.0,
    v_bow: float = 0.2,
    betas=(0.05, 0.1, 0.2),
    force_fractions: np.ndarray | None = None,
    duration_s: float = 1.5,
    params: BowedStringParams | None = None,
):
    """Spectral centroid (up to 10 kHz) of the bridge force against bow force.

    Force is given as a fraction of Schelleng's F_max at each bow position.
    """
    params = params or BowedStringParams()
    force_fractions = np.geomspace(0.05, 1.0, 12) if force_fractions is None else force_fractions
    n = int(duration_s * params.fs)
    rows = []
    for beta in betas:
        for fraction in force_fractions:
            force = playable_force(beta, v_bow, params, fraction)
            render = simulate(f0, beta, v_bow, force, n, params)
            steady = render.bridge_force[n // 2 :]
            stats = motion_statistics(render, f0, beta, start_s=duration_s / 2)
            rows.append(
                {
                    "beta": float(beta),
                    "force_fraction": float(fraction),
                    "force": force,
                    "label": stats.label,
                    "centroid_hz": spectral_centroid(steady, params.fs, f_max=10000.0),
                }
            )
    return rows


def tuning_table(midi_notes, sample_rates=(44100.0, 48000.0, 96000.0), beta: float = 0.1,
                 v_bow: float = 0.2, duration_s: float = 1.5, oversample: int | str = "auto"):
    """Measured pitch error (cents) of bowed notes across the range and sample rates.

    Each note uses the impedance of the string it would be played on and a
    bow force of 0.3 F_max. `oversample="auto"` uses recommended_oversample(fs).
    """
    rows = []
    for fs in sample_rates:
        for note in midi_notes:
            f0 = midi_to_hz(note)
            factor = recommended_oversample(fs) if oversample == "auto" else int(oversample)
            params = BowedStringParams(fs=fs, oversample=factor, impedance=string_for_note(note).impedance)
            b = max(beta, 1.2 * min_beta(f0, params))
            force = playable_force(b, v_bow, params)
            n = int(duration_s * fs)
            render = simulate(f0, b, v_bow, force, n, params)
            stats = motion_statistics(render, f0, b, start_s=duration_s / 2)
            steady = render.bridge_force[n // 2 :]
            rows.append(
                {
                    "fs": fs,
                    "oversample": factor,
                    "midi_note": note,
                    "f0": f0,
                    "beta": b,
                    "force": force,
                    "label": stats.label,
                    "cents": cents(estimate_f0(steady, fs, f0), f0),
                }
            )
    return rows


def string_force_windows(beta: float = 0.1, v_bow: float = 0.2, duration_s: float = 1.5,
                         fractions: np.ndarray | None = None):
    """Range of bow force (as a fraction of F_max) giving Helmholtz motion on each open string."""
    fractions = np.geomspace(0.03, 1.2, 24) if fractions is None else fractions
    rows = []
    for name, spec in STRINGS.items():
        params = BowedStringParams(impedance=spec.impedance)
        n = int(duration_s * params.fs)
        labels = []
        for fraction in fractions:
            force = playable_force(beta, v_bow, params, fraction)
            render = simulate(spec.open_f0, beta, v_bow, force, n, params)
            labels.append(motion_statistics(render, spec.open_f0, beta, start_s=duration_s / 2).label)
        helm = [f for f, lab in zip(fractions, labels) if lab == "helmholtz"]
        rows.append(
            {
                "string": name,
                "f0": spec.open_f0,
                "impedance": spec.impedance,
                "f_max_n": playable_force(beta, v_bow, params, 1.0),
                "helmholtz_min_fraction": float(min(helm)) if helm else None,
                "helmholtz_max_fraction": float(max(helm)) if helm else None,
                "labels": labels,
                "fractions": [float(f) for f in fractions],
            }
        )
    return rows


def with_params(params: BowedStringParams, **changes) -> BowedStringParams:
    return replace(params, **changes)
