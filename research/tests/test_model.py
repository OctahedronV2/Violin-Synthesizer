"""Checks on the Phase 1 prototype. Run from research/:  python -m pytest"""

import math

import numpy as np
import pytest

from violin_model.analysis import cents, estimate_f0, helmholtz_onset_time, motion_statistics
from violin_model.body import BodyModel, generate_default_modes, resonator_coefficients
from violin_model.experiments import playable_force, schelleng_limits
from violin_model.friction import friction_coefficient, solve_junction
from violin_model.notes import BowStroke, render_bowed_note
from violin_model.strings import STRINGS, midi_to_hz
from violin_model.waveguide import (
    BowedStringParams,
    loss_filter_coefficients,
    pluck_excitation,
    recommended_oversample,
    simulate,
)

Z = 0.197
MU_S, MU_D, V0 = 0.8, 0.3, 0.1


# --- friction junction -------------------------------------------------------


def test_friction_curve_limits():
    assert friction_coefficient(1e-9, MU_S, MU_D, V0) == pytest.approx(MU_S, rel=1e-6)
    assert friction_coefficient(-1e-9, MU_S, MU_D, V0) == pytest.approx(-MU_S, rel=1e-6)
    assert friction_coefficient(1e6, MU_S, MU_D, V0) == pytest.approx(MU_D, rel=1e-4)


def test_junction_sticks_within_static_limit():
    force = 1.0
    v_h = 0.2 - 0.9 * MU_S * force / (2 * Z)  # just inside the static limit
    v, sticking = solve_junction(0.2, v_h, force, Z, MU_S, MU_D, V0, True)
    assert sticking
    assert v == pytest.approx(0.2)


@pytest.mark.parametrize("v_h", [-3.0, -1.0, 1.5, 4.0])
def test_junction_slip_satisfies_force_balance(v_h):
    v_bow, force = 0.2, 0.5
    v, sticking = solve_junction(v_bow, v_h, force, Z, MU_S, MU_D, V0, False)
    assert not sticking
    string_force = 2 * Z * (v - v_h)  # force the string feels from the bow
    friction = force * friction_coefficient(v_bow - v, MU_S, MU_D, V0)
    assert string_force == pytest.approx(friction, rel=1e-9, abs=1e-12)


def test_junction_without_force_passes_waves_through():
    v, sticking = solve_junction(0.2, -0.7, 0.0, Z, MU_S, MU_D, V0, True)
    assert v == -0.7 and not sticking


def test_junction_hysteresis_keeps_slipping():
    # Inside the static limit a slipping bow stays slipping if a slip solution exists.
    v_bow, force = 0.2, 1.0
    v_h = v_bow - 0.95 * MU_S * force / (2 * Z)
    _, stuck = solve_junction(v_bow, v_h, force, Z, MU_S, MU_D, V0, True)
    _, stuck_after_slip = solve_junction(v_bow, v_h, force, Z, MU_S, MU_D, V0, False)
    assert stuck and not stuck_after_slip


# --- loss filter and tuning ---------------------------------------------------


@pytest.mark.parametrize("fs", [88200.0, 192000.0])
def test_loss_filter_meets_decay_targets(fs):
    from scipy.signal import freqz

    f0, t60, t60_high, f_high = 440.0, 1.5, 0.25, 4000.0
    g, a = loss_filter_coefficients(f0, fs, t60, t60_high, f_high)
    _, h = freqz([g * (1 - a)], [1, -a], worN=[0.0, f_high], fs=fs)
    assert abs(h[0]) == pytest.approx(10 ** (-3 / (f0 * t60)), rel=1e-9)
    assert abs(h[1]) == pytest.approx(10 ** (-3 / (f0 * t60_high)), rel=1e-6)


def test_recommended_oversample():
    assert recommended_oversample(44100.0) == 4
    assert recommended_oversample(48000.0) == 4
    assert recommended_oversample(96000.0) == 2
    assert recommended_oversample(192000.0) == 1


@pytest.mark.parametrize("fs", [44100.0, 48000.0, 96000.0])
@pytest.mark.parametrize("note", [55, 69, 81, 93])
def test_pluck_tuning_within_half_a_cent(fs, note):
    params = BowedStringParams(fs=fs, oversample=recommended_oversample(fs), tuning="fundamental")
    f0 = midi_to_hz(note)
    n = int(0.6 * fs)
    exc = pluck_excitation(n * params.oversample, params.internal_fs)
    render = simulate(f0, 0.15, 0.0, 0.0, n, params, excitation=exc)
    measured = estimate_f0(render.bridge_force[int(0.02 * fs) :], fs, f0)
    assert abs(cents(measured, f0)) < 0.5


