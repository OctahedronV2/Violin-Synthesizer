#!/usr/bin/env python3
"""Regenerate every Phase 1 result: body mode table, sweeps, figures, reference renders.

    python scripts/run_phase1.py            # everything (about a minute)
    python scripts/run_phase1.py --renders  # reference audio only

Outputs:
    data/body_modes.json        modal bank used by the prototype (and later the plugin)
    data/phase1_results.json    numbers behind every figure and table in the findings
    figures/*.png               plots referenced from docs/PHASE1_FINDINGS.md
    renders/*.wav               reference audio (48 kHz, 24-bit mono)
    renders/measurements.json   per-render measurements for Phase 2 regression checks
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
from matplotlib.colors import ListedColormap  # noqa: E402
from scipy.io import wavfile  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from violin_model import body  # noqa: E402
from violin_model.analysis import (  # noqa: E402
    cents,
    estimate_f0,
    helmholtz_onset_time,
    motion_statistics,
    spectral_centroid,
)
from violin_model.experiments import (  # noqa: E402
    MOTION_LABELS,
    brightness_sweep,
    force_sweep,
    schelleng_limits,
    schelleng_sweep,
    string_force_windows,
    tuning_table,
)
from violin_model.friction import friction_curve  # noqa: E402
from violin_model.notes import (  # noqa: E402
    BowStroke,
    fade_edges,
    normalise,
    render_bowed_note,
    render_pizzicato,
)
from violin_model.strings import STRINGS, midi_to_hz  # noqa: E402
from violin_model.waveguide import BowedStringParams, simulate  # noqa: E402

DATA = ROOT / "data"
FIGURES = ROOT / "figures"
RENDERS = ROOT / "renders"

LABEL_COLOURS = {
    "helmholtz": "#2a9d8f",
    "multiple_slip": "#e9c46a",
    "subharmonic": "#8d6cab",
    "raucous": "#e76f51",
    "silent": "#cccccc",
}


def _clean(value):
    if isinstance(value, dict):
        return {k: _clean(v) for k, v in value.items()}
    if isinstance(value, (list, tuple)):
        return [_clean(v) for v in value]
    if isinstance(value, (np.floating, float)):
        v = float(value)
        return None if np.isnan(v) else round(v, 6)
    if isinstance(value, np.integer):
        return int(value)
    return value


def figure_friction():
    dv = np.linspace(-1.0, 1.0, 801)
    fig, ax = plt.subplots(figsize=(6, 3.5))
    ax.plot(dv, friction_curve(dv), color="#264653")
    ax.axhline(0, color="#999", lw=0.5)
    ax.set_xlabel("sliding speed  v_bow − v_string  (m/s)")
    ax.set_ylabel("friction coefficient μ")
    ax.set_title("Hyperbolic friction curve (μs = 0.8, μd = 0.3, v0 = 0.1 m/s)")
    fig.tight_layout()
    fig.savefig(FIGURES / "friction_curve.png", dpi=120)
    plt.close(fig)


def figure_helmholtz():
    params = BowedStringParams()
    f0, beta = 440.0, 0.1
    render = simulate(f0, beta, 0.2, 0.4, 48000, params)
    fs = render.internal_fs
    start = int(0.8 * fs)
    span = int(3 * fs / f0)
    t = np.arange(span) / fs * 1000.0

    fig, axes = plt.subplots(3, 1, figsize=(7, 6), sharex=True)
    axes[0].plot(t, render.string_velocity[start : start + span], color="#264653")
    axes[0].set_ylabel("string velocity\nat bow (m/s)")
    axes[1].fill_between(t, render.sticking[start : start + span].astype(float), step="pre", color="#2a9d8f")
    axes[1].set_ylabel("sticking")
    axes[1].set_yticks([0, 1])
    out_start = int(0.8 * params.fs)
    out_span = int(3 * params.fs / f0)
    axes[2].plot(np.arange(out_span) / params.fs * 1000.0, render.bridge_force[out_start : out_start + out_span],
                 color="#e76f51")
    axes[2].set_ylabel("bridge force (N)")
    axes[2].set_xlabel("time (ms)")
    fig.suptitle("Helmholtz motion: A4, β = 0.1, v_bow = 0.2 m/s, F = 0.4 N")
    fig.tight_layout()
    fig.savefig(FIGURES / "helmholtz_motion.png", dpi=120)
    plt.close(fig)


def figure_schelleng(results):
    params = BowedStringParams()
    v_bow = 0.2
    sweep = schelleng_sweep(f0=440.0, v_bow=v_bow, params=params)
    f_max, f_min = schelleng_limits(sweep.betas, v_bow, 440.0, params)

    codes = np.vectorize(MOTION_LABELS.index)(sweep.labels)
    cmap = ListedColormap([LABEL_COLOURS[k] for k in MOTION_LABELS])
    fig, ax = plt.subplots(figsize=(7, 5))
    b_edges = np.sqrt(sweep.betas[:-1] * sweep.betas[1:])
    b_edges = np.concatenate([[sweep.betas[0] ** 2 / b_edges[0]], b_edges, [sweep.betas[-1] ** 2 / b_edges[-1]]])
    f_edges = np.sqrt(sweep.forces[:-1] * sweep.forces[1:])
    f_edges = np.concatenate([[sweep.forces[0] ** 2 / f_edges[0]], f_edges, [sweep.forces[-1] ** 2 / f_edges[-1]]])
    ax.pcolormesh(b_edges, f_edges, codes.T, cmap=cmap, vmin=-0.5, vmax=len(MOTION_LABELS) - 0.5)
    ax.plot(sweep.betas, f_max, "k-", lw=1.5, label="Schelleng F_max (theory)")
    ax.plot(sweep.betas, f_min, "k--", lw=1.5, label="Schelleng F_min (theory, DC loss)")
    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.set_xlim(b_edges[0], b_edges[-1])
    ax.set_ylim(f_edges[0], f_edges[-1])
    ax.set_xlabel("bow position β (fraction of string length from bridge)")
    ax.set_ylabel("bow force (N)")
    ax.set_title("Schelleng diagram, A4 on the A string, v_bow = 0.2 m/s")
    handles = [plt.Rectangle((0, 0), 1, 1, color=LABEL_COLOURS[k]) for k in MOTION_LABELS[:4]]
    first = ax.legend(handles, [k.replace("_", " ") for k in MOTION_LABELS[:4]], loc="lower left", fontsize=8)
    ax.add_artist(first)
    ax.legend(loc="upper right", fontsize=8)
    fig.tight_layout()
    fig.savefig(FIGURES / "schelleng.png", dpi=120)
    plt.close(fig)

    # Measured lower edge of the Helmholtz region for each beta.
    measured_min = []
    for i, beta in enumerate(sweep.betas):
        helm = np.flatnonzero(sweep.labels[i] == "helmholtz")
        measured_min.append(float(sweep.forces[helm[0]]) if len(helm) else None)
    measured_max = []
    for i, beta in enumerate(sweep.betas):
        helm = np.flatnonzero(sweep.labels[i] == "helmholtz")
        measured_max.append(float(sweep.forces[helm[-1]]) if len(helm) else None)

    results["schelleng"] = {
        "f0": 440.0,
        "v_bow": v_bow,
        "impedance": params.impedance,
        "betas": sweep.betas.tolist(),
        "forces": sweep.forces.tolist(),
        "labels": sweep.labels.tolist(),
        "theory_f_max": f_max.tolist(),
        "theory_f_min": f_min.tolist(),
        "measured_helmholtz_min_force": measured_min,
        "measured_helmholtz_max_force": measured_max,
    }


def figure_force(results):
    rows = force_sweep()
    forces = np.array([r["force"] for r in rows])
    fig, axes = plt.subplots(2, 1, figsize=(7, 5.5), sharex=True)
    for r in rows:
        axes[0].scatter(r["force"], r["cents"], color=LABEL_COLOURS[r["label"]], s=25)
        if not np.isnan(r["onset_s"]):
            axes[1].scatter(r["force"], r["onset_s"] * 1000.0, color=LABEL_COLOURS[r["label"]], s=25)
    axes[0].axhline(0, color="#999", lw=0.5)
    axes[0].set_ylabel("pitch error (cents)")
    axes[1].set_ylabel("Helmholtz onset (ms)")
    axes[1].set_xlabel("bow force (N)")
    axes[1].set_xscale("log")
    axes[0].set_title("A4, β = 0.1, v_bow = 0.2 m/s: pitch flattening and attack time vs force")
    handles = [plt.Line2D([], [], marker="o", ls="", color=LABEL_COLOURS[k]) for k in MOTION_LABELS[:4]]
    axes[0].legend(handles, [k.replace("_", " ") for k in MOTION_LABELS[:4]], fontsize=8)
    fig.tight_layout()
    fig.savefig(FIGURES / "force_sweep.png", dpi=120)
    plt.close(fig)
    results["force_sweep"] = rows
    del forces


def figure_brightness(results):
    rows = brightness_sweep()
    fig, ax = plt.subplots(figsize=(6.5, 3.8))
    for beta, colour in zip((0.05, 0.1, 0.2), ("#e76f51", "#264653", "#2a9d8f")):
        sub = [r for r in rows if r["beta"] == beta and r["label"] == "helmholtz"]
        ax.plot([r["force_fraction"] for r in sub], [r["centroid_hz"] for r in sub], "o-", color=colour,
                label=f"β = {beta:g}")
    ax.set_xscale("log")
    ax.set_xlabel("bow force / Schelleng F_max")
    ax.set_ylabel("spectral centroid ≤ 10 kHz (Hz)")
    ax.set_title("Brightness vs bow force (A4, Helmholtz points only)")
    ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(FIGURES / "brightness_vs_force.png", dpi=120)
    plt.close(fig)
    results["brightness_vs_force"] = rows


def figure_tuning(results):
    notes = list(range(55, 101, 3))  # G3 ... E7
    rows = tuning_table(notes) + tuning_table(notes, sample_rates=(44100.0, 48000.0), oversample=2)
    fig, ax = plt.subplots(figsize=(7.5, 4))
    styles = {44100.0: ("o", "#e76f51"), 48000.0: ("s", "#264653"), 96000.0: ("^", "#2a9d8f")}
    for fs, (marker, colour) in styles.items():
        for factor in sorted({r["oversample"] for r in rows if r["fs"] == fs}):
            sub = [r for r in rows if r["fs"] == fs and r["oversample"] == factor]
            ls = "-" if factor * fs >= 176400.0 else ":"
            ax.plot([r["midi_note"] for r in sub], [r["cents"] for r in sub], marker=marker, ls=ls, lw=0.9,
                    color=colour, alpha=1.0 if ls == "-" else 0.6, label=f"{fs / 1000:g} kHz × {factor}")
    ax.axhspan(-2, 2, color="#2a9d8f", alpha=0.12, label="±2 cent target")
    ax.set_xlabel("MIDI note (55 = G3, 100 = E7)")
    ax.set_ylabel("bowed pitch error (cents)")
    ax.set_title("Bowed tuning across the range (β = 0.1, 0.3·F_max)")
    ax.legend(fontsize=7, ncol=2)
    fig.tight_layout()
    fig.savefig(FIGURES / "tuning.png", dpi=120)
    plt.close(fig)
    results["bowed_tuning"] = rows


def figure_body(results):
    model = body.BodyModel()
    fs = 48000.0
    freqs, h = model.frequency_response(fs)
    fig, ax = plt.subplots(figsize=(7, 3.8))
    ax.semilogx(freqs[1:], 20 * np.log10(np.abs(h[1:]) + 1e-9), color="#264653", lw=0.9)
    for m in model.modes:
        if m.name:
            ax.annotate(m.name.split(" ")[0], (m.freq, 20 * np.log10(m.gain) + 3), fontsize=7, ha="center")
    ax.set_xlim(100, 20000)
    ax.set_ylim(-40, 15)
    ax.set_xlabel("frequency (Hz)")
    ax.set_ylabel("gain (dB)")
    ax.set_title("Body modal bank: bridge force → output")
    fig.tight_layout()
    fig.savefig(FIGURES / "body_response.png", dpi=120)
    plt.close(fig)
    results["body"] = {"num_modes": len(model.modes), "direct_gain": model.direct_gain}


def measure_performance():
    """Rough cost of the numba reference implementation (not the C++ target)."""
    import time

    params = BowedStringParams()
    n = int(params.fs)
    simulate(440.0, 0.1, 0.2, 0.4, 1000, params)  # compile / warm up
    start = time.perf_counter()
    for _ in range(5):
        simulate(440.0, 0.1, 0.2, 0.4, n, params)
    per_second = (time.perf_counter() - start) / 5
    return {
        "internal_fs": params.internal_fs,
        "seconds_per_audio_second_one_string": per_second,
        "note": "numba reference loop incl. decimation; single core; for orientation only",
    }


REFERENCE_NOTES = [
    # name, midi, stroke overrides
    ("A4_mf_senza_vibrato", 69, {}),
    ("A4_mf_vibrato", 69, {"vibrato_rate_hz": 5.5, "vibrato_depth_cents": 30.0}),
    ("G3_mf_vibrato", 55, {"force_fraction": 0.5, "vibrato_rate_hz": 5.2, "vibrato_depth_cents": 30.0}),
    ("E5_f_vibrato", 76, {"bow_speed": 0.4, "vibrato_rate_hz": 6.0, "vibrato_depth_cents": 25.0}),
    ("A4_p_sul_tasto", 69, {"bow_speed": 0.1, "beta": 0.2}),
    ("A4_f_sul_ponticello", 69, {"bow_speed": 0.4, "beta": 0.04, "force_fraction": 0.7}),
    ("A4_too_little_force", 69, {"force_fraction": 0.02}),
    ("A4_too_much_force", 69, {"force_fraction": 1.6}),
]


def write_wav(path: Path, audio: np.ndarray, fs: float):
    pcm = np.clip(audio, -1.0, 1.0)
    pcm24 = np.round(pcm * (2**23 - 1)).astype(np.int32)
    wavfile.write(path, int(fs), pcm24)


def reference_renders():
    RENDERS.mkdir(exist_ok=True)
    measurements = {}
    for name, midi, overrides in REFERENCE_NOTES:
        stroke = BowStroke(**overrides)
        audio, render, params, beta = render_bowed_note(midi, stroke)
        audio_n, gain = normalise(fade_edges(audio, params.fs))
        write_wav(RENDERS / f"{name}.wav", audio_n, params.fs)

        f0 = midi_to_hz(midi)
        mid = slice(int(0.8 * params.fs), int((stroke.duration_s - 0.3) * params.fs))
        stats = (
            motion_statistics(render, f0, beta, start_s=0.8, end_s=stroke.duration_s - 0.3)
            if stroke.vibrato_depth_cents == 0
            else None
        )
        entry = {
            "midi_note": midi,
            "f0": f0,
            "fs": params.fs,
            "oversample": params.oversample,
            "impedance": params.impedance,
            "beta": beta,
            "stroke": stroke.__dict__,
            "normalisation_gain": gain,
            "rms_dbfs": 20 * np.log10(np.sqrt(np.mean(audio_n[mid] ** 2))),
            "centroid_hz": spectral_centroid(audio_n[mid], params.fs, f_max=10000.0),
            "helmholtz_onset_s": helmholtz_onset_time(render, f0),
        }
        if stats is not None:
            entry["motion"] = stats.to_dict()
            entry["cents_audio"] = cents(estimate_f0(audio_n[mid], params.fs, f0), f0)
        measurements[name] = entry
        print(f"  {name}: {entry.get('motion', {}).get('label', 'vibrato')}")

    for midi, name in [(69, "A4_pizzicato"), (55, "G3_pizzicato")]:
        audio, render, params = render_pizzicato(midi)
        audio_n, gain = normalise(fade_edges(audio, params.fs))
        write_wav(RENDERS / f"{name}.wav", audio_n, params.fs)
        f0 = midi_to_hz(midi)
        seg = audio_n[int(0.05 * params.fs) : int(0.8 * params.fs)]
        measurements[name] = {
            "midi_note": midi,
            "f0": f0,
            "fs": params.fs,
            "normalisation_gain": gain,
            "cents_audio": cents(estimate_f0(seg, params.fs, f0), f0),
            "centroid_hz": spectral_centroid(seg, params.fs, f_max=10000.0),
        }
        print(f"  {name}")

    (RENDERS / "measurements.json").write_text(json.dumps(_clean(measurements), indent=2) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--renders", action="store_true", help="only regenerate the reference renders")
    args = parser.parse_args()

    DATA.mkdir(exist_ok=True)
    FIGURES.mkdir(exist_ok=True)
    body.save_modes(body.generate_default_modes())

    if not args.renders:
        results = {
            "strings": {
                k: {"f0": s.open_f0, "tension_n": s.tension_n, "linear_density": s.linear_density,
                    "impedance": s.impedance}
                for k, s in STRINGS.items()
            },
            "default_params": BowedStringParams().__dict__,
        }
        print("friction / helmholtz figures")
        figure_friction()
        figure_helmholtz()
        print("schelleng sweep")
        figure_schelleng(results)
        print("force sweep")
        figure_force(results)
        print("bow position sweep")
        figure_brightness(results)
        print("tuning table")
        figure_tuning(results)
        print("per-string force windows")
        results["string_force_windows"] = string_force_windows()
        results["performance"] = measure_performance()
        print("body response")
        figure_body(results)
        (DATA / "phase1_results.json").write_text(json.dumps(_clean(results), indent=1) + "\n")

    print("reference renders")
    reference_renders()


if __name__ == "__main__":
    main()
