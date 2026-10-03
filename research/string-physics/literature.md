# Bowed violin string physics: literature survey for a real-time physical-model violin

Scope: the string, bow, friction and bridge coupling, with numbers and equations a synth developer can use. The body radiation (the convolution stage) is out of scope except where it acts as the string termination.

Conventions: β = bow–bridge distance divided by string length L. Z0 = √(T·ρL) is the transverse wave impedance. vb is bow velocity and Fb is normal bow force. Loss factor η = 1/Q. T60 = 6.91/(π f η) ≈ 2.2/(f·η).

Every number carries a citation tag in square brackets. The full source list is in Section 10.
- **[computed]** means I calculated the value from cited inputs using a cited formula.
- **[UNVERIFIED]** means I could not confirm the value against a primary source during this survey.

Our current engine, for reference: digital waveguide (DWG) at 96 kHz; hyperbolic friction with μs 0.8, μd 0.3, v0 0.1 m/s; Z = 0.2 kg/s; one-pole loss filter; torsion option (speed ratio 5, impedance ratio 3, Q 2); 3 bow points over 1 cm, with hair spring 2000 N/m and damper 5 kg/s; 3% rosin noise; rigid bridge; no dispersion.

---

## 1. Friction models

### Key sources
- **Smith & Woodhouse 2000**, "The tribology of rosin", *J. Mech. Phys. Solids* 48, 1633–1681 [SW00]. Covers steady-sliding friction measurements, the thermal-plastic model, and the hysteresis seen in stick-slip.
- **Woodhouse, Schumacher & Garoff 2000**, "Reconstruction of bowing point friction force in a bowed string", *JASA* 108, 357–368 [WSG00]. Measured friction-versus-velocity loops in the "glass-rod bow" experiment.
- **Woodhouse 2003**, "Bowed string simulation using a thermal friction model", *Acta Acustica u. Acustica* 89, 355–368 [W03]. The original thermal model inside a string simulation.
- **Galluzzo PhD thesis 2004**, Cambridge, "On the playability of stringed instruments" [G04]. Contains the bowing-machine Guettler scans and the thermal-plastic model equations. https://www.repository.cam.ac.uk/handle/1810/244810
- **Galluzzo & Woodhouse 2014**, "High-performance bowing machine tests of bowed-string transients", *Acta Acust. u. Acust.* 100, 139–153 [GW14].
- **Galluzzo, Woodhouse & Mansour 2017**, "Assessing friction laws for simulating bowed-string motion", *Acta Acust. u. Acust.* 103, 1080–1099 [GWM17].
- **Woodhouse & Galluzzo 2025**, "Enhanced tribological modelling of violin rosin", *Tribology Letters* 73:128 [WG25]. https://doi.org/10.1007/s11249-025-02062-4 · open PDF at https://www.repository.cam.ac.uk/items/b510d46f-3675-4f78-b347-c2b94a6a823e
- **Woodhouse, *Euphonics* web book**, §9.6–9.7 (2026) [EUPH]. https://euphonics.org/9-6-friction-and-rosin-a-sticky-problem/ and https://euphonics.org/9-6-2-varieties-of-thermal-friction-model/
- **Serafin, Avanzini & Rocchesso 2003**, elasto-plastic bowed string, SMAC-03 [SAR03].
- **Willemsen, Bilbao & Serafin 2019**, real-time elasto-plastic friction on a finite-difference (FD) stiff string, DAFx-19, 40–46 [WBS19].
- **Matusiak & Chatziioannou 2024**, "Elasto-plastic friction modeling toward reconstructing measured bowed-string transients", *JASA* 156(2), 1135–1147 [MC24].
- **Matusiak, Chatziioannou & van Walstijn 2025**, "Numerical modelling of elasto-plastic friction in bow–string interaction with guaranteed passivity", *Frontiers in Signal Processing* 5 [MCvW25]. https://www.frontiersin.org/journals/signal-processing/articles/10.3389/frsip.2025.1525044/full
- **van Walstijn, Chatziioannou, Lampis & Matusiak 2026**, "A thermal elasto-plastic friction model for bowed-string simulation", *Acta Acustica* 10, 47 [vW26]. https://doi.org/10.1051/aacus/2026042 — **the current state of the art, with a complete parameter table.**

### Models and equations

**(a) Friction-curve (Stribeck) model**, which is the kind our engine uses:

μ = μ(v), applied with a hysteresis rule (McIntyre–Woodhouse) when the load line 2Z0 cuts the curve three times [WG25].

- Classic steady-sliding fit [SW00, quoted in WG25 eq. 1]: μ(v) = 0.4·e^(−|v|/0.01) + 0.45·e^(−|v|/0.1) + 0.35. This gives μs = 1.2 and μd = 0.35.
- Hyperbolic form used by Schoonderwaldt et al. 2008 [SGA08]: μ = μd + K/(z − z0), with K = 8 cm/s, μd = 0.4 and z0 = 20 cm/s. This gives μs = 0.8 and Δμ = 0.4. Our μs 0.8 / μd 0.3 / v0 0.1 m/s sits in this conventional range.
- WG25 proposed a revised steady-sliding fit that must decay to 0 at high speed: μsteady(v) = 0.2·(|v| + 0.011)^−0.4 [WG25 eq. 13]. It fits the measured points equally well but behaves very differently above about 0.3 m/s.

**(b) Thermal-plastic model** [SW00, W03]:

f = A·Y(T)·sgn(v). The contact area A is proportional to Fb, so Coulomb's law holds. Y(T) is a temperature-dependent shear yield stress.

Heat balance used by Smith & Woodhouse [G04 eq. 1.12; WG25 eq. 5]:
- frictional work = advection by moving rosin + heat storage in the contact volume + conduction, which is a convolution with a heat-diffusion Green's function.
- Steady-state calibration [WG25 eq. 5]: Fb·μ(v)·v = v·a·δ·ρr·cr·T + T·A·Kr·√(3v/(8a·Dr)), where a is the contact radius, δ the rosin thickness and Dr the thermal diffusivity.
- Smith & Woodhouse also tested a thermal-viscous model (f ∝ γ(T)·v) and rejected it: it cannot produce stick-slip [G04 §1.1; EUPH §9.6.2].

**(c) Enhanced thermal ("rate-and-state") model** [WG25]:

f = A·h(v)·Y(T), with Y normalised so that K·Y(T0) = 1, where K = A/Fb.
- h(v) = 0.5·sgn(v)·(|v| + 0.27)^−0.4 for a normal cello bow [WG25 eq. 14].
- h(v) = 0.37·sgn(v)·(|v| + 0.35)^−0.4 for a rosin-coated rod [WG25 eq. 15].
- The crossing temperature T0 is 26.2 K above ambient (46.2 °C) for the bow, and 36.8 K above ambient for the rod [WG25].
- How h(v) was obtained: from the size of the force jump at first slip in more than 1000 measured Guettler transients [WG25 Fig. 6].
- Why it matters: it reproduces the abrupt first-slip jump that the original thermal model cannot produce [WG25].

**(d) Elasto-plastic (bristle / LuGre-family) models** [SAR03, MC24, MCvW25]:

F = σ0·z + σ1·ż + σ2·v, with ż = v·(1 − α(z,v)·z/zss(v)). Here α is an adhesion map with breakaway displacement zba, and LuGre is the special case α = 1.

Lumped-mass test values from MCvW25 (as reported by the fetch tool):
- σ0 ≈ 10⁶ N/m and σ1 = 0.5 kg/s
- Stribeck velocity 0.228 m/s
- μs 1.02 and μd 0.507

Distributed bow (10 mm hair width) values from MCvW25:
- σ0 = 2.82×10⁵ N/m² and σ1 = 0.0027 kg/(m·s)
- reference solutions needed 1024× oversampling; the audio-rate scheme ran at 48 kHz