@pytest.mark.parametrize("fs", [44100.0, 48000.0])
@pytest.mark.parametrize("note", [55, 62, 69, 76, 88, 97])
def test_bowed_tuning_within_two_cents(fs, note):
    params = BowedStringParams(fs=fs, oversample=recommended_oversample(fs))
    f0 = midi_to_hz(note)
    n = int(1.2 * fs)
    force = playable_force(0.1, 0.2, params)
    render = simulate(f0, 0.1, 0.2, force, n, params)
    measured = estimate_f0(render.bridge_force[n // 2 :], fs, f0)
    assert abs(cents(measured, f0)) < 2.0


# --- bowed motion ---------------------------------------------------------------


@pytest.fixture(scope="module")
def helmholtz_render():
    params = BowedStringParams()
    return simulate(440.0, 0.1, 0.2, 0.4, int(1.2 * params.fs), params)


def test_nominal_bowing_gives_helmholtz_motion(helmholtz_render):
    stats = motion_statistics(helmholtz_render, 440.0, 0.1, start_s=0.6)
    assert stats.label == "helmholtz"
    assert stats.slips_per_period == pytest.approx(1.0, abs=0.02)
    # Helmholtz motion sticks for (1 - beta) of each period.
    assert stats.stick_fraction == pytest.approx(0.9, abs=0.02)


def test_slip_velocity_matches_helmholtz_theory(helmholtz_render):
    fs = helmholtz_render.internal_fs
    v = helmholtz_render.string_velocity[int(0.6 * fs) :]
    slipping = ~helmholtz_render.sticking[int(0.6 * fs) :]
    # Ideal Helmholtz slip velocity is -v_bow (1 - beta) / beta.
    assert np.median(v[slipping]) == pytest.approx(-0.2 * 0.9 / 0.1, rel=0.05)


def test_helmholtz_onset_is_fast_at_nominal_force(helmholtz_render):
    assert helmholtz_onset_time(helmholtz_render, 440.0) < 0.25


def test_too_little_force_gives_multiple_slipping():
    params = BowedStringParams()
    render = simulate(440.0, 0.1, 0.2, 0.03, int(1.2 * params.fs), params)
    assert motion_statistics(render, 440.0, 0.1, start_s=0.6).label == "multiple_slip"


def test_force_above_schelleng_maximum_breaks_helmholtz_motion():
    params = BowedStringParams()
    f_max, _ = schelleng_limits(np.array([0.1]), 0.2, 440.0, params)
    render = simulate(440.0, 0.1, 0.2, 1.5 * f_max[0], int(1.2 * params.fs), params)
    assert motion_statistics(render, 440.0, 0.1, start_s=0.6).label != "helmholtz"


def test_random_control_changes_stay_finite_and_bounded():
    rng = np.random.default_rng(7)
    params = BowedStringParams(oversample=2)
    n = int(0.5 * params.fs)
    steps = np.repeat(rng.uniform(0, 1, (n // 480 + 1, 4)), 480, axis=0)[:n]
    f0 = 196.0 * 2 ** (steps[:, 0] * 3)
    beta = 0.05 + 0.2 * steps[:, 1]
    v_bow = 0.5 * steps[:, 2] * np.sign(rng.standard_normal(n).cumsum())
    f_bow = 3.0 * steps[:, 3]
    render = simulate(f0, beta, v_bow, f_bow, n, params)
    assert np.all(np.isfinite(render.bridge_force))
    assert np.max(np.abs(render.string_velocity)) < 50.0


# --- body and notes ---------------------------------------------------------------


def test_default_modes_are_reproducible():
    assert generate_default_modes() == generate_default_modes()


def test_resonators_are_stable():
    for m in generate_default_modes():
        _, a = resonator_coefficients(m.freq, m.q, 48000.0)
        assert np.all(np.abs(np.roots(a)) < 1.0)


def test_body_response_peaks_at_named_modes():
    model = BodyModel(generate_default_modes())
    freqs, h = model.frequency_response(48000.0, n=1 << 16)
    mag = np.abs(h)
    for m in model.modes[:5]:
        if m.gain < 0.5:
            continue
        band = (freqs > m.freq * 0.97) & (freqs < m.freq * 1.03)
        peak_freq = freqs[band][np.argmax(mag[band])]
        assert peak_freq == pytest.approx(m.freq, rel=0.02)


def test_string_table_is_physical():
    for spec in STRINGS.values():
        assert 0.1 < spec.impedance < 0.5
        assert spec.wave_speed == pytest.approx(2 * spec.length_m * spec.open_f0)


def test_bowed_note_render_is_clean():
    audio, render, params, beta = render_bowed_note(69, BowStroke(duration_s=1.0))
    assert np.all(np.isfinite(audio))
    assert np.max(np.abs(audio)) > 0
    assert abs(audio[-1]) < 0.05 * np.max(np.abs(audio))
    assert not math.isnan(helmholtz_onset_time(render, 440.0))
