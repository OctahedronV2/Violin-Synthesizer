#!/usr/bin/env python3
"""Check the body-estimation pipeline on synthetic recordings with a known body.

A "hidden" body (different from the default table) is applied to bowed notes
from our string model across the violin's range, with varied bow position,
speed, force and vibrato, mimicking a chromatic sample library. The pipeline
must recover that body from the audio alone.

    python scripts/validate_body_estimation.py          # full run (~1 min)
    python scripts/validate_body_estimation.py --quick  # fewer notes

Writes figures/body_estimation_validation.png and
data/body_estimation_validation.json.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from violin_model.body import NAMED_LOW_MODES, BodyModel, Mode, generate_default_modes  # noqa: E402
from violin_model.body_estimation import (  # noqa: E402
    compare_responses,
    estimate_body,
    fit_modes,
    harmonic_observations,
    minimum_phase_fir,
    segment_notes,
)
from violin_model.notes import BowStroke, render_bowed_note  # noqa: E402
from violin_model.strings import midi_to_hz  # noqa: E402


def hidden_body() -> BodyModel:
    """A body unlike the default: shifted signature modes, different high modes."""
    rng = np.random.default_rng(2024)
    low = [Mode(round(m.freq * float(rng.uniform(0.94, 1.06)), 1), m.q, m.gain, m.name) for m in NAMED_LOW_MODES]
    high = generate_default_modes(seed=99)[len(NAMED_LOW_MODES):]
    return BodyModel(low + high)


def render_library(model: BodyModel, notes, seed: int = 5):
    """Chromatic 'sample library': each note once senza and once with vibrato, random bowing."""
    rng = np.random.default_rng(seed)
    takes = []
    for midi in notes:
        for vibrato in (False, True):
            stroke = BowStroke(
                duration_s=1.5,
                bow_speed=float(rng.uniform(0.12, 0.4)),
                beta=float(rng.uniform(0.08, 0.16)),
                force_fraction=0.5 if midi < 62 else float(rng.uniform(0.3, 0.5)),
                vibrato_rate_hz=5.5 if vibrato else 0.0,
                vibrato_depth_cents=35.0 if vibrato else 0.0,
                vibrato_delay_s=0.1,
                vibrato_onset_s=0.2,
            )
            _, render, params, _ = render_bowed_note(midi, stroke)
            audio = model.process(render.bridge_force, params.fs)
            takes.append((midi, vibrato, audio, params.fs))
    return takes


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--quick", action="store_true")
    args = parser.parse_args()

    notes = list(range(55, 100, 3)) if args.quick else list(range(55, 101))
    truth_model = hidden_body()
    takes = render_library(truth_model, notes)

    # Join the takes into one "recording" separated by silence, like a sample-library file,
    # so the segmentation step is exercised too.
    fs = takes[0][3]
    gap = np.zeros(int(0.3 * fs))
    recording = np.concatenate([np.concatenate([gap, audio]) for _, _, audio, _ in takes] + [gap])
    recording /= np.max(np.abs(recording))

    segments = segment_notes(recording, fs)
    expected = [midi_to_hz(m) for m, _, _, _ in takes]
    seg_cents = [1200 * np.log2(s.f0 / e) for s, e in zip(segments, expected)]
    observations = [
        harmonic_observations(recording[s.start : s.end], fs, s.f0) for s in segments
    ]
    estimate = estimate_body(observations)

    _, h_true = truth_model.frequency_response(fs, n=1 << 15)
    f_true = np.linspace(0, fs / 2, len(h_true))
    truth_db = np.interp(estimate.freqs, f_true, 20 * np.log10(np.abs(h_true) + 1e-9))

    raw = compare_responses(estimate.freqs, estimate.response_db, truth_db)
    smooth = compare_responses(estimate.freqs, estimate.response_db, truth_db, smooth_octaves=1 / 6)
    modes = fit_modes(estimate.freqs, estimate.response_db, fs)
    fitted = BodyModel(modes)
    _, h_fit = fitted.frequency_response(fs, n=1 << 15)
    fit_db = np.interp(estimate.freqs, f_true, 20 * np.log10(np.abs(h_fit) + 1e-9))
    fit_cmp = compare_responses(estimate.freqs, fit_db, truth_db, smooth_octaves=1 / 6)
    fir = minimum_phase_fir(estimate.freqs, estimate.response_db, fs)

    # Signature-mode recovery: nearest fitted mode to each strong named mode.
    signature = []
    for m in truth_model.modes[: len(NAMED_LOW_MODES)]:
        if m.gain < 0.8:
            continue
        nearest = min(modes, key=lambda x: abs(np.log(x.freq / m.freq)))
        signature.append({"name": m.name, "true_hz": m.freq, "fitted_hz": nearest.freq,
                          "error_pct": 100 * (nearest.freq / m.freq - 1)})

    result = {
        "notes": len(notes),
        "takes": len(takes),
        "segments_found": len(segments),
        "segment_pitch_error_cents_max": float(np.max(np.abs(seg_cents))) if seg_cents else None,
        "observations": int(sum(len(o.freqs) for o in observations)),
        "residual_db": estimate.residual_db,
        "rejected_notes": [
            {"midi": takes[k][0], "vibrato": takes[k][1], "median_misfit_db": float(estimate.note_residual_db[k])}
            for k in estimate.rejected_notes
        ],
        "estimate_vs_truth": raw,
        "estimate_vs_truth_sixth_octave": smooth,
        "fitted_modes": len(modes),
        "fitted_bank_vs_truth_sixth_octave": fit_cmp,
        "signature_modes": signature,
        "fir_taps": len(fir),
    }
    print(json.dumps(result, indent=2))

    fig, ax = plt.subplots(figsize=(8, 4.2))
    offset = np.mean(estimate.response_db - truth_db)
    ax.semilogx(estimate.freqs, truth_db, color="#999", lw=2.2, label="hidden body (truth)")
    ax.semilogx(estimate.freqs, estimate.response_db - offset, color="#e76f51", lw=1.1,
                label="estimated from audio")
    ax.semilogx(estimate.freqs, fit_db - np.mean(fit_db - truth_db), color="#264653", lw=0.9, ls="--",
                label=f"fitted modal bank ({len(modes)} modes)")
    ax.set_xlim(180, 12000)
    ax.set_xlabel("frequency (Hz)")
    ax.set_ylabel("dB")
    ax.set_title(f"Body estimation from {len(takes)} synthetic notes: "
                 f"{smooth['rms_db']:.1f} dB RMS error (1/6-octave smoothed)")
    ax.legend(fontsize=8)
    fig.tight_layout()
    suffix = "_quick" if args.quick else ""
    fig.savefig(ROOT / "figures" / f"body_estimation_validation{suffix}.png", dpi=120)
    (ROOT / "data" / f"body_estimation_validation{suffix}.json").write_text(json.dumps(result, indent=2) + "\n")


if __name__ == "__main__":
    main()
