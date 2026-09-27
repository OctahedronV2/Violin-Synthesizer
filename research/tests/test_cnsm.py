"""Checks on the measured CNSM bodies. Skipped unless the dataset is present (scripts/fetch_cnsm.py)."""

import numpy as np
import pytest

from violin_model.cnsm import VIOLINS, MeasuredBody, dataset_root, load_admittance

pytestmark = pytest.mark.skipif(
    not (dataset_root() / "admittances").exists(), reason="CNSM dataset not downloaded (scripts/fetch_cnsm.py)"
)


@pytest.mark.parametrize("violin", VIOLINS)
def test_admittance_has_violin_signature_modes(violin):
    adm = load_admittance(violin)
    f, y = adm.freqs, np.abs(adm.y)
    band = (f > 240) & (f < 300)
    a0 = f[band][np.argmax(y[band])]
    assert 255 < a0 < 290  # A0 air mode
    b1 = (f > 400) & (f < 600)
    assert np.max(y[b1]) > 3 * np.median(y[(f > 150) & (f < 250)])  # B1 modes dominate the low range


@pytest.mark.parametrize("violin", VIOLINS)
def test_measured_body_impulse_response_is_compact_and_finite(violin):
    body = MeasuredBody(violin)
    assert np.all(np.isfinite(body.ir))
    energy = np.cumsum(body.ir**2) / np.sum(body.ir**2)
    assert energy[int(0.05 * body.fs)] > 0.95  # 95% of the energy within 50 ms
    out = body.process(np.random.default_rng(0).standard_normal(4800), body.fs)
    assert np.all(np.isfinite(out))