**(e) Thermal elasto-plastic model** [vW26]: the current state of the art.

Friction per unit length F = Fb·σ0·z, where Fb is the normal force per unit length = fb/Wb.

Bristle (rosin shear) equation:
- ż = v·(1 − α(z,v,τ)·z/zss(v,τ))
- zss = y(τ)·μstr(v)/σ0

Temperature factor and steady curve:
- y(τ) = (1 + ya·(τ/τg)^ξ) / (1 + (τ/τg)^ξ)
- μss(τ) = (μs + μd·ya·(τ/τg)^ξ) / (1 + (τ/τg)^ξ)
- μstr(τ) = μss/y

Simplified, non-convolutional heat balance [vW26 eq. 22]:
- aτ·Fb·τ̇ + (bτ·√(|v|/Fb) + cτ)·Fb·τ = Qf, where Qf = Fb·σ0·z·ς is the frictional heat net of stored bristle energy.

Physical meaning of σ0: σ0 = r·Gr/(hr·Tb), with rosin shear modulus Gr ≈ 500 MPa below 40 °C. With layer thickness 0.05 mm and bow tension 50 N this gives σ0 ≈ 2×10⁶ m⁻¹ [vW26 §2.2.3]. The rosin shear displacement is then bounded by μs/σ0, which is a few µm [vW26 eq. 18].

### Numbers

| Quantity | Value | Source |
|---|---|---|
| Steady-sliding μs (static peak) / μd asymptote | 1.2 / 0.35 | [SW00 via WG25] |
| Static μ immediately before first slip (bowing machine) | ~0.4–1.4. Highest at low acceleration (<~0.5 m/s²), lowest at low force | [WG25 Fig. 5] |
| Glass transition, violin rosin (DSC) | ~49–50 °C | [EUPH §9.6; WG25] |
| Glass transition, bass rosin | 16 °C | [EUPH §9.6] |
| Rosin viscosity change over the relevant T range | ~7 orders of magnitude | [EUPH §9.6] |
| Contact temperature in Helmholtz motion (original thermal model) | mean ~70 °C, with ~30 °C swing every cycle | [EUPH §9.6] |
| Enhanced model temperature peaks | ~10 °C lower than the original thermal model | [WG25 Fig. 13] |
| Rosin shear modulus (solid, <40 °C) | ~500 MPa | [vW26, citing GWM17] |
| Bow hair tension | 40–70 N, i.e. Tb = 4000–7000 N/m for Wb = 10 mm | [vW26] |
| vW26 calibrated set (cello G, 98 Hz, L 0.7 m) | μs 1.05, μd 0.3, σ0 1.0e6 m⁻¹, τg 25 K, ya 0.4, ξ 2.0, χ (zba/zd) 0.5 | [vW26 Table 1] |
| vW26 heat constants | aτ 1.0e-6 m/K; bτ 0.22 W^½ m^-½ K⁻¹; cτ 1.0e-4 m s⁻¹ K⁻¹ | [vW26 Table 1] |
| Resulting thermal time constant aτ/(bτ√(v/Fb)+cτ) | 10 ms while sticking; 30–200 µs while slipping (0.1–1 m/s, 0.5–2 N over 10 mm) | [computed from vW26] |
| Timestep used for "robust prediction" in research | Δt = 5 µs (200 kHz) | [vW26, citing W03] |
| Bow points needed (finite width) | N = 4; more points made "very little difference" | [vW26] |
| Real contact-area scaling | A ∝ Fb, Coulomb-like for a hair ribbon; a rosin rod behaves more like Hertzian contact (F ∝ load^2/3) | [WG25; EUPH §9.6] |

### What each model changes, audibly and measurably
- **Friction-curve model:** gives first-slip jumps that are too large. Its Guettler diagram is "spotty" and shows "little agreement" with measurement; it does "a uniformly poor job" [WG25 §4]. With the old steady fit, a player of a simulated "friction-curve cello" "would find their job almost impossible" [EUPH §9.6].
- **Hysteresis:** measured μ–v plots show a loop during slipping. Friction is lower on the way back to sticking because the rosin is still hot [WSG00; G04 Fig. 1.11]. A single-valued μ(v) cannot produce this.
- **Original thermal model:** establishes Helmholtz motion more readily. Its Guettler region is a solid, larger patch [EUPH §9.6]. However, it cannot make the abrupt first-slip jump; its first slips are too gradual [WG25].
- **Enhanced thermal model:** gives correct first-jump magnitudes and "strikingly similar" subsequent transient waveforms. It is "the clear winner" for early transients [WG25]. It still predicts too few successful transients at high force and low acceleration [WG25].
- **Thermal elasto-plastic model:** predicts the measured Fmin and Fmax across β with a single parameter set. Pitch flattening appears just below Fmax "although not quite to the same extent" as measured. It overestimates RMS at high force [vW26 §3].
- **Bristle damping σ1 and viscous σ2:** "only a marginal influence" on behaviour with realistic parameters; vW26 drops both.
- **Measured bridge-force corner rounding at first slip** (sampled at 20 µs) is consistent with finite rosin shear rather than an infinitely fast jump [vW26 Fig. 1]. An elasto-plastic σ0 gives this rounding naturally.
- **Sensitive dependence:** measured and simulated Guettler maps are "speckly". Tiny parameter perturbations move individual speckles; the system is "on the edge of chaos" [WG25; vW26]. A realistic model will therefore make some fine-grained irregularity without any added noise.

### Efficient real-time thermal model
**Yes, one is available.** vW26 eq. 22 replaces the Smith–Woodhouse convolution with **one first-order ODE per contact point** (one state τ). It is a linear first-order system with a time-varying coefficient, so exponential or implicit Euler integration is unconditionally stable. Cost: about 10 flops plus a lookup for y(τ) and μstr per bow point per sample [computed].

Maestre, Spa & Smith 2014 (ICMC) [MSS14] took another route:
- a 1-D heat-diffusion PDE along the bow width (FTCS: forward-time, centred-space), driven by |F·v|, with convection loss ∝ |v|
- a piecewise-linear μ(T)
- inside a DWG with up to N nodes under the bow

The FTCS scheme is stable for α·Δt/Δx² < ½ [MSS14].

### Implications for our engine
- Replace the static hyperbolic curve with a **temperature-scaled friction curve**: μ(v,τ) = y(τ)·μstr(v), using vW26 eqs. 30–32 with σ0 → ∞, or WG25 h(v)·Y(T). This keeps our Friedlander/hysteresis solver. Add the first-order heat ODE (vW26 eq. 22) per contact point.
- Expose "rosin" presets as (τg, ya, ξ, μs, μd), for example "violin rosin Tg ≈ 49 °C" and "soft rosin" with a lower τg. Add an "ambient temperature" macro, since bass rosin is soft at 16 °C.
- Optional "pre-slide" mode: an elasto-plastic σ0 ≈ 10⁶ m⁻¹ (force-normalised). It needs a local implicit (Newton) solve, because Fb·σ0 ≈ 10⁶ N/m is far stiffer than the sample rate resolves [computed].
- Reduce the artificial 3% rosin grain noise once thermal and finite-width effects are in: real irregularity comes from sensitive dependence and differential slipping (see §2).

---

## 2. Bow: finite width, hair compliance, stick modes

