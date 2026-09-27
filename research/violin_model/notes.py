"""Musical note rendering: bow envelopes, vibrato and the body, for reference audio."""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np

from .body import BodyModel
from .strings import midi_to_hz, string_for_note
from .waveguide import BowedStringParams, min_beta, pluck_excitation, simulate


@dataclass
class BowStroke:
    duration_s: float = 2.0
    bow_speed: float = 0.2  # m/s, sustain level
    force_fraction: float = 0.3  # of Schelleng's F_max at this beta and speed
    beta: float = 0.1
    attack_s: float = 0.06
    release_s: float = 0.12
    vibrato_rate_hz: float = 0.0
    vibrato_depth_cents: float = 0.0
    vibrato_delay_s: float = 0.35
    vibrato_onset_s: float = 0.3


def _envelope(n: int, fs: float, attack_s: float, release_s: float) -> np.ndarray:
    t = np.arange(n) / fs
    env = np.ones(n)
    a = t < attack_s
    env[a] = 0.5 - 0.5 * np.cos(np.pi * t[a] / attack_s)
    r = t > (n / fs - release_s)
    env[r] = 0.5 + 0.5 * np.cos(np.pi * (t[r] - (n / fs - release_s)) / release_s)
    return env


def vibrato_f0(f0: float, n: int, fs: float, stroke: BowStroke) -> np.ndarray:
    if stroke.vibrato_depth_cents <= 0.0 or stroke.vibrato_rate_hz <= 0.0:
        return np.full(n, f0)
    t = np.arange(n) / fs
    depth = np.clip((t - stroke.vibrato_delay_s) / stroke.vibrato_onset_s, 0.0, 1.0) * stroke.vibrato_depth_cents
    # Violinists vibrate mostly below the note, so the centre sits slightly flat.
    cents_offset = depth * 0.5 * (np.sin(2.0 * np.pi * stroke.vibrato_rate_hz * t) - 1.0)
    return f0 * 2.0 ** (cents_offset / 1200.0)


def render_bowed_note(midi_note: float, stroke: BowStroke, params: BowedStringParams | None = None):
    """Bowed note through the string and body. Returns (audio, string_render, params)."""
    spec = string_for_note(midi_note)
    base = params or BowedStringParams()
    params = BowedStringParams(**{**base.__dict__, "impedance": spec.impedance})
    f0 = midi_to_hz(midi_note)
    n = int(stroke.duration_s * params.fs)

    beta = max(stroke.beta, 1.2 * min_beta(f0 * 2.0 ** (1 / 12), params))
    f_max = 2.0 * params.impedance * stroke.bow_speed / (beta * (params.mu_s - params.mu_d))
    env = _envelope(n, params.fs, stroke.attack_s, stroke.release_s)
    v_bow = stroke.bow_speed * env
    # Force leads the speed slightly on the attack, as players "set" the bow.
    f_bow = stroke.force_fraction * f_max * np.clip(env * 1.5, 0.0, 1.0)

    render = simulate(vibrato_f0(f0, n, params.fs, stroke), beta, v_bow, f_bow, n, params)
    audio = BodyModel().process(render.bridge_force, params.fs)
    return audio, render, params, beta


def render_pizzicato(midi_note: float, duration_s: float = 2.0, pluck_position: float = 0.2,
                     amplitude: float = 0.5, params: BowedStringParams | None = None):
    spec = string_for_note(midi_note)
    base = params or BowedStringParams()
    params = BowedStringParams(**{**base.__dict__, "impedance": spec.impedance, "tuning": "fundamental",
                                  "t60": 0.8, "t60_high": 0.15})
    f0 = midi_to_hz(midi_note)
    n = int(duration_s * params.fs)
    exc = pluck_excitation(n * params.oversample, params.internal_fs, amplitude=amplitude,
                           start=int(0.01 * params.internal_fs))
    render = simulate(f0, pluck_position, 0.0, 0.0, n, params, excitation=exc)
    audio = BodyModel().process(render.bridge_force, params.fs)
    return audio, render, params


def normalise(audio: np.ndarray, peak_dbfs: float = -1.0):
    peak = float(np.max(np.abs(audio)))
    gain = 10.0 ** (peak_dbfs / 20.0) / peak if peak > 0 else 1.0
    return audio * gain, gain


def fade_edges(audio: np.ndarray, fs: float, fade_s: float = 0.005) -> np.ndarray:
    n = max(1, int(fade_s * fs))
    ramp = 0.5 - 0.5 * np.cos(np.pi * np.arange(n) / n)
    out = audio.copy()
    out[:n] *= ramp
    out[-n:] *= ramp[::-1]
    return out


def stroke_for_dynamic(dynamic: str) -> BowStroke:
    """Simple dynamic presets: louder = faster bow, same relative force."""
    speeds = {"p": 0.1, "mf": 0.2, "f": 0.4}
    return BowStroke(bow_speed=speeds[dynamic])
