#!/usr/bin/env python3
"""Independent check of the CNSM-based body filters against the anechoic Iowa violin.

The CNSM radiation balance comes from recordings in a room. The Iowa MIS
violin was recorded in an anechoic chamber with a measurement microphone, on
a different instrument and by a different player. If the broad (1/3-octave)
balance of the CNSM-derived body filters matches the Iowa violin about as
well as the three CNSM violins match each other, the correction is a violin
property rather than a room or microphone artefact.

Needs: data/body_iowa_violin.json (scripts/estimate_body_from_recordings.py on
the Iowa files) and data/cnsm_radiation_correction.json plus the CNSM
admittances (scripts/radiation_from_cnsm.py).

Writes figures/iowa_vs_cnsm.png and data/iowa_vs_cnsm.json.
"""

from __future__ import annotations

import json
import sys
from itertools import combinations
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from violin_model.body import BodyModel  # noqa: E402
from violin_model.body_estimation import _smooth_log  # noqa: E402
from violin_model.cnsm import VIOLINS, MeasuredBody, load_admittance  # noqa: E402

BAND = (250.0, 8000.0)
SMOOTH = 1 / 3


def band_rms(freqs, a_db, b_db):
    sel = (freqs >= BAND[0]) & (freqs <= BAND[1])
    d = a_db[sel] - b_db[sel]
    d = d - np.mean(d)
    return float(np.sqrt(np.mean(d**2)))


def main():
    iowa = json.loads((ROOT / "data" / "body_iowa_violin.json").read_text())
    freqs = np.array(iowa["estimate"]["freqs_hz"])
    curves = {"Iowa (anechoic recordings)": _smooth_log(freqs, np.array(iowa["estimate"]["response_db"]), SMOOTH)}

    cnsm = json.loads((ROOT / "data" / "cnsm_radiation_correction.json").read_text())["violins"]
    corrections = {}
    for violin in VIOLINS:
        f = np.array(cnsm[violin]["freqs_hz"])
        rec = np.interp(np.log(freqs), np.log(f), np.array(cnsm[violin]["recorded_db"]))
        curves[f"{violin} (room recordings)"] = _smooth_log(freqs, rec, SMOOTH)
        corrections[violin] = (f, np.array(cnsm[violin]["correction_db"]))

    def filter_curve(body):
        w, h = body.frequency_response(48000.0, n=1 << 15)
        return _smooth_log(freqs, np.interp(freqs, w, 20 * np.log10(np.abs(h) + 1e-12)), SMOOTH)

    filters = {}
    for violin in VIOLINS:
        filters[f"{violin} filter (Y × R)"] = filter_curve(MeasuredBody(violin, correction=corrections[violin]))
        adm = load_admittance(violin)
        y_db = 20 * np.log10(np.interp(freqs, adm.freqs, np.abs(adm.y)) + 1e-12)
        filters[f"{violin} |Y| alone"] = _smooth_log(freqs, y_db, SMOOTH)
        filters[f"{violin} |jωY|"] = _smooth_log(freqs, y_db + 20 * np.log10(freqs), SMOOTH)
    filters["Phase 1 synthetic body"] = filter_curve(BodyModel())

    iowa_key = "Iowa (anechoic recordings)"
    result = {
        "band_hz": BAND,
        "smoothing_octaves": SMOOTH,
        "iowa_notes": iowa["notes_used"],
        "cnsm_recordings_vs_each_other_db": {
            f"{a.split()[0]}-{b.split()[0]}": band_rms(freqs, curves[a], curves[b])
            for a, b in combinations([k for k in curves if k != iowa_key], 2)
        },
        "iowa_vs_cnsm_recordings_db": {k.split()[0]: band_rms(freqs, curves[iowa_key], v)
                                        for k, v in curves.items() if k != iowa_key},
        "iowa_vs_filters_db": {k: band_rms(freqs, curves[iowa_key], v) for k, v in filters.items()},
    }
    print(json.dumps(result, indent=2))
    (ROOT / "data" / "iowa_vs_cnsm.json").write_text(json.dumps(result, indent=2) + "\n")

    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(8, 7.5), sharex=True)
    colours = {"Levaggi": "#e76f51", "Klimke": "#264653", "Stoppani": "#2a9d8f"}

    def norm(c):
        sel = (freqs >= BAND[0]) & (freqs <= BAND[1])
        return c - np.mean(c[sel])

    for name, c in curves.items():
        is_iowa = name == iowa_key
        ax1.semilogx(freqs, norm(c), lw=2.4 if is_iowa else 1.0, color="#000" if is_iowa else colours[name.split()[0]],
                     label=name)
    ax1.set_title("Radiated balance (1/3 octave): anechoic Iowa violin vs CNSM room recordings", fontsize=10)
    ax1.set_ylabel("dB")
    ax1.legend(fontsize=7)
    ax2.semilogx(freqs, norm(curves[iowa_key]), lw=2.4, color="#000", label=iowa_key)
    for violin in VIOLINS:
        ax2.semilogx(freqs, norm(filters[f"{violin} filter (Y × R)"]), lw=1.0, color=colours[violin],
                     label=f"{violin} body filter")
    ax2.semilogx(freqs, norm(filters["Klimke |jωY|"]), lw=1.0, ls=":", color="#264653", label="Klimke |jωY| (rejected)")
    ax2.semilogx(freqs, norm(filters["Phase 1 synthetic body"]), lw=1.0, ls="--", color="#999",
                 label="Phase 1 synthetic body")
    ax2.set_title("Iowa violin vs the body filters", fontsize=10)
    ax2.set_ylabel("dB")
    ax2.set_xlabel("frequency (Hz)")
    ax2.set_xlim(200, 10000)
    ax2.legend(fontsize=7)
    for ax in (ax1, ax2):
        ax.axvspan(BAND[0], BAND[1], color="#eee", zorder=-1)
        ax.set_ylim(-25, 15)
    fig.tight_layout()
    fig.savefig(ROOT / "figures" / "iowa_vs_cnsm.png", dpi=120)


if __name__ == "__main__":
    main()
