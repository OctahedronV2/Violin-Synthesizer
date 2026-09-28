# Bow Noise

After Phase 6, held legato notes had a start/stop pattern, and bow scratch was too loud on most articulations, worst on staccato and on the Bright Soloist preset. This page explains what caused it, what changed, and how it was measured.

## The scratch meter

Scratch is the part of the sound that doesn't repeat from one pitch period to the next. The meter (`noiseDb` in `tests/BowNoiseTests.cpp`) predicts each sample from one period earlier, with a gain fitted every two periods so a swelling or fading note doesn't count. It reports what's left over, in dB against the whole signal:

- A clean, steady tone reads about −40 dB.
- Pure noise reads 0 dB.

The bowed string is chaotic while it settles into the sawtooth (Helmholtz) motion. One extra sample of force can flip a single note between a clean and a scratchy start, so every figure here is averaged over many notes. Most average 16 notes from G3 to C6.

`ViolinSynthTests "[.bownoisereport]"` prints the full scorecard, plus a sweep of scratch across the Bow Pressure range.

## What was wrong, and what changed

### 1. Held legato notes stopped and restarted

Legato notes use up bow hair, so every 62 cm the bow turned automatically: about every 1.4 s at a moderate dynamic (see 1b for the longer bow). The turn took 80 ms. The bow slowed to 15% of its speed, then **reversed in a single sample**. That jolted the string, so the level fell to 14% and the note restarted with a scratch. The scratch was worst on the low strings, where the string takes longest to settle again.

**Change:** the turn now takes 40 ms, and the bow speed passes smoothly through zero (`Violin.cpp`). The force follows the speed, so the string is released and keeps ringing through the turn. The level now dips to about 40% instead of 14%.

### 1b. Bow turns landed in the middle of held notes

Even the smooth 40 ms turn scratches for about 50 ms: the string has to rebuild its Helmholtz motion in the other direction, and the level dips to about 40%. Neither a shorter or longer turn (15–150 ms) nor more or less weight through it made a reliable difference. On a held note nothing covers the scratch, so it sounds close and dry against the rest of the violin. The showcase had 21 turns, 20 of them mid-note, including the ones at 4 s and 15 s. A turn just before a note change was the worst: the new note starts on a string that is still recovering.

**Change** (`Violin.h`, `Violin.cpp`):

- **A longer bow.** The bow has 2 m of hair per stroke instead of 62 cm. The model bows a long note faster than a player would (a player saves bow on a long note), and every turn costs a scratch, so fewer turns sound better.
- **Turns with the note.** In a slur, once half the bow is used, the bow turns at the next note change, as a player would. A turn mid-note happens only if one note uses more than the rest of the bow.

The turn itself is unchanged, so the bow-change figures below still hold for each turn; there are just fewer of them, and they sit on note changes.

| Showcase, dry render, sympathetic resonance off so the meter only hears the bowed string | Before | After |
|---|---|---|
| Bow turns | 21 (20 mid-note) | 9 (all on a note change) |
| Time in the three legato sections scratchier than −12 dB | 2.2% | 0.8% |
| Time scratchier than −8 dB | 0.8% | 0.1% |

### 2. The Bow Pressure range reached into force levels where the model scratches

Bow Pressure spans each string's force window, as a fraction of Schelleng's maximum force. The window came from Phase 1 (`PHASE1_FINDINGS.md` §2.6), which measured where Helmholtz motion *exists* on the open string. The model already turns noisy well below that upper limit, long before a real string would, especially on the heavy G string and the thin E string. So the default pressure of 0.5 put the G string at half its maximum force, where notes stayed at −22 dB of scratch for over a second.

**Change:** the top of the window now stops where the tone is still clean after the attack (`StringData.h`):

| String | Before | After |
|---|---|---|
| G | 0.24–0.74 | 0.24–0.44 |
| D | 0.13–0.87 | 0.13–0.55 |
| A | 0.09–0.87 | unchanged |
| E | 0.11–0.87 | 0.11–0.36 |

