# Pizzicato

Jake found pizzicato the most synthetic articulation by far. This change
rebuilds the pluck from how violinists actually play it and fits it to 248
recorded plucked notes.

## What was wrong

The old pluck pushed a short burst of velocity into the string at a quarter of
its length, with the same loss for every note. Measured the same way as the
recordings (below), that gave three giveaways:

- **A hammer, not a pluck.** A velocity burst is how a piano hammer excites a
  string. Its spectrum is flat, so harmonics 2 and 3 came out as loud as the
  fundamental (up to 17 dB louder on the G string). A real plucked note falls
  away from the fundamental.
- **Every fourth harmonic missing.** Plucking at exactly a quarter of the
  string cancels harmonics 4, 8, 12 ... on every note, 20 to 30 dB down.
  Real notes have no such hole, because the finger lands a fixed distance
  from the bridge, not a fixed fraction of the string.
- **One decay for every note.** Open and stopped notes, high and low, all
  decayed at about 70 dB per second, in a single straight line.

## How violinists pluck

From playing technique and string acoustics:

1. **The fingertip grips, draws and lets go.** The pad of the finger pulls
   the string aside and the string slides off it. What starts the note is a
   displaced string being released, not a strike. The fingertip rolling off
   takes a fraction of a millisecond; a harder pluck lets go more sharply
   (the harp-plucking studies by Chadefaux, Le Carrou and Fabre measure this
   stick-then-slip on a real finger).
2. **Over the end of the fingerboard.** The right hand stays in one place, so
   the pluck lands about 7 cm from the bridge whatever note the left hand
   stops. On a shorter, stopped string that is nearer the middle, so the tone
   gets rounder as a string is played higher. Each pluck lands a little
   differently.
3. **Open strings ring; stopped notes don't.** Players know open-string
   pizzicato sustains much longer, and that a firmly stopped finger rings
   better than a light one. The soft fingertip takes energy from the string
   every period.
4. **Two directions of swing.** A plucked string moves both sideways
   (parallel to the top) and up and down. The bridge rocks easily sideways, so
   that swing drives the body strongly, sounds loud and dies fast; the
   vertical swing hardly moves the bridge and rings on. That gives the
   well-known double decay of plucked strings (Woodhouse, "A necessary
   condition for double-decay envelopes in stringed instruments", JASA 2021;
   the same effect is studied in guitar and piano strings).

## What the recordings say

`research/scripts/measure_pizzicato.py` downloads the University of Iowa
anechoic violin pizzicato runs (every note on every string at pp, mf and ff)
and measures notes up to a fifth above each open string. Medians:

| | Fundamental, first 80 ms / later | Harmonics 2-3 | Harmonics 4-8 |
|---|---|---|---|
| Stopped notes, recorded | -91 / -56 dB/s | -143 / -71 | -209 / -73 |
| Stopped notes, old synth | -77 / -70 | -123 / -100 | -330 / -201 |
| Stopped notes, new synth | -113 / -42 | -133 / -49 | -203 / -90 |
| Open strings, recorded | -17 / -15 | -78 / -15 | -20 / -32 |
| Open strings, old synth | -71 / -68 | -96 / -90 | -258 / -178 |
| Open strings, new synth | -28 / -18 | -38 / -22 | -94 / -21 |

Real stopped notes decay about twice as fast in their first 80 ms as later.
Open strings decay four times more slowly than stopped notes, and a stopped
note's decay rate rises with its pitch (about 0.13 dB per period, which is
what a fingertip taking a fixed share per period gives).

## The model

All in `StringVoice` (source/engine/StringVoice.cpp) and one addition to
`BowedString`:

- **A finger on the string** (`BowedString::setFinger`). At the pluck point
  the string moves partway from its free velocity to the finger's: `hold`
  is R / (R + 2Z) for a fingertip of mechanical resistance R on a string of
  impedance Z. The finger draws the string aside over 2.5 ms with a smooth
  (raised-cosine) velocity, holding it at 0.9, then lets go over 0.2 ms
  (softest) to 0.05 ms (hardest). The string's own restoring force does the
  rest: the release of a drawn string gives the plucked spectrum, falling as
  1/n with the notches of the pluck point. A finger touching a string that is
  still ringing also damps it, as a real one does. How far the finger draws
  the string (up to 1.7 mm) sets the loudness.
- **A fixed pluck point**: 70 mm from the bridge on the 328 mm string, so
  beta = 0.21 on an open string and more on stopped notes (up to 0.42).
- **Two swings.** The existing string is the horizontal swing; a second
  waveguide is the vertical one, used only for plucked notes. The pluck is
  split between them by its angle (0.5 rad from the top). The vertical swing
  drives the bridge at 0.9 of the horizontal's strength and is tuned 0.7
  cents sharp (it sees a stiffer bridge), which adds a slow, gentle beat.
- **Decay fitted to the recordings** (T60 at the fundamental and at 4 kHz):

  | | Vertical | Horizontal |
  |---|---|---|
  | Open string | 6.0 s, 1.5 s | 3.5 s, 0.1 s |
  | Stopped note | 700 / f0 s, 0.25 s | 120 / f0 s, 0.08 s |

  The stopping fingertip damps the horizontal swing most: the string can roll
  across the soft fingertip but is pressed vertically into the hard
  fingerboard.
- **Humanise** moves the pluck point by up to 8 mm, the angle by 0.2 rad, the
  strength by 10% and the release time by 20% from one pluck to the next, so
  repeated notes are never identical. At Humanise 0 every pluck is the same.

For other instruments the same model needs only the scale length, the pluck
distance and the fitted decay constants.

## Results

- The spectrum now falls away from the fundamental as recorded notes do, and
  the fixed hole at every fourth harmonic is gone.
- Decays: see the table. Stopped notes have the fast-then-slow shape; open
  strings ring for seconds.
- Loudness: the pluck has a wider dynamic range than before (median peak 0.10
  at pp to 0.24 at ff, against 0.09 to 0.16). The Pizzicato presets were
  re-levelled (output gain 6.0 to 2.25 dB, and 8.5 to 3.5 dB for Pizzicato
  Hall), because the notes ring longer.
- The pluck lands when the finger lets go, 2.5 ms after the note starts.
- CPU: the vertical swing is a second string waveguide for each plucked note,
  about 0.8% of a core per note on the cloud test machine. In the Callgrind
  benchmark the pizzicato scenario needs 30% more instructions (4325 to 5608
  per output sample; the baseline was updated), still about the cost of one
  bowed note. The other scenarios need 0.5% more, for the finger and
  vertical-swing checks on every sample.

## Still open

- **Brightness of the first 40 ms.** Recorded stopped notes have harmonics
  5 to 8 about 15 to 20 dB below the fundamental; the synth has them 22 to
  36 dB below. The pluck on the string itself has the ideal 1/n spectrum
  (checked on the waveguide alone), so the difference comes after the string,
  in the body path that bowed notes share. Not changed here.
- **Body-mode damping.** On a real violin, partials that land on a strong body
  resonance lose energy fastest; here the decay is smooth across frequency.
  The bridge admittance could set the horizontal swing's loss per partial.
- **Snap (Bartók) pizzicato and left-hand pizzicato** are not modelled.

## Checking

- `ViolinSynthTests "[pizzicato]"`: open strings ring longer than stopped
  notes, stopped notes decay fast then slowly, and repeated plucks differ with
  Humanise on.
- `ViolinSynthTests "[.pizzrender]"` writes single notes G3 to B5 at three
  dynamics and a musical phrase; `python research/scripts/measure_pizzicato.py
  <folder>` compares them with the recordings.
- The sound-check reference (tests/golden/soundcheck.json) was re-recorded:
  the articulations phrase contains a pizzicato note, and the bowed notes after
  it start from a different string and random-number state, and bowing is
  chaotic.
