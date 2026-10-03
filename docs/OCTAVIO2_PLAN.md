# Octavio 2.0.0 plan

Written 2026-10-01 for Jake's ask: "plan an entirely new version of the plugin (Octavio 2.0.0) that
uses all of this research; don't rule anything out for performance; reconsider earlier decisions
against the feedback since."

Inputs: the four world-class research threads in `/mnt/project-files/research/world-class/`
(string-physics, references, body-radiation, player-features), the engine notes in
`engine/CURRENT.md`, the Lite engine and string-physics lab (branch `claude/project-thread-kj0aaw`)
and project memory. Code for 2.0 lives on branch `claude/project-thread-bxawxe`, folder `octavio2/`.

## 1. What 2.0 is

A physical violin, not a modelled-looking synth: four real strings on one bridge, bowed by a real
bow (a ribbon of springy hair with temperature-dependent rosin), radiating through a full-band,
directional body into a real room, played by a **virtual violinist** that turns notes, velocity
and optional curves into bow and finger gestures. Everything 1.x did is rebuilt; nothing in
`source/engine` or `source/dsp` is carried over except plugin plumbing that works (preset files,
FL keyboard focus, host note names, CI, release flow).

Three ideas carry the whole design:

1. **Physics first, then EQ never.** Every timbre control reweights a physical part (bow, rosin,
   string, bridge, body, mic, room). No post-EQ. That is what keeps vibrato shimmer, ring and
   transitions consistent, and it is what 1.x got wrong when it stacked fixes on top.
2. **Engine = physics, Player = musicianship.** The engine only takes physical gestures per
   string (bow speed m/s, force N, contact point, finger position, finger pressure, pluck, damp).
   All intelligence is in the player layer. The same player will later drive viola, cello, bass
   and Octastra sections.
3. **Measured, not guessed.** Every step is scored against real violins with the scorer
   (`references/tool/score.py`) and the physics tests (Schelleng limits, Guettler attacks), then
   played to Jake. The scorer also tunes the player automatically (as in the Brahms rounds).

## 2. Targets (how we'll know it's world class)

| Measure | Real violins | Octavio Lite today | 2.0 target |
|---|---|---|---|
| Unreal notes, single-note suite (scorer) | 1.8-5.4% | 23.8% | under 6% |
| Unreal notes, Haydn phrases | 0-11% | 13.7% | under 8% |
| Note time on the wrong pitch (whistles, flips) | 3-7% | 17% | under 5% |
| Schelleng force limits c_lower at 5/10/20 cm/s | 7.0/4.2/1.8 g/s | 0.8/0.8/none | within 30% |
| Clean attacks from rest (under 50 ms) on player-generated gestures | pros ~44% perfect | 4% | over 60% under 50 ms |
| Vibrato shimmer, one mic (0.5-1.5/1.5-4/4-10 kHz) | 1.15/2.1/2.7 dB | 0.9/0.95/0.9 | within 0.3 dB |
| Note-change level dip | -9.6 dB | -3.3 dB | -7 to -11 dB |
| Air above 10 kHz | -47 dB | -54 dB | within 3 dB |
| Brightness follows bow force (centroid rise pp to ff) | strong | none | measurable rise matching Iowa pp/mf/ff |
| Blind A/B with Jake vs 1.1 and vs real clips | | | prefers 2.0 to 1.1 every time |

## 3. Architecture

```
MIDI notes, velocity, CCs, keyswitches, MPE, host tempo/transport, drawn curves
   │
 [Reader]   note list, look-ahead window (Studio mode), phrases, tempo, metre
   │
 [Planner]  string+position (Viterbi / greedy), bowing plan + bow budget, articulation,
   │        dynamic arc, vibrato plan, intonation targets
   │
 [Gesture]  per-string continuous curves at 1 kHz: bow velocity, force, contact, hair position,
   │        finger position, finger pressure; pluck/damp events; the "listening" correction
   │        (Helmholtz health reported back from the engine)
   │
 [Strings]  4 waveguide strings at 4x (192 kHz) or 2x, bowed by a 4-point compliant bow with
   │        thermal + elasto-plastic rosin, all 4 attached to one passive modal bridge
   │        (sympathetic strings, double stops, wolf for free)
   │
 [Radiation] bridge force -> full-band body x direction field L / R (direct)
   │                       -> full-band all-direction body -> room tail
   │        Brilliance, Bridge tone, Mute, Body size, Violin choice = which IRs are loaded
   │
 [Room]     convolution halls Jake liked (Arvedi, Detmold, BBC/WDR studios, church) + FDN
            option; Distance sets direct/reverb, pre-delay and air together
```

One core library (`octavio2/core/`, header-only C++17, no allocation on the audio thread, seeded
randomness) builds three things: the plugin, the offline renderer used for every sound iteration,
and the browser playground. Same code, bit-identical renders.

