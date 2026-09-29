# Reference sound (v1.1)

Octavio 1.1 was tuned against a recording Jake chose as his favourite real
violin sound: Dmitry Sinkovsky playing the Passacaglia from Biber's Mystery
Sonatas (baroque violin, gut strings, A415, a resonant church). The opening
phrase was transcribed to MIDI and rendered through Octavio in the same key, so
the two could be compared note by note.

## What was measured

Medians over the opening phrase. Ring is the level of the first three
harmonics 0.4 s and 0.8 s after the bow leaves the string, relative to the
held note. Peakiness is the spread of harmonic levels around their local
average (how uneven the spectrum is from harmonic to harmonic). Noise is the
bow noise between harmonics. Tuning spread is the standard deviation of each
note's centre pitch from equal temperament.

| | Ring 0.4 s / 0.8 s | Peakiness | Noise | Vibrato depth | Tuning spread |
|---|---|---|---|---|---|
| Recording | -18 / -20 dB | 10.3 dB | -62 dB | 4.6 cents | 4.7 cents |
| Octavio 1.0 | -20 / -33 dB | 8.0 dB | -68 dB | 8.7 cents | 2.3 cents |
| Octavio 1.1 violin | -13 / -25 dB | 8.8 dB | -69 dB | 9.7 cents | 2.2 cents |
| Octavio 1.1 baroque violin | -13 / -22 dB | 9.3 dB | -66 dB | 6.0 cents | 4.5 cents |

The two biggest gaps were the ring after each note and the unevenness of the
spectrum.

## What changed

- **Notes ring on after the bow leaves.** In 1.0 the bow slowed to a stop on
  the string at the end of every stroke, which damped the note in a few tens
  of milliseconds. Now, at the end of a smooth stroke, the bow keeps most of
  its speed while its weight lifts (`InstrumentSpec::releaseRing`), so the
  string is left vibrating, and the stopped string's own decay is twice as
  long (`violinSpec.loss`).
- **Radiation peaks** (`engine/Radiation.h`). The measured bridge admittances
  give about 4.5 dB of harmonic-to-harmonic spread; recorded violins show 10
  to 11. A fixed set of 36 peaks and dips from 250 Hz to 7 kHz follows the
  body, at the instrument's depth (`bodyPeaksDb`), and 40 narrower ones model
  the fine structure heard in one direction. Under vibrato, each harmonic then
  swells and fades on its own, as in recordings.
- **Air band.** The measured bodies stop at 10 kHz, which left the violin a
  low-passed sawtooth. The bridge force above 11 kHz is now added back after
  the body, at the level of the Iowa recordings.
- **Sympathetic strings** are a tenth of the model's full coupling on the
  modern violin, which matches the recordings; the baroque violin keeps full
  coupling.
- **Intonation** (the new Intonation control). Each note lands a little
  flat, by an amount that scales with the setting, and settles within 90 ms;
  its centre then strays from true pitch with a 16-cent standard deviation at
  100%. 10% (the default) is a modern soloist; 31% matches the recording.
- **Vibrato blooms.** It rises over 0.6 s from the Vibrato Delay with a raised
  cosine and speeds up slightly as it widens, rather than starting at full
  depth.
- **Stage.** A few early reflections and gentle left/right directivity
  differences sit between the violin and the Room, so the violin sounds
  played in a space even with little Room. The stage fades in over the first
  5% of the Room, so Room at 0 stays the dry violin, for your own reverb.

## The baroque violin

A fourth instrument, **Baroque violin**, uses the violin's strings with gut
losses: they ring 25% longer, the open strings answer at full coupling, and
the bow comes off at 80% of its speed at the end of each stroke. Its body
peaks are 1 dB deeper. The **Baroque Violin** preset adds a grainier bow
(Bow Noise 100%), a firmer pressure, sparing vibrato (12 cents at 5.4 Hz) and
freer intonation (31%). It plays at A440; transpose a semitone down in your
DAW for baroque pitch (A415).

Its bow noise is still about 4 dB below the recording's.
