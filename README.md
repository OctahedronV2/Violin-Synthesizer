# Violin-Synthesizer

An expressive, physically modelled violin synthesizer plugin (VST3 / AU / Standalone) built with JUCE.

See [docs/DEVELOPMENT_PLAN.md](docs/DEVELOPMENT_PLAN.md) for the synthesis approach, architecture and milestones.

> **Status: Phases 0–6 implemented.** The plugin models the whole violin: four physically modelled bowed strings (digital waveguides with a bow–string friction model) sharing one bow, measured violin bodies, double stops, legato and string crossings, sympathetic resonance, humanised vibrato, MPE, and ten articulations from staccato to pizzicato. It comes with 27 level-matched factory presets and a resizable editor. See [docs/PHASES_2_3.md](docs/PHASES_2_3.md), [docs/PHASE4.md](docs/PHASE4.md), [docs/PHASE5.md](docs/PHASE5.md) and [docs/PHASE6.md](docs/PHASE6.md).

![The editor](docs/editor.png)

---

## Trying the plugin in a DAW

### 1. Get a build

The **Build** workflow on GitHub Actions builds and tests the plugin. To keep metered Actions minutes low, it builds automatically on **Linux only** (every push). Windows and macOS (universal: Apple silicon and Intel) are built for release tags (`v*`) or on request:

1. Open the repository's **Actions** tab and choose **Build**. To get Windows or macOS builds, click **Run workflow**, pick your branch, set **platforms** to `all`, and run it. Windows and macOS minutes cost several times more than Linux.
2. When the run is green, download `ViolinSynth-Windows`, `ViolinSynth-macOS` or `ViolinSynth-Linux` under **Artifacts**.
3. Unzip it. Inside are one zip per format (`ViolinSynth-VST3-…zip`, `ViolinSynth-AU-…zip`, `ViolinSynth-Standalone-…zip`). Unzip the ones you need.