### Key sources
- **Pitteroff & Woodhouse 1998**, "Mechanics of the contact area between a violin bow and a string", Parts I–III, *Acta Acust. u. Acust.* 84. Part I is pp. 543–562 [PW98]; the page numbers for Parts II and III are [UNVERIFIED].
- **Guettler 1997**, "Bow notes", *Proc. IOA* 19(5) [Gu97]. https://knutsacoustics.com/files/bow-notes--i.o.a-1997-.pdf
- **Guettler**, "Some physical properties of the modern violin bow" [Gu-bow]. https://knutsacoustics.com/files/some-bow-properties-cism.pdf
- **Guettler & Askenfelt 1995**, "Some aspects of bow resonances", *STL-QPSR* 36(2–3), 107–118 [GA95].
- **Schoonderwaldt, Guettler & Askenfelt 2003**, "Effect of the width of the bow hair on the violin string spectrum", SMAC-03, 91–94 [SGA03]. https://knutsacoustics.com/files/bow-width-smac03-.pdf
- **Woodhouse, *Euphonics* §9.7–9.8** [EUPH-9.7]. https://euphonics.org/9-7-finite-bow-width/ and https://euphonics.org/9-7-1-simulating-a-finite-width-bow/
- vW26 Table 1 hair parameters, taken from PW98.

### Numbers

| Quantity | Value | Source |
|---|---|---|
| Hair ribbon width, violin | ~10 mm mid-bow | [EUPH-9.7.1; SGA03 ("roughly 1/32 of string length, about 10 mm")] |
| | 2.4–3.7% of open-string length, i.e. 7.8–12 mm for L = 325 mm (wider at frog, narrower at tip) | [Gu97], [computed] |
| Number of hairs | 140–200, at ~0.03 g/hair, so ribbon mass 4.2–6.0 g | [Gu-bow] |
| Single-hair stiffness (650 mm) | ~0.2 N/mm. 60 N of tension stretches the ribbon 1.5–2.1 mm | [Gu-bow] |
| Whole-ribbon longitudinal stiffness | 200 hairs stretched 1 mm needs ~60 N, i.e. ~6×10⁴ N/m end-to-end | [Pitteroff via Gu97] |
| Hair stiffness seen at the contact | k = k_full·(1/p + 1/(1−p)), with p the fractional position along the hair: ≈ 2.4×10⁵ N/m mid-bow, rising toward frog and tip | [computed from Gu97] |
| Distributed hair model used in research | Kh = 11×10⁶ N/m² per unit width, i.e. 1.1×10⁵ N/m over 10 mm; Dh = 0.62 kg/m, i.e. 6.2 g over 10 mm; ζh = 1000 kg/(s·m), i.e. 10 kg/s over 10 mm | [vW26 Table 1, from PW98] |
| Longitudinal wave speed in tensioned hair | 2200–2500 m/s | [Gu97, citing Bissinger] |
| Bow-stick modes (freely suspended, 9 bows) | ~60, 160, 300, 500, 750, 1000, 1300, 1700 Hz, with damping 0.2–0.6% (Q 250–80) | [Askenfelt via Gu97] |
| Bow rotational modes about the frog | lowest ~6 Hz (at frog) to ~30 Hz (at tip); at the spiccato point ~13, 130, 150 Hz | [Gu97] |
| Normal violin bow force | 0.5–1.5 N | [Gu97] |
| Effect of narrowing the hair (8→4 mm, 15→8 mm) | +2–4 dB above ~partial 11 at 30 cm/s; up to +6 dB, and +3–6 dB above partial 20; high force enhances the effect | [SGA03] |

### What changes audibly
- **Differential slipping and "spikes":** the string cannot stick across the whole ribbon during Helmholtz motion. Partial slips at the bridge-side edge put irregular spikes on the sawtooth, and these change from cycle to cycle [EUPH-9.7; PW98]. McIntyre, Schumacher & Woodhouse 1981 identify differential slipping as "the most important musically" source of aperiodicity [MSW81 abstract].
  - This is the physical origin of the noise and roughness between harmonics, especially near the bridge, and is what we currently fake with rosin noise.
- **Transients:** going from a point bow to even 2 "hairs" over 10 mm grows the simulated Guettler region to roughly the measured size [EUPH-9.7.1]. In other words, finite width substantially improves attack playability.
- **Brightness:** a wider ribbon slightly rounds the corner and reduces brilliance [SGA03; Guettler table, knutsacoustics "table-for-rossing"].
- **Tilt:** tilting the bow increases sharpness moderately, allows gentler onsets closer to the bridge [Guettler table], and makes partial slips less pronounced [SGA03].
  - In the EUPH-9.7 tilt study, the "correct" tilt runs force linearly from 0 at the bridge edge to 2× mean at the far edge. The "incorrect" tilt is the reverse. The two give different spike patterns [EUPH-9.7 §C].
- **Finite width suppresses S-motion and other higher types** near Fmax, because it smooths Schelleng ripple; "players would welcome this difference" [EUPH-9.7].
- **Hair compliance** strongly affects differential slipping [PW98 abstract].

### Implications for our engine
- **Our hair spring (2000 N/m) looks ~50× too soft** against 1.1×10⁵ N/m over the ribbon [vW26/PW98] and ~10⁵ N/m from hair tension [Gu97 computed]. Our damper (5 kg/s) is about right against ~10 kg/s per 10 mm.
  - Recommendation: distribute k ≈ 1–2.5×10⁵ N/m, m ≈ 6 g and c ≈ 10 kg/s over the contact points.
  - Make k a function of bow position (frog to tip) using k_full·(1/p + 1/(1−p)).
- **Set the number of contact points per string**, not as a fixed 3. In a DWG the point spacing is quantised to c/fs. At 96 kHz that is 1.3 mm (G), 2.0 mm (D), 2.9 mm (A) and 4.4 mm (E) [computed, using c from §3]. A 10 mm ribbon is therefore ~7, 5, 3 and 2 points.
  - Torsional waves are about 5× faster, so the torsional delay between points falls below one sample. Woodhouse used a minimum delay of 1 sample locally ("fudge") [EUPH-9.7.1]. Research models used 4–6 points at 120–200 kHz [vW26; EUPH-9.7.1].
- **Add "tilt" and "hair width" controls.** Tilt sets a per-point normal-force distribution (linear taper) and reduces the active width. These are expressive controls players really use, and the evidence above supports them.
- **Bow-stick modes:** add an optional modal bow (8 modes, 60–1700 Hz, Q 80–250) coupled to the hair termination. Use it for "bow character" presets and bounce strokes (spiccato rotational mode ~13 Hz). Its sustained-tone audibility is lower than the items above (it is debated in GA95).

---

## 3. String properties

### Key sources
- **Pickering 1985**, "Physical properties of violin strings", *J. Catgut Acoust. Soc.* 44 [Pick85]. Reached via the Desvages thesis table [D18]; Guettler also discusses Pickering's stiffness rankings (knutsacoustics "String-stiffness").
- **Guettler, "Typical string properties"** [Gu-typ]. https://knutsacoustics.com/files/Typical-string-properties.pdf
- **Desvages 2018 PhD thesis**, Edinburgh, "Physical modelling of the bowed string and applications to sound synthesis" [D18]. https://era.ed.ac.uk/items/60cf4338-33b4-4943-8d25-43e574ff5a2e/full
- **Desvages & Bilbao 2016**, *Applied Sciences* 6(5):135 [DB16].
- **Mansour, Woodhouse & Scavone 2016**, "Enhanced wave-based modelling of musical strings", Parts 1 & 2, *Acta Acust. u. Acust.* 102(6) [MWS16]. https://www.repository.cam.ac.uk/handle/1810/261228
- **Woodhouse *Euphonics* §5.4.4–5.4.5** (damping formulas) [EUPH-5.4].
- **Torsion sources:**
  - Woodhouse & Loach 1999, "Torsional behaviour of cello strings", *Acustica* 85 [WL99]; values used via vW26 Table 1
  - Gillan & Elliott 1989, *J. Sound Vib.* 130, 347–351 [GE89]
  - Bavu, Smith & Wolfe 2005, *Acta Acust. u. Acust.* 91, 241–246 [BSW05]
  - Mores 2019, *PLoS ONE* 14(2):e0211217 [Mo19]
  - Woodhouse *Euphonics* §9.5.3 [EUPH-9.5.3]

### Per-string data

