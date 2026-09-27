#!/usr/bin/env python3
"""Estimate a violin body response from recordings of sustained bowed notes.

Intended for anechoic sample libraries such as the University of Iowa
Musical Instrument Samples (arco notes, one file per chromatic run, notes
separated by silence), but any recordings of separate, sustained bowed notes
work.

    python scripts/estimate_body_from_recordings.py path/to/violin/arco/*.aif --name iowa_violin

Writes:
    data/body_<name>.json        fitted modal bank (loadable by violin_model.body.load_modes)
                                 plus the raw estimate and provenance
    data/body_<name>_48k.wav     minimum-phase FIR (float32, 48 kHz) for convolution
    figures/body_<name>.png      estimate, fitted bank and coverage

Pizzicato files should be left out: their source spectrum is not a sawtooth.
Remember to credit the recordings' source (see research/data/SOURCES.md).
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
import soundfile as sf  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from violin_model.body import BodyModel  # noqa: E402
from violin_model.body_estimation import (  # noqa: E402
    estimate_body,
    fit_modes,
    harmonic_observations,
    minimum_phase_fir,
    segment_notes,
)

VIOLIN_RANGE_HZ = (180.0, 3600.0)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("files", nargs="+", type=Path, help="audio files, or directories to search")
    parser.add_argument("--name", required=True, help="short name used for the output files")
    parser.add_argument("--source", default="", help="provenance note stored in the JSON (dataset, licence)")
    parser.add_argument("--max-modes", type=int, default=40)
    args = parser.parse_args()

    paths = []
    for p in args.files:
        if p.is_dir():
            paths += sorted(q for q in p.rglob("*") if q.suffix.lower() in {".wav", ".aif", ".aiff", ".flac"})
        else:
            paths.append(p)
    if not paths:
        sys.exit("no audio files found")

    observations, notes = [], []
    for path in paths:
        audio, fs = sf.read(path, always_2d=True)
        audio = audio.mean(axis=1)
        for seg in segment_notes(audio, fs):
            if not VIOLIN_RANGE_HZ[0] <= seg.f0 <= VIOLIN_RANGE_HZ[1]:
                continue
            obs = harmonic_observations(audio[seg.start : seg.end], fs, seg.f0)
            if len(obs.freqs) > 0:
                observations.append(obs)
                notes.append({"file": path.name, "start_s": seg.start / fs, "f0": seg.f0})
        print(f"{path.name}: {len(notes)} notes so far")

    if not observations:
        sys.exit("no usable notes found")

    est = estimate_body(observations)
    print(f"{len(observations)} notes, {len(est.rejected_notes)} rejected, misfit {est.residual_db:.2f} dB")

    modes = fit_modes(est.freqs, est.response_db, max_modes=args.max_modes)
    fir = minimum_phase_fir(est.freqs, est.response_db, 48000.0)
    fir /= np.max(np.abs(np.fft.rfft(fir, 1 << 16)))  # 0 dB peak gain

    (ROOT / "data").mkdir(exist_ok=True)
    (ROOT / "figures").mkdir(exist_ok=True)
    payload = {
        "description": f"Body response estimated from recordings ({args.name}) with "
                       "violin_model.body_estimation; modal bank fitted to the estimate.",
        "source": args.source,
        "notes_used": len(observations) - len(est.rejected_notes),
        "notes_rejected": len(est.rejected_notes),
        "misfit_db": est.residual_db,
        "modes": [m.__dict__ for m in modes],
        "estimate": {"freqs_hz": est.freqs.round(2).tolist(), "response_db": est.response_db.round(3).tolist(),
                     "coverage": est.coverage.tolist()},
        "notes": notes,
    }
    (ROOT / "data" / f"body_{args.name}.json").write_text(json.dumps(payload, indent=1) + "\n")
    sf.write(ROOT / "data" / f"body_{args.name}_48k.wav", fir.astype(np.float32), 48000, subtype="FLOAT")

    fitted = BodyModel(modes)
    w, h = fitted.frequency_response(48000.0, n=1 << 15)
    fit_db = np.interp(est.freqs, w, 20 * np.log10(np.abs(h) + 1e-9))
    fig, (ax, ax2) = plt.subplots(2, 1, figsize=(8, 5.5), sharex=True, gridspec_kw={"height_ratios": [3, 1]})
    ax.semilogx(est.freqs, est.response_db - np.max(est.response_db), color="#e76f51", lw=1.1, label="estimate")
    ax.semilogx(est.freqs, fit_db - np.max(fit_db), color="#264653", lw=0.9, ls="--",
                label=f"fitted bank ({len(modes)} modes)")
    ax.set_ylabel("dB")
    ax.set_title(f"Body response estimated from {len(observations)} notes ({args.name})")
    ax.legend(fontsize=8)
    ax2.semilogx(est.freqs, est.coverage, color="#999")
    ax2.set_ylabel("observations")
    ax2.set_xlabel("frequency (Hz)")
    ax2.set_xlim(est.freqs[0], est.freqs[-1])
    fig.tight_layout()
    fig.savefig(ROOT / "figures" / f"body_{args.name}.png", dpi=120)
    print(f"wrote data/body_{args.name}.json, data/body_{args.name}_48k.wav, figures/body_{args.name}.png")


if __name__ == "__main__":
    main()
