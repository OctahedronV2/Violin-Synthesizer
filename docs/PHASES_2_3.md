# Phases 2 and 3: C++ String, Body and Output Chain

These two phases were done together, as agreed. The plugin now plays the physically modelled bowed string through measured violin bodies, in place of the Phase 0 placeholder sine.

## Signal chain

```
MIDI ─► ViolinVoice ───────────────► decimate ─► DC block ─► sordino ─► Body ─► width ─► room ─► gain ─► limiter ─► out
        (bow gestures,  BowedString    (JUCE       (mono, host rate)       (convolution    (stereo)
         at ≥176.4 kHz) waveguide      polyphase                             or modal)
                                       IIR)
```

| Stage | File | Notes |
|---|---|---|
| Bowed string | `source/dsp/BowedString.*`, `FractionalDelay.h`, `LoopFilter.h`, `FrictionJunction.h` | Line-for-line port of the Phase 1 Python model, in double precision |
| Voice | `source/engine/ViolinVoice.*`, `StringData.h` | Monophonic, last-note-priority legato, automatic string choice, bow envelope with force following speed, vibrato |
| Oversampling | `source/engine/ViolinEngine.*` | Power-of-two factor giving ≥176.4 kHz: 4× at 44.1/48 kHz, 2× at 88.2/96, 1× at 176.4/192. Latency is reported to the host. |
| Body | `source/engine/Body.*`, `source/dsp/BodyModes.h`, `resources/bodies/*.wav` | Four measured violins (Levaggi, Klimke, Stoppani, Tambovsky/Iowa). **Measured** = convolution with the impulse response; **Light** = fitted modal bank, within 0.4–1.0 dB of the IR at 1/6-octave resolution. |
| Output | `source/engine/OutputChain.*`, `Filters.h` | DC blocker, sordino (a high shelf plus low-pass before the body, as a mute on the bridge), mid/side width from decorrelating all-pass filters, JUCE reverb, gain, limiter at −0.3 dBFS |

All plugin assets are generated from the research code by `research/scripts/export_plugin_assets.py`: the body IRs, the fitted modes and the golden test data for the string.

## Playing it

| Input | Effect |
|---|---|
| Velocity | Dynamics, i.e. bow speed from 0.08 to 0.6 m/s |
| CC11 (Expression) or CC2 (Breath) | Dynamics, overriding velocity once received |
| CC1 (Mod wheel) | Bow pressure, overriding the Pressure knob once received |
| Channel or poly aftertouch | Up to 30 cents of extra vibrato depth |
| Pitch bend | ± Bend range (default 2 semitones) |
| Overlapping notes | Legato: the bow keeps going and the pitch glides (Glide knob) |
| Separate notes | A new stroke, alternating up- and down-bow |

Bow force always follows bow speed through the attack and release. The Pressure knob places it inside the playable window of the string being played, which differs per string (Phase 1 findings, §2.6). Vibrato starts after the Vib Delay time. It is centred on the note, because listeners hear roughly the mean pitch of a vibrato.

## Verification

The C++ tests are Catch2, run in CI on Linux, macOS and Windows:

- **Golden match with the Python reference:** the C++ string reproduces `_simulate` to within 1e-6 of the peak over the first 2,048 samples of four cases (Helmholtz, heavy G string, E6 at 176.4 kHz, multiple slip). The stick fraction and slip rate over 0.5 s also match.
- **Tuning:** bowed notes from G3 to E7 at 176.4 and 192 kHz are within ±2 cents, and within 3.5 cents at the top two notes (as documented in Phase 1). The whole engine plays A4 within ±2 cents at 44.1, 48 and 96 kHz, with both body qualities and at 32- and 512-sample blocks.
- **Behaviour:** legato glides to the new note; released notes fall silent; every body and quality gives bounded output. Random MIDI and parameter changes stay finite, and blocks larger than announced are handled.
- **Sanitisers:** all tests pass under AddressSanitizer and UBSan. This found and fixed a buffer overrun on oversized host blocks.
- **pluginval** at strictness 10 passes.

Rendering the demo phrases through the full plugin showed every note in pitch. It also exposed the flat bias of the Phase 1 vibrato, which is now centred.

### Level and CPU

Measured with `ViolinSynthTests "[.diagnostics]"`: an mf A4 at 48 kHz, release build.

| Body | Measured (convolution) | Light (modal) |
|---|---|---|
| Level | −18 dBFS RMS on every body (per-body calibration) | same |
| CPU, whole engine | 2.6 – 2.9% of one core | 2.5 – 2.8% |

Most of the CPU goes to the string running at 192 kHz. The plan's target is under 3% per voice.

## Deferred to later phases

- **Phase 4:** several strings sounding at once (double stops), MPE, the full expression mapping, and presets.
- **Phase 5:** pizzicato and other articulations. The string supports free vibration (`Tuning::fundamental`), but no articulation drives it yet.
- **Overpressure:** above F_max the model still turns to noise (Phase 1 findings, §3.1). The per-string pressure window keeps normal playing below it.
- **Hosts that send wildly varying block sizes:** handled correctly, at the cost of processing in chunks.

## Attribution

The measured bodies come from the CNSM Dataset (Pauget Ballesteros 2026, CC BY 4.0) and the University of Iowa Musical Instrument Samples. They are credited in the editor footer and in [research/data/SOURCES.md](../research/data/SOURCES.md).