The knob still goes from light to firm on every string, but its top is no longer the scratchy zone. The A string was clean across its whole window, so it is unchanged, and the default tone on it is unchanged too.

Sul ponticello, sul tasto and harmonics used to set their pressure as a point on the knob's range. They now set the force fraction directly, at the same values they had on the A string. So their tone colours are the same as before, and the same on every string.

### 3. Staccato crunched

- **Bite:** the extra force at the onset was +90%, which pushed the start of every stroke far past the clean limit. It is now +30%.
- **Stop:** the bow stopped on the string at full weight. With the weight fixed while the speed fell to zero, the force ended up far too high for the speed, and the stop crunched. The weight now eases off with the speed.
- **Carry-over:** staccato and spiccato set their weight at once. Before, it was smoothed from the previous note's value, so a stroke's first 20 ms used the wrong weight.

## Results

Before is the Phase 6 build and after is this change, with the same notes and the same meter. Values are the mean over 16 notes, with the worst single note in brackets. Lower is cleaner.

**First 100 ms of each note** (bow position 0.11, pressure 0.5 unless stated)

| Articulation | Before | After |
|---|---|---|
| Legato | −13.3 (−4.0) | −19.3 (−13.9) |
| Detache | −10.3 (−0.1) | −17.4 (−8.6) |
| Staccato | −8.3 (+1.3) | −8.2 (−3.4) |
| Staccato, bow stopping | −7.3 | −15.5 |
| Spiccato | −3.2 (+0.9) | −4.8 (+0.7) |
| Tremolo | −6.3 (+0.1) | −8.0 (−3.8) |
| Harmonics | −22.2 (−16.3) | −20.5 (−11.4) |
| Sul ponticello | −17.7 (−7.0) | −19.0 (−7.3) |
| Sul tasto | −17.3 (−6.0) | −17.6 (−6.8) |
| Con sordino | −13.5 (−4.8) | −19.7 (−10.1) |
| Legato, Bright Soloist bowing (0.075, 0.65) | −8.8 (+0.8) | −14.5 (−4.6) |
| Staccato, Bright Soloist bowing | −6.5 (+1.0) | −8.1 (−1.5) |

**Held legato notes through automatic bow changes** (48 changes; a turn sounds the same after 1b, it just happens less often)

| | Before | After |
|---|---|---|
| Level at the lowest point of the turn | 14% | 37% |
| Worst scratch around the turn | −4.6 dB | −7.3 dB |
| Scratch 50–300 ms after the turn | −17.5 dB | −19.2 dB |
| Tone just before the turn | −32.3 dB | −36.8 dB |
| Same, Bright Soloist bowing: level at lowest point | 15% | 41% |

Four tests in the normal suite guard these results: the bow-change dip and recovery, slurred lines turning only on note changes, the staccato stop, and legato and detache attacks.

## What is still open

- **The first 100–300 ms of a note** still scratch more than a real violin's, most on the G string. The model takes a while to settle into Helmholtz motion from rest, and these changes shorten that settling time without removing it. This is also why legato string crossings changed little (mean −10.8 → −11.0 dB): the new string starts from rest. Easing the weight in, a flatter rosin friction curve, extra string damping and a bow-hair filter were all tried. None gave a reliable gain without changing the tone colours (a flatter friction curve made sul tasto 20 dB brighter), so they were left out.
- **Spiccato and tremolo** are made of constant restarts, so they stay the noisiest articulations.
- **Harmonics** read about 2 dB noisier in the first 100 ms. Their tone colour is unchanged.
- **Presets:** 12 presets had their output gain re-levelled, because the new force windows change the level. All are within the level-match test's tolerance again.

Scratch and multiple slipping in the sustain are now handled by a player that listens to the string and adjusts the bow weight, with an Imperfection knob to turn it down: see [CLEAN_BOWING.md](CLEAN_BOWING.md).
