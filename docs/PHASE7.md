# Phase 7: Optimisation, Benchmarks and DAW Compatibility (plan)

This is the plan for Phase 7. It replaces the short outline in [DEVELOPMENT_PLAN.md](DEVELOPMENT_PLAN.md) §8 with measured numbers, concrete targets and a work order.

Phase 7 has three goals:

1. **Make the engine cheaper**, without changing how it sounds, so the violin is light and the same engine can run a whole orchestra in [Octastra](OCTASTRA_DESIGN.md) (about 60 players).
2. **Measure it properly**: a benchmark suite and a sound-regression check that run on every change, so speed-ups are proven and nothing gets slower or sounds different by accident.
3. **Prove it works in real hosts**: a DAW compatibility matrix, sample-rate and buffer-size changes, offline rendering and real-time safety.

## Where the CPU goes today

Measured on `main` (a529d62) before any changes, in a cloud Linux VM with a 2.1 GHz Xeon, GCC 13, Release with LTO. Each case renders 10 s of audio through the whole engine with the default settings (sympathetic resonance 30%, small room). The 2.6–2.9% in [PHASES_2_3.md](PHASES_2_3.md) was measured on a faster machine; the same test gives 3.5–4.0% here, so compare rows with each other, not with that number.

| Case (48 kHz, 128-sample buffer unless noted) | CPU, % of one core |
|---|---|
| **Idle, no notes playing** | 1.9–2.9 |
| One note, measured body (convolution) | 4.9 |
| One note, light body (modal) | 4.3 |
| Four-note chord, measured body | 12.4 |
| One note at 44.1 kHz | 4.5 |
| One note at 96 kHz | 7.1 |
| One note, 32-sample buffer | 6.2 |
| One note, 1024-sample buffer | 4.3 |

So the violin costs about **2.4% of a core before it plays anything**, and about **2.5% for each note** on top. The original target (under 3% per voice at 48 kHz, 128-sample buffer, on a 2020-era laptop) is roughly met for one note on a fast machine, but not with chords, small buffers or 96 kHz.

### Profile of one note

From Callgrind (instruction counts, both body types):

| Part | Share of the work | What it is |
|---|---|---|
| String voice at 192 kHz, excluding the string itself | ≈ 43% | Envelope, glide, vibrato, bends, bow position, speed and force, **recomputed every internal sample**. Most of it is maths library calls: `exp`, two or three `pow` and a `sin` per sample. There were 45 million `pow` calls for 15 million internal samples. |
| Bowed string (`BowedString::process`), played string | ≈ 12% | Two Lagrange delay reads, loop filter, friction solve |
| Sympathetic strings | ≈ 9% | Three open-string waveguides at 48 kHz |
| Measured body (convolution) | ≈ 19% when on | JUCE's convolution with its built-in fallback FFT |
| Stereo width, room and limiter | ≈ 10% | Decorrelating all-passes, JUCE reverb, limiter |
| Oversampling | ≈ 6% | JUCE polyphase IIR, up and down |

### What the profile shows

1. **The control maths costs more than the physics.** Pitch, vibrato and bow speed change slowly, but they are recomputed 192,000 times a second. This is the biggest single saving, and it scales with every note and every Octastra player.
2. **Idle is half a note.** With no notes playing, the engine still runs the convolution on silence (40% of the idle cost), the sympathetic strings on silence (17%), the 192 kHz string loop and the oversampler. `BowedString::minBeta` is recalculated for each open string on every block, which is 40 `atan2` calls each time (7.5% of idle). In an orchestral template with 60 tracks loaded and most of them silent, idle cost is what fills the CPU meter.
3. **The upsampler does nothing useful.** `ViolinEngine::renderString` clears a host block and upsamples it only to get an internal-rate buffer, which the violin then overwrites. Only the downsampler is needed.
4. **The measured body is slow on Windows and Linux.** JUCE only uses a fast FFT on macOS (Accelerate) or with IPP or FFTW enabled. Everywhere else it uses its fallback FFT. Windows is the main platform, so this matters most there. The per-block overhead is also why a 32-sample buffer costs 45% more than a 128-sample one.
5. **Vibrato keeps recomputing the loop filter.** A pitch change of more than 0.17 cents recalculates the loop coefficients and the harmonic phase delay (40 `atan2` calls). Under vibrato this happens often.