Pickering-derived values, as tabulated in [D18], with L = 0.32 m:

| String | ρL (g/m) | r (mm) | T (N) | E (GPa) | Z0 = √(TρL) (kg/s) | c (m/s) | B (inharmonicity)* |
|---|---|---|---|---|---|---|---|
| E5 | 0.41 | 0.165 | 73.0 | 62.5 | 0.173 | 422 | 4.7e-5 |
| A4 | 0.72 | 0.30 | 57.1 | 19.5 | 0.203 | 282 | 1.3e-5 |
| D4 | 1.61 | 0.44 | 56.9 | 4.56 | 0.303 | 188 | 1.4e-5 |
| G3 | 2.79 | 0.425 | 43.9 | 4.79 | 0.350 | 125 | 1.6e-5 |

\*B is defined by f_n = n·f1·√(1 + B·n²), with B = π²EI/(T·L²) [computed]. Desvages assumes that only a core of radius r/2 bends for wound strings [D18]. The resulting stretch is about 1.1–1.4 cents at n = 10 and about 10–13 cents at n = 30 for A, D and G; for E it is 4 cents at n = 10 and 36 cents at n = 30 [computed].

The pure-steel E check uses Schoonderwaldt's Prim E: d = 0.26 mm, ρL = 0.41 g/m, Z0 = 0.18 kg/s [SGA08]. With E = 200 GPa this gives B ≈ 5.7e-5 [computed], which is consistent.

Commercial ranges [Gu-typ], for a 32.8 cm string length:

| String | f (Hz) | ρL (g/m) | c (m/s) | Z0 (g/s) | T (N) |
|---|---|---|---|---|---|
| E5 | 659.3 | 0.38–0.48 | 432.5 | 165–210 | 71.4–90.7 |
| A4 | 440 | 0.58–0.75 | 288.6 | 167–217 | 48.3–62.7 |
| D4 | 293.7 | 0.92–1.63 | 192.7 | 178–193 | 34.3–60.6 |
| G3 | 196 | 2.12–3.09 | 128.6 | 272–397 | 35.0–51.1 |

Total violin string tension is 189–265 N [Gu-typ]. Schoonderwaldt's Prim steel D measured 1.29 g/m and Z0 = 0.25 kg/s [SGA08].

**Implication:** Z0 ranges from about 0.17 (E/A) to 0.35–0.40 kg/s (G). Our single Z = 0.2 kg/s is right for A and E and about 2× too low for G. Because Fmax ∝ Z0 and Fmin ∝ Z0², the G string's playable force window should sit about 2× (upper) and about 4× (lower) higher than A [formula from §5].

### Damping

Woodhouse's decomposition of the loss factor for mode n of a string [EUPH-5.4; vW26 eq. 35 citing refs 43–44]:

- Air (Stokes, with an ad-hoc correction (d + 0.2), d in mm): η_air ≈ (d + 0.2)·(ρa/ρ)·(2√2·M + 1)/M², with M = (d/4)·√(ω/νa), ρa = 1.2 kg/m³ and νa = 1.5e-5 m²/s [EUPH-5.4.5].
- Bending (viscoelastic): η_bend ≈ 2·α·n²·η_E, where α = B/2 is the inharmonicity parameter and η_E is the material loss factor [EUPH-5.4.4]. Desvages notes that η_E is of order 10⁻³ [D18]. A "musically acceptable" plucked string needs at least 10 overtones with η_bend below about 2×10⁻³ [EUPH-7.2.2].
- Combined Q formula used in vW26 (verified fit to experiments): Q_i = (T + B·β_i²) / [T·(η_F + η_A/ω_i) + B·η_B·β_i²]. Its cello G values are η_A = 0.02 s⁻¹, η_F = 0.25 and η_B = 7.0×10⁻⁴ [vW26 Table 1].
  - As printed, η_F = 0.25 implies Q ≈ 4, so there is almost certainly a unit or scale typo. **[UNVERIFIED scale; do not copy η_F literally.]**
- Bridge/body loss, the dominant one for open strings: η_body(n) ≈ 2·Z0·Re{Y(ω_n)}/(π·n) [EUPH-5.1.1 eq. 8]. Y is the bridge admittance. This follows from the reflection coefficient R ≈ −1 + 2·Y·Z0.
- Desvages' fitted time-domain loss model for a violin A string uses 4 + 2 relaxation terms; the coefficients are tabulated in D18 p. xxxi.

Air damping alone gives very long decays [computed with the EUPH formula and the Pickering data]:

| String | Q, fundamental | T60, fundamental | Q, 5th partial | Q, 20th partial | T60, 20th partial |
|---|---|---|---|---|---|
| G | ≈ 2200 | ≈ 25 s | ≈ 5500 | ≈ 11000 | ≈ 6 s |
| A | ≈ 1600 | ≈ 8 s | | ≈ 8200 | ≈ 2 s |

So intrinsic air loss is small at low frequency for violin strings. Measured T60s of a few seconds for open strings on an instrument are dominated by body/bridge loss plus internal friction.

Other Q values reported:
- Bavu et al. measured a transverse Q of about 900 for a double-bass E string on a monochord [BSW05].
- Torsional Q was 57 on the instrument and 17 on the monochord [BSW05].
- Torsional Q is about an order of magnitude below transverse Q [Mo19]. "Torsional resonances have relatively small Q (~15–20)" is quoted from GE89 via BSW05 [UNVERIFIED as primary].

**Comparison with our filter:** T60 2.5 s at low frequency means η ≈ 4.5e-3 at 196 Hz (Q ≈ 220), and 0.25 s at 4 kHz means η ≈ 2.2e-3 (Q ≈ 450) [computed]. This is far lossier than the intrinsic string at low frequency, which is acceptable only because the bridge is rigid: the filter is standing in for the body. Once a body-admittance termination is added (§4), the string's own loss must drop to the intrinsic values above. Otherwise damping is double-counted.

### Torsion

| Quantity | Value | Source |
|---|---|---|
| Torsional wave equation | I·θ̈ − GJ·θ'' = M. Wave speed c_t = √(GJ/I). Surface impedance Z_t = √(GJ·I)/a² (= (πa²/2)·√(Gρ) for a uniform rod) | [EUPH-9.5.3] |
| Combined impedance seen by the bow (infinite string) | 1/Z_eff = 1/Z0 + 1/Z_t | [EUPH-9.5.3] |
| Cello G (Woodhouse & Loach values) | P_T = 4.2e-10 kg·m, K_T = 2.9e-4 N·m², Q_T = 30 | [vW26 Table 1, from WL99] |
| From those values: c_t, c_t/c, Z_t, Z_t/Z0 | c_t ≈ 830 m/s; c_t/c ≈ 6.1; Z_t ≈ 1.5 kg/s; Z_t/Z0 ≈ 1.25 | [computed] |
| Cello G steel (Chromcor) | c_t/c = 738/133 ≈ 5.55, torsional f1 = 543 Hz, Z0 = 0.93 kg/s | [Mo19] |
| Cello string used in Woodhouse's finite-width model | c_t/c = 5.2 | [EUPH-9.7.1] |
| Typical Z_R/Z0 | "typically a factor 2 to 4" | [SGA08 citing Schumacher] |
| Violin strings | torsion/transverse frequency ratio varies by string and maker | [GE89 via BSW05; violin-specific ratios UNVERIFIED] |
| Torsional damping | dominated by internal friction; Q approximately frequency-independent | [vW26 citing WL99] |

Effects of torsion:
- The torsional and transverse motion lock together: the torsional spectrum shows harmonics of the transverse fundamental, with "formants" near the torsional natural frequencies [BSW05].
- "Every ripple is strictly related to a torsional motion", torsion "sharpens" the transverse waves, and gut strings show more torsional flexibility [Mo19].
- Mansour et al. 2017 found that replacing Z0 by Z_tot in the Schelleng formulas overestimates the effect of torsion. The first torsional mode sits about 5× above the playing frequency, so torsion barely changes the bow-point admittance below about the 5th harmonic [MWS17].