You can also build locally for free (see [Building from source](#building-from-source)). On Windows that needs Visual Studio 2022 with the "Desktop development with C++" workload, plus CMake.

### 2. Install it

| Platform | Format | Copy to |
|---|---|---|
| macOS | VST3 | `~/Library/Audio/Plug-Ins/VST3/` |
| macOS | AU | `~/Library/Audio/Plug-Ins/Components/` |
| Windows | VST3 | `C:\Program Files\Common Files\VST3\` |
| Linux | VST3 | `~/.vst3/` |

**macOS only:** the CI builds are ad-hoc signed but not notarised, so Gatekeeper blocks them when downloaded. After copying, run:

```sh
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/"Violin Synthesizer.vst3"
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/"Violin Synthesizer.component"
xattr -dr com.apple.quarantine "/path/to/Violin Synthesizer.app"   # Standalone, if used
```

If Logic or GarageBand does not list the AU straight away, run `killall -9 AudioComponentRegistrar` and restart the host.

### 3. Check that it works

Rescan plugins in your DAW, then create an instrument track with **Violin Synthesizer** (vendor **OctahedronV2**).

In **FL Studio**:
1. Open **Options → Manage plugins → Find installed plugins**.
2. Add **Violin Synthesizer** from the Channel Rack.
3. The plugin reports a small latency (from oversampling), which FL Studio compensates automatically.

Things to try:

- **Start from a preset:** click the preset name at the top for 27 factory sounds by category (Solo, Styles, Articulations, Character, Expressive), or step through them with **<** and **>**. **Save** keeps your own versions; they are stored in `Documents/OctahedronV2/Violin Synthesizer/Presets`.
- **Play expressively:**
  - Velocity sets how hard the bow is drawn.
  - Overlapping notes glide legato; separate notes get a new bow stroke.
  - Chords play as double stops, up to all four strings.
  - Pitch bend works.
  - The mod wheel (CC1) sets bow pressure.
  - The expression pedal (CC11) sets dynamics.
  - CC74 moves the bow between the fingerboard and the bridge.
- **MPE controllers:** turn on **Play → MPE** for per-note bend, pressure (vibrato) and slide (bow position).
- **Articulations:** pick one under **Articulation**, or use keyswitches. MIDI notes 24–33 (C2–A2 in FL Studio's note names) select legato, détaché, staccato, spiccato, tremolo, pizzicato, harmonics, sul ponticello, sul tasto and con sordino. Drag [docs/demo/articulations.mid](docs/demo/articulations.mid) onto the plugin's track to hear them all. Details are in [docs/PHASE5.md](docs/PHASE5.md).
- **Change the violin:** under **Body → Violin**, choose one of four measured instruments. **Quality → Light** uses less CPU.
- **Shape the sound:**
  - Drag the **bow pad**: left/right moves the bow between the bridge (bright, glassy) and the fingerboard (soft), and up/down changes its pressure on the string.
  - **Mute** adds a practice-style sordino.
- **Resize the window** from its corner; the whole editor scales.
- **Check save and restore:** save the project, reopen it, and the settings and preset name come back.

The **Standalone** app is the quickest way to hear it without a DAW: open it, pick an audio output under *Options → Audio/MIDI Settings*, and play the on-screen keyboard.

---

## Building from source

### Requirements

- CMake 3.24 or newer
- A C++20 compiler: Xcode 15+ (macOS), Visual Studio 2022 (Windows), GCC 11+ or Clang 14+ (Linux)
- Git and network access at configure time. JUCE 9.0.2 and Catch2 are downloaded automatically.
- Linux only: the JUCE system packages:

  ```sh
  sudo apt install ninja-build libasound2-dev libjack-jackd2-dev ladspa-sdk \
    libfontconfig1-dev libfreetype-dev libx11-dev libxcomposite-dev libxcursor-dev \
    libxext-dev libxinerama-dev libxrandr-dev libxrender-dev libxi-dev \
    libglu1-mesa-dev mesa-common-dev
  ```

### Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build --build-config Release --output-on-failure
```

The plugins are written to `build/ViolinSynth_artefacts/Release/{VST3,AU,Standalone}`.

Useful options:

| Option | Default | Effect |
|---|---|---|
| `-DVIOLINSYNTH_COPY_PLUGIN_AFTER_BUILD=ON` | `OFF` | Installs the plugins into your user plugin folders after every build |
| `-DVIOLINSYNTH_BUILD_TESTS=OFF` | `ON` | Skips Catch2 and the test executable |
| `-DVIOLINSYNTH_ENABLE_SANITIZERS=ON` | `OFF` | AddressSanitizer + UBSan (GCC/Clang) |
| `-DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE` | — | Uses a local JUCE checkout instead of downloading one |
| `-G Xcode` / `-G "Visual Studio 17 2022"` | — | Generates an IDE project |

### Validating

CI runs [pluginval](https://github.com/Tracktion/pluginval) at strictness level 10 on the VST3 of every platform it builds, and `auval -strict` on the AU when it builds macOS. To run it locally:

```sh
pluginval --strictness-level 10 --validate "build/ViolinSynth_artefacts/Release/VST3/Violin Synthesizer.vst3"
```

### Code style

Formatting follows `.clang-format` (JUCE style) and is checked in CI with clang-format 18:

```sh
git ls-files '*.h' '*.cpp' | xargs clang-format -i
```

`.clang-tidy` holds the static-analysis configuration. Run it with `run-clang-tidy -p build source tests`.

---

## Project layout

```
CMakeLists.txt        plugin target, formats and plugin IDs
cmake/                dependency fetching, warnings, sanitizers
source/dsp/           bowed-string waveguide (friction junction, delays, loss filter), body modes
source/engine/        voice, oversampling engine, body, output chain
source/plugin/        processor, editor, parameters
resources/bodies/     measured body impulse responses
research/             Python prototype, body estimation, data sources (see research/README.md)
tests/                Catch2 tests, incl. golden data from the Python reference
docs/                 development plan, phase notes, demo MIDI file
.github/workflows/    CI: build, test, pluginval/auval, package
```

## Credits

The measured violin bodies come from the [CNSM Dataset](https://doi.org/10.5281/zenodo.18696786) (Pauget Ballesteros 2026, CC BY 4.0) and the [University of Iowa Musical Instrument Samples](https://theremin.music.uiowa.edu/MIS.html). Details are in [research/data/SOURCES.md](research/data/SOURCES.md).

## Licensing

The project uses JUCE, which is dual-licensed under AGPLv3 and a commercial licence. A licensing decision for this project is still open (see the development plan); until then, builds are for private testing only.