## 4. The engine in detail

All numbers below come from the research threads; file references in brackets.

### 4.1 Bow and rosin [string-physics FINDINGS P1, P2, P4, open problem 1]
- **Thermal rosin friction** (van Walstijn 2026): mu_s 1.05, ya 0.4, tauG 25 K, xi 2, heat
  equation per contact point. Reproduces measured Schelleng limits without tuning and makes all
  12 Iowa open-string notes play.
- **Compliant hair, 4 contact points over 10 mm**: 110 000 N/m, 10 kg/s. Only with thermal
  friction, never alone.
- **Elasto-plastic pre-sliding layer** (vW26, sigma0 ~1e6/m, 2-3 implicit iterations per point):
  the most likely fix for "brightness doesn't follow force". The physics thread held this back for
  CPU (+10-30%); per Jake, it goes in.
- **Rosin grain** 3% faded in over the first 50 ms of each stroke (keeps attacks clean, keeps the
  sustained texture real).
- **Bow position matters**: hair stiffness rises toward frog and tip, hair tilt and width become
  gesture inputs (the player tilts at pp, flat at ff). Bow-change transient comes from physics.
- **Bow noise** is not a separate noise generator any more. It must emerge from grain + slip
  irregularity. If the scorer still shows a gap above 10 kHz after the body fix, add a physically
  placed noise source (hair-on-string friction noise injected at the contact point, so it goes
  through string and body).
- User controls: **Rosin** (tauG/ya presets: light, standard, dark/sticky, baroque), **Bow**
  (modern / baroque: shorter, lighter, different balance), Imperfection (how much the player's
  listening correction is relaxed).

### 4.2 Strings [P3, P5, P6, P7, low-priority list]
- **Per-string data**: Z0 G .350, D .303, A .203, E .173 kg/s; loss fitted to Iowa pizzicato
  (`strings_pickering.txt`). String sets as presets: synthetic (default), gut (baroque), steel
  (fiddle).
- **Bending stiffness** (Pickering B) as an allpass: free in CPU terms now, needed for pizzicato and
  ring-out.
- **Torsion**: implemented in the lab, measured no gain and fewer clean attacks. Kept as an option
  (Q 30+), off by default, and re-tested once elasto-plastic friction is in, since the two
  interact. (Reconsidered: it was partly skipped for CPU; the measurement, not CPU, decides.)
- **Second polarisation** (vertical string motion): new item, not yet studied. Real strings swing
  in two planes and the bridge couples them; it adds slow beating to the ring-out and to pizzicato.
  Research spike in M5.
- **Finger as a physical stop**: the finger is a lossy, slightly compliant termination with a
  pressure input. Light pressure = harmonics (natural and artificial), lifting = left-hand pizz
  and the finger-lift ring, landing = the tiny finger tap on slurred changes. Replaces 1.x's
  scripted "finger-change slur" and "tap" effects.
- **Intonation by ear**: stopped notes are corrected against bow flattening (5% per period, +-40
  cents), open strings are not. Plus the player's expressive intonation targets on top.
- **Regression test for the string-lock bug** (beta 0.04, high force must still slip).
- **Oversampling**: 2x minimum (96 kHz); test 4x (192 kHz), since the E string's bow-point gaps are
  only ~1.4 samples at 96 kHz. Ship whichever scores better as "High quality" default, the other as
  "Eco". (Reconsidered: 96 kHz was chosen for CPU.)

### 4.3 Bridge and sympathetic strings [P5, body F7]
- One passive modal bridge admittance (60-100 positive-residue modes fitted to CNSM |Y|, signature
  modes A0 275, CBR 405, B1- 470, B1+ 540 Hz, bridge hill ~2.4-2.9 kHz), all four strings attached
  and always live. Sympathetic ringing, double-stop interaction, per-note body colour at the
  source, faster decay of body-resonant partials and the wolf all come from this one model.
- The same modes drive the radiated body below ~1.5 kHz so the bridge and the sound share poles
  (Maestre/Scavone/Smith 2017).
- User: **Sympathetic** amount, **Wolf**, **Hold** (chin/hand damping of the low modes),
  **Bridge** tone (2.4-3.6 kHz rocking resonance), **Mute** (off / sordino / practice).

### 4.4 Body, microphones and room [body FINDINGS recommendations 1-7]
- **Full-band body** (measured below 8 kHz, random-modal tail to 20 kHz); **Brilliance** -10..+6
  dB tilt above 5 kHz.
- **Directional microphones**: L/R through two direction fields (shimmer under vibrato matches
  the real violin: 2.14/2.64 dB vs 2.09/2.68). User: **Mic position** (front / above / player's
  ear / side), **Stereo width**.
