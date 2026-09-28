# Phase 5: Articulations

The violin now plays ten articulations. Choose one with the **Articulation** parameter or with keyswitches:

| Keyswitch (MIDI note) | Name (C4 = 60) | In FL Studio | Articulation |
|---|---|---|---|
| 24 | C1 | C2 | Legato (default) |
| 25 | C♯1 | C♯2 | Détaché |
| 26 | D1 | D2 | Staccato / martelé |
| 27 | D♯1 | D♯2 | Spiccato |
| 28 | E1 | E2 | Tremolo |
| 29 | F1 | F2 | Pizzicato |
| 30 | F♯1 | F♯2 | Natural harmonics |
| 31 | G1 | G2 | Sul ponticello |
| 32 | G♯1 | G♯2 | Sul tasto |
| 33 | A1 | A2 | Con sordino |

Keyswitch notes are below the violin's range (G3 = 55) and never sound. A keyswitch applies to notes that start after it. It stays in effect until the next keyswitch, or until the **Articulation** parameter changes, which is useful for automation. The editor's new **Articulation** panel shows what is playing now. Keyswitches work on any MIDI channel, including in MPE mode.

[`demo/articulations.mid`](demo/articulations.mid) goes through all ten in turn. Drop it on a track with the plugin to hear them.

## What each articulation does

Each note keeps the articulation it started with, so a keyswitch never changes a note that is already sounding. The exception is con sordino, which puts the mute on the whole instrument.

| Articulation | Strokes | How it is modelled |
|---|---|---|
| **Legato** | Overlapping notes slur in one bow | The Phase 4 behaviour: pitch glides on one string, or the bow crosses strings mid-stroke |
| **Détaché** | Every note gets its own stroke | Overlapping notes re-stroke instead of slurring: the bow turns and the speed dips and recovers. The attack is capped at 40 ms. |
| **Staccato / martelé** | Short and separate | A 12 ms attack with a force "bite" (+90%, decaying in 20 ms). The bow moves for 110 ms, then stops on the string over 35 ms, damping it. Releasing the key earlier stops it sooner. |
| **Spiccato** | Short and separate | A bouncing bow. It is already moving when it lands, and the force is a single half-sine pulse (60–84 ms, shorter when louder). The bow then leaves and the string rings briefly (T60 0.4 s). |
| **Tremolo** | One stroke per note, many reversals | About 13 reversals a second with ±12% random jitter per stroke. The speed falls to zero at each turn while the bow stays on the string. |
| **Pizzicato** | Plucked | The bow is off. A raised-cosine velocity pulse at a quarter of the string length plucks the string. It is shorter and brighter for harder plucks, and never longer than a quarter period. The string vibrates freely (free-vibration tuning, T60 1.0 s) and the finger damps it on release. |
| **Natural harmonics** | Slurred | A light, flute-like tone: light bow (pressure ≤ 20%), β = 0.13, and strong damping of the upper partials. |
| **Sul ponticello** | Slurred | A light bow very near the bridge (β = 0.07, pressure ≤ 15%): weak fundamental, glassy upper partials. |
| **Sul tasto** | Slurred | A slower, firmer bow over the fingerboard (β = 0.15, pressure ≥ 50%, 85% speed) and softer high partials. |
| **Con sordino** | Slurred | The **Sordino** filter at 100%, ramped in over 150 ms so it doesn't click |

The legato-type articulations (legato, harmonics, ponticello, tasto and sordino) slur overlapping notes. The others give each note a new stroke. With them, releasing a note never falls back to an older held one, as it does in a legato trill.

Short strokes and tremolo don't use up bow hair, so they never trigger an automatic bow change.

### Approximations and what we learned

- **Sul ponticello needs a light bow in this model.** Heavy force near the bridge gave a pure, fundamental-heavy tone: at β = 0.035 and 60–80% pressure, the upper partials were *weaker* than normal bowing. A light bow at β ≈ 0.06–0.08 gives the weak-fundamental, strong-overtone sound of real ponticello playing. Its 2.5–10 kHz energy is 23 dB above normal bowing. Research (PHASE1_FINDINGS §2.4) found brightness rising with force at a *fixed* bow position, which is a different comparison.
- **Sul tasto is limited by the model.** Beyond β ≈ 0.17 the model turns subharmonic and raucous (PHASE1_FINDINGS §2.2), so real fingerboard positions (β = 0.2–0.3) don't work. Tasto therefore stays at β = 0.15 and gets a softer high-frequency loss (T60 at 4 kHz of 0.08 s instead of 0.25 s). This stands in for the wide, soft contact of the bow over the fingerboard, which the single-point bow model lacks. Going further (0.06 s) made the tone nearly sinusoidal and 8 cents flat.
- **Natural harmonics are played at the sounding pitch.** A real harmonic touches the string lightly at a node (½, ⅓, ¼ of its length), so the string vibrates in segments. The spectrum of the full string then holds only multiples of the harmonic's pitch. That is the same as a shorter string at the sounding pitch, which is what we model, with light bowing and strong upper-partial damping for the pure tone. A true node damper needs a second junction in the waveguide. That is planned for the model upgrade and would also allow artificial harmonics.
- **Pizzicato:** the Phase 1 pluck ran a fixed 0.5–1.5 ms pulse. Near and above E6, that pulse is as long as the period and nearly cancels itself, so high plucks were silent. The pulse is now scaled to the period.

## Verification

Eleven new test cases (`tests/Phase5Tests.cpp`, tag `[phase5]`):

- **Keyswitches** make no sound, set the articulation, and hold until the parameter changes.
- **Staccato** falls silent within 0.4 s while the key is held. Legato keeps sounding.
- **Spiccato** decays while held and is nearly silent after 0.9 s.
- **Tremolo** makes 9–20 amplitude dips a second. Legato makes none.
- **Pizzicato** on G3, D4, A4, E5 and E6: the peak arrives within 30 ms, pitch is within 3 cents, it decays while held, and it is damped at least 26 dB within 0.15 s of release. Its level is within a factor of 2 of a bowed note.
- **Tone colour**, measured as the share of energy between 2.5 and 10 kHz: ponticello is ≥ 6 dB brighter than legato, and tasto, harmonics and sordino are ≥ 6 dB darker. Measured: legato −30 dB, ponticello −7, tasto −40, harmonics −52, sordino −45.
- **Pitch** is within 5 cents for all sustained articulations.
- **Détaché** turns the bow on every overlapping note (6 strokes for 6 notes). Legato stays in one stroke.
- **Random transitions**, in all three play modes: 240 random notes and keyswitches over 8 s.
  - The output stays finite and within ±1.
  - All strings return to open, and the output dies away after the final note-off, so no notes stick.
  - The largest sample-to-sample step is within 30% of the largest step from the *same notes* with each articulation held fixed. Switching adds no clicks.
- **Demo file:** the committed `docs/demo/articulations.mid` matches its generator (`tests/ArticulationDemo.h`), and playing it activates all ten articulations and leaves no notes stuck.

`ViolinSynthTests "[.demo]"` regenerates the MIDI file and renders it through the plugin (`articulations.wav`). `ViolinSynthTests "Articulation harmonic levels"` prints the per-articulation spectra used to tune them.

All tests (55 cases) pass in Release and under AddressSanitizer + UBSan. pluginval passes at strictness 10.
