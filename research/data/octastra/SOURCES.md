# Octastra body data

Reference data for the body plan in [docs/OCTASTRA_DESIGN.md](../../../docs/OCTASTRA_DESIGN.md#body-data-for-every-instrument), collected on 27 September 2026. Nothing here is used by either plugin yet.

## Files

| File | What it is |
|---|---|
| `rabab_transfer_function_IWK_2020_20_14404Hz.via` | Measured transfer function of a Moroccan rabāb: shaker on the bridge (bass side) to a microphone 1 m in front, 20 Hz – 14.4 kHz, 5,017 complex points. Columns: frequency (Hz), magnitude, phase (rad), real, imaginary; 9 header lines. |
| `estimate_kanun.json` | First-pass body estimate from the kanun recording below (36 notes, F3–E6, one rejected, 4.6 dB misfit) |
| `estimate_oud_kemence_rebab.json` | First-pass estimates from the oud notes, the kemençe phrase and the two rebab recordings below |

Each estimate holds `freqs` (Hz, 48 points per octave), `db` (response in dB, 0 dB at the peak) and `cov` (observations per grid point). They were made with `research/violin_model/body_estimation.py` (`segment_notes` or `segment_by_pitch`, then `harmonic_observations` and `estimate_body`), using the Freesound HQ previews (about 128 kbps MP3). The oud estimate used the three notes with their known pitches, because YIN's default 150 Hz floor misses G2–C3. The pipeline assumes a bowed (sawtooth) source, so the plucked kanun and oud estimates are only approximate.

**Not validated.** The Moroccan rebab field recording's estimate differs from the measured rabāb by about 5 dB RMS (1/3 octave, 500 Hz – 8 kHz), which is no closer than a flat line. They are different instruments and the recording was made outdoors, so the pipeline is not yet proven on skin-topped instruments.

## Sources and licences

The recordings are not committed. Download them from the links (the Freesound previews need no account).

| Data | Source | Licence | Credit |
|---|---|---|---|
| Rabāb transfer function | [Zenodo 10079957](https://zenodo.org/records/10079957); bridge admittance in [10079910](https://zenodo.org/records/10079910) | CC BY 4.0 | Thilo Hirsch and Alexander Mayer, Bern University of the Arts / mdw Vienna. Instrument by Abdessalam Chiki, Fès, 2015 |
| Kanun, isolated chromatic notes | [Freesound 211133](https://freesound.org/people/barisbozkurt/sounds/211133/) | CC0 | Barış Bozkurt (not required) |
| Oud, single notes G2, A2, C3 | [Freesound 172682](https://freesound.org/people/hammondman/sounds/172682/), [172683](https://freesound.org/people/hammondman/sounds/172683/), [172684](https://freesound.org/people/hammondman/sounds/172684/) | CC0 | hammondman (not required) |
| Kemençe, uşşak motif | [Freesound 194700](https://freesound.org/people/ajaysm/sounds/194700/) | CC BY 4.0 | "ussak_cesni_kemence.wav" by ajaysm, played by Neva Günaydın (CompMusic) |
| Rebab, Moroccan field recording | [Freesound 338238](https://freesound.org/people/brightonmatt2/sounds/338238/) | CC0 | brightonmatt2 (not required) |
| Rebab (tagged Java, may be a Javanese rebab) | [Freesound 348175](https://freesound.org/people/redafs/sounds/348175/) | CC BY 4.0 | "Rebab" by redafs |

Not used: an "oud" set on Freesound by tarane468 converted from Yamaha Motif keyboard samples, since it probably comes from a commercial instrument.