- **Physical room split**: direct from the mic bodies, reverb fed by the all-direction body.
  Convolution halls (the ones from the final tune-up Jake liked) plus the FDN as an Eco option.
  User: **Room**, **Distance**.
- **Player sway** (0.05-0.2 Hz crossfade of direction fields), **Movement** amount.
- **Violin choice** = body set (Stoppani, Klimke, Levaggi, Iowa, TU Berlin-balanced) and
  **Body size** for viola later.

### 4.5 Pizzicato
Pluck as a finite-width displacement (finger vs nail), through the same strings and bridge. Lab
`pluck` is a placeholder impulse; replace with a proper shaped excitation and score against the
Iowa pizzicato notes and the Philharmonia pizz set.

## 5. The player (virtual violinist)

The spec is `player-features/3-spec.md`; 2.0 builds all of its P0 and P1. Summary of what changes
from 1.x:

- **Velocity is a full-range expressive input**: about 64 = mp, 100 = mf-f, 127 = ff, with a
  sensitivity control; a constant 100 still sounds musical because the phrase shaping supplies the
  variation. Dynamics move bow speed, force **and** contact point together (Schoonderwaldt).
- **Bowing plan with a bow budget** (62 cm of hair): overlap = slur, bow changes when the hair runs
  out at the least exposed point, down-bows on strong beats (Studio), sustain pedal = slur all.
- **Left hand**: string and position choice (Viterbi in Studio, greedy in Live), shifts that leave
  late and land on the beat, B- and L-portamento by style, landing error corrected by ear.
- **Context vibrato** as finger motion (delayed bloom, width by dynamic, register, stress and
  length, off on open strings, phase-continuous over slurs, seeded wander).
- **Articulations inferred** from length, gap, overlap, tempo and velocity (détaché, legato,
  martelé, staccato, spiccato, sautillé, tremolo, portato, pizz...), overridable by keyswitch or
  the Articulation parameter.
- **Live and Studio modes**: Studio reports ~250 ms latency so the player can see ahead
  (anticipated shifts, on-the-beat attacks, bow planning); the DAW compensates.
- **Auto curves, drawn curves win**: the player's guesses are CC curves you can drag out as MIDI
  and edit; any lane you draw takes over that one dimension (Auto / Guided / Manual per dimension).