**Implication:** our torsion Q of 2 is far below every reported value (17–60). Use Q_T ≈ 20–50 per string, speed ratio ≈ 5–6 and impedance ratio Z_t/Z0 ≈ 1.2–4, with a per-string preset. A Q of 2 effectively turns torsion into a dashpot at the bow. That over-damps the bow-point admittance and probably distorts ripple and jitter.

### Two polarisations
Only the in-plane (bowing-direction) polarisation is directly excited. The vertical polarisation is excited by variations in normal force and by coupling at the bridge and finger [EUPH-9.5.2]. For "full realism" the bridge needs the measured 2×2 admittance matrix [EUPH-9.5.2; MSS14]. Mansour et al. found that second-polarisation motion can change minimum-bow-force details [MWS17 abstract].

### Implications for our engine
- Use per-string physical presets (G, D, A, E) with the Z0, c, B, Q_T and torsion ratios above, plus string-brand variants (steel, synthetic, gut) taken from Gu-typ ranges.
- Add dispersion: B ≈ 1.3–1.6e-5 for G/D/A and about 5e-5 for E. Bending stiffness raises the playing frequency slightly at low force; in finite-width simulations notes play "a little sharp" at low force [EUPH-9.7]. It also rounds the Helmholtz corner.
- Rebuild the loss filter from the physical η components (air + internal + bend). Body loss comes from the bridge admittance (§4).

---

## 4. Bridge/body coupling as the string termination

### Key sources
- **Woodhouse *Euphonics* §5.1.1, §5.3.1, §9.4** [EUPH-5.1.1, EUPH-9.4]. https://euphonics.org/5-1-1-coupling-a-string-to-the-instrument-body/ and https://euphonics.org/9-4-chasing-the-wolf/
- **Woodhouse 2014**, "The acoustics of the violin: a review", *Rep. Prog. Phys.* 77, 115901 [W14].
- **Maestre, Scavone & Smith 2017**, "Joint modeling of bridge admittance and body radiativity for efficient synthesis of string instrument sound by digital waveguides", *IEEE/ACM TASLP* 25(5), 1128–1139 [MSS17].
- **Maestre, Scavone & Smith**, SMAC 2013 and WASPAA 2015, "Digital modeling of bridge driving-point admittances from measurements on violin-family instruments" [MSS13/15].
- **Maestre, Spa & Smith 2014**, ICMC-SMC [MSS14]. https://mtg.upf.edu/system/files/publications/icmc2014.pdf
- **Mansour, Woodhouse & Scavone 2017**, "On minimum bow force for bowed strings", *Acta Acust. u. Acust.* 103 [MWS17]. https://www.repository.cam.ac.uk/handle/1810/263508
- **Zhang & Woodhouse 2014**, *JASA* 136, 3371–3381 (reliability of hammer-measured admittance) [ZW14].
- **Chafe, Maestre et al. 2017**, ISMA (Messiah Stradivari admittance) [CM17].

### Equations
- Reflection at a bridge of admittance Y: R(ω) = (Y·Z0 − 1)/(Y·Z0 + 1) ≈ −1 + 2·Y·Z0 [EUPH-5.1.1 eqs. 4–5].
- Modal loss factor from the body: η_body(n) ≈ 2·Z0·Re{Y(nω0)}/(π·n) [EUPH-5.1.1 eq. 8].
- Passive modal admittance matrix, realisable as a parallel IIR filter [MSS14 eq. 11, from MSS13]:
  - Ŷ(z) = Σ_m H_m(z)·R_m, where each R_m is a 2×2 positive-semidefinite matrix (horizontal and vertical).
  - H_m(z) = (1 − z⁻²)/((1 − p_m·z⁻¹)(1 − p_m*·z⁻¹)), which is positive-real for |p_m| < 1, so it is guaranteed passive.
  - Fitting: mode frequencies and bandwidths by sequential quadratic programming (SQP), then gains by semidefinite programming (SDP) with passivity enforced. A cello Y_hh was fitted well with M = 15 modes [MSS14 Fig. 5].
- In MSS14 the strings couple at the nut through a real 2×2 matrix and at the bridge through the complex 2×2 body matrix, both polarisations, with a DWG for propagation and cascaded IIR loss filters.
- Bridge hill: bridge resonance on a body admittance, Y_b = (iω + k·Y_v)/(k − ω²m + iω·m·k·Y_v) [EUPH-5.3.1 eq. 6]. It produces a broad admittance hump around 2–3 kHz in violins ([Jansson et al. 2016]; [Elie et al. JASA 2014]; details [UNVERIFIED]).
- Minimum bow force with a measured admittance (Woodhouse 1993, as restated by MWS17 eq. 5):
  - Fmin = (2·vb·Z0²/(π²·β²·Δμ))·{max_t Re[Σ (−1)^(n+1)·Y(nω0)·e^(inω0t)/n²] + Re[Σ Y(nω0)/n]}
  - Note: the text extraction lost the exponent on Z0. It must be Z0², both for dimensional consistency and because it reduces to Raman's Z0²·vb/(2·R·Δμ·β²) [UNVERIFIED as printed].
  - MWS17 revise this for notes near strong body modes, where the bridge force departs from a sawtooth. Peak "wolfiness" shifts away from the body-mode frequency.

### What matters
- **Wolf notes:** when the string fundamental sits near a strong body mode, effective Fmin rises above the applied force. The motion alternates between Helmholtz and double-slip, giving a "warbling" wolf [EUPH-9.4].
  - The cello's strongest scaled-admittance peak is about 5 dB higher than either violin's, which is nearly 2× the string-to-body coupling [EUPH-9.4]. Violins have the same signature modes but weaker coupling, so wolves are milder.
- **Note-to-note playability and decay** come from Re{Y(nω0)}. Open-string ring time, pizzicato decay and how quickly a note dies after the bow lifts are all set by the bridge. A rigid bridge plus one global loss filter cannot vary note by note.
- **Example magnitudes [illustrative, Re Y values UNVERIFIED]:**
  - With Z0 = 0.35 kg/s and Re Y = 0.01 s/kg, η_body ≈ 2.2e-3 (T60 ≈ 5 s at 196 Hz).
  - At a body resonance with Re Y = 0.1 s/kg, η_body ≈ 0.022 (T60 ≈ 0.5 s).
  - Both values exceed air losses (η ≈ 4–6e-4 at the fundamental [computed, §3]). Bridge loss therefore dominates the damping of low partials on the instrument.
- **Second polarisation and coupling:** Re, Im and the off-diagonal Y_hv terms make the two polarisations beat or double-decay, and transfer energy between strings (§6).

### Implications for our engine
- Replace "rigid bridge + global loss filter" with a **2×2 (per string port) passive modal bridge admittance**: 20–60 modes [MSS14 used 15 for a cello Y_hh].
  - Fit it to a measured violin bridge admittance; the radiativity can share the same poles [MSS17].
  - Our convolution body can then become the "radiativity" filter driven by bridge force or velocity, ideally sharing the modal poles for consistency.
- Keep a simple "body coupling strength" user control (scaling Y·Z0) and a "wolf" control (gain and Q of the strongest mode near 400–500 Hz for violin) [frequency is a typical value, UNVERIFIED].
- Optional automation: compute the per-note Fmin from Y using the MWS17/Woodhouse formula to drive "auto-pressure" assistance, which keeps the force inside the playable window.

---

## 5. Transients and playability

