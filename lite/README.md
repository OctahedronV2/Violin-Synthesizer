# Octavio Lite

A from-scratch, single-file violin engine for fast sound iteration. It is separate
from the plugin engine in `source/` and has no dependencies.

    g++ -O2 -std=c++17 -o lite lite/lite.cpp      # about 2 s
    ./lite in.mid out.wav [name=value ...]        # run from the repo root

Signal path: MIDI -> player (voices, strokes, slurs, vibrato) -> bowed waveguide
string per voice at 96 kHz -> 48 kHz -> body impulse response
(`resources/bodies/stoppani.wav`, or `LITE_BODY`) -> room impulse response
(`LITE_ROOM`, stereo wav, mixed by `room`) -> 16-bit wav normalised to `level` dB RMS.

Every tunable number is a named parameter in `lite.cpp` (search for `P ("`).

## Engine and front ends

- `LiteCore.h` is the engine: freestanding C++ with its own maths, no allocation.
  Its parameter table (`kParams`) is the single list of controls.
- `lite.cpp` is the offline renderer (MIDI in, wav out).
- `web/` is the playground: `build.sh` compiles the engine to `lite.wasm` with
  plain clang, `worklet.js` runs it in an AudioWorklet, `index.html` builds a
  slider for every parameter. The browser and the renderer produce the same
  samples bit for bit (checked by rendering the lyric melody both ways).
- A VST wrapper only has to forward notes and parameters to `lite::Engine` and
  call `process()`.
