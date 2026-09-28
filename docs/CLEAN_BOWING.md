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

## The string's twist

The player cannot fix the first moments of a note: a string at rest scraped
for its first 100 to 300 ms whatever the bow did. Jake heard it on the second
note of his scale (D4 on a D string at rest).

A real string twists as well as bends where the bow drags its surface. The
twist travels several times faster than the bend and is damped far more
heavily, and it steadies the stick-slip at the bow (Woodhouse). The waveguide
now has a second pair of delay lines for the twist (`TorsionParams` in
source/dsp/BowedString.h). The bow sees the bending and twisting impedances in
series, and the change in contact velocity is shared between them by
impedance. The values (StringVoice.cpp): the twist travels 5 times faster, its
impedance at the string's surface is 3 times the bending one, and it rings
with a quality factor of 2, so it dies within a couple of its own periods. It
is off for harmonics, where it brightened the flageolet tone.

Two side effects are compensated. The twist takes a quarter of the bow's
motion, so the bow moves 4/3 as fast and the note stays as loud. And it
lengthens each period slightly, 0.85 cents flat on average (up to 1.8), so a
bowed string is tuned 1.4 cents sharp, which also removes the 0.55 cents the
model was already flat.

Measured with the player (dry, no vibrato):

| Measure | Before | With the twist |
|---|---|---|
| Scale notes (448) with a 50 ms window above -20 dB | 105 | about 16 to 29 |
| Mean time to Helmholtz motion, scale notes | 0.31 s | 0.11 s |
| Share of sustain not in Helmholtz motion | 13.4% | 1 to 3% |
| Noise in the first 30 ms of a note on a still string | -35 dB | -41 dB |
| ... 30 to 100 ms | -28 dB | -33 dB |
| ... 100 to 300 ms | -29 dB | -36 dB |

On Jake's scale over ten velocities, notes with a scratchy moment went from
16 of 80 to 2, settling in 0.11 s instead of 0.36 s. The tone is darker: the
spectral centroid falls from 1.63 to about 1.42 harmonics, partly because
the scratch was adding brightness. A weaker twist (impedance ratio 5 or 8)
keeps more brightness but settles slower. Presets were re-levelled.

Tried and dropped along the way: letting the bow's weight arrive before its
speed at note starts. It settled scale notes faster, but a string at rest was
grabbed and scraped for about 50 ms (+10 dB of noise at 30 to 100 ms). Starts
with the speed leading, and textbook starts with constant acceleration and
constant force, were noisier too. Lesson for measuring: scale tests mostly
re-bow strings that are already ringing and skip the first 100 ms, so starts
on a still string need their own test.

### A string locked to the bow (v1.0.1)

Near the bridge with a firm bow, the twisting string can lock onto the bow:
it sticks and travels with the hair, with no slip at all, and stays silent for
as long as the note is held. The twist damps even a steady twist at each
end, so the string never builds up enough pull to break free. Bright Soloist
(bow position 0.075, pressure 0.65) lost most of its G string and the low A
string to it; at the default bow position it took a pressure above 0.7 on
the A string. Holding the steady twist fixed it too, but brightened every
note by about 10 dB, so instead the player reacts: a string that sticks for
two periods without letting go (Helmholtz motion sticks for less than one)
gets 0.3 of the force, and less the longer it holds. Once it slips, the
full force keeps it in Helmholtz motion. Only a very soft note right at the
bridge (0.05 or less) with pressure above 0.8 still stays quiet. The test "A
firm bow near the bridge never locks the string silent" checks it.

## The Imperfection knob

Imperfection (0 to 100%, default 0, automatable, in the Play panel) scales all
of the above. At 0 the player corrects fully. At 100% the player does
nothing and the force windows and release are the old ones; the string itself
keeps its twist (below), so it is cleaner than before this change even there.

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

The player costs about +5.7% CPU per bowed note and +11% for four-note chords;
the twist another +10% and +18% (Callgrind, docs/benchmarks/baseline.json). The
twist is skipped while the bow is off the string, so pizzicato costs +4% and
idle nothing. The cost is the same at any Imperfection setting.

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
