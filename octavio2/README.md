# Octavio 2

New engine and player for Octavio 2.0.0. Plan: `docs/OCTAVIO2_PLAN.md`.

- `core/Strings.h`: four bowed strings on one modal bridge (thermal rosin, 4-point compliant hair,
  per-string data, dispersion, ear intonation, rosin grain faded in per stroke).
- `core/Player.h`: M0 Live-mode violinist (velocity -> bow speed/force/contact, string choice,
  slurs, bow changes and bow budget, shifts, vibrato as finger motion, lift-off releases).
- `render/render.cpp`: MIDI -> bridge force (48 kHz). `render/finish.py`: body, mics, hall.
- `data/`: full-band and directional bodies, string and bridge data from the research threads.

    g++ -O2 -std=c++17 -o /tmp/o2 octavio2/render/render.cpp
    /tmp/o2 in.mid force.wav [name=value ...]
    python3 octavio2/render/finish.py force.wav out --mp3      # out.dry.wav, out.wav, out.mp3
