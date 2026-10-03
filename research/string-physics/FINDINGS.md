# String and bow physics: findings and recommended engine changes

Thread: "Bowed-string physics" (world-class violin research, 2026-10-01). Owner of the engine merge: the "Sound engine flow diagram" thread.

Everything here was measured with a new lab engine (`research/string-physics/lab/` on branch `claude/project-thread-kj0aaw`): four strings on one bridge, the lite BowString waveguide extended so every physical ingredient can be switched on and off. Scores come from three independent references:

1. **Measured bow-force limits** (Schoonderwaldt, Guettler & Askenfelt 2008, bowing machine on a steel D string): the minimum and maximum bow force for Helmholtz motion across bow positions and speeds. Our test reproduces their sweep method (establish Helmholtz, then slowly lower or raise the force until it breaks).
2. **Attack playability** (Guettler diagram): 100 attacks from rest, force 0.05-2.5 N x acceleration 0.2-5 m/s^2, D string, beta 0.09. Counted as perfect (Helmholtz from the first slip) or acceptable (under 50 ms, Guettler & Askenfelt's listener limit).
3. **The real Iowa violin**: 12 bowed open-string notes (G D A E x pp mf ff) and 4 pizzicato open strings, analysed for harmonic spectrum, noise between harmonics, cycle-to-cycle shimmer and jitter, and per-partial decay times.

The literature survey behind the numbers is `literature.md` (about 50 sources, every number cited). Tables: `results/limits.md`, `results/experiments.md`; raw JSON per experiment in `results/exp/`.

## Headline results

| | measured (SGA08) | lite today (hyperbolic friction) | thermal rosin + real hair (recommended) |
|---|---|---|---|
| Fmin coefficient c_lower at 5 / 10 / 20 cm/s (g/s) | 7.0 / 4.2 / 1.8 | 0.8 / 0.8 / none | **8.6 / 4.2 / 2.5** (4 bow points: 7.9 / 4.4 / 2.5) |
| Fmax coefficient c_upper (kg/s) | 1.0 / 0.73 / 0.75 | 0.62 / 0.56 / none | **0.62 / 0.76 / 0.64** (4 points: 0.97 / 0.73 / 0.69) |
| Fmin independent of bow speed (measured fact) | yes | no (Fmin grows with speed) | yes (within 2x over 4x speed) |
| Attacks from rest: perfect / under 50 ms (of 100) | players: 44% perfect | 0 / 4 | 8 / 26 (4 points: 13 / 30) |
| Iowa open-string notes that reach Helmholtz motion (of 12) | 12 | 7 | 12 |
| Noise between harmonics vs real | 0 dB | +1 dB | -3.5 dB |
| Shimmer (dB) / jitter (cents) | 0.085 / ~1 | 0.093 / 1.5 | 0.068 / 0.93 |

"None" means the lite model never held Helmholtz motion at 20 cm/s on that string in the sweep. In plain words: with today's friction curve the engine's playable window is the wrong shape (too narrow at normal speeds, wrong trend with speed), which is why so many settings scratch or whistle and why attacks are rarely clean. Temperature-dependent rosin friction plus realistic bow-hair stiffness reproduces the measured window almost exactly, with no tuning to that data.

## Ranked engine changes

Priority = audible gain x confidence / cost. CPU figures are from the unoptimised lab (double precision), relative to today's lite string (85 ms per second of audio in the lab; the shipping engine is far cheaper).

### P1. Thermal rosin friction (replace the hyperbolic curve) - do first
- Model: van Walstijn et al. 2026 (Acta Acustica 10:47), friction limit mu_s * y(tau) with mu_s 1.05, y(tau) = (1 + ya (tau/tauG)^xi) / (1 + (tau/tauG)^xi), ya 0.4, tauG 25 K, xi 2. One contact temperature per bow point, one first-order heat equation per sample: aT Fb dtau/dt + (bT sqrt(|v|/Fb) + cT) Fb tau = f |v_slip| / contact length, with aT 1e-6, bT 0.22, cT 1e-4 (Fb = force per metre of hair). Stick when |2Z (vBow - vh)| <= mu F, else slide with f = mu F. No Stribeck curve needed (their best fit used mu_d = mu_s too).
- Evidence: Schelleng limits above; Helmholtz cells in the from-rest map 70 -> 100 of 384; perfect attacks 0 -> 16; 12/12 Iowa notes play. Contact temperature swings 10-25 K per cycle, matching the literature (~30 K).
- Cost: +17% (one exp and one sqrt per bow point per sample; the exp can be a table).
- Side effect to handle: pitch flattens with force (-2 cents at 0.2 N, -7 at 0.45 N, -20 at 1.2 N on A4, v 0.2, beta 0.1). Real violins flatten too, but this is more than players tolerate. See P6.
- Code: `lab.h`, `String::contact`, branch "thermal friction after van Walstijn". Rosin presets fall out naturally (tauG = rosin softness; ya = how slippery hot rosin gets).

### P2. Realistic bow hair, 4 contact points - with P1, never alone
- Hair spring 110 000 N/m over the 10 mm ribbon (ours: 2000, 50x too soft), damper 10 kg/s (Pitteroff & Woodhouse via vW26). Use 4 contact points (vW26 found more makes no difference; our 4-point run matched the measured Fmax best: 0.97 / 0.73 / 0.69).
- Real hair alone with the old friction curve is worse (only 4/12 Iowa notes play; this is why the lite thread found "rigid hair is raucous"). The combination is what works.
- Cost: +11% for the 4th point. Stiffer hair is free.
- Later: make stiffness depend on bow position (k_full (1/p + 1/(1-p)), stiffer at frog and tip) and add tilt (linear force taper across the points) and hair width as player controls.

### P3. Per-string physical data - free, prerequisite
- Impedance Z0 (Pickering via Desvages): G 0.350, D 0.303, A 0.203, E 0.173 kg/s. Today every string is an A string (0.2), so the G string's force window sits 2x too low (Fmax) and 4x too low (Fmin).
- Intrinsic loss per string, fitted to the Iowa pizzicato partial decays (one-pole loss, T60 low / T60 at f_hi): G 5.7 s / 0.27 s at 2 kHz, D 1.9 / 0.26 at 2 kHz, A 1.5 / 0.50 at 3 kHz, E 2.5 / 0.84 at 4 kHz. The E string keeps its treble far longer than the generic filter allowed (0.84 s vs 0.25 s at 4 kHz).
- Files: `results/strings_pickering.txt` (rigid bridge), `results/strings_bridge0.5.txt` (with P5).

### P4. Rosin grain only after the attack
- Without any added irregularity the physics is too clean: noise between harmonics 60 dB below the real violin, shimmer 0.004 dB (real 0.085). Thermal friction and finite width do not create it on their own in this model.
- But the 3% grain halves clean attacks (perfect 34 -> 16, under 50 ms 51 -> 34 with thermal friction). Recommendation: keep grain at 3% for the sustained sound and fade it in over the first ~50 ms of a stroke. Making it slip-only or tied to bow speed changed little.

### P5. Shared bridge with all four strings live (sympathetic strings, double stops)
- A modal bridge admittance (40 modes: signature modes A0 275, CBR 405, B1- 470, B1+ 540 Hz plus modes fitted from the Iowa body IR above 700 Hz) that all four strings push on. Strings not being bowed ring sympathetically: bowing D5 on the A string leaves the open D and G strings ringing (energy ratio about 1:20 after release), an unrelated B4 leaves them nearly silent. Double stops interact through the bridge for free.
- Neutral on the playability scores (Fmin/Fmax unchanged within noise). Cost +13% (shared across strings).
- The modal set should come from the body thread's modal fit so the bridge and the radiated sound share poles (Maestre, Scavone & Smith 2017). Our generic set does not line up with the real violin's mode frequencies, so a joint fit to the pizzicato decays pushed its level to near zero; per-note decay dips only appear when the modes are the real instrument's. Until then use admScale 0.5 with the matching intrinsic loss file.
- Each open string adds a dip/peak pair to the bridge impedance, which shifts Fmin for harmonically related notes (Mansour et al. 2017). That is part of why real open-string resonances feel different under the bow.

### P6. The player's ear: automatic finger correction
- Stopped notes: the finger creeps to cancel bow-induced flattening (lab option `autoTune=1`; correction 5% of the error per period, up to +-40 cents). Open strings stay uncorrected, as on a real violin. In the clips this keeps stopped notes within 1 cent; open strings sit 3-6 cents flat under the bow.
- Expose as "Intonation follows ear" (on by default) plus a "pressure flattening" amount for players who want the raw effect.

### P7. Static string stiffness check (bug class)
- The waveguide loss filter loses energy at DC, so a string that sticks to the bow cannot build the restoring force a real string does. This is a candidate cause of the old "string locks silent near the bridge" bug (project memory: string-lock). Not tested here; worth a regression test in the new engine (bow at beta 0.04, high force, check it still slips).

### Low priority (measured, little or no gain)
- **Torsion**: realistic torsion (speed 5.5x, Q 30-50, impedance 1.25-4 x Z0) did not help any score and cut clean attacks (8 -> 4) while costing +25%. This agrees with Mansour, Woodhouse & Scavone 2017 (torsion barely changes the bow-point admittance below the 5th harmonic). The lite thread's Q 2 was unphysical; if torsion returns, use Q 30+.
- **Bending stiffness (dispersion)**: with Pickering's B (1.3-1.6e-5 G/D/A, 4.7e-5 E) the bowed scores are unchanged; it only matters for pizzicato and ring-out partial stretch (about 1 cent at partial 10). Add as a cheap allpass later. Our pizzicato fits gave larger B (5e-5 to 1.9e-4), probably biased by body-mode pulling; trust Pickering.
- **Hair damping 5 vs 10 kg/s, hair 110k vs 240k N/m, width 8 vs 10 mm**: all within noise on the scores. Width and tilt matter for timbre per SGA03 (+2-6 dB above partial 11 for a narrower ribbon), not for playability.

## Open problems (next research round)

1. **Brightness does not follow bow force.** Real violins brighten strongly with force (Schoonderwaldt: force "totally dominates" the spectral centroid). In the thermal model the centroid stays at ~2.2-2.5 kHz from 0.2 to 1.2 N on A4. The likely missing piece is the rosin's elastic pre-sliding layer (vW26 elasto-plastic part, sigma0 ~ 1e6 per metre), which needs an implicit 2-3 iteration solve per bow point (+10-30% CPU). Try next.
2. **Body IR above 6 kHz.** The Iowa body IR falls to -35..-45 dB above 6 kHz, so even a perfect sawtooth comes out 7-17 dB too dark at 5-13 kHz vs the real Iowa notes. Not a string issue; sent to the body thread.
3. **Attack rate vs players.** Professionals start 44% of attacks perfectly. Our best is 13% perfect on a grid that includes many bad gestures, so these are not comparable yet. The player-model thread should measure attack success for the gestures it actually generates.
4. **Release ring.** The Iowa recordings ring about 3 s after the note, our renders about 2 s, but the real endings include the player's diminuendo, so the measure is unreliable. Re-test with a real lift-off recording.

## Suggested defaults for the new engine (all as named parameters)

| parameter | value |
|---|---|
| friction | thermal, mu_s 1.05, ya 0.4, tauG 25 K, xi 2, aT 1e-6, bT 0.22, cT 1e-4 |
| bow | 4 points over 10 mm, hair 110 000 N/m, damping 10 kg/s, grain 3% faded in over 50 ms |
| strings | Z0 G .350 D .303 A .203 E .173 kg/s; loss per `strings_pickering.txt` |
| bridge | shared modal admittance, 4 strings live, scale 0.5 until the body thread's modal fit lands |
| intonation | finger correction on stopped notes, rate 0.05 per period |
| sample rate | 96 kHz internal (the narrowest gap between bow points is ~1.4 samples on the E string; the lab adds linear interpolation below 2 samples, lite clamps to 2) |

## Reproducing

```
g++ -O2 -std=c++17 -o /tmp/lab research/string-physics/lab/lab.cpp
cd /mnt/project-files/research/world-class/string-physics/tools
LAB=/tmp/lab python3 exp.py NAME [options]   # full score for one configuration
LAB=/tmp/lab python3 limits_all.py NAME      # measured-method Schelleng limits
LAB=/tmp/lab python3 clips.py                # listening clips
/tmp/lab limits D 62 0.1 strings=../results/strings_sga08.txt friction=thermal muS=1.05 hairStiffness=110000 hairDamping=10 bowPoints=4
```
Iowa references: `refs/iowa/` (48 kHz mono wav), metrics in `refs/iowa_metrics.json`.