### Key sources
- **Schelleng 1973**, "The bowed string and the player", *JASA* 53, 26–41 [S73].
- **Schoonderwaldt, Guettler & Askenfelt 2008**, "An empirical investigation of bow-force limits in the Schelleng diagram", *Acta Acust. u. Acust.* 94, 604–622 [SGA08]. https://knutsacoustics.com/files/schoonderwaldt-et-al.-empirical-schelleng.pdf
- **Guettler 2002**, "On the creation of the Helmholtz motion in bowed strings", *Acta Acust. u. Acust.* 88, 970–985 [Gu02]. https://knutsacoustics.com/files/actaacustica-on-the-creation.pdf
- **Guettler & Askenfelt 1997**, "Acceptance limits for the duration of pre-Helmholtz transients in bowed string attacks", *JASA* 101, 2903–2913 [GA97]. https://knutsacoustics.com/files/acceptance-limits-jas002903.pdf
- GW14, G04, WG25, vW26, MWS17.
- **Lampis, Mayer & Chatziioannou 2024**, "Assessing playability limits of bowed-string transients using experimental measurements", *Acta Acustica* 8, 44 [LMC24]. Not read in full.
- **Beigi & Conneely 2026**, arXiv:2609.14990 [BC26]. A preprint, not peer-reviewed; see the caution below.

### Schelleng limits

Classic forms [SGA08 eqs. 1–2; G04 eqs. 1.15–1.16]:
- Fmax = 2·Z0·vb/(Δμ·β). The factor 2 was in Schelleng's footnote and is used by Askenfelt, Woodhouse, Schumacher and Galluzzo.
- Fmin = Z0²·vb/(2·R·Δμ·β²), where R is the bridge resistance (Raman dashpot).

Modified for a hyperbolic friction curve, where Δμ is evaluated at the slip speed [SGA08 eqs. 4–5]:
- Fmax = (2·Z0/Δμ)·(vb + β·z0)/β
- Fmin = (Z0²/(2R·Δμ))·(vb + β·z0)/β²
- As vb → 0 these limits stay finite, and Fmin tends to ∝ 1/β.

Torsion correction proposed by Schelleng: replace Z0 with Z_tot = Z0·Z_R/(Z0 + Z_R) in Fmax, and Z0² with Z0·Z_tot in Fmin [SGA08]. MWS17 found that this overcorrects (§3).

### Measured results [SGA08]
Bowing machine; steel D (Z0 = 0.25 kg/s) and E (0.18 kg/s) strings; 11 β values × 24 forces at 5, 10, 15 and 20 cm/s.

| vB (cm/s) | c_upper (kg/s), Fmax = c_upper·vB/β | c_lower (g/s), Fmin = c_lower·vB/β² | Δμ estimate |
|---|---|---|---|
| 5 | 1.0 ± 0.24 | 7.0 ± 1.0 | 0.51 |
| 10 | 0.73 ± 0.05 | 4.2 ± 0.4 | 0.69 |
| 15 | 0.75 ± 0.04 | 2.3 ± 0.3 | 0.67 |
| 20 | 0.75 ± 0.03 | 1.8 ± 0.3 | 0.67 |

- Fmax matches Schelleng in the modified (friction-curve) form. The fitted slope is only about −0.5 at 5–10 cm/s instead of −1.
- **Fmin was found to be independent of bow velocity**, "in clear contradiction" to Schelleng. The breakdown at low force involves ripple and corner rounding.
- Δμ ≈ 0.6 without torsion, or about 0.4 with Z_tot ≈ 0.17 kg/s.
- Typical simulation Δμ values in the literature are 0.3–0.8.
- Worked example [computed from the table]: D string, vB = 0.1 m/s, β = 0.1 gives Fmax ≈ 0.73 N and Fmin ≈ 0.04 N.

Other results:
- vW26 matched measured Fmin and Fmax across β with the thermal elasto-plastic model [vW26].
- **Caution on BC26:** the 2026 arXiv preprint claims Fmin = C·Z·vb/β with C = 1.112 ± 0.017, from FD simulations. It is not peer-reviewed, conflicts with the β⁻² slope confirmed by SGA08, and is **not recommended** [BC26; treat as unverified].

### Guettler diagram (constant acceleration a, constant force F, starting from rest) [Gu02 eqs. 8a–8b]
For a lossless string with integer 1/β, β < 0.5, and F = normal bow force, perfect-attack bounds are:

- **Constant velocity:**
  - lower bound: v0 > β(1−β)·F·(μs−μd)/(2Z0)
  - upper bound: v0 ≤ [β(1−β)/(1−2β)]·F·(μs−μd)/(2Z0)
- **Constant acceleration from rest:**
  - lower bound: a > β(1−β)·F·[3μs − μd − 2√(2μs² − μs·μd)]/(T·Z0)
  - upper bound: a ≤ β(1−β)·F·[(3−4β)μs − μd − 2√((1−2β)(2(1−β)μs² − μs·μd))]·[(1−2β)·T·Z0]⁻¹
  - Here T is the fundamental period, and T·Z0 equals twice the string mass. On a given string, the allowed acceleration is therefore proportional to f0 and inversely proportional to string mass [Gu02].
- **Nut-side loss:** this raises the minimum velocity or acceleration through the λ^(1/β) terms (eqs. 10a–b). Losses on the nut side, or bow acceleration, are needed to prevent the cancellation of the 1/β-th slip [Gu02 §4].
- **Shape:** the allowed region is a wedge (triangle) in the (a, F) plane [Gu02; G04; GW14].

### Measured data
| Quantity | Value | Source |
|---|---|---|
| Acceptance of pre-Helmholtz transients, violin open G (196 Hz), 20 advanced players | prolonged periods up to **50 ms (~10 periods)**; multiple flyback/slips up to **90 ms (~18 periods)** | [GA97; Gu02] |
| Fraction of real attacks that are "perfect" (<5 ms to Helmholtz triggering) | **44% of 1694** attacks by two professionals playing repertoire | [Gu02, citing GA97 data] |
| Galluzzo machine grid | cello D (146.8 Hz). 20×20 (also 30×30) grids up to ~3 m/s² and ~3 N; transient counted in periods after first slip, capped at 25 | [WG25; GW14] |
| Example | β = 0.09, a = 1.56 m/s²: 1.73 N gives a perfect start; 0.84 N never reaches Helmholtz | [WG25 Fig. 3] |
| Quasi-static pre-slip force under constant acceleration | f ≈ T·y/(L·β(1−β)), y = a·t²/2; bridge force f_br ≈ T·y/(β·L) | [WG25 eqs. 6–8] |
| First slip in Helmholtz motion vs force | at higher force the first slip is an abrupt jump; at lower force there is no jump | [WG25 Fig. 9; EUPH-9.6.2] |

Note that WG25 uses P for tension in the f and f_br equations; it is written T here.

### Regimes and artefacts (qualitative)
- **Below Fmin:** double or multiple slipping, the "surface" or "whistly" sound [MWS17; G04 Fig. 1.14].
- **Above Fmax:** raucous or crunchy, aperiodic motion [MWS17].
- **S-motion:** occurs mostly beyond Fmax, and also at high force within the Helmholtz region [SGA08]. MWS17 classified it as Helmholtz-type because it has one slip per period.
- **Schelleng ripple:** about L/xb ripples per period, e.g. about 13 at β = 0.075 [vW26].
- **Pitch flattening:** occurs just below Fmax. It was observed in measurement and simulation [vW26 Fig. 12; EUPH-9.7 Fig. 11].
  - At low force notes play slightly sharp, attributed to bending stiffness [EUPH-9.7].
  - Typical magnitudes are up to roughly 10–20 cents near Fmax [UNVERIFIED; the EUPH plot uses −10 cents as a non-Helmholtz floor marker, so its range is around 10 cents].
  - Musicians notice "pitch wavering" more than steady flattening, because the effect is twitchy [G04 p. 6].
