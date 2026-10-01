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
