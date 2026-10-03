# Octavio 2

New engine, player and plugin for Octavio 2.0.0. Plan: `docs/OCTAVIO2_PLAN.md`.

- `core/Strings.h`: four bowed strings on one modal bridge (thermal rosin, 4-point compliant hair,
  per-string data, dispersion, ear intonation, rosin grain faded in per stroke).
- `core/Player.h`: the violinist (velocity -> bow speed/force/contact, string choice, slurs, bow
  changes and bow budget, shifts, vibrato as finger motion, lift-off releases).
- `core/Radiation.h`: bridge force -> sound: 60 Hz high-pass, brightness shelf, full-band body,
  left/right directional mic bodies, hall tail (the C++ port of `render/finish.py`).
- `core/Engine.h`: MIDI events -> player -> strings -> radiation, at 48 kHz. Live mode plays at
  once; Studio mode looks 1.2 s ahead so the player knows each note's length. The plugin and the
  renderer's `sound=` mode both run this, so a render is what the plugin plays.
- `plugin/`: the Octavio 2 plugin (VST3, AU, Standalone; installs beside Octavio 1). `plugin/ui/`
  is the interface from the mockups (Play in Live and Studio, Curves, Tone, MIDI); controls of
  later milestones show dimmed, with a tooltip saying which milestone brings them.
  `OCTAVIO2_SNAPSHOTS=<folder> build/octavio2/Octavio2Tests` saves every tab as a PNG.
- `render/render.cpp`: MIDI -> bridge force (research path) or, with `sound=`, the engine's stereo.
- `data/`: full-band and directional bodies, halls (`data/halls/SOURCES.md`, CC BY), string and
  bridge data from the research threads.

Render with the plugin's engine (from the repo root):

    ./octavio2/render/build.sh /tmp/o2
    /tmp/o2 in.mid - sound=out.wav mode=live       # or mode=studio, mode=planned (score: lengths known)
    # options: hall=0..8 bright=5 reverb=0 volume=0 vibrato=1 velCurve=1 start= length=

The research path (force + finish.py, used by fitnotes.py and the audit) is unchanged:

    /tmp/o2 in.mid force.wav [name=value ...]
    python3 octavio2/render/finish.py force.wav out --mp3      # out.dry.wav, out.wav, out.mp3

The plugin builds with the rest of the project (`cmake --build build --target Octavio2_VST3`);
its tests are `Octavio2Tests`.
