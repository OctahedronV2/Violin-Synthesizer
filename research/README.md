# Research prototype (Phase 1)

A Python/numba model of the bowed violin string, used to check the physics and settle parameters before the C++ implementation. Results are written up in [docs/PHASE1_FINDINGS.md](../docs/PHASE1_FINDINGS.md).

## Setup

Python 3.11+:

```sh
cd research
python -m venv .venv && . .venv/bin/activate      # Windows: .venv\Scripts\activate
pip install -r requirements.txt
```

## Run

```sh
python -m pytest                    # model checks (~30 s)
python scripts/run_phase1.py        # all sweeps, figures and reference renders (~20 s)
python scripts/run_phase1.py --renders   # reference renders only

# Body from open data (docs/BODY_MODELLING.md)
python scripts/validate_body_estimation.py         # pipeline check on synthetic data (~1.5 min)
python scripts/fetch_cnsm.py --recordings          # CNSM dataset, CC BY 4.0 (654 MB download)
python scripts/radiation_from_cnsm.py              # radiation balance from 84 min of recordings (~6 min)
python scripts/render_measured_bodies.py           # renders through the measured bodies
python scripts/estimate_body_from_recordings.py <files> --name <name>   # any other recordings

# Pizzicato against recordings (docs/PIZZICATO.md)
python scripts/measure_pizzicato.py [<render folder>]   # Iowa pizzicato runs, 105 MB download
```

Third-party data and attribution: [data/SOURCES.md](data/SOURCES.md).

## Quick experiment

```python
from violin_model.waveguide import BowedStringParams, simulate
from violin_model.analysis import motion_statistics
from violin_model.body import BodyModel

params = BowedStringParams()                      # 48 kHz, 4x oversampled, A string
r = simulate(f0=440.0, beta=0.1, v_bow=0.2, f_bow=0.4, num_samples=48000, params=params)
print(motion_statistics(r, 440.0, beta=0.1, start_s=0.5).label)   # 'helmholtz'
audio = BodyModel().process(r.bridge_force, params.fs)
```

`f0`, `beta`, `v_bow` (m/s) and `f_bow` (N) accept either scalars or per-sample arrays.

## Layout

```
violin_model/   model and analysis code (see the findings doc for a module map)
scripts/        run_phase1.py regenerates everything below
tests/          pytest checks (also run in CI)
data/           body_modes.json, phase1_results.json
figures/        plots used in the findings
renders/        reference WAVs + measurements.json for Phase 2 regression tests
```
