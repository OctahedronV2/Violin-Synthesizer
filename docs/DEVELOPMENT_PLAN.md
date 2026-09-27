# Violin Synthesizer: Development Plan

A plan for building an expressive violin synthesizer as a VST3 plugin (plus AU and Standalone) with the JUCE framework.

---

## 1. Goals and scope

**Product goal:** a lightweight, very playable violin instrument. It should respond continuously to performance gestures (bow pressure, bow speed, vibrato, bow position) instead of switching between static samples.

**In scope (v1.0)**
- VST3 on Windows, macOS and Linux, AU on macOS, and a Standalone app.
- Real-time **physical-model synthesis**: a bowed string on a digital waveguide, driven by a bow–string friction model and filtered through a violin body resonator.
- Four virtual strings (G3, D4, A4, E5). Notes are assigned to strings automatically, with legato, portamento and simple double stops.
- Expressive MIDI: velocity, CC1/CC2/CC11, aftertouch, pitch bend and **MPE**.
- Core articulations: legato, détaché, staccato, spiccato, tremolo, pizzicato, natural harmonics, sul ponticello/tasto and con sordino.
- Parameter automation, a preset system and a resizable editor.

**Out of scope for v1.0** (candidates for later versions)
- Ensemble or section mode (several violins, divisi).
- Other instruments in the family (viola, cello, bass). The architecture should make these straightforward by changing string and body data.
- A neural or DDSP timbre layer.
- Col legno battuto, sul ponticello tremolo presets and other extended techniques.

**Success criteria**
- Tuning accuracy within ±2 cents across the whole range at 44.1–192 kHz.
- Under 3 % of one core per voice at 48 kHz with a 128-sample buffer on a 2020-era laptop CPU.
- Passes `pluginval` at strictness level 10 on all platforms.
- Blind A/B listening: testers prefer the output over a basic sample-based violin for legato and vibrato phrases.

---

## 2. Choosing a synthesis approach

| Approach | Pros | Cons | Verdict |
|---|---|---|---|
| Multi-sampled library | Realistic single notes | GBs of data, weak legato and continuous control, recording cost | No |
| Additive / spectral modelling | Precise timbre control | Hard to make attacks and bow noise sound natural | Maybe as a later layer |
| **Waveguide bowed string + friction + body filter** | Continuous, gesture-driven control; tiny footprint; natural transients (Helmholtz motion, scratch, and pressure that is too low) | Needs careful tuning and stability work | **Chosen core** |
| Finite-difference / modal string | Most physically accurate | Much more CPU, more complex | Offline reference only |
| Neural (DDSP, RAVE) | Very realistic timbre | Needs a model and training data, latency and CPU concerns, harder to control | Research track after v1 |

**Decision:** build a **hybrid physical model**:

1. **Excitation:** a bow–string friction junction using a hyperbolic friction curve (McIntyre/Schumacher/Woodhouse; Smith's *Physical Audio Signal Processing*). The inputs are bow velocity, bow force and bow position β.
2. **String:** two delay lines (bridge side and nut side of the bow point), each with fractional-delay tuning (Thiran all-pass or 3rd-order Lagrange), a loop filter for frequency-dependent loss, and an optional all-pass dispersion filter for stiffness.
3. **Bridge and body:** a parallel bank of about 24–40 resonant biquads. Their frequencies, Q factors and gains come from measured violin body modes: A0 ≈ 275 Hz, CBR ≈ 400 Hz, B1− ≈ 460 Hz, B1+ ≈ 530 Hz, and the "bridge hill" at 2–3 kHz. A convolution mode (`juce::dsp::Convolution`) with a body impulse response is an optional high-quality alternative.
4. **Noise and colour:** bow-hair noise injected at the junction and scaled by bow force and speed, plus an optional sympathetic resonance from the open strings, modelled with lightly coupled idle waveguides.
5. **Output:** a mute (sordino) filter, a stereo image built from two body IRs or decorrelated mode banks, and an optional small built-in room reverb.

---

## 3. Technology stack

- **JUCE 9.x** (currently 9.0.2), included through CMake `FetchContent` and pinned to a release tag.
- **C++20** and **CMake ≥ 3.24**, using the `juce_add_plugin` formats `VST3 AU Standalone` (plus optionally `CLAP` via `clap-juce-extensions`).
- **Tests:** Catch2 v3 for DSP units, JUCE `UnitTest` where it needs plugin context, and `pluginval` in CI.
- **Prototyping:** Python with NumPy, SciPy and Jupyter under `/research`, for fast iteration on the friction and body models before porting to C++.
- **CI:** GitHub Actions with a matrix of ubuntu-latest, macos-latest and windows-latest. It builds, runs unit tests, runs pluginval and uploads the build artifacts.
- **Code quality:** `clang-format`, `clang-tidy` and, on Linux debug builds, the address and undefined-behaviour sanitizers.

**Licensing (decided):** JUCE 9 is dual-licensed under **AGPLv3** or a commercial license. The project uses **AGPLv3**: it is open source and its repository is public (see `LICENSE` and the README). The Steinberg VST3 SDK bundled with JUCE is MIT-licensed. A closed-source or commercial release would need a JUCE commercial licence.

---

## 4. Architecture

```
┌───────────────────────── PluginProcessor ─────────────────────────┐
│  MIDI in ─► ExpressionMapper ─► StringAllocator ─► 4 × StringVoice │
│              (CC/MPE/velocity     (note → string,   ┌───────────┐  │
│               → gesture targets)   legato, pizz)    │ BowModel  │  │
│                                                     │ Waveguide │  │
│                                                     │ Pluck exc │  │
│                                                     └─────┬─────┘  │
│                           bridge force (sum) ◄────────────┘        │
│                                   │                                │
│                           BodyResonator (modal bank / IR)          │
│                                   │                                │
│                         Sordino ─► Stereo/Room ─► Output gain      │
└────────────────────────────────────────────────────────────────────┘
        ▲ AudioProcessorValueTreeState (params, presets, automation)
        ▼ PluginEditor (bow/vibrato visualiser, articulation, body, FX)
```

### Proposed source layout

```
CMakeLists.txt
cmake/                      # JUCE fetch, compiler warnings, sanitizers
source/
  plugin/  PluginProcessor.{h,cpp}  PluginEditor.{h,cpp}  Parameters.{h,cpp}
  dsp/     FrictionJunction.h  Waveguide.h  FractionalDelay.h  LoopFilter.h
           StringVoice.{h,cpp}  PluckExciter.h  BodyResonator.{h,cpp}
           BodyModes.h (mode tables)  Sordino.h  Smoothers.h  Vibrato.h
  control/ ExpressionMapper.{h,cpp}  StringAllocator.{h,cpp}
           Articulations.{h,cpp}  MpeHandler.{h,cpp}
  ui/      LookAndFeel  components (BowPad XY, VibratoMeter, StringView)
  presets/ factory *.xml
tests/     dsp/ (tuning, stability, energy) control/ (allocation, MIDI)
research/  python notebooks, reference renders, body-mode extraction
docs/
```

### Key design rules
- **Real-time safety:** no allocation, locks or I/O on the audio thread. Delay lines are pre-allocated for the lowest note at the highest supported sample rate. `juce::ScopedNoDenormals` wraps each block.
- **Sample-rate independence:** every coefficient is recomputed in `prepareToPlay` and derived from physical units (Hz, seconds, m/s, N).
- **Control-rate and audio-rate split:** gesture parameters are smoothed per sample (`juce::SmoothedValue` or one-pole filters). Expensive coefficient updates, such as the loop filter and body, run at a control rate of about 32 samples.
- **Oversampling:** the string and friction junction run at an internal rate of at least 176.4 kHz (4× at 44.1/48 kHz, 2× at 88.2/96 kHz). Phase 1 found that lower rates let stick/slip timing snap to the sample grid and detune the top octave by up to 10 cents (see [PHASE1_FINDINGS.md](PHASE1_FINDINGS.md)). The body model runs at the host rate. The added latency must be reported.
- **Headroom and safety:** a DC blocker, a soft limiter on the output, and a NaN/Inf guard that resets a voice instead of producing noise.

---

## 5. Parameters (initial set)

| Group | Parameter | Range / notes |
|---|---|---|
| Bow | Force | 0–1 (mapped to the physical range for each string) |
| Bow | Velocity | 0–1 (m/s mapping), or taken from expression |
| Bow | Position β | 0.02–0.25 (ponticello ↔ tasto) |
| Bow | Noise | 0–1 |
| Bow | Attack / release shape | ms |
| Vibrato | Rate, depth, delay, onset time | Hz, cents, ms |
| Vibrato | Humanise | random rate and depth drift |
| Pitch | Portamento time and curve; legato mode | ms; always / legato-only / off |
| String | Inharmonicity, damping (hi/lo), sympathetic amount | |
| Body | Model (Mode bank A/B/C, IR), size, brightness | |
| Output | Sordino, stereo width, room mix, gain | |
| Expression | CC mappings for force / velocity / vibrato, velocity curve, MPE on/off | |

All of these live in `AudioProcessorValueTreeState` with stable parameter IDs, so automation and presets stay compatible between versions.

---

## 6. Expressive control mapping (defaults)

| Source | Target |
|---|---|
| Note-on velocity | Initial bow force and attack sharpness (accent) |
| CC11 (Expression) | Bow velocity, which sets dynamics |
| CC1 (Mod wheel) | Bow force / pressure |
| CC2 (Breath) | Alternative source for bow velocity |
| Channel or poly aftertouch / MPE pressure | Vibrato depth |
| MPE slide (CC74) | Bow position β |
| Pitch bend / MPE per-note bend | Pitch (±2 st default, ±48 st in MPE) |
| Keyswitches (C0–B0, configurable) | Articulation selection |
| Sustain pedal (CC64) | Forced legato |

**Automatic behaviour when no continuous controller is present:** when CC11 and CC1 are idle, derive a bow envelope from velocity and note length, so the synth still sounds good played from a plain keyboard.

---

## 7. String allocation and articulations

**StringAllocator**
- A violin has 4 strings, each monophonic, so the instrument allows at most 4 simultaneous notes (double and triple stops).
- A new note goes to the highest string whose range contains it (the usual violinist choice), unless a string is already sounding and legato applies. In that case the same string glides or re-stops.
- Legato on the same string changes the delay length with a short smoothed "finger" transition. Crossing strings hands the bow over with a small overlap.
- Open-string notes can use the undamped open string, which gives a natural brighter timbre.

**Articulations**

| Articulation | Implementation |
|---|---|
| Legato | Keep the bow moving; smooth pitch change and optional portamento |
| Détaché | New bow stroke for each note: brief velocity dip and reversal |
| Staccato / martelé | Force spike at onset, fast stop (bow stays on string) |
| Spiccato | Bouncing-bow force envelope: short force pulses, bow lifted between them |
| Tremolo | Rapid velocity reversals at a set rate with random jitter |
| Pizzicato | Bow disabled; plucked excitation (shaped impulse at pluck position) |
| Natural harmonics | Light-touch damping node in the waveguide at 1/2, 1/3, 1/4 of the string length |
| Sul ponticello / tasto | Bow position β preset near the bridge or over the fingerboard |
| Con sordino | Low-pass and notch filter on the bridge-to-body transfer |

---

## 8. Milestones

Durations assume one developer working part-time and are only indicative.

### Phase 0: Project foundation (1 week) — done
- CMake project that fetches JUCE and builds an empty VST3, AU and Standalone plugin.
- GitHub Actions matrix build, Catch2 test target and pluginval step.
- `.clang-format`, `.clang-tidy`, `.editorconfig`, and a README with build instructions.
- **Done when:** CI is green on all 3 OSes and the empty plugin loads in a DAW.

### Phase 1: DSP research prototype (2 weeks) — done, see [PHASE1_FINDINGS.md](PHASE1_FINDINGS.md)
- Python notebook with a waveguide bowed string and friction junction. Check that stable Helmholtz motion appears inside the Schelleng diagram's playable force range.
- Extract or choose body mode tables from published measurements and design the modal bank.
- Produce reference renders (WAV) and measurements for later regression tests.
- **Done when:** a convincing sustained bowed tone and pizzicato are rendered offline, and the parameter ranges are documented.

### Phase 2: Core C++ DSP, single string (3 weeks) — done together with Phase 3, see [PHASES_2_3.md](PHASES_2_3.md)
- `FractionalDelay`, `LoopFilter`, `FrictionJunction`, `Waveguide` and `StringVoice`.
- Tuning compensation for the loop filter's phase delay.
- Unit tests: pitch accuracy within ±2 cents (autocorrelation or YIN) over the full range and at 44.1, 48, 96 and 192 kHz. Stability: no NaN and bounded output under random parameter sweeps.
- **Done when:** the Standalone plays one monophonic bowed string from MIDI with correct pitch.

### Phase 3: Body, output chain and oversampling (2 weeks) — done, see [PHASES_2_3.md](PHASES_2_3.md) and [BODY_MODELLING.md](BODY_MODELLING.md)
- `BodyResonator` modal bank with a preset table, plus a convolution option.
- Sordino, DC blocker, limiter and stereo widening.
- 2× oversampling around the string and bow section, with latency reporting.
- **Done when:** A/B comparison against reference recordings is judged "violin-like", and the CPU budget is measured.

### Phase 4: Four strings, allocation and expression (3 weeks) — done, see [PHASE4.md](PHASE4.md)
- `StringAllocator`, legato and portamento, double stops, sympathetic resonance.
- `ExpressionMapper` with MIDI CC, aftertouch, pitch bend and MPE (`juce::MPEInstrument`).
- Vibrato engine with humanisation, and the automatic bow envelope for keyboard-only playing.
- **Done when:** melodic phrases can be played expressively with a keyboard alone, and also with an MPE controller (Seaboard/Linnstrument/Osmose).

### Phase 5: Articulations (2–3 weeks) — done, see [PHASE5.md](PHASE5.md)
- Keyswitches and the articulation state machine, covering every item in §7.
- Tests for articulation transitions: no clicks and no stuck notes.
- **Done when:** each articulation is audible and demonstrable in a demo MIDI file.

### Phase 6: UI and presets (3 weeks) — implemented, see [PHASE6.md](PHASE6.md); UX review and multi-DAW checks pending
- Editor: bow XY pad (force × position), velocity meter, vibrato controls, string display showing which string plays which note, body selector and FX section.
- Resizable vector UI with a custom `LookAndFeel`; accessibility labels.
- Preset browser and 20–30 factory presets (Solo Romantic, Baroque, Folk Fiddle, Sordino, Pizz, etc.).
- **Done when:** a UX review is complete and state save/restore round-trips in several DAWs.

### Phase 7: Optimisation and hardening (2 weeks)
- Profiling (perf, Instruments, VTune), SIMD for the modal bank (`juce::dsp::SIMDRegister` or a structure-of-arrays layout), and control-rate tuning.
- DAW compatibility matrix: Reaper, Ableton Live, Bitwig, Cubase, Logic, FL Studio, Studio One.
- pluginval at strictness 10; testing with sample-rate and buffer-size changes and offline rendering.
- **Done when:** the performance targets in §1 are met, there are no pluginval failures and no crashes in the DAW test matrix.

### Phase 8: Release (1–2 weeks)
- Code signing: an Apple Developer ID with notarisation, and Authenticode on Windows.
- Installers: a macOS `.pkg` via `pkgbuild`/`productbuild`, Inno Setup or WiX on Windows, and a tarball or `.deb` on Linux.
- User manual, demo audio and video, changelog, and version tagging with release automation in CI.
- **Done when:** a v1.0.0 GitHub release has signed installers.

**Total:** about 20–23 weeks part-time. Phases 1–4 are the critical path. UI work (Phase 6) can start in parallel after Phase 2, using stub parameters.

---

## 9. Testing strategy

- **Unit tests (Catch2):** fractional-delay accuracy, filter responses, friction-junction solution continuity, allocator decisions and MIDI mapping.
- **Signal tests:** pitch accuracy, onset time (bow attack under 60 ms at mf), spectral centroid trends as bow position changes, and absence of aliasing above a threshold.
- **Regression renders:** fixed MIDI files are rendered offline and compared against golden files with a tolerance on spectral distance, not bit-exact output.
- **Stress and fuzz tests:** random parameter automation, extreme buffer sizes (1–4096), sample-rate switches in the middle of playback, and MIDI floods.
- **Plugin validation:** pluginval (strictness 10), the Steinberg VST3 validator, and `auval` on macOS.
- **Listening tests:** periodic blind A/B sessions with string players; their feedback is recorded in `docs/listening-tests.md`.

---

## 10. Risks and mitigations

| Risk | Impact | Mitigation |
|---|---|---|
| Friction model unstable or "squeaky" at some force and velocity combinations | High | Prototype in Python first; clamp to the Schelleng playable region by default; use oversampling; use a smooth friction curve with a hysteresis option |
| Output sounds synthetic or "buzzy" | High | Invest in body modelling (measured modes and an IR option), bow noise, vibrato humanisation and listening tests with players |
| Tuning drift from the loop filter and interpolation | Medium | Compensate for phase delay analytically and verify with automated pitch tests |
| CPU cost with 4 strings, oversampling and a large modal bank | Medium | Update coefficients at control rate, use SIMD, and offer an oversampling quality switch |
| Too complex to play from a plain keyboard | Medium | Automatic bow envelope and sensible defaults; "Easy" presets |
| JUCE licensing constraints | Resolved | AGPLv3 chosen; the project is open source |
| Code-signing and notarisation friction | Low | Automate it in CI early, in Phase 7 |

---

## 11. Future work (post v1.0)
- Ensemble mode with detuned and humanised instances and section body IRs.
- Viola, cello and double bass variants using the same engine with different string and body data.
- Neural timbre refinement (DDSP or a small real-time network) as a post-processing layer.
- CLAP with per-note modulation, and bow-controller hardware support such as sensors or an OSC mapping.
- A "Performance AI" that infers bowing from phrasing: automatic bow changes, slurs and dynamics shaping.

---

## 12. References
- J. O. Smith, *Physical Audio Signal Processing*, CCRMA (bowed strings, waveguides, fractional delay).
- M. E. McIntyre, R. T. Schumacher, J. Woodhouse, "On the oscillations of musical instruments," JASA 74(5), 1983.
- S. Serafin, *The sound of friction: real-time models, playability and musical applications*, PhD thesis, Stanford, 2004.
- M. Demoucron, *On the control of virtual violins*, PhD thesis, KTH/IRCAM, 2008.
- J. Woodhouse, "The acoustics of the violin: a review," Rep. Prog. Phys. 77, 2014.
- S. Bilbao, *Numerical Sound Synthesis*, Wiley, 2009.
- STK (Synthesis ToolKit) `Bowed` class, as a baseline reference implementation.
- JUCE documentation: `AudioProcessorValueTreeState`, `dsp::Oversampling`, `MPEInstrument`, `dsp::Convolution`.