- **Player styles**: Modern soloist, Romantic, Hungarian, Baroque, Maqam, Fiddle, Student.
- **Expressive intonation**, Scala and MTS-ESP, A4 reference, per-string tuning (Maqam support for
  Jake's composing and Octastra).
- **Auto-tuned from real playing**: player parameters (bow-change time, vibrato bloom, slide
  speeds, messa di voce depth) are fitted with CMA-ES against the anechoic Haydn performances and
  the slow-legato references, scored on held-out pieces (the Brahms-round method, now on clean,
  anechoic, multi-piece data so it can't overfit one melody).

## 6. The plugin

- Same plugin identity as 1.x? See decision D1 below. Recommended: install side by side as
  "Octavio 2" so old songs keep their sound; presets and projects from 1.x don't load into 2.0.
- **Main view**: Player style, Instrument, six big controls (Dynamics, Expression, Vibrato,
  Portamento, String preference, Room), Live/Studio toggle, and a live **fingerboard and bow
  view** showing what the player decided (string, position, bow direction, hair position, contact
  point), with an A badge per dimension to switch Auto / Guided / Manual.
- **Advanced tabs**: Bow, Left hand, Articulation rules, MIDI (mapping page with learn, CC map
  presets, MPE), Tone (rosin, strings, bridge, mute, body, mic, room), Seed.
- Every control is a host parameter with a stable ID (append only). FL Studio keyboard input by
  default, z = C4 behaviour and host note names carry over from 1.x.
- CPU: no limits during development (Jake: optimise later). An **Eco / High quality** switch
  arrives with the optimisation milestone, not before.
- Instruments at 2.0: modern violin, baroque violin (gut strings, baroque bow and rosin,
  Pythagorean player, A415). The bowed guitars and 1.x presets stay in 1.x (see D3).

## 7. Earlier decisions, re-checked

| Earlier decision | Feedback since | 2.0 decision |
|---|---|---|
| Tune defaults to sound good at velocity 64 | Jake 2026-10-01: keyboards send ~100, he has a velocity controller, composers edit velocity | Full-range velocity curve; 100 must sound good, every value predictable |
| No drawn curves needed, velocity-only | Jake 2026-10-01: keep drawn curves, auto mode guesses them | Auto curves + export to MIDI + drawn lanes override per dimension |
| Skip torsion (+25% CPU, no gain) | Don't rule out for CPU | Keep implemented, off; re-test after elasto-plastic friction |
| Hold back elasto-plastic friction and coupled bridge for CPU | Don't rule out for CPU | Both in the core |
| 96 kHz internal | same | Test 192 kHz; pick by score |
| Lightweight engine for fast iteration | Still wanted for iteration speed | Fast offline renderer stays; the engine is complete but each ingredient switchable for A/B |
| Bow noise as a separate knob/generator (1.x) | Physics thread: grain gives real texture | Emergent noise first; physically placed noise source only if the scorer shows a gap |
| Clean bowing by default, Imperfection adds error | Still Jake's preference | Kept: physics now gives clean attacks; listening correction on top; Imperfection relaxes it |
| Body EQ fixes (h2-h7 "weak", WARM_DB, X2_DB) | Body thread: that gap was the fundamental and the dark top | No EQ; fundamental comes from bow force physics, top from full-band body |
| "Ring after release" via longer strings | Body thread: mostly the room | Real hall + room split; strings stay physically fitted |
| Hungarian Dance 5 cover as the benchmark | Overfit risk seen in E1; new anechoic corpus exists | Haydn anechoic + single-note suite are the main score; Brahms and slow legato stay as listening tests and held-out checks |
| Room: 8-line FDN in Lite | Jake liked the convolution halls in the final tune-up | Convolution halls default, FDN as Eco |
| Sound tweaks via offline renderer, mp3s, Artifact page, no PR until Jake likes it | unchanged | Unchanged for every milestone |
| Ask before merging | unchanged | Unchanged |

## 8. Milestones

Each milestone ends with clips on an Artifact page, a scorer table against the previous one, and
Jake's listen. No plugin build or PR until a milestone's sound is approved.

| # | Milestone | Contents | Done when |
|---|---|---|---|
| **M0** | Core + minimal player | `octavio2/core`: 4 strings, thermal rosin, 4-point compliant hair, grain fade-in, per-string data, dispersion, ear intonation, modal bridge (lab set, scale 0.5); minimal Live player (velocity -> bow speed/force/contact, string choice, slurs on overlap, bow changes, shifts, vibrato as finger motion, lift-off release); full-band directional body L/R; offline renderer from MIDI | Renders the test melodies; scorer table vs Lite baseline; clips for Jake |
| M1 | Radiation and room | Diffuse body -> convolution hall split, Distance, Mic position, Brilliance, Bridge/Mute, sway | M3 shimmer and M4 air within targets |
| M2 | Bow physics round 2 | Elasto-plastic rosin, position-dependent hair stiffness, tilt/width, torsion re-test, 192 kHz test, physically placed friction noise if needed | Brightness follows force; Schelleng and attack targets met |
| M3 | Real bridge | Passive modal fit to CNSM (60-100 modes) shared by bridge and low body, sympathetic, wolf, hold | Per-note colour and ring match Iowa; no instability in a 1-hour soak |
| M4 | Player P0 | Phrase dynamics, bow budget, Studio look-ahead, Viterbi fingering, context vibrato, articulation inference, curve export, auto-tune against Haydn/slow legato | Phrase-suite unreal notes under target; Jake A/B |
| M5 | Pizzicato, harmonics, second polarisation spike | Shaped pluck, finger pressure, left-hand pizz, Bartók; polarisation study | Pizz scores vs Iowa/Philharmonia |
| M6 | Plugin shell and UI | JUCE processor on the core, parameters, main view, fingerboard/bow view, advanced tabs, latency reporting, MIDI mapping, presets | Runs in FL, Reaper and Ableton on Windows 11 |
| M7 | P1 features | Player styles, intonation systems, extended articulations, MPE/CLAP, DAW articulation maps, baroque violin | Feature checklist |
| M8 | Optimise and release | Profiling, SIMD, Eco/HQ, RealtimeSanitizer, VST3 validator, docs, 2.0.0 release | CI green; Jake approves merge |

After 2.0: viola, cello, bass by string data + body size + player range, then Octastra sections
reuse the player with per-player variation.

## 9. How work is run

- Sound iteration: edit `octavio2/core`, rebuild the renderer (seconds), render, score, mp3 to
  `/mnt/project-files/showcase/octavio-2/mN/`, Artifact page with playback and download.
- Scoring per milestone: `score.py notes` on `suite_notes.mid`, `score.py phrases` on the Haydn
  MIDI, physics tests (`limits`, `guettler`), plus Jake's listening tests (lyric melody, Brahms,
  slow legato, violin showcase).
- Parallel threads where it speeds things up (e.g. M2 bow physics and M4 player can run side by
  side once M0 defines the gesture interface).

## 10. Decisions for Jake (defaults in bold, work proceeds on them)

- **D1 Plugin identity**: **install side by side as "Octavio 2"** (old songs unchanged) vs replace
  1.x in place.
- **D2 Open questions from the player spec**: **CC1 = dynamics** (legacy map one click away);
  **Live mode** default; **Modern soloist** default style.
- **D3 Bowed guitars**: **stay in 1.x for now**, rebuilt on the 2.0 core after the violin.
