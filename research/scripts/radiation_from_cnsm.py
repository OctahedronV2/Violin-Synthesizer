#!/usr/bin/env python3
"""Derive each CNSM violin's radiation balance from its scale recordings.

The bridge admittance Y gives the body's resonances precisely but not how
strongly each frequency region radiates. The dataset's recordings of the same
violins (scales, many players) do: running the body-estimation pipeline on
them gives the radiated response B_rec, including the room and microphone.
The smooth ratio R = B_rec / |Y| (1/3-octave smoothed) is the radiation
correction, and Y * R becomes the body filter: measured resonance detail with
the recorded spectral balance.

    python scripts/fetch_cnsm.py --recordings
    python scripts/radiation_from_cnsm.py

Writes data/cnsm_radiation_correction.json and figures/cnsm_radiation.png.
Credit: Pauget Ballesteros, H. (2026). CNSM Dataset (1.0.0). Zenodo.
https://doi.org/10.5281/zenodo.18696786 (CC BY 4.0).
"""

from __future__ import annotations

import json
import os
import sys
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402
import soundfile as sf  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from violin_model.body_estimation import (  # noqa: E402
    _smooth_log,
    estimate_body,
    harmonic_observations,
    segment_by_pitch,
)
from violin_model.cnsm import VIOLINS, dataset_root, load_admittance  # noqa: E402

VIOLIN_RANGE_HZ = (180.0, 3600.0)
SMOOTH_OCTAVES = 1 / 3


def file_observations(path: Path):
    audio, fs = sf.read(path)
    obs = []
    for seg in segment_by_pitch(audio, fs):
        if VIOLIN_RANGE_HZ[0] <= seg.f0 <= VIOLIN_RANGE_HZ[1]:
            o = harmonic_observations(audio[seg.start : seg.end], fs, seg.f0)
            if len(o.freqs):
                obs.append(o)
    return obs


def observations_for(violin: str):
    paths = sorted((dataset_root() / "recordings").glob(f"Session */{violin}/*/*.wav"))
    with ProcessPoolExecutor(max_workers=os.cpu_count()) as pool:
        per_file = list(pool.map(file_observations, paths, chunksize=4))
    return [o for obs in per_file for o in obs], len(paths)


def main():
    results = {}
    fig, axes = plt.subplots(len(VIOLINS), 1, figsize=(8, 9), sharex=True)
    for ax, violin in zip(axes, VIOLINS):
        obs, n_files = observations_for(violin)
        est = estimate_body(obs, f_min=180.0, f_max=10000.0)
        adm = load_admittance(violin)
        y_db = 20 * np.log10(np.abs(np.interp(est.freqs, adm.freqs, np.abs(adm.y))) + 1e-12)
        jw_db = y_db + 20 * np.log10(est.freqs)

        rec_s = _smooth_log(est.freqs, est.response_db, SMOOTH_OCTAVES)
        y_s = _smooth_log(est.freqs, y_db, SMOOTH_OCTAVES)
        jw_s = _smooth_log(est.freqs, jw_db, SMOOTH_OCTAVES)
        correction = rec_s - y_s
        correction -= np.max(correction)

        # How much of the recorded balance each simple model explains (1/3-oct, after level match).
        def misfit(model_s):
            d = rec_s - model_s
            d -= np.mean(d)
            return float(np.sqrt(np.mean(d**2)))

        results[violin] = {
            "files": n_files,
            "notes": len(obs),
            "notes_rejected": len(est.rejected_notes),
            "misfit_db": est.residual_db,
            "balance_rms_vs_Y_db": misfit(y_s),
            "balance_rms_vs_jwY_db": misfit(jw_s),
            "freqs_hz": est.freqs.round(2).tolist(),
            "correction_db": correction.round(3).tolist(),
            "recorded_db": est.response_db.round(3).tolist(),
        }
        print(f"{violin}: {n_files} files, {len(obs)} notes ({len(est.rejected_notes)} rejected); "
              f"balance vs |Y| {results[violin]['balance_rms_vs_Y_db']:.1f} dB, "
              f"vs |jωY| {results[violin]['balance_rms_vs_jwY_db']:.1f} dB RMS")

        ref = np.mean(rec_s)
        ax.semilogx(est.freqs, est.response_db - ref, color="#e76f51", lw=0.7, alpha=0.6,
                    label="recordings (estimate)")
        ax.semilogx(est.freqs, rec_s - ref, color="#e76f51", lw=1.8, label="recordings, 1/3 oct")
        ax.semilogx(est.freqs, y_s - np.mean(y_s), color="#264653", lw=1.4, label="|Y|, 1/3 oct")
        ax.semilogx(est.freqs, jw_s - np.mean(jw_s), color="#264653", lw=1.2, ls=":", label="|jωY|, 1/3 oct")
        ax.set_ylabel("dB")
        ax.set_title(f"{violin}: {len(obs)} notes from {n_files} recordings", fontsize=10)
        ax.set_ylim(-30, 20)
    axes[0].legend(fontsize=7, ncol=2)
    axes[-1].set_xlabel("frequency (Hz)")
    axes[-1].set_xlim(180, 10000)
    fig.tight_layout()
    fig.savefig(ROOT / "figures" / "cnsm_radiation.png", dpi=120)

    payload = {
        "description": "Radiation correction R = recorded balance / |Y| (1/3-octave smoothed, dB, max 0) per "
                       "CNSM violin, from scripts/radiation_from_cnsm.py. Body filter = Y * R.",
        "source": "Pauget Ballesteros, H. (2026). CNSM Dataset (1.0.0). Zenodo. "
                  "https://doi.org/10.5281/zenodo.18696786 (CC BY 4.0)",
        "violins": results,
    }
    (ROOT / "data" / "cnsm_radiation_correction.json").write_text(json.dumps(payload, indent=1) + "\n")


if __name__ == "__main__":
    main()
