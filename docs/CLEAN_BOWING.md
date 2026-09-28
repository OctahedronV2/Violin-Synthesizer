# Clean Bowing

Jake's C major scale (C4 to C5, half notes at 90 bpm, velocity 64, default
settings) scratched on A4 and B4, and C4 had a gritty layer under its tone.
A professional hears that and changes the bow weight until the string speaks
cleanly. This change gives the instrument that player.

## What was wrong

The bowed string (source/dsp/BowedString) is a waveguide with a friction
junction. Played well it settles into Helmholtz motion: one stick and one slip
per period. Two other states made the noise:

- **Raucous motion**: the bow is too heavy for this note at this speed, and
  the string never repeats itself. That is the scratch on A4 and B4.
- **Multiple slipping**: the bow is too light, and the string lets go two or
  more times per period. The tone is hollow, often with the octave above the
  note. On the G string the default weight (0.34 of Schelleng's maximum) sits
  right in this region, which was the grit under C4.

Which state a note lands in depends on tiny details. The model is chaotic: a
one-bit change in a calculation can flip a note between clean and scratchy.
So every result below is averaged over many notes, never read from one render.

## The player

`dsp::BowController` (source/dsp/BowController.h) listens to each bowed string
and scales its bow force:

| It hears | Meaning | It does |
|---|---|---|
| The bridge force does not repeat after one period (scratch above -25 dB) | Too heavy, or still settling | Eases off |
| More than 1.2 slips per period while the tone is steady | Too light | Leans in |
| About two slips per period with little scratch | A steady double slip | Holds, drifting back |
| Clean | | Drifts back to the asked-for weight within 0.5 s |

The weight moves between half and double the asked-for force, at about x2.7
per 100 ms, and is remembered from note to note on the same string. It never
takes the force above 0.9 of the string's maximum. Leaning in while the string
still chatters after the attack made things worse, so the weight only rises
once the tone is steady. Easing off out of a double slip locks it in, hence
the third row.

The string also reports each slip (`BowedString::slipStarted()`), which the
player counts. The listener updates its averages on every second internal
sample; the internal rate is at least 176.4 kHz, so nothing audible is lost.

Two smaller changes go with it:

- **Player force window.** Each string's Bow Pressure range now tops out at
  `playerWindowHigh` (source/engine/StringData.h) rather than `forceWindowHigh`.
  The G string's range starts higher, since below about 0.5 of the maximum it
  locks into a double slip.
- **Release lift.** When a note ends and the next begins, the old note's bow
  lifts faster (the force follows the release envelope cubed), so it rings out
  instead of being scraped.

## The Imperfection knob

Imperfection (0 to 100%, default 0, automatable, in the Play panel) scales all
of the above. At 0 the player corrects fully. At 100% the output is identical
to the instrument before this change: the player does nothing, the force
windows and release are the old ones.

## Results

256 scale notes (roots across the range, several velocities, dry, no
vibrato), unassisted then with the player:

| Measure | Unassisted | Player |
|---|---|---|
| Notes whose worst 50 ms is above -20 dB scratch | 112 | about 60 |
| Notes whose worst 50 ms is above -10 dB | 28 | 1 to 5 |
| Notes that never reach Helmholtz motion | 56 | 7 to 9 |
| Share of sustain not in Helmholtz motion | 27.6% | 11 to 13% |

The tone colour (spectral centroid) is unchanged. On Jake's scale averaged
over ten velocities, A4's worst moment went from -14.7 to about -21 dB and
C4's from -20.9 to between -26 and -33 dB. A single render can still land a
note in a brief double slip or scratch; the player gets it out, usually within
a few hundred milliseconds.

The test "The player keeps scales free of scratch" (tests/BowNoiseTests.cpp)
checks this on 64 notes.

## Cost

About +5.7% CPU per bowed note and +11% for four-note chords (Callgrind,
docs/benchmarks/baseline.json). Pizzicato and idle are unchanged. The cost is
the same at any Imperfection setting.

## Tried and dropped

- Pressing harder at the first sign of multiple slipping (the textbook rule):
  it pressed while the string was still settling, which scratched.
- Force leading bow speed at the attack: a crunchy first 30 ms (as Phase 1
  found).
- A smooth bow turn at re-strokes, raising the D string's window, a weight
  floor of 0.25 (traps the G string in double slip), pressing out of a double
  slip (noise bursts on the way out), a low-passed detector, correcting only
  in the sustain, delayed or faster return: no better or worse.
- Earlier rejected fixes from BOW_NOISE.md still stand: friction curve v0,
  extra high-frequency damping, a bow-hair low-pass, easing weight in at the
  attack.
