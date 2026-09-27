# Violin-Synthesizer

An expressive, physically modelled violin synthesizer plugin (VST3 / AU / Standalone) built with JUCE.

See [docs/DEVELOPMENT_PLAN.md](docs/DEVELOPMENT_PLAN.md) for the synthesis approach, architecture and milestones.

> **Status: Phase 0 (project foundation).** The plugin builds, loads in hosts and passes validation, but it only plays a **placeholder sine tone** with an output-gain control and an on-screen keyboard. It exists to prove that MIDI in, audio out, parameters and state saving all work. The violin model arrives in Phase 2.

---

## Trying the plugin in a DAW

### 1. Get a build

Every push runs the **Build** workflow on GitHub Actions for Linux, macOS (universal: Apple silicon and Intel) and Windows.

1. Open the repository's **Actions** tab, then the latest green **Build** run for your branch.
2. Under **Artifacts**, download `ViolinSynth-macOS`, `ViolinSynth-Windows` or `ViolinSynth-Linux`.
3. Unzip it. Inside are one zip per format (`ViolinSynth-VST3-…zip`, `ViolinSynth-AU-…zip`, `ViolinSynth-Standalone-…zip`). Unzip the ones you need.

You can also build locally (see [Building from source](#building-from-source)).

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

Rescan plugins in your DAW, then create an instrument track with **Violin Synthesizer** (vendor **OctahedronV2**). A good smoke test:

- [ ] The plugin appears as an **instrument** and loads without errors.
- [ ] The editor opens and can be resized.
- [ ] Playing MIDI notes (or clicking the on-screen keyboard) produces a sine tone. Pitch bend works (±2 semitones).
- [ ] The **Output Gain** knob changes the level, and it can be automated from the DAW.
- [ ] Saving the project, closing it and reopening it restores the gain value.

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

CI runs [pluginval](https://github.com/Tracktion/pluginval) at strictness level 10 on the VST3 on all platforms, and `auval -strict` on the AU on macOS. To run it locally:

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
source/plugin/        processor, editor, parameters, placeholder voice
tests/                Catch2 unit tests
docs/                 development plan
.github/workflows/    CI: build, test, pluginval/auval, package
```

## Licensing

The project uses JUCE, which is dual-licensed under AGPLv3 and a commercial licence. A licensing decision for this project is still open (see the development plan); until then, builds are for private testing only.
