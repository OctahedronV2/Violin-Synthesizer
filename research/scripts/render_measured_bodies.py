#!/usr/bin/env python3
"""Render the same performances through the synthetic body and three measured violin bodies.

Needs the CNSM admittances (python scripts/fetch_cnsm.py). Writes
renders/bodies/<clip>_<body>.wav, data/body_ir_<violin>_48k.wav and
figures/measured_bodies.png.

Measured bodies: Pauget Ballesteros, H. (2026). CNSM Dataset (1.0.0). Zenodo.
https://doi.org/10.5281/zenodo.18696786 (CC BY 4.0).
"""

from __future__ import annotations

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
from violin_model.cnsm import VIOLINS, MeasuredBody  # noqa: E402
from violin_model.notes import BowStroke, fade_edges, normalise, render_bowed_note, render_detache_phrase  # noqa: E402

FS = 48000.0
OUT = ROOT / "renders" / "bodies"


def clips():
    phrase, _, _ = render_detache_phrase([69, 71, 73, 74, 76, 78, 80, 81])
    _, g3, _, _ = render_bowed_note(55, BowStroke(force_fraction=0.5, vibrato_rate_hz=5.2, vibrato_depth_cents=30.0))
    _, a4, _, _ = render_bowed_note(69, BowStroke(vibrato_rate_hz=5.5, vibrato_depth_cents=30.0))
    return {
        "A_major_detache": phrase,
        "G3_vibrato": g3.bridge_force,
        "A4_vibrato": a4.bridge_force,
    }


def load_corrections():
    """Radiation corrections from scripts/radiation_from_cnsm.py, if it has been run."""
    path = ROOT / "data" / "cnsm_radiation_correction.json"
    if not path.exists():
        print("no radiation correction found; using j*omega*Y")
        return {}
    data = json.loads(path.read_text())["violins"]
    return {v: (np.array(d["freqs_hz"]), np.array(d["correction_db"])) for v, d in data.items()}


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    corrections = load_corrections()
    bodies = {"synthetic": BodyModel()}
    for violin in VIOLINS:
        bodies[violin] = MeasuredBody(violin, fs=FS, correction=corrections.get(violin))
        sf.write(ROOT / "data" / f"body_ir_{violin.lower()}_48k.wav", bodies[violin].ir.astype(np.float32),
                 int(FS), subtype="FLOAT")

    for clip, force in clips().items():
        for name, body in bodies.items():
            audio, _ = normalise(fade_edges(body.process(force, FS), FS))
            sf.write(OUT / f"{clip}_{name.lower()}.wav", audio, int(FS), subtype="PCM_24")
        print(clip)

    fig, ax = plt.subplots(figsize=(8, 4.2))
    colours = {"synthetic": "#999999", "Levaggi": "#e76f51", "Klimke": "#264653", "Stoppani": "#2a9d8f"}
    for name, body in bodies.items():
        f, h = body.frequency_response(FS, n=1 << 15)
        sel = (f > 150) & (f < 16000)
        db = 20 * np.log10(np.abs(h[sel]) + 1e-12)
        ax.semilogx(f[sel], db - np.max(db), lw=0.8 if name != "synthetic" else 1.6, color=colours[name],
                    label=name + (" (Phase 1 placeholder)" if name == "synthetic"
                                  else " (measured Y × recorded radiation)" if name in corrections
                                  else " (measured, jω·Y)"))
    ax.set_xlim(150, 16000)
    ax.set_ylim(-50, 3)
    ax.set_xlabel("frequency (Hz)")
    ax.set_ylabel("dB re peak")
    ax.set_title("Body filters: bridge force → radiated sound")
    ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(ROOT / "figures" / "measured_bodies.png", dpi=120)


if __name__ == "__main__":
    main()