- **ALF (anomalous low frequencies):** pitches up to an octave below the open string at high force and low speed (Mari Kimura) [secondary source: Ljubljana journal summary of Schoonderwaldt 2009]. Mechanisms involve torsional or transverse triggering [same, UNVERIFIED in primary].
- **Wolf:** see §4.

### Implications for our engine
- Implement a playability-assist layer driven by the physics:
  - compute Fmax and Fmin from per-string Z0, Δμ, β and vb, plus Y(ω) for Fmin
  - optionally compute the Guettler acceleration window for the attack
  - offer "auto" (keep inside the window), "assist" (soft-clamp) and "raw" (full physics, including scratch, whistle and ALF) modes
- Default attack automation: aim for a <5 ms transient about 50–60% of the time. Allow occasional 10–50 ms imperfect starts so attacks are not machine-perfect, since 44% of professional attacks are perfect and up to 50–90 ms is accepted [Gu02; GA97].
- Expose user controls for bow acceleration (attack profile), bow force and β, and show them on a live Schelleng/Guettler display.
- Validation metrics: run Schelleng and Guettler grids on our engine and compare region shapes with SGA08 (slopes and c coefficients) and GW14/WG25. Use transient length in periods after first slip (25-period cap), first-jump magnitude, the "sawtoothness" metric (f1/f2 ratio normalised by 2 [MWS17]), ripple count, flattening in cents, RMS and spectral centroid maps [vW26 §3].

---

## 6. Double stops and sympathetic coupling

### Key sources
- MWS17 §4.2 (sympathetic strings)
- MSS14 (4 strings × 2 polarisations coupled at the bridge through a 2×2 body admittance, and at the nut)
- **Zheng, Darabundit & Scavone 2026**, "Physical model of the Chinese yehu for sound synthesis", DAFx-26 [ZDS26]. https://www.dafx.de/paper-archive/2026/papers/DAFx26_paper_43.pdf
- *Euphonics* §7.3 "Multiple strings and double decays" [EUPH-7.3]