## Targets

Targets are measured with the benchmark suite below on two machines: the **CI runner** (Linux, for regression tracking) and **Jake's Windows 11 PC** (the absolute numbers that matter). The benchmark prints the CPU model it ran on, so the reference CPU is recorded the first time it runs there. Percentages are of one core at 48 kHz with a 128-sample buffer, default settings.

| # | Target | Today (VM) | Goal |
|---|---|---|---|
| T1 | One sustained note with vibrato | 4.9% | ≤ 2.0% |
| T2 | Each extra note (chord) | ≈ 2.5% | ≤ 1.0% |
| T3 | Idle, once the tail has died away | 1.9–2.9% | ≤ 0.1% |
| T4 | Worst block, four notes, 32-sample buffer | not measured | ≤ 30% of the block's duration, including the p99.9 block |
| T5 | 32-sample buffer overhead versus 128 | +45% | ≤ +15% |
| T6 | 96 kHz versus 48 kHz | +45% | ≤ +25% |
| T7 | 16 violin engines playing, summed (the Octastra section stand-in) | not measured | ≤ 20% on Jake's PC, and a measured per-player cost to set the Octastra CPU target |
| T8 | Real-time safety | not checked | No allocation, lock or system call on the audio thread, checked in CI |
| T9 | Sound | — | Unchanged within the tolerances below |

The goals for T1 and T2 assume the control-rate work (7.2) delivers about half of what the profile suggests. They will be tightened once it is measured.

### "Sounds the same"

Several optimisations can't be bit-exact: moving pitch maths to control rate changes the last few bits. Before any change, a set of reference renders is committed from the current `main` and every later build is compared with it:

- **Pitch:** the pitch track stays within 0.2 cents of the reference (the plan's tuning target is ±2 cents).
- **Tone:** the long-term spectrum stays within 0.5 dB in every third-octave band from 100 Hz to 16 kHz.
- **Level:** the loudest 100 ms stays within 0.1 dB.
- **Onsets:** the attack of every note reaches half its peak within 1 ms of the reference.
- **Exact where possible:** changes that only skip work on silence, or move work without changing it, must null to within −120 dB.

The reference phrases cover a sustained note with vibrato, a legato line with slides, a four-note chord, every articulation (from `docs/demo/articulations.mid`), MPE bends and pressure, and the automatic bow change. A listening check on the same phrases closes each step, because metrics can't catch everything.

## 7.0 Benchmarks and the sound check (first)

Nothing is optimised until this exists, so every later step can show its gain.

**A benchmark executable** (`ViolinSynthBench`, built with the tests, not shipped). Each scenario reports the mean CPU, the p99 and p99.9 block times against the block's duration, and the instructions per output sample.

| Scenario | Why |
|---|---|
| Idle, right after a note, and idle after the tail | T3, and the orchestral-template case |
| One note, sustained with vibrato | T1 |
| Chords of 2, 3 and 4 notes | T2 |
| Legato phrase with slides | Pitch changes every sample |
| Tremolo, spiccato and pizzicato bursts | Envelope and articulation paths |
| MPE: four notes with independent bends and pressure | Per-note expression |
| All parameters automated every block | Parameter smoothing and settings copies |
| Preset and body changes while playing | Must not spike |
| Sample rates 44.1, 48, 88.2, 96, 176.4, 192 kHz | Oversampling factor changes |
| Buffers 16 to 2048 samples, and randomly varying sizes | Small buffers, FL Studio-style hosts |
| 1, 4, 8, 16, 32 and 60 engines summed | Octastra scaling (T7) |

**In CI:** a Linux job runs the suite under Callgrind and compares **instruction counts** with a committed baseline (`docs/benchmarks/baseline.json`). Instruction counts are stable on shared runners, where timings are not. The job fails on a regression of more than 3%. Wall-clock numbers are printed on every platform for information. When a change is meant to speed things up, the baseline is updated in the same PR, so the gain is visible in the diff.

**The sound check:** a test that renders the reference phrases and applies the tolerances above. The reference WAVs are rendered once from `main` and committed. The existing golden tests stay as they are.

**On Jake's Windows 11 PC:** the same executable, run by hand, prints the CPU model and a table to paste into the PR. This gives the absolute numbers. The Windows CI job also prints wall-clock numbers, as a rough check between runs on Jake's PC.

## 7.1 Remove wasted work (low risk, sound unchanged)

Each of these must null against the reference.

| Change | Where | Expected saving |
|---|---|---|
| Skip the sympathetic strings when nothing drives them and their ringing has died away | `SympatheticStrings.cpp` | Most of the 17% of idle |
| Compute each open string's `f0`, `beta` and `minBeta` once per prepare or parameter change, not every block | `SympatheticStrings.cpp` | 7.5% of idle |
| Skip the 192 kHz string loop and the downsampler when all four strings are open and silent, flushing the filter tail first | `ViolinEngine.cpp`, `Violin.cpp` | The rest of the string cost at idle |
| Drop the useless upsampling; keep an internal-rate buffer and only downsample | `ViolinEngine.cpp` | About 3% of a note, 4.5% of idle |
| Bypass body, width, room and limiter when their input and tails are silent (tail length known for each) | `Body.cpp`, `OutputChain.cpp` | Idle goes to about zero (T3) |
| Skip the room reverb when **Room** is 0 and its tail is gone, and the width all-passes when **Width** is 0 | `OutputChain.cpp` | Up to 10% for dry presets |

Bypassing on silence has to be seamless: a note must start on the very next sample with no click, and a tail must never be cut. Tests check both.

**Status (body and idle PR):** done except two rows: skipping the 192 kHz string loop, and the skips for **Room** or **Width** at 0.

- The sympathetic strings, the body (both forms) and the chain after the body each stop computing once their input has been below −200 dBFS for longer than their own tail, and start on the first sample of sound. The reverb never decays to zero in float (it settles near −140 dBFS), so the chain after the body ends its tail at −120 dBFS.
- The open strings' tuning is computed in `prepare()`, and the upsampler is gone: the string renders straight into the oversampler's buffer. Both are bit-exact.
- Idle after a note has rung out: **2.6% → 0.34%** (Linux VM, same method as the table above). What remains is the string loop and the downsampler, which wait for 7.2 because skipping them changes when a silent voice's control-rate updates fall.

## 7.2 Control-rate voice maths (largest gain)

The voice recomputes pitch, vibrato, bends, bow position, speed and force every internal sample. They will be computed every 32 internal samples (6 kHz at 192 kHz, far faster than any gesture) and interpolated linearly in between.

- **Pitch** is interpolated in log frequency. Vibrato uses a rotating phasor instead of `sin`, and `pow(2, x)` becomes `exp2` on the control-rate value only.
- **Bow speed and force** are interpolated linearly. `pow(dynamics, 1.5)` becomes `d * sqrt(d)`.
- **Envelopes** (attack, release, staccato bite, spiccato bounce, tremolo reversals) stay per sample where their timing is sharper than a control period. These are cheap once the transcendental calls are gone. The staccato and spiccato onsets are the risky ones; the onset tolerance above checks them.
- **Loop-filter delay under vibrato:** the harmonic phase delay depends only on the string and the pitch, so it is tabulated per string (for example every 5 cents over the string's range) and interpolated, instead of 40 `atan2` calls each time the pitch moves.

**Expected:** 30–40% less per note (T1, T2). This scales directly to every Octastra player.

**Sequencing:** this touches `StringVoice.cpp`, which the legato-stutter and bow-noise work is changing now. 7.2 starts after that work is merged, and its fixes join the reference renders first.

## 7.3 A faster measured body

- **Replace JUCE's fallback FFT** with a partitioned convolution on PFFFT (BSD licence, SSE and NEON, a single C file). It gives the same speed on all three platforms. FFTW was considered and rejected as a large dependency to build in CI.
- **Non-uniform partitions:** a short first partition for low latency and longer ones for the rest of the impulse response. This removes most of the small-buffer penalty (T5). It also allows reporting **zero latency** for the body, down from JUCE's block-sized latency.
- **Trim the impulse responses:** the measured bodies are 200 ms (the Iowa body 43 ms). Most body modes have decayed well before 200 ms. Trimming to the shortest length that passes the sound check cuts the cost further.
- **Resample the IRs once** at `prepare` for the host rate, as now, but off the audio thread and without reallocating when only the body changes.

**Expected:** the measured body costs 2–4× less and becomes nearly as cheap as the light body. For Octastra, bodies run per body bus, not per player, so this matters for sections more than for players.

**Status (body and idle PR):** done, without trimming.

- `source/dsp/PartitionedConvolution.cpp`: uniform partitions of about 2.7 ms (128 samples at 44.1/48 kHz, 256 at 88.2/96, 512 above), on PFFFT, vendored in `third_party/pffft`. Every host call transforms its partial block, so the body has **zero latency** at any buffer size, and a small buffer costs one extra FFT pair instead of more partitions. Non-uniform partitions weren't needed for the targets.
- All four bodies are resampled (the way JUCE did) and transformed in `prepare()`. A body change is a 50 ms crossfade on the audio thread, with no loading or allocation; both bodies share the input history, so the new one starts with its full tail. The first note after `prepare()` now goes through the body; JUCE's background load played it dry for the first few blocks.
- Output nulls with `main` to −120 dB below the peak (float rounding).
- One note with the measured body now costs the same as the light body. Before and after, 48 kHz, one note with vibrato unless noted, Linux VM:

| Case | `main` | This PR |
|---|---|---|
| Idle, never played | 2.6% | 0.35% |
| Idle, 5 s after a note | 2.6% | 0.34% |
| One note, measured body | 5.0% | 4.0% |
| One note, light body | 4.8% | 4.0% |
| Four-note chord, measured body | 13.0% | 11.7% |
| One note, 32-sample buffer | 6.2% | 4.2% |
| One note, 1024-sample buffer | 4.3% | 4.1% |
| One note, 96 kHz | 7.4% | 5.3% |

## 7.4 Real-time safety

- **RealtimeSanitizer** (Clang 20's `-fsanitize=realtime`): mark `processBlock` as non-blocking and run the test suite in a CI job. It fails on any allocation, lock or system call reached from the audio thread.
- **Audit the known risks:** the convolution IR swap, preset loading, the parameter-to-settings copy each block, and the editor's meters reading engine state.
- **Denormals:** `processBlock` already sets `ScopedNoDenormals`. Add a test that plays a note, lets it decay for 30 s and checks the idle cost doesn't rise as the tails fall towards zero.

## 7.5 Experiments for Octastra

These don't have to ship in the violin, but Octastra's CPU target depends on their answers. Each one ends with a measured result and a decision, written up in this document.

| Experiment | Question | Pass condition |
|---|---|---|
| **SIMD across strings** | Can four or eight strings run in one AVX2/NEON register (structure-of-arrays state, masked stick/slip branch, gathered delay reads)? | At least 2× per string with 4 strings playing, and bit-exact to the scalar version |
| **Single precision** | Can the string run in `float` instead of `double`? That doubles the SIMD width. | Tuning tests stay within ±2 cents at all sample rates, and the sound check passes |
| **Lower internal rate for low strings** | Can the G string (and later cello and bass) run at 88.2/96 kHz and stay in tune? | Tuning within ±2 cents and the sound check passes for that string ([OCTASTRA_DESIGN.md](OCTASTRA_DESIGN.md) raises this for cello and bass) |
| **Shared sympathetic strings** | Does a section need open-string resonators per player, or can one set per section serve? | A listening check on a 16-player section |
| **Scaling** | What does the 60-engine benchmark cost after 7.1–7.3? | Sets the Octastra CPU target for its phase O2 |

If SIMD passes, the violin itself gets it for chords, and it becomes the core of Octastra's section engine.

**Also measured:** the effect of `-march` choices. A build with runtime dispatch (an AVX2 path with an SSE2 fallback) is worth it only if the SIMD experiment shows a gain. `-ffast-math` stays off, because it changes the friction solve's numerics.

## 7.6 DAW compatibility

### Automated, in CI

- pluginval at strictness 10 on all three platforms and auval on macOS (already running).
- **Steinberg's VST3 validator** from the VST3 SDK, on all three platforms.
- **A host-behaviour test** in the test suite, through the plugin's own processor:
  - Sample-rate changes mid-session (44.1 → 96 → 48 kHz) keep the tuning within ±2 cents and don't crash.
  - A maximum block size of 16 up to 4096, and blocks larger than announced.
  - **Offline rendering** (`isNonRealtime`) gives the same output as real-time rendering.
  - **Reported latency** matches the actual delay of a click through the engine, at every sample rate and in both body modes, so host latency compensation lines up.
  - States saved by earlier versions (fixtures committed from Phase 6) still load and sound the same.
  - Bypass, reset and `releaseResources` followed by `prepareToPlay` repeated 100 times.

### By hand, in real hosts

A checklist, `docs/DAW_TESTS.md` (written in 7.6), with one row per host and format. Windows 11 is the main platform, and FL Studio, Reaper and Ableton Live must pass there. Jake runs those; the rest are best effort.

| Host | OS | Formats | Priority |
|---|---|---|---|
| FL Studio | Windows 11 | VST3 | Must |
| Reaper | Windows 11 | VST3 | Must |
| Ableton Live | Windows 11 | VST3 | Must |
| Bitwig Studio | all three | VST3 | Should (MPE) |
| Logic Pro | macOS | AU | Should (MPE, AU-only) |
| Cubase | Windows / macOS | VST3 | Should (VST3 reference host) |
| Studio One | Windows / macOS | VST3, AU | Could |
| Ardour, Carla | Linux | VST3 | Could |

Checks in each host:

- Load the plugin, play, and open, close and resize the editor, including at Windows display scaling of 125%, 150% and 200%.
- With both an ASIO driver and WASAPI, at buffer sizes from 32 to 1024. Ableton and Reaper can change the buffer size while playing.
- Record automation and play it back. Switch presets while playing.
- Save the project, close and reopen it: the sound, the preset name and the edits come back.
- Change the sample rate and the buffer size with the project open.
- Offline bounce matches real-time playback; freeze and render in place work.
- Latency compensation: a bounced note lines up with a click track.
- MPE, where the host supports it.
- **16 instances playing** a four-part phrase. Record the host's CPU meter. This is the Octastra template workflow.

## Work order

| Step | Content | Depends on |
|---|---|---|
| 7.0 | Benchmark suite, CI regression job, reference renders and sound check | — |
| 7.1 | Remove wasted work | 7.0 |
| 7.2 | Control-rate voice maths | 7.0, legato-stutter and bow-noise work merged |
| 7.3 | Faster measured body | 7.0 |
| 7.4 | Real-time safety | — (can run in parallel) |
| 7.5 | Octastra experiments | 7.1–7.3, so they start from the cheaper engine |
| 7.6 | DAW matrix | Automated part in parallel; the hand checks last, on the final build |

Each step is its own PR, with the benchmark table before and after in its description.

## Done when

- T1–T8 are met on Jake's Windows 11 PC, and T1–T6 are tracked in CI.
- The sound check passes on every reference phrase, and a listening check confirms it.
- pluginval, auval and the VST3 validator pass on all platforms, and RealtimeSanitizer reports nothing.
- The DAW checklist passes in FL Studio, Reaper and Ableton Live on Windows 11, with no crashes in any host tried.
- Each Octastra experiment has a measured result and a decision in this document.

## Decisions

Jake, 2026-09-27:

- **Reference machine:** Jake's Windows 11 PC. FL Studio, Reaper and Ableton Live must pass there.
- **Tolerances:** the "sounds the same" tolerances above are accepted, so the control-rate work (7.2) goes ahead. Output is not required to be bit-exact.
- **FFT library:** PFFFT for the measured body (7.3).
