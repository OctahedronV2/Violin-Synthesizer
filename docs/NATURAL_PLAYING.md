# Natural playing: less like a synth lead

User testing of Octavio 1.0.1 said it sounded "like a lead". Two things in the
model behaved like a synthesiser rather than a player, and both are changed in
the voice (`source/engine/StringVoice.cpp`), not with filtering.

## 1. Slurs change finger instead of sliding

Before, every slurred note on the same string glided to its pitch over the
Portamento time (50 ms by default, a one-pole curve). That is a mono synth's
glide. On a violin, most slurred notes are a finger dropping onto the string or
lifting off it, and the pitch changes at once; only a shift of the hand slides.

Each string now keeps track of where the hand is:

- A position covers seven semitones: from the lowest first finger to the fourth
  finger stretched (`handSpan` = 6 above the lowest). First position starts a
  semitone above the open string.
- A slur to a note the hand reaches, to the open string, or from the open
  string changes finger: the pitch moves in 10 ms (`fingerChangeSeconds`).
- A slur to a note outside the hand's reach is a shift: the pitch slides over
  the Portamento time, easing in and out (raised cosine in log frequency)
  rather than rushing off at once and creeping in. After a shift up the new
  note falls under the third finger; after a shift down, under the first.

So Portamento now sets the time of a shift, and the factory presets with long
portamento (Romantic Soloist, Singing Legato) still slide on their shifts.

The old glide also made a swell: in the preset level test, the slur from the
open D to A was the loudest moment of the default preset, about 1.5 dB above
the notes around it. It is gone.

## 2. The player's arm and hand wander

Before, bow speed, contact point and the stopped pitch were exactly constant
through a note; only vibrato moved. Held notes in the Iowa recordings
(`research/scripts/fetch_iowa.py`, mf, 115 notes, no vibrato) wander slowly:

| Held notes, middle of the note | Iowa (median) | Before | After |
|---|---|---|---|
| Level wander below 3 Hz (std) | 1.0 dB | 0.23 dB | 1.09 dB |
| Pitch drift below 2.5 Hz (std) | 3.0 cents | 1.1 cents * | 2.7 cents |

\* vibrato leaking through the filter; the model had no drift.

Humanise now also drives, per string:

- bow speed: ±22% at full Humanise (standard deviation), so ±11% at the
  default 50%. The force follows the speed, so the bow stays in the clean
  window and Clean bowing is unaffected;
- contact point: ±10% of the distance from the bridge at full Humanise;
- the stopped pitch: ±7 cents at full Humanise.

The wander is noise through two one-poles in series (1.5 Hz), stepped at the
control rate. A single one-pole was tried first: its fine, fast jitter (about
1% of the bow speed above 100 Hz) made the string itself noisier, by up to
11 dB on the E string in the period-to-period scratch meter. With the second
stage the meter reads the same as with no wander. It has its own random
sequence, so the vibrato and tremolo draws are unchanged.

At Humanise 0 nothing wanders, as before.

## Checks

- Scratch (`ViolinSynthTests "[.bownoisereport]"`, Humanise 50%, pitch drift
  off so the meter reads only noise): every articulation within about 1 dB of
  before; held notes through bow changes −39.4 dB (was −39.2).
- `ViolinSynthTests "[.naturalrender]"` renders held notes G3 to E6 at
  velocity 64 without room (for comparison with the Iowa notes), a slow
  slurred melody and slurred runs.
- The sound check reference (`tests/golden/soundcheck.json`) was re-recorded.
- The preset level test now runs with Humanise off, and allows 3 dB instead of
  1.5 (2.5 for three presets): without the slide's swell, the default's
  loudest moment is a sustained note, while a detached preset's is a stroke's
  onset, up to about 2.5 dB higher.