### What is known
- **Effective bridge impedance with the other strings attached** [MWS17 eq. 17]:
  - Z_eff = 1/Y + Σ_strings i·Z0,sym·cot(k_sym·L_sym)
  - Each open string adds a dip and peak pair (a tuned-absorber pattern, obeying Foster's theorem) at its own mode frequencies.
  - Its harmonics and subharmonics change Fmin for harmonically related notes. Example: a G2 string produced a 65.4 Hz peak (3rd harmonic on the string's 2nd mode) and a spike at 146.83 Hz [MWS17].
  - "Sympathetic strings can have a significant effect on the playability of the notes that are harmonically related to them" [MWS17].
- **Two strings coupled through a shared bridge** split and double-decay when mistuned by a few cents; EUPH-7.3 shows 0, 1, 3 and 5 cents mistuning [EUPH-7.3].
- **Double stops:** no dedicated violin double-stop study was found in this survey [gap]. Physically, the case reduces to two bowed strings sharing a bow and a bridge admittance matrix (as ZDS26 for the 2-string yehu and MSS14 for a quartet framework).
  - Expected effects: intermodulation through the bridge, mutual pulling of slip timing when the strings are nearly harmonically related, and beating when mistuned.

### Implications for our engine
- Always simulate all 4 strings (cheap with a DWG) as ports on the shared modal bridge: each string port injects force into the modes, and the modes return a velocity to every port.
  - This gives sympathetic ring of open strings, resonance boosts at G/D/A/E unisons and octaves, and correct double-stop interaction "for free".
- For double stops, add bow-force sharing between two strings (normal-force split by bow angle and tilt) and a two-string contact geometry (bow angle parameter).
- User control: "sympathetic resonance amount", with a physically correct default of 1.0. Options for muted open strings and finger damping of stopped strings.

---

## 7. Efficient real-time methods and existing products

### Academic
| Method | Example | Cost evidence | Source |
|---|---|---|---|
| DWG + scattering bow junction + lookup "bow table" | Smith 1986; STK Bowed | the classic real-time approach; a few dozen operations per sample | [JOS online book: ccrma.stanford.edu/~jos/jnmr/Bowed_Strings.html] |
| DWG + finite-width thermal friction + hair FDTD + modal bridge matrix | Maestre, Spa & Smith 2014 | nodes under the bow (NB) vs the sample rate the DWG then needs: NB = 3 at 3.3 mm and 30 kHz; 6 at 1.6 mm and 61 kHz; 10 at 1.0 mm and 91 kHz; 20 at 0.5 mm and 182 kHz (cello) | [MSS14 table] |
| FD stiff string + elasto-plastic friction (Newton) | Willemsen, Bilbao & Serafin 2019 | under 6% CPU for one bowed string | [WBS19 abstract] |
| FD, two stiff strings + elasto-plastic bow (Newton, ≤3 iterations at 44.1 kHz with Fb ≥ 0.05 N) + finger + modal bridge (11 modes) + parallel radiation filters | yehu, 2026 | C++: 10 s of audio in 1.49 s, ≈ 6.6× faster than real time | [ZDS26] |
| FD, two polarisations, collisions (bow, finger, fingerboard), energy-stable | Desvages & Bilbao 2016 | MATLAB timings in thesis Table 3.1; real-time port by J. Perry mentioned | [DB16; D18] |
| Modal string + torsion + finite-width thermal elasto-plastic | van Walstijn et al. 2026 | research tool: Δt = 5 µs, energy-stable fixed-step scheme, N = 4 bow points | [vW26] |
| Wave-based model with measured damping, dispersion, 2-polarisation body coupling, bow-hair longitudinal and transverse motion, bow stick | Mansour, Woodhouse & Scavone 2016 | research (cello) | [MWS16] |

### Commercial
- **SWAM Solo Strings (Audio Modeling):** "pure physical modelling with no samples". It uses a proprietary engine (SWAM-S) "derived from the Digital Waveguides approach", combined with "behavioural modeling" for expressive control (bow speed, pressure, position, vibrato, portamento) [audiomodeling.com SWAM-S page; Sound On Sound; KVR]. Internal details such as the friction law are not public [UNVERIFIED].
- Other products named in the brief (Pianoteq/Modartt, Aodyo, Garritan, Chafe) were not investigated in detail. No public primary technical documentation on violin friction modelling was found for them [gap].

### Implications for our engine
- Stay with a DWG per string: it is cheapest and our transient behaviour already lives there. Add:
  - modal 2×2 bridge
  - allpass dispersion
  - physical loss filters
  - a thermal state per bow point
- A modal string is the cleaner alternative if we want exact per-mode damping and the vW26 friction formulation. At 96 kHz, a violin G string needs about 100 modes to 20 kHz and an E string about 30 [computed]. Each mode costs about 4–6 operations, plus an M×N projection onto the bow points. That is roughly 1–3k flops per sample per string: affordable for one bowed string, but more than a DWG.

---

## 8. Measured spectral and behavioural signatures to reproduce

| Signature | Expected behaviour | Source |
|---|---|---|
| Helmholtz bridge force | sawtooth, harmonic amplitudes ∝ 1/n (−6 dB/oct) before body filtering. Deviations come from corner rounding (high-frequency roll-off) and Schelleng ripple (≈ L/xb ripples per period, i.e. structure near harmonics n ≈ k/β) | [G04; vW26; MWS17 "sawtoothness"] |
| Bow-point string velocity | rectangular stick/slip pulse with slip fraction ≈ β. Its spectrum is sinc-like with near-zeros at n = k/β (standard Helmholtz kinematics) | [standard; G04 ch. 1] |
| Brightness vs force | **bow force "totally dominates" spectral centroid**: more force sharpens the corner | [Schoonderwaldt 2009 via secondary summary; Guettler table row 1] |
| Brightness vs speed | more speed (same force) rounds the corner, giving less brilliance | [Guettler table row 2] |
| Brightness vs β | "only local spectral deviations"; the brightening near the bridge comes from the higher force needed there, not from β itself | [Guettler table row 7] |
| Hair width / tilt | narrower contact: +2–6 dB on partials above ~11–20 | [SGA03] |
| Finger-pad damping | reduces brilliance when stopped | [Guettler table row 6] |
| Pitch flattening | appears just below Fmax; slight sharpening at low force (bending stiffness) | [vW26; EUPH-9.7] |
| Jitter | small amounts make bowed tone "natural", too much sounds unmusical. Bound from bass-string measurements: period jitter < 0.4% (resolution limit 0.1 ms on a 25 ms period). Main musical source: differential slipping across the hair; torsion is a secondary source | [BSW05; MSW81 abstract] |
| Speckle / sensitive dependence | Guettler and Schelleng maps measured on the same rig are speckly; RMS and centroid maps vary cell to cell | [WG25; vW26 §3] |
| Torsional "formants" | torsional velocity spectrum has transverse harmonics, peaked near torsional resonances | [BSW05] |
| Partial-slip spikes | irregular spikes on the sawtooth, strongest near the bridge and with a flat bow | [EUPH-9.7; PW98] |
| Noise between harmonics | arises from the spikes and jitter above, plus true rosin and hair irregularity. No validated "% of force" figure was found for rosin noise [gap] | |

### Implications for our engine (metrics)
- Automated test suite:
  - Schelleng grid: β 0.02–0.2 × force (log) × speeds 5–20 cm/s; compare slopes and coefficients with SGA08.
  - Guettler grid: a 0–3 m/s² × F 0–3 N at β ≈ 0.09–0.18; compare with GW14/WG25.
  - Per-cell metrics: transient periods-to-Helmholtz, first-jump size, sawtoothness, ripple count, flattening (cents), centroid, RMS, period jitter (%).
- Comparison clips: real violin open strings and stopped notes at controlled bow force, speed and β. Bridge-pickup recordings are ideal because they remove the body. Compare spectral centroid vs force curves, partial slopes, and ring-out after bow release (bridge-loss check).

---

## 9. Ranked list: the 10 most impactful physics changes

CPU costs are per bowed string at 96 kHz relative to our current DWG voice (≈ 1×), and are my estimates [computed/estimated].

| # | Change | Why (evidence) | Est. CPU |
|---|---|---|---|
| 1 | **Thermal friction**: temperature-scaled friction curve μ = y(τ)·μstr(v) (vW26) or h(v)·Y(T) (WG25), with a first-order heat ODE per bow point (vW26 eq. 22). Rosin presets via τg (glass transition ≈ 49 °C), ya, ξ | the friction-curve model is "uniformly poor" for transients; both thermal models are much better, and the enhanced one is best for first slips [WG25]. Gives real hysteresis, attack character, flattening and the playable region | +2–5% |
| 2 | **Measured 2×2 modal bridge admittance as the termination** (passive parallel IIR, 20–60 modes, shared by all strings), replacing rigid bridge + global loss filter | sets per-note damping η = 2Z0·Re Y/(πn), Fmin variation and wolf notes, ring-out, polarisation coupling [EUPH-5.1.1; MSS14/17; MWS17] | +0.5–1× (shared across 4 strings) |
| 3 | **Realistic bow-hair mechanics**: k ≈ 1–2.5×10⁵ N/m (ours is about 50× too soft), m ≈ 6 g and c ≈ 10 kg/s per 10 mm, position-dependent k; per-string contact point count tied to c/fs; tilt/width controls | finite width with realistic compliance gives differential slipping (natural noise and jitter), grows the attack window to the measured size, and gives tilt/width brightness of 2–6 dB [PW98; vW26; EUPH-9.7; SGA03] | +5–20% (more points on G) |
| 4 | **Per-string physical parameters** (Z0 0.17–0.40 kg/s, c, T, string brands) | force windows scale with Z0 (Fmax) and Z0² (Fmin); G needs about 2–4× more force than A [S73; SGA08; Gu-typ] | 0 |
| 5 | **Physically derived intrinsic damping** (air + internal + bending loss; much lower loss at low frequency once body loss comes from the bridge) | corner rounding vs sharpening balance; avoids double-counting body loss [EUPH-5.4; vW26 eq. 35; D18] | +2–5% (a 2nd–4th order filter) |
| 6 | **Bending stiffness / dispersion** (B ≈ 1.3–1.6e-5 for G/D/A, ≈ 5e-5 for E) via allpass chain | corner rounding, low-force sharpening, Schelleng limits [EUPH-9.7; Pick85/D18] | +5–10% (4th–8th order allpass) |
| 7 | **Torsion fixed**: Q_T 20–50 (ours is 2), speed ratio 5–6, Z_t/Z0 1.2–4 per string; finite-width torsional delays | ripple and jitter character, transient detail. A Q of 2 is non-physical [BSW05; Mo19; vW26/WL99; MWS17] | already present (≈ +1×) |
| 8 | **All four strings always live, coupled through the bridge**, plus a double-stop bow model | sympathetic resonance, Fmin changes for harmonically related notes, double-stop interaction [MWS17; EUPH-7.3; ZDS26] | +3 × (string DWG ≈ 0.3×) ≈ +1× |
| 9 | **Second (vertical) polarisation** driven by normal-force fluctuation and bridge coupling | beating and double decay, pizzicato/release realism; can alter Fmin details [EUPH-9.5.2; MWS17] | +0.5–1× |
| 10 | **Optional elasto-plastic pre-sliding** (σ0 ≈ 10⁶ m⁻¹, bounded µm shear) plus **bow-stick modes** (8 modes, 60–1700 Hz, Q 80–250) | slightly rounded first-slip corner seen in measurements; finite-time stick→slip; bow character and bounce [vW26; MCvW25; Gu97] | +10–30% (implicit Newton, 2–3 iterations per point) + ~3% |

**Recommended order:** 4 → 1 → 3 → 2 → 5/6 → 7 → 8 → 9 → 10. Item 4 is cheap and needed by everything else. Items 1 and 3 interact strongly; validate them together on the Guettler/Schelleng grids before adding the body termination.

---

## 10. Source list and verification status

**Read in full, or the relevant sections fetched as text:**
- vW26 (Acta Acustica 2026, doi 10.1051/aacus/2026042)
- WG25 (Tribology Letters 2025)
- G04 (Galluzzo thesis)
- Gu02 (Guettler 2002)
- SGA08
- SGA03
- MWS17 (Cambridge repository preprint)
- MSS14 (ICMC 2014)
- ZDS26 (DAFx-26)
- BSW05
- D18 (parameter tables)
- Gu97 (bow notes)
- Gu-bow
- Gu-typ (typical string properties)
- the Guettler spectrum table ("table-for-rossing")
- *Euphonics* §5.1.1, 5.3.1, 5.4.4–5.4.6, 7.2.2, 7.3, 9.2–9.8

**Abstract or secondary source only:**
- MCvW25: numbers came via a fetch-tool summary, so treat its σ values as approximate.
- MC24, WBS19, MSS17, MSS13/15, LMC24, Mo19 (via fetch summary), GA97 (via Gu02's quotation)
- Schoonderwaldt 2009 "sound palette" (via secondary summary)
- MSW81 (abstract)
- SWAM (marketing pages)

**UNVERIFIED items:**
- the η_F scale in vW26 Table 1
- typical violin Re{Y} magnitudes
- the bridge-hill details
- the violin wolf-mode frequency
- violin-specific torsional ratios (GE89)
- pitch-flattening magnitude in cents
- ALF mechanism
- the BC26 Fmin law (preprint, contradicts SGA08)
- PW98 Parts II–III page numbers
- MWS16 page numbers

The IOA proceedings PDFs could not be downloaded: Smith & Woodhouse "Modelling the friction of rosin", McIntyre/Schumacher/Woodhouse "Aperiodicity", and "Violinist's menagerie".
