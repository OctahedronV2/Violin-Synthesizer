"""Checks on the body-estimation pipeline (fast: mostly synthetic observations, no long renders)."""

import numpy as np
import pytest

from violin_model.body import BodyModel, generate_default_modes
from violin_model.body_estimation import (
    HarmonicObservations,
    compare_responses,
    estimate_body,
    fit_modes,
    harmonic_observations,
    minimum_phase_fir,
    segment_notes,
    yin_f0,
)
from violin_model.notes import BowStroke, render_bowed_note

FS = 48000.0


@pytest.fixture(scope="module")
def body():
    return BodyModel(generate_default_modes())


def body_db(body, freqs):
    w, h = body.frequency_response(FS, n=1 << 15)
    return np.interp(freqs, w, 20 * np.log10(np.abs(h) + 1e-12))


@pytest.mark.parametrize("f0", [196.0, 440.0, 1000.0, 2600.0])
@pytest.mark.parametrize("second_harmonic", [0.0, 6.0])
def test_yin_avoids_octave_errors(f0, second_harmonic):
    t = np.arange(4000) / FS
    x = np.sin(2 * np.pi * f0 * t) + second_harmonic * np.sin(4 * np.pi * f0 * t)
    assert yin_f0(x, FS) == pytest.approx(f0, rel=0.002)


def test_segmentation_finds_separated_notes():
    t = np.arange(int(0.5 * FS)) / FS
    gap = np.zeros(int(0.2 * FS))
    notes = [220.0, 330.0, 440.0]
    audio = np.concatenate([np.concatenate([gap, np.sin(2 * np.pi * f * t)]) for f in notes] + [gap])
    segments = segment_notes(audio, FS)
    assert [round(s.f0) for s in segments] == [220, 330, 440]


def test_harmonic_levels_of_a_bowed_note_equal_body_times_source(body):
    _, render, params, _ = render_bowed_note(69, BowStroke(duration_s=1.0))
    audio = body.process(render.bridge_force, params.fs)
    rec = harmonic_observations(audio, params.fs, 440.0)
    src = harmonic_observations(render.bridge_force, params.fs, 440.0)
    for n in (1, 2, 3, 5):
        f = np.median(rec.freqs[rec.harmonic == n])
        gain = np.median(rec.level_db[rec.harmonic == n]) - np.median(src.level_db[src.harmonic == n])
        assert gain == pytest.approx(body_db(body, np.array([f]))[0], abs=0.2)


def synthetic_observations(body, rng, n_notes=46, outlier_notes=()):
    """Observations a perfect sawtooth source would give, with vibrato spread and level offsets."""
    obs = []
    for k, midi in enumerate(range(55, 55 + n_notes)):
        f0 = 440.0 * 2 ** ((midi - 69) / 12)
        n = np.arange(1, int(12000 / f0) + 1)
        spread = 2 ** (rng.uniform(-30, 30, (20, 1)) / 1200)  # vibrato frames
        freqs = (n * f0 * spread).ravel()
        harmonic = np.tile(n, 20)
        level = body_db(body, freqs) - 20 * np.log10(harmonic) + rng.uniform(-10, 10) + rng.normal(0, 0.5, len(freqs))
        if k in outlier_notes:
            level = level + rng.normal(0, 12, len(freqs))
        obs.append(HarmonicObservations(freqs, level, harmonic, f0))
    return obs


def test_joint_solve_recovers_body(body):
    rng = np.random.default_rng(3)
    est = estimate_body(synthetic_observations(body, rng))
    cmp = compare_responses(est.freqs, est.response_db, body_db(body, est.freqs), smooth_octaves=1 / 6)
    assert cmp["rms_db"] < 1.0
    assert est.rejected_notes == []


def test_badly_fitting_notes_are_rejected(body):
    rng = np.random.default_rng(4)
    est = estimate_body(synthetic_observations(body, rng, outlier_notes=(5, 30)))
    assert est.rejected_notes == [5, 30]
    cmp = compare_responses(est.freqs, est.response_db, body_db(body, est.freqs), smooth_octaves=1 / 6)
    assert cmp["rms_db"] < 1.0


def test_fitted_modes_find_signature_modes(body):
    rng = np.random.default_rng(3)
    est = estimate_body(synthetic_observations(body, rng))
    modes = fit_modes(est.freqs, est.response_db, FS)
    for true in (275.0, 460.0, 540.0):  # A0, B1-, B1+ of the default table
        nearest = min(modes, key=lambda m: abs(np.log(m.freq / true)))
        assert nearest.freq == pytest.approx(true, rel=0.02)


def test_minimum_phase_fir_matches_magnitude(body):
    freqs = 180.0 * 2 ** (np.arange(0, 290) / 48)
    target = body_db(body, freqs)
    fir = minimum_phase_fir(freqs, target, FS, n_taps=4096)
    spectrum = np.fft.rfft(fir, 1 << 16)
    f = np.fft.rfftfreq(1 << 16, 1 / FS)
    got = np.interp(freqs, f, 20 * np.log10(np.abs(spectrum) + 1e-12))
    assert compare_responses(freqs, got, target, smooth_octaves=1 / 6)["rms_db"] < 0.5
    # Minimum phase: energy is concentrated at the start.
    energy = np.cumsum(fir**2) / np.sum(fir**2)
    assert energy[len(fir) // 8] > 0.9
