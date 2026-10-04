// Octavio 2 player, M0: a minimal Live-mode violinist.
//
// Turns note-on / note-off events into physical gestures for the four strings: one bow (signed
// speed in m/s, force per string in N, contact point) and a left hand (finger position per string
// as a pitch in semitones, finger damping). Everything musical lives here; the strings only do
// physics. Live mode: it decides at note-on and never looks ahead (Studio mode comes in M4).
//
// What M0 does:
//  - velocity -> dynamics d (0..1) -> bow speed, contact point and force together, the force
//    placed inside the Schelleng window for that speed, contact and string (Schoonderwaldt)
//  - string choice: greedy cost (low position, few crossings, little hand movement)
//  - overlap = slur (same bow; finger change, shift or string crossing); else a new stroke
//  - bow changes through zero with limited acceleration, alternate directions, bow budget
//    (62 cm of hair) forces a change mid-note when the hair runs out
//  - shifts slide the finger with a raised-cosine curve, the bow lightens during the slide
//  - vibrato as finger motion: delayed bloom, width from dynamics, seeded rate/width wander,
//    none on open strings
//  - release = bow lifts off while moving, the string rings; idle fingers lift with a damped touch
//  - double stops on two strings when notes start together
//
// M4 adds: phrasing (phrase arch, high-loud, agogic and beat stress, messa di voce on long
// notes, fading when the velocities already carry dynamics), Studio look-ahead notes (ahead[]),
// a Viterbi string plan and anticipated shifts (Studio, opt-in), faster vibrato up the string
// and wider on stressed notes, bow styles (Auto, Legato, Detache, Staccato, Martele, Spiccato),
// and drawn curves (CC1/26/19/74) that take over from the player.

#pragma once
#include "Strings.h"

namespace o2
{
struct PlayerParams
{
    // dynamics from velocity: d = clamp((vel - velLo) / (velHi - velLo)) ^ velCurve
    double velLo = 20, velHi = 125, velCurve = 1.0;
    // bow speed (m/s) = speedLo + speedRange * d^speedCurve
    double speedLo = 0.05, speedRange = 0.35, speedCurve = 1.3;
    // speedMap 1: exponential, so equal velocity steps give equal loudness steps:
    // speed = speedPP * (speedFF / speedPP) ^ d (speedLo/speedRange/speedCurve unused)
    double speedMap = 1, speedPP = 0.05, speedFF = 0.55;
    // in a slur the bow follows each note's velocity (1) or keeps the stroke's dynamics (0)
    double slurFollow = 1.0;
    // auto bowing in Live mode: an overlapping note is slurred unless the slur already holds
    // slurMaxNotes notes or slurMaxTime seconds, or the note is accented (velocity up by
    // slurAccent or more): then the bow changes. 0 turns a rule off.
    double slurMaxNotes = 0, slurMaxTime = 0,
           slurAccent = 0; // off: Jake heard notes no longer ringing out (2026-10-01)
    // detache in quick passages (the previous note started less than shapeIOI s before): each
    // stroke speaks, then the bow eases to strokeSus of its speed (time constant strokeTau),
    // so the notes are shaped and separated instead of an even organ-like line. 0 = off.
    double shapeIOI = 0.3831, strokeSus = 0.7078, strokeTau = 0.1498;
    // ... and the bow force is released after the attack (Guettler's martele/detache: high force
    // to start, then a diminuendo by releasing force while the speed holds) to forceSus of it
    // quick strokes (quickRun note starts in a row each less than quickIOI s apart: passagework) use
    // their own stroke shape and stop (q* below, same meaning as the unprefixed settings), pressure
    // higher in the Schelleng window (quickP added to p) and the bow nearer the bridge (contact x
    // quickContact). Slower shaped notes (up to shapeIOI) keep the settings above.
    double quickIOI = 0.3, quickRun = 3, quickP = 0.04922, quickContact = 1.383;
    double qForceSus = 0.1172, qForceTau = 0.2024, qForceHold = 0.001039, qStrokeSus = 0.6583, qStrokeTau = 0.2016;
    double qStopForce = 0.4649, qStopTime = 0.04107, qStopAccel = 40.32;
    double forceSus = 0.4883, forceTau = 0.07482, forceHold = 0.0159;
    double dynGlide = 0.04; // s: speed, contact and force move to a new dynamic this smoothly
    // register: dB of extra bow speed by pitch (G3 .. E7 every 6 semitones), so a velocity plays
    // about equally loud anywhere on the instrument (fitted on the dry render)
    double reg55 = 4.7, reg61 = 1.4, reg67 = 3.5, reg73 = -2.8, reg79 = 4.1, reg85 = 8.7, reg91 = 9.5, reg97 = 9.0;
    // contact point, mm from the bridge: pp .. ff
    double contactPP = 32.0, contactFF = 14.0;
    double contactFollow = 0.0; // contact distance scales with the stopped length ^ this
    // force inside the Schelleng window: F = Fmin^(1-p) Fmax^p, p = posLo + posRange * d
    double cLower = 0.0042, cUpper = 0.75; // measured coefficients (SGA08, D string, kg/s)
    double posLo = 0.55, posRange = 0.2712;
    // the most a player presses (N): real ff tops out around 2-3 N; above that the rosin layer
    // pulls an open string flat (M2: -40 to -60 cents at the 4 N velocity 127 asked for)
    double forceCap = 2.0;
    // the player tilts the stick towards the fingerboard at pp, so only part of the ribbon
    // touches the string: the hair's width on the string is tiltPP of the full width at pp,
    // all of it at ff
    double tiltPP = 1.0; // 1 = off: in M2 a narrower contact made pp brighter, against real violins
    // bow acceleration limit, m/s^2 (higher at ff); 20/40 turn the bow round a little quicker
    // than 12.8/30 did (Jake's pick, bow-change listening test 2026-10-03)
    double accel = 20.0;
    double accelFF = 40.0;
    double landTime = 0.006; // bow lands on the string (force rise), s
    double biteFF = 0.35, biteTime = 0.03; // extra force at the start of loud strokes
    double changeDip = 0.5; // force reduction at a bow change (M4: 0.25 -> 0.5, fitted on the Haydn note-change dip)
    double releaseTime = 0.07368; // lift-off force time constant, s
    // a short separate note (held less than stopBelow s) ends with the bow stopping on the string
    // (decelerating at stopAccel m/s^2, force x stopForce) for stopTime s before it lifts: the
    // stopped hair damps the string, so quick detache notes end crisply instead of ringing on
    double stopBelow = 0.6, stopAccel = 25.0, stopForce = 0.5138, stopTime = 0.035;
    // hair resting on a stopped string is lossy: while the bow is stopped on it the string loses
    // stopDamp per round trip (else the pinned string rings on below the note, nut side of the bow)
    double stopDamp = 0.0, qStopDamp = 0.0;
    double crossTime = 0.02; // string crossing: force moves to the new string, s
    double bowLength = 0.62; // hair, m
    // bow distribution in Live mode (no note lengths): in a run of short strokes (under
    // taperLiveMax s) the player learns how long they last and spreads the bow so a stroke fits
    // the hair it has (0 = off); long notes keep their speed and change bow when the hair runs out
    double liveDistribute = 1.0;
    // a lifted bow is set down again where the next stroke has at least retakeRoom m of hair,
    // once it has been off the string for retakeAfter s
    // and at least retakeMin m from the end it starts at (players don't start at the very frog)
    double retakeAfter = 0.25, retakeRoom = 0.4, retakeMin = 0.12;
    // nearing the end of the hair the bow slows (decelerating at budgetSoft m/s^2) so the change
    // it is forced into is a gentle one, as a player saves bow (0 = off)
    double budgetSoft = 4.0;
    // a separate stroke eases off over its last strokeTaper s (bow speed, and the force with it,
    // down by taperDepth): the stroke's sound rounds off into the change as a player's does.
    // Studio knows the note's length; Live guesses it from the last strokes when they are short
    // (under taperLiveMax s) and recovers if the note goes on (0 = off)
    double strokeTaper = 0.22, taperDepth = 0.65, taperLiveMax = 0.8; // Jake's pick (version C)
    // left hand
    double shiftBase = 0.045, shiftPerSemi = 0.006; // slide time, s
    double shiftLighten = 0.2; // bow force reduction during a slide
    double vibDelay = 0.14, vibBloom = 0.3; // s
    // held notes: the width grows as the note goes on (players warm a long note up with the
    // left hand), from vibGrowStart x to at most vibGrowMax x the dynamic's width, by vibGrow
    // per second; and when the note's length is known it relaxes over its last vibTaper seconds,
    // gone for the last third of them (fitted to the held notes of the anechoic Haydn Finale,
    // 2nd violin, 2026-10-02)
    double vibGrowStart = 0.4, vibGrow = 0.5, vibGrowMax = 2.4, vibTaper = 0.3;
    // the same rock of the finger is a wider swing in cents up the string, where the sounding
    // length is shorter: width x 2^(semitones above the open string / 12 * vibPosition)
    double vibPosition = 1.0;
    // a note known to be shorter than vibShort seconds gets its vibrato's delay, bloom and
    // relaxing squeezed in proportion: a quick note is vibrated at once and to its end
    double vibShort = 1.2;
    double vibWidthLo = 8.0, vibWidthHi = 30.0; // cents peak-to-peak at d = 0 / 1
    double vibRate = 5.3, vibRateDyn = 0.6; // Hz, plus per unit d (M4: 5.6 -> 5.3 with vibRateHigh, fitted on Haydn)
    double vibWander = 0.15; // relative random wander of rate and width
    double vibAmount = 1.0; // the plugin's Vibrato control: scales every note's width
    double dynBias = 0.0; // the plugin's Dynamics control: added to every note's dynamics (0..1 scale)
    double liftAfter = 0.35; // an idle string's finger lifts after this long, s
    double liftDamp = 0.25, liftDampTime = 0.03;
    double openMute = 0.1, openMuteTime = 0.15; // muting an open string the bow just left (loss per round trip, s)
    double chordWindow = 0.03; // s
    // loudness balance across strings: a violinist plays the low strings with less bow so a melody
    // stays even. Fitted so mf notes match TinySOL's register balance (G -1.5, D -2.3, E -5.6 dB vs A)
    double speedG = 0.43, speedD = 0.50, speedA = 1.0, speedE = 0.75;
    // a string the bow lands on while already moving (crossing, double stop) gets a short extra
    // force so it is captured into Helmholtz motion at once instead of multiple slipping
    double crossBite = 0.3, crossBiteTime = 0.05;
    double noiseStart = 0.3, noiseRise = 0.08; // slip hiss while a note starts, then full
    double bite = 0.07393; // extra force at the start of every stroke (Guettler: capture needs force)
    // the player's ear: Helmholtz health from the strings (slips per period). Multiple slipping
    // -> more force; a string that sticks silent -> less force. Imperfection will scale this.
    double earUp = 0.25, earDown = 0.15, earMax = 2.106, earMin = 0.4, earRelax = 0.3, earWindow = 0.005,
           earWait = 0.05, earPeriods = 6.0;

    // ---- M4 phrasing: the player shapes the line so a constant velocity still sounds musical.
    // Every term is in dynamics units (d, 0..1; 0.1 is about 2 dB) and scaled by phrase (the
    // plugin's Phrasing control, 0 = every note at its velocity's dynamics).
    double phrase = 1.0;
    // a phrase starts after phraseGap s of rest or after a long note (over 0.8 s and phraseLong
    // times the recent inter-onset time); its dynamics rise from -phraseArc/2 to +phraseArc/2 with
    // time constant phraseRise s, and fall by phraseArc over the last phraseFall s when the end
    // is in view (Studio look-ahead)
    double phraseArc = 0.18, phraseRise = 1.5, phraseFall = 1.0, phraseGap = 0.3, phraseLong = 2.5;
    double highLoud = 0.08; // per octave above the recent mean pitch (melodic charge, high = loud)
    double agogic = 0.04; // a note twice as long as the recent notes (length known)
    double stress = 0.04; // the beat-like notes of a run (every 2nd, or 4th when quick) and phrase starts
    double restAccent = 0.25; // extra attack bite on the first note of a phrase
    // messa di voce: a note known to last mdvMin s or more swells to its middle and relaxes,
    // by up to mdvDepth (full from mdvFull s); a long note in Live swells by half and holds
    double mdvDepth = 0.12, mdvMin = 0.6, mdvFull = 1.5;
    // > 0: phrasing fades (to 30%) as the played velocities vary by this many steps on average:
    // a performance with its own dynamics needs less help (0 = off)
    double phraseVelSpread = 10.0;
    // ---- M4 left hand: in Studio (look-ahead) the strings are planned over the coming notes with
    // a Viterbi search (costs: hand position, shifts, string crossings, open strings on long notes);
    // Live stays greedy. anticipate: a shift on one string leaves late in the old note and lands
    // on the new note's start instead of after it. Both off by default: on the Haydn phrase suite
    // they raised unreal notes 1.4% -> 2.2-2.4% and off-pitch time 9.8% -> 10.5% (M4); 1 = on
    double fingerPlan = 0.0, anticipate = 0.0;
    double costShift = 1.0, costShiftSemi = 0.08, costCross = 0.9, costOpen = 0.8, costHigh = 0.6;
    double vibRateHigh = 0.4; // Hz faster an octave up the string
    double vibStress = 0.25; // vibrato width x (1 + this) on stressed and long notes
    // ---- M4 bow strokes: 0 Auto (inferred), 1 Legato, 2 Detache, 3 Staccato, 4 Martele, 5 Spiccato
    double bowStyle = 0;
    double legatoGap = 0.15; // Legato: notes this close are slurred too, s
    // Auto: a short separate note (under 0.45 s, with a gap after it) at this velocity or more is
    // played martele (0 = off); separate notes faster than autoSpiccato s apart and detached are
    // played spiccato (0 = off)
    double autoMartele = 0.0, autoSpiccato = 0.0;
    // drawn curves: CC1/26/19/74 lanes take over dynamics, vibrato width, rate, contact (0 = ignored)
    double drawnCurves = 1.0;
    unsigned seed = 1;

    // ---------------------------------------------------------------- M5 articulations
    // articulation: 0 Arco, 1 Pizzicato, 2 Bartok (snap) pizz, 3 Left-hand pizz, 4 Harmonic. The
    // keyswitches keyswitchBase .. keyswitchBase + 4 (MIDI 24..28 = C1..E1 with C4 = 60, "C0..E0"
    // in the C3 = 60 naming) select the same five and win over this until it changes.
    double articulation = 0, keyswitchBase = 24;
    double pizzModel = 1; // 0: the 2.0 placeholder impulse (comparisons only), 1: the shaped pluck
    double pizzImpulse = 0.3; // placeholder amplitude at velocity 127
    // right-hand pizz: plucked pizzPointMM from the bridge (over the end of the fingerboard),
    // displacement pizzAmpPP .. pizzAmpFF (m, exponential in the dynamics), release time
    // pizzTau (s), shortened by pizzBright at ff (a harder pluck leaves the finger faster)
    double pizzPointMM = 70.0, pizzAmpPP = 0.4e-3, pizzAmpFF = 1.5e-3, pizzTau = 0.1e-3, pizzBright = 0.5;
    // the plucking finger touches a ringing string first (pizzTouch s, loss pizzTouchDamp per round
    // trip); after the note-off a stopped note's finger eases off (loss pizzOffDamp per round trip)
    double pizzTouch = 0.004, pizzTouchDamp = 0.3, pizzOffDamp = 0.015;
    double pizzPull = 0.015; // s the finger takes to pull the string aside (whole periods, at least two)
    double pizzStopLoss = 0.02; // loss per round trip at the stopping fingertip while a plucked stopped note rings
    // Bartok: pulled up and let go so it slaps the fingerboard: bartokAmp x the displacement, a
    // nail-like release, the slap's knock (N at ff) a little after the release
    double bartokAmp = 0.7, bartokTau = 0.06e-3, bartokPointMM = 90.0, bartokClick = 10.0, bartokClickTime = 0.004;
    double bartokKick = 1.5; // how hard the board stops the string (x the pluck's wave), each rattle half the last
    // left-hand pizz: a left finger plucks lhFromNut of the string from the nut, softer and duller
    double lhAmp = 0.6, lhTau = 0.25e-3, lhFromNut = 0.2;
    // harmonics: the light finger's resistance in units of the string's impedance
    double harmTouch = 1.0, harmForce = 0.3; // ... and the bow force on a harmonic, x the normal

    // ---------------------------------------------------------------- M6 plugin controls
    // The user's scales on top of the player's (and its style's) own settings. At their defaults
    // (1, or 0 for the offsets) every result is bit-identical to the fitted player.
    double slideScale = 1.0; // Portamento: shift slide time x this (0: clean shifts)
    // String preference: -1 bright (low positions, higher strings) .. +1 dark (high positions on
    // lower strings); scales the string-choice costs (greedy and planned)
    double stringPref = 0.0;
    double vibRateAdd = 0.0; // Hz added to every note's vibrato rate
    double vibDelayScale = 1.0; // vibrato delay and bloom time x this
    double accelScale = 1.0; // bow acceleration (bow changes, getting up to speed) x this
    double shapeAmount = 1.0; // stroke shaping (detache speed and force release, end taper) x this
    double biteScale = 1.0; // the extra force at the start of a stroke x this
    double contactScale = 1.0; // the contact point's distance from the bridge x this
    // ---------------------------------------------------------------- M7 player styles
    // Terms the player styles (applyStyle below) turn on. The defaults are the Modern soloist,
    // which is exactly the 2.1 player: every one of these is off.
    // portamento: a note on the string the finger is already on (a leap of slideMin semitones or
    // more, in reach of the hand, so 2.1 would just put the finger down) is slid into with chance
    // slideProb, over slideTime x the shift's slide time
    double slideProb = 0.0, slideMin = 3.0, slideTime = 1.0;
    // an inflection: the finger lands scoop cents low and slides up over scoopTime s (chance
    // scoopProb per stopped note; Maqam, fiddle and Hungarian ornaments)
    double scoop = 0.0, scoopProb = 0.0, scoopTime = 0.08;
    // landing error, cents (sd) of a stopped note; the ear takes 60% of it away over earFix s
    double pitchError = 0.0, earFix = 0.3;
    double openPenalty = 0.8; // string choice: an open string on a melody note costs this (Live)
    double mdvPeak = 0.5; // where in a long note its swell peaks (0.5 the middle; later leans in)
    // Expressive intonation: this share of the Pythagorean leaning (sharp leading tones and
    // major thirds, flat minor ones, pure-fifth open strings) relative to the Key parameter
    double intonAmount = 0.0;
    // ---------------------------------------------------------------- M7 articulations and bow
    // articulation (continued): 5 Tremolo, 6 Sautille, 7 Portato, 8 Col legno battuto; keyswitches
    // keyswitchBase + 5 .. + 8 (MIDI 29-32, F1-G#1). contact: 0 ordinario, 1 sul ponticello,
    // 2 sul tasto; keyswitches keyswitchBase + 9 .. + 11 (MIDI 33-35, A1-B1), latched separately so
    // they combine with any bowed articulation (tremolo sul ponticello).
    double contact = 0;
    // tremolo: the bow reverses tremoloRate times a second (free), or synced to the host's tempo
    // (tempo > 0, bpm): tremoloSync 1 = 16ths, 2 = 16th triplets, 3 = 32nds. Short strokes in the
    // upper half (tremoloHair of the bow from the frog), each reversal a fresh catch (tremoloBite)
    double tremoloRate = 12.0, tremoloSync = 0, tempo = 0.0, tremoloHair = 0.7, tremoloBite = 0.15;
    // sautille: one stroke per note at the balance point (sautHair), the stick bouncing: the force
    // swings between sautFloor and full once per stroke (a half sine), the stroke no longer than
    // sautLen m; the bounce period follows the notes (sautMin .. sautMax s)
    double sautHair = 0.38, sautFloor = 0.25, sautLen = 0.035, sautMin = 0.05, sautMax = 0.16;
    // portato (loure): notes joined in one bow, each with a pulse: the bow eases to portRest of its
    // speed between notes (for up to portGap s after a note-off), then each note swells in
    // (from portDip to 1 + portSwell, rise portRise s, decay portDecay s)
    double portGap = 0.35, portRest = 0.12, portDip = 0.45, portSwell = 0.15, portRise = 0.04, portDecay = 0.15;
    // col legno battuto: the stick's speed at pp .. ff (m/s), contact time (s), struck clMM from
    // the bridge, the wood's click (N at ff)
    double clSpeedPP = 0.25, clSpeedFF = 1.6, clContact = 0.0006, clMM = 30.0, clKnock = 2.0;
    // contact overrides: mm from the bridge, and the pressure moved in the window (sul ponticello
    // is played light, the bow nearly on the bridge: glassy upper partials; sul tasto over the
    // end of the fingerboard: soft, flute-like)
    // and the bow speed (x): less bow near the bridge (the string swings wider there for the same
    // speed), more over the fingerboard (flautando)
    double pontMM = 7.0, pontPress = -0.12, pontSpeed = 0.6, tastoMM = 72.0, tastoPress = -0.08, tastoSpeed = 1.4;
    // bow (Tone tab): the tip's lightness (force x 1 - tipLight * hair fraction: a baroque bow is
    // light at the tip and the player lets it be) and the lift-off stroke (a separate note dies
    // away by liftStroke of its speed, time constant liftTau: every stroke breathes). 0 = modern.
    double tipLight = 0.0, liftStroke = 0.0, liftTau = 0.35;
};

// M7: the player styles, in the order of the Player Style parameter (append only)
enum PlayerStyle : int
{
    styleModern,
    styleRomantic,
    styleHungarian,
    styleBaroque,
    styleMaqam,
    styleFiddle,
    styleStudent,
    styleCount
};

// A style is a set of offsets on the automation layer only (tone presets stay separate), applied
// to the user's own settings. Modern soloist leaves them as they are. Tuned by ear-proxy against
// the renderer (octavio-2/2.2/m7-player clips): each moves only what its players are known for.
inline void applyStyle (PlayerParams& p, int style)
{
    switch (style)
    {
        case styleRomantic: // slow, wide vibrato; more and slower slides; deep swells, broad arcs
            p.vibRate -= 0.5;
            p.vibWidthLo *= 1.3;
            p.vibWidthHi *= 1.35;
            p.vibDelay *= 0.7;
            p.vibBloom *= 1.3;
            p.shiftBase *= 1.8;
            p.shiftPerSemi *= 1.8;
            p.shiftLighten = 0.3;
            p.slideProb = 0.45;
            p.slideTime = 1.6;
            p.mdvDepth *= 1.8;
            p.mdvMin *= 0.8;
            p.mdvPeak = 0.6; // leans into the note: the swell peaks late (agogic within the note)
            p.phraseArc *= 1.4;
            p.agogic *= 2.0;
            p.highLoud *= 1.3;
            p.intonAmount = 0.5;
            break;
        case styleHungarian: // bite, fast intense vibrato that blooms at once, quick ornamental slides
            p.bite *= 2.0;
            p.biteFF *= 1.6;
            p.restAccent *= 1.8;
            p.stress *= 2.0;
            p.vibRate += 0.8; // ~6.7 Hz and ~40 cents at f: fast and intense, but still a violinist's
            p.vibRateDyn += 0.3;
            p.vibWidthLo *= 1.2;
            p.vibWidthHi *= 1.15;
            p.vibWander = 0.25; // and alive: no two notes the same
            p.vibDelay *= 0.35;
            p.vibBloom *= 0.4;
            p.vibGrowStart = 0.8;
            p.shiftBase *= 0.8;
            p.shiftPerSemi *= 0.8;
            p.slideProb = 0.35;
            p.slideTime = 0.9;
            p.scoop = 35.0;
            p.scoopProb = 0.2;
            p.scoopTime = 0.06;
            p.phraseArc *= 1.2;
            p.intonAmount = 0.7;
            break;
        case styleBaroque: // little vibrato (an ornament on long notes), messa di voce, light, articulated
            p.vibWidthLo *= 0.25;
            p.vibWidthHi *= 0.35;
            p.vibDelay = 0.45;
            p.vibBloom *= 1.5;
            p.vibGrowStart = 0.2;
            p.mdvDepth *= 2.5;
            p.mdvMin = 0.4;
            p.mdvFull = 1.0;
            p.posRange *= 0.75; // lighter strokes: lower in the Schelleng window
            p.biteFF *= 0.6;
            p.speedFF *= 0.85;
            p.strokeTaper = 0.3;
            p.taperDepth = 0.8; // more space between notes
            p.stopBelow = 0.8;
            p.phraseArc *= 0.8;
            p.stress *= 1.5;
            p.costHigh *= 1.5; // low positions, open strings welcome
            p.openPenalty = 0.3;
            p.costOpen *= 0.4;
            p.intonAmount = 1.0;
            break;
        case styleMaqam: // slides and inflections between degrees, moderate vibrato
            p.slideProb = 0.6;
            p.slideMin = 2.0;
            p.slideTime = 1.4;
            p.scoop = 45.0;
            p.scoopProb = 0.35;
            p.scoopTime = 0.11;
            p.vibWidthHi *= 0.8;
            p.vibRate += 0.3;
            p.vibDelay *= 1.4;
            p.mdvDepth *= 1.3;
            break;
        case styleFiddle: // light short bows, open strings, little vibrato, rhythmic bite, slides into notes
            p.posRange *= 0.9;
            p.bite *= 1.8;
            p.stress *= 2.5;
            p.restAccent *= 1.4;
            p.strokeTaper = 0.28;
            p.taperDepth = 0.75;
            p.speedFF *= 0.9;
            p.vibWidthLo *= 0.4;
            p.vibWidthHi *= 0.45;
            p.vibDelay = 0.3;
            p.openPenalty = 0.2; // takes the ringing open string far more often
            p.costOpen = 0.2;
            p.costHigh *= 2.0;
            p.scoop = 40.0;
            p.scoopProb = 0.25;
            p.scoopTime = 0.07;
            p.phraseArc *= 0.6;
            p.mdvDepth *= 0.5;
            p.intonAmount = 0.5;
            break;
        case styleStudent: // pitch error, slow bow changes, scratchy, irregular vibrato
            p.pitchError = 14.0;
            p.accel *= 0.5;
            p.accelFF *= 0.5;
            p.changeDip = 0.75;
            p.posRange *= 1.45; // pressing: high in the Schelleng window, scratchier
            p.earUp *= 0.4; // and slower to hear it
            p.earMax = 1.4;
            p.vibWander = 0.35;
            p.vibRate += 0.5;
            p.vibWidthHi *= 0.8;
            p.vibDelay *= 1.5;
            p.shiftBase *= 2.0;
            p.shiftLighten = 0.05;
            p.phraseArc *= 0.5;
            p.mdvDepth *= 0.4;
            break;
        default:
            break;
    }
}

// M7 intonation systems, in the order of the Intonation parameter (append only)
enum Intonation : int
{
    intonExpressive,
    intonEqual,
    intonJust,
    intonPythagorean,
    intonScala,
    intonMts,
    intonCount
};

// cents from 12-TET of each degree above the tonic: 5-limit just and Pythagorean (fifths -5..+6)
inline double justCents (int degree)
{
    static constexpr double c[12]
        = { 0.0, 11.73, 3.91, 15.64, -13.69, -1.96, -9.78, 1.96, 13.69, -15.64, -3.91, -11.73 };
    return c[((degree % 12) + 12) % 12];
}
inline double pythagoreanCents (int degree)
{
    static constexpr double c[12] = { 0.0, -9.78, 3.91, -5.87, 7.82, -1.96, 11.73, 1.96, -7.82, 5.87, -3.91, 9.78 };
    return c[((degree % 12) + 12) % 12];
}

struct Player
{
    static constexpr double openPitch[4] = { 55, 62, 69, 76 };
    static constexpr double stringLength = 0.325; // m
    PlayerParams pp;
    Violin* vn = nullptr;
    double fs = 48000.0; // control rate = output rate
    double t = 0.0;
    Rng rng;
    bool log = false;
    long capN = 0, capSlow = 0, capVerySlow = 0;
    double capSum = 0.0;

    struct Held
    {
        int pitch;
        double vel, on;
        int str;
    };
    Held held[16];
    int nHeld = 0;

    // bow
    double dir = 1.0; // +1 down, -1 up
    double v = 0.0, vTarget = 0.0; // signed m/s
    double V = 0.0; // stroke speed magnitude
    double hair = 0.0; // hair position from the frog, m
    double accel = 8.0;
    bool changing = false, releasing = true, stopping = false; // the bow starts off the string, at rest
    double stopT = 0.0;
    double strokeStart = -1.0, lastStop = -10.0;
    double nextDur = 0.0; // set by the host before noteOn: how long the coming note lasts (0 = unknown)
    double strokeCap = 0.0; // bow speed that makes the stroke fit the hair left (0 = none)
    double strokeEst = 0.0; // how long strokes last lately (Live bow distribution), s
    double lastBudget = -10.0; // when the hair last ran out
    double strokeLen = 0.0; // the stroke's expected length for the taper (0 = unknown)
    double d = 0.6; // dynamics of the current stroke
    double dTarget = 0.6, noteNow = 69.0;
    int slurNotes = 0;
    double lastOn = -10.0;
    int shortRun = 0; // consecutive note starts less than quickIOI apart
    bool shaped = false, quick = false;
    double expr = 1.0, exprTarget = 1.0, centsTrim = 0.0, pressTrim = 0.0, biteTrim = 0.0, vibScale = 1.0,
           vibEnd = -1.0;
    double bendCents = 0.0; // M6: channel pitch bend (the plugin's controller 128, in cents)
    // the stroke shape in use (set at each new stroke from the normal or the quick settings)
    double fSus = 1, fTau = 0.1, fHold = 0, sSus = 1, sTau = 0.1, stF = 1, stT = 0.04, stA = 25, stD = 0;
    double lastVel = 64.0;
    double contactMM = 22.0;

    // M4: the notes coming up, set by the host before noteOn (Studio look-ahead or a score):
    // start dt seconds from now, length (0 = unknown), velocity. aheadValid: the host can see
    // ahead, so "no note within the window" means a rest is coming.
    struct Ahead
    {
        double dt;
        int pitch;
        double dur, vel;
    };
    static constexpr int maxAhead = 24;
    Ahead ahead[maxAhead];
    int nAhead = 0;
    bool aheadValid = false;
    // phrasing state
    double phraseStart = -10.0, lastOffT = -10.0, ioiMean = 0.3, pMean = -1.0, velMean = -1.0, velDev = 0.0;
    double dEnv = 0.0; // the note's own swell (messa di voce), added to d
    double phraseOff = 0.0, noteWeight = 0.0, phraseBite = 0.0;
    int runIdx = 0;
    // planned shift: the next note, on the same string, slid to before it starts (Studio)
    double preT = -1.0, preDur = 0.0;
    int prePitch = -1, preString = -1, planNextString = -1;
    bool preDone = false;
    // stroke articulation in use
    int art = 0; // 0 normal, 3 staccato, 4 martele, 5 spiccato
    double cutAt = -1.0, relTime = 0.07368, strokeBite = 0.0;
    double pendingOff = -1.0; // Legato: the bow keeps going this long after the last note-off
    bool cutDone = false;
    double dEff() const { return std::clamp (d + dEnv, 0.0, 1.0); }
    // M6 Stroke shaping: a sustain level (1 = no release) moved away from 1 by pp.shapeAmount
    double shapedSus (double sus) const
    {
        return pp.shapeAmount == 1.0 ? sus : std::clamp (1.0 - (1.0 - sus) * pp.shapeAmount, 0.05, 1.0);
    }
    // drawn curves (CC lanes) that have taken over a dimension; -1 / false = the player's own
    bool manDyn = false;
    double ccDyn = 0.6, ccVib = -1.0, ccRate = -1.0, ccContact = -1.0;

    struct Str
    {
        bool bowed = false; // the bow is (or should be) on this string
        double force = 0.0, forceTarget = 0.0;
        double pitch = 0.0; // finger, semitones (MIDI)
        double target = 0.0;
        double slideFrom = 0.0, slideT0 = -1.0, slideDur = 0.0;
        double noteOn = -1.0;
        double vibPhase = 0.0, vibRate = 5.6, vibWidth = 0.0, vibWidthTarget = 0.0;
        double planEnd = -1.0; // when this note is due to end, if the host said (else -1)
        double vibSqueeze = 1.0; // vibrato timing scale for a known short note
        double wanderR = 0.0, wanderW = 0.0;
        double rateAdd = 0.0; // M4: faster vibrato up the string
        double lastBowed = -10.0;
        double setPitchAt = -1e9;
        bool lifted = true;
        double dampEnv = 0.0, muteEnv = 0.0;
        double fScale = 1.0;
        double landAt = -10.0; // when the bow last landed on this string while moving
        double ear = 1.0; // force correction from listening
        double spp = 1.0; // slips per period at the last listen (1 = clean Helmholtz motion)
        long slipMark = 0;
        bool captured = true;
        int earHigh = 0;
        double earT = 0.0;
        // M7: MPE per-note bend (cents), a landing error the ear corrects (cents), an inflection slide
        double mpeBend = 0.0, err = 0.0;
        bool scooping = false;
    } st[4];

    // hand position: semitones above the open string where the first finger sits. A position
    // reaches handPos - 1 (stretched back) to handPos + 5 (fourth finger) without shifting:
    // first position (handPos 2) plays semitones 1..7 on each string, E on the D string to A.
    double handPos = 2.0;
    bool inReach (double semis) const { return semis >= handPos - 1.0 && semis <= handPos + 5.0; }
    int lastString = 2;

    void init (Violin& v_, double fs_)
    {
        vn = &v_;
        fs = fs_;
        rng.s ^= 0x2545F4914F6CDD1Dull * (pp.seed + 1);
        for (int i = 0; i < 4; ++i)
        {
            st[i].pitch = st[i].target = openPitch[i];
            st[i].fScale = 1.0;
        }
        for (auto& p : mpePressFor) // M7: no MPE pressure yet
            p = -1.0;
    }

    double dynFromVel (double vel127) const
    {
        const double x = std::clamp ((vel127 - pp.velLo) / (pp.velHi - pp.velLo), 0.0, 1.0);
        return std::clamp (std::pow (x, pp.velCurve) + pp.dynBias, 0.0, 1.0);
    }

    // ---------------------------------------------------------------- string choice
    // M6 String preference (pp.stringPref) as weights on the costs: 1, 1, 0 at the default
    // (on a square-root scale: the greedy choice holds a position until a cost tips it over, so
    // the first half of the knob would do little on a linear one)
    double prefK() const { return pp.stringPref < 0.0 ? -std::sqrt (-pp.stringPref) : std::sqrt (pp.stringPref); }
    double prefSemi() const { return 1.0 - 0.9 * prefK(); }
    double prefHigh() const { return std::pow (6.0, -prefK()); }
    double prefString (int s) const { return 0.9 * prefK() * (double) s; }
    // bright still keeps long notes off open strings (they ring much louder than a stopped note)
    double prefOpen() const { return 1.0 + 1.5 * std::max (0.0, -prefK()); }
    int chooseString (int pitch, int avoid1 = -1, int avoid2 = -1) const
    {
        if (m5String >= 0) // M5: a harmonic's string, picked by m5Harmonic
            return m5String;
        int best = -1;
        double bestCost = 1e30;
        for (int s = 0; s < 4; ++s)
        {
            if (s == avoid1 || s == avoid2)
                continue;
            const double semis = pitch - openPitch[s];
            const double top = s == 3 ? 28 : 16;
            if (semis < 0 || semis > top)
                continue;
            double c = 0.12 * semis * prefSemi() + 0.6 * prefHigh() * std::max (0.0, semis - 7.0) + prefString (s);
            c += 0.9 * std::abs (s - lastString);
            if (semis > 0)
                c += inReach (semis)
                    ? 0.0
                    : 0.6 + 0.1 * std::min (std::abs (semis - handPos), std::abs (semis - handPos - 5.0));
            if (semis == 0 && pitch != 55)
                c += pp.openPenalty * prefOpen(); // open strings can't vibrate: a violinist mostly stops the note
            if (tuneOn)
                c += m7OpenCost (s, pitch); // M7: an open string out of tune with the system
            if (c < bestCost)
            {
                bestCost = c;
                best = s;
            }
        }
        if (best < 0) // out of range: nearest string
            best = pitch < 55 ? 0 : 3;
        return best;
    }

    // ---------------------------------------------------------------- M4: planned fingering
    // Hand position (first finger, semitones above the open string) after playing semis with the
    // hand at hp: unchanged when in reach, else the note under the third finger going up, under
    // the first going down (as fingerNote moves it). Open strings leave the hand where it is.
    static double handAfter (double hp, double semis)
    {
        if (semis <= 0.0 || (semis >= hp - 1.0 && semis <= hp + 5.0))
            return hp;
        return semis > hp ? std::max (2.0, semis - 3.0) : std::max (2.0, semis);
    }
    // what playing pitch on string s costs on its own: high positions, open strings on long notes
    double placeCost (int s, int pitch, double dur) const
    {
        const double semis = pitch - openPitch[s];
        double c = 0.12 * semis * prefSemi() + pp.costHigh * prefHigh() * std::max (0.0, semis - 7.0) + prefString (s);
        if (semis == 0 && pitch != 55) // an open string can't vibrate: avoided on expressive notes
            c += (dur <= 0.0 || dur > 0.25 ? pp.costOpen : 0.15 * pp.costOpen) * prefOpen();
        if (tuneOn)
            c += m7OpenCost (s, pitch);
        return c;
    }
    // moving from string s0 (hand at hp0) to pitch on s after gap seconds of rest
    double moveCost (int s0, double hp0, int s, int pitch, double gap, double ioi) const
    {
        const double semis = pitch - openPitch[s];
        const int x = std::abs (s - s0);
        // crossing two strings at once in quick notes is awkward
        double c = pp.costCross * (x <= 1 || ioi > 0.25 ? x : 1.5 * x);
        const double hp = handAfter (hp0, semis);
        if (hp != hp0)
            c += (pp.costShift + pp.costShiftSemi * std::abs (hp - hp0)) * (gap > 0.15 ? 0.4 : 1.0);
        return c;
    }
    static bool playable (int s, int pitch)
    {
        const double semis = pitch - openPitch[s];
        return semis >= 0 && semis <= (s == 3 ? 28 : 16);
    }

    // Viterbi over this note (pitch, dur, starting after gap s of rest) and the coming notes:
    // returns the string for this note and sets planNextString for the one after it.
    int planString (int pitch, double dur, double gap)
    {
        constexpr int N = maxAhead + 1;
        int P[N];
        double D[N], G[N], I[N];
        int n = 0;
        P[n] = pitch;
        D[n] = dur;
        G[n] = gap;
        I[n] = nAhead > 0 ? ahead[0].dt : 1.0;
        ++n;
        double lastT = 0.0, lastDur = dur;
        for (int i = 0; i < nAhead && n < N; ++i)
        {
            if (ahead[i].dt - lastT < pp.chordWindow) // a chord's other notes: planned with the top one
                continue;
            P[n] = ahead[i].pitch;
            D[n] = ahead[i].dur;
            G[n] = lastDur > 0.0 ? std::max (0.0, ahead[i].dt - lastT - lastDur) : 0.0;
            I[n] = ahead[i].dt - lastT;
            lastT = ahead[i].dt;
            lastDur = ahead[i].dur;
            ++n;
        }
        double cost[N][4], hp[N][4];
        int from[N][4];
        for (int s = 0; s < 4; ++s)
        {
            cost[0][s] = 1e30;
            if (! playable (s, P[0]))
                continue;
            cost[0][s] = placeCost (s, P[0], D[0]) + moveCost (lastString, handPos, s, P[0], G[0], I[0]);
            hp[0][s] = handAfter (handPos, P[0] - openPitch[s]);
            from[0][s] = -1;
        }
        for (int i = 1; i < n; ++i)
            for (int s = 0; s < 4; ++s)
            {
                cost[i][s] = 1e30;
                from[i][s] = -1;
                if (! playable (s, P[i]))
                    continue;
                for (int r = 0; r < 4; ++r)
                {
                    if (cost[i - 1][r] >= 1e29)
                        continue;
                    const double c = cost[i - 1][r] + moveCost (r, hp[i - 1][r], s, P[i], G[i], I[i]);
                    if (c < cost[i][s])
                    {
                        cost[i][s] = c;
                        from[i][s] = r;
                    }
                }
                if (from[i][s] < 0)
                    continue;
                cost[i][s] += placeCost (s, P[i], D[i]);
                hp[i][s] = handAfter (hp[i - 1][from[i][s]], P[i] - openPitch[s]);
            }
        int best = -1;
        for (int s = 0; s < 4; ++s)
            if (cost[n - 1][s] < 1e29 && (best < 0 || cost[n - 1][s] < cost[n - 1][best]))
                best = s;
        if (best < 0)
            return chooseString (pitch);
        int path[N];
        for (int i = n - 1; i >= 0; --i)
        {
            path[i] = best;
            best = i > 0 ? from[i][best] : best;
        }
        planNextString = n > 1 ? path[1] : -1;
        return path[0];
    }

    // ---------------------------------------------------------------- M4: phrasing
    // At a note start (not a chord's second note): the note's place in its phrase -> phraseOff
    // (added to its dynamics), noteWeight (0..1, stress for vibrato) and phraseBite.
    void phraseNote (int pitch, double vel127, bool anyHeld)
    {
        const double gap = anyHeld ? 0.0 : t - lastOffT;
        const double ioi = t - lastOn;
        const double prevLen = anyHeld ? ioi : lastOffT - lastOn;
        const double dur = nextDur;
        const bool first = lastOn < -5.0;
        const bool newPhrase = first || gap > pp.phraseGap || (prevLen > 0.8 && prevLen > pp.phraseLong * ioiMean);
        if (! first && ioi < 3.0)
        {
            const double r = std::abs (std::log (std::max (0.03, ioi) / ioiMean));
            runIdx = newPhrase || r > 0.4 ? 0 : runIdx + 1;
            ioiMean = std::clamp (ioiMean + 0.3 * (std::clamp (ioi, 0.05, 2.0) - ioiMean), 0.05, 2.0);
        }
        else
            runIdx = 0;
        if (newPhrase)
        {
            phraseStart = t;
            runIdx = 0;
        }
        pMean = pMean < 0.0 ? pitch : pMean + 0.15 * (pitch - pMean);
        if (velMean < 0.0)
            velMean = vel127;
        velDev += 0.2 * (std::abs (vel127 - velMean) - velDev);
        velMean += 0.2 * (vel127 - velMean);
        double amount = pp.phrase;
        if (pp.phraseVelSpread > 0.0)
            amount *= std::clamp (1.0 - velDev / pp.phraseVelSpread, 0.3, 1.0);

        // the phrase's end, if the look-ahead shows it: a rest or a long note
        double tEnd = -1.0;
        if (aheadValid && dur > 0.0)
        {
            double end = dur, curDur = dur;
            int i = 0;
            for (;; ++i)
            {
                const double next = i < nAhead ? ahead[i].dt : 1e9;
                if (curDur > 0.0 && (next - end > pp.phraseGap || (curDur > 0.8 && curDur > pp.phraseLong * ioiMean)))
                {
                    if (next < 1e8 || end < 1.5) // a rest beyond the window counts only when near
                        tEnd = end;
                    break;
                }
                if (i >= nAhead || ahead[i].dur <= 0.0)
                    break;
                curDur = ahead[i].dur;
                end = std::max (end, ahead[i].dt + curDur);
            }
        }
        const double age = t - phraseStart;
        double off = pp.phraseArc * ((1.0 - std::exp (-age / pp.phraseRise)) - 0.5);
        if (tEnd > 0.0)
        {
            const double k = std::clamp (1.0 - tEnd / pp.phraseFall, 0.0, 1.0);
            off -= pp.phraseArc * k * k * (3 - 2 * k);
        }
        off += pp.highLoud * std::clamp ((pitch - pMean) / 12.0, -1.0, 1.0);
        double w = 0.0;
        if (dur > 0.0)
        {
            const double a = std::clamp (std::log2 (dur / std::max (0.05, ioiMean)), -1.0, 1.0);
            off += pp.agogic * a;
            w = std::max (w, a);
        }
        const int group = ioiMean < 0.2 ? 4 : 2;
        const bool strong = runIdx % group == 0;
        off += pp.stress * ((strong ? 1.0 : 0.0) - 0.5);
        if (strong)
            w = std::max (w, 0.5);
        phraseOff = amount * off;
        noteWeight = std::clamp (w, 0.0, 1.0) * std::min (1.0, amount);
        phraseBite = newPhrase && ! first ? amount * pp.restAccent : (first ? amount * pp.restAccent : 0.0);
    }

    // the note's own swell, per sample (sounding string s)
    void updateEnvelope (int s)
    {
        double e = 0.0;
        if (! manDyn && pp.phrase > 0.0 && pp.mdvDepth > 0.0 && nHeld > 0 && ! releasing && s >= 0)
            e = swell (st[s]);
        dEnv += (e - dEnv) * std::min (1.0, 1.0 / (fs * 0.05));
    }
    double swell (const Str& S) const
    {
        double dEnv = 0.0;
        const double age = t - S.noteOn;
        const double len = S.planEnd > 0.0 ? S.planEnd - S.noteOn : 0.0;
        if (len >= pp.mdvMin)
        {
            const double depth = pp.mdvDepth * pp.phrase
                * std::clamp ((len - pp.mdvMin) / std::max (0.05, pp.mdvFull - pp.mdvMin), 0.0, 1.0);
            double u = std::clamp (age / len, 0.0, 1.0);
            if (pp.mdvPeak != 0.5) // M7: the swell peaks at mdvPeak of the note
                u = std::pow (u, std::log (0.5) / std::log (std::clamp (pp.mdvPeak, 0.1, 0.9)));
            dEnv = depth * (std::sin (pi * u) - 0.4);
        }
        else if (len <= 0.0 && age > 0.3)
        {
            const double k = std::clamp ((age - 0.3) / 1.0, 0.0, 1.0);
            dEnv = 0.5 * pp.mdvDepth * pp.phrase * k * k * (3 - 2 * k);
        }
        return dEnv;
    }

    // ---------------------------------------------------------------- bow targets from dynamics
    double betaFor (int s) const
    {
        if (ccContact > 0.0)
            return ccContact;
        const double L = stringLength * std::pow (2.0, -(st[s].pitch - openPitch[s]) / 12.0);
        if (const double mm = m7ContactMM(); mm > 0.0) // M7: sul ponticello, sul tasto
            return std::clamp (mm * 1e-3 / L, 0.02, 0.3);
        // on a shorter (stopped) string the player moves the bow towards the bridge too
        const double c = contactMM * (quick ? pp.quickContact : 1.0) * std::pow (L / stringLength, pp.contactFollow)
            * pp.contactScale * mpeContact;
        return std::clamp (c * 1e-3 / L, 0.02, 0.3);
    }

    double forceFor (int s, double speed) const
    {
        const double beta = betaFor (s);
        const double z = vn->s[s].d.Z / 0.303; // the measured window is for a D string
        const double fMax = pp.cUpper * speed / beta * z;
        const double fMin = pp.cLower * speed / (beta * beta) * z * z;
        const double p
            = std::clamp (pp.posLo + pp.posRange * dEff() + (quick ? pp.quickP : 0.0) + pressTrim + m7Press(),
                          0.02,
                          0.95);
        return std::min (pp.forceCap, std::exp ((1 - p) * std::log (fMin) + p * std::log (fMax)));
    }

    double regTrim (double pitch) const
    {
        const double r[8] = { pp.reg55, pp.reg61, pp.reg67, pp.reg73, pp.reg79, pp.reg85, pp.reg91, pp.reg97 };
        const double x = std::clamp ((pitch - 55.0) / 6.0, 0.0, 6.999);
        const int i = (int) x;
        return std::pow (10.0, (r[i] + (x - i) * (r[i + 1] - r[i])) / 20.0);
    }

    double speedFor (double dd) const
    {
        if (pp.speedMap > 0)
            return pp.speedPP * std::pow (pp.speedFF / pp.speedPP, dd);
        return pp.speedLo + pp.speedRange * std::pow (dd, pp.speedCurve);
    }

    // jump: a new stroke takes the dynamics at once; a slur glides there (dynGlide)
    void setStroke (double vel127, bool jump = true)
    {
        dTarget = manDyn ? ccDyn : std::clamp (dynFromVel (vel127) + phraseOff, 0.0, 1.0);
        if (mpeDyn >= 0.0 && ! manDyn) // M7: MPE pressure is the note's dynamics
            dTarget = mpeDyn;
        if (jump)
            d = dTarget;
        applyDyn();
        accel = (pp.accel + (pp.accelFF - pp.accel) * dTarget) * pp.accelScale;
    }
    void applyDyn()
    {
        V = speedFor (dEff()) * regTrim (noteNow);
        contactMM = pp.contactPP + (pp.contactFF - pp.contactPP) * dEff();
    }

    // ---------------------------------------------------------------- left hand
    void fingerNote (int s, int pitch, bool slurred)
    {
        Str& S = st[s];
        const double semis = pitch - openPitch[s];
        const double from = S.lifted ? openTuned (s) : S.pitch;
        const double tp = semis == 0 ? openTuned (s) : tunedPitch (pitch); // M7: the system's pitch
        const double jump = std::abs (tp - from);
        // a shift: same string, finger already down, the hand moves more than a tone
        // a shift: the note is out of the hand's reach (the finger slides on this string if one is down)
        const bool outOfReach = semis > 0 && ! inReach (semis);
        const bool pre = preDone && s == preString && pitch == prePitch; // slid there already (Studio)
        const bool shift = ! pre && ! S.lifted && outOfReach && jump > 1.0;
        S.target = tp;
        S.scooping = false;
        m7Note (S, pitch, semis);
        if (shift)
        {
            S.slideFrom = S.pitch;
            S.slideT0 = t;
            S.slideDur = std::max (0.004, (pp.shiftBase + pp.shiftPerSemi * jump) * pp.slideScale);
        }
        else if (pre) // a planned shift's slide carries on to the note; the ear starts afresh
            vn->s[s].fingerCents = 0.0;
        else if (pp.slideProb > 0.0 && m7Portamento (S, semis, jump))
            S.slideDur = (pp.shiftBase + pp.shiftPerSemi * jump) * pp.slideTime; // M7: slid though in reach
        else
        {
            S.slideT0 = -1.0;
            S.pitch = tp;
            if (pp.scoop > 0.0 && semis > 0 && 0.5 + 0.5 * rng.uni() < pp.scoopProb)
            {
                // M7: an inflection, the finger lands low and slides up into the note
                S.pitch = tp - pp.scoop / 100.0;
                S.slideFrom = S.pitch;
                S.slideT0 = t;
                S.slideDur = pp.scoopTime;
                S.scooping = true;
            }
            vn->s[s].setNote (S.pitch);
            S.setPitchAt = t;
        }
        if (outOfReach) // the hand moves: up, the note under the third finger; down, under the first
            handPos = semis > handPos ? std::max (2.0, semis - 3.0) : std::max (2.0, semis);
        S.lifted = semis == 0;
        S.noteOn = t;
        S.planEnd = nextDur > 0.0 ? t + nextDur : -1.0;
        S.vibSqueeze = nextDur > 0.0 && pp.vibShort > 0.0 ? std::min (1.0, nextDur / pp.vibShort) : 1.0;
        S.captured = false;
        // vibrato restarts on a new bow, continues (phase kept) over a slur in one position
        if (! slurred || shift || pre)
            S.vibWidth = 0.0;
        S.vibWidthTarget = semis > 0
            ? (pp.vibWidthLo + (pp.vibWidthHi - pp.vibWidthLo) * d) * std::pow (2.0, semis / 12.0 * pp.vibPosition)
            : 0.0;
        S.wanderR = pp.vibWander * rng.gauss() * 0.5;
        S.wanderW = pp.vibWander * rng.gauss() * 0.5;
        S.rateAdd = pp.vibRateHigh * std::clamp (semis / 12.0, 0.0, 1.5);
        S.vibWidthTarget *= 1.0 + pp.vibStress * noteWeight;
        S.vibRate = (pp.vibRate + pp.vibRateDyn * d + S.rateAdd + pp.vibRateAdd) * (1.0 + S.wanderR);
    }

    // ---------------------------------------------------------------- M4: note decisions
    // the string for a note: the planned shift's string, the Studio plan (Viterbi), or greedy
    int pickString (int pitch, double gap)
    {
        // (M5: a forced string from its hooks goes through chooseString and wins there)
        if (preDone && pitch == prePitch && preString >= 0)
            return preString;
        if (pp.fingerPlan > 0.0 && aheadValid)
            return planString (pitch, nextDur, gap);
        return chooseString (pitch);
    }

    // a planned shift to the next note when it is on this string and follows on (Studio)
    void planShift (int s, int pitch)
    {
        preDone = false;
        preT = -1.0;
        if (pp.anticipate <= 0.0 || ! aheadValid || nAhead == 0 || nextDur <= 0.0 || planNextString != s)
            return;
        const Ahead& a = ahead[0];
        const double semis = a.pitch - openPitch[s];
        if (a.dt - nextDur > 0.05 || a.dt < 0.08 || semis <= 0 || ! playable (s, a.pitch) || inReach (semis)
            || std::abs (a.pitch - pitch) <= 1)
            return;
        preT = t + a.dt;
        prePitch = a.pitch;
        preString = s;
        preDur = std::min (
            0.6 * a.dt,
            std::max (0.004, (pp.shiftBase + pp.shiftPerSemi * std::abs (a.pitch - pitch)) * pp.slideScale));
    }

    // the stroke's articulation: the Bow style override, or inferred from length and gap (Auto)
    void chooseArt (int style, double vel127)
    {
        art = style >= 3 ? style : 0;
        const double dur = nextDur;
        if (style == 0 && dur > 0.0 && aheadValid)
        {
            const double ioiN = nAhead > 0 ? ahead[0].dt : 1e9;
            const bool detached = ioiN - dur > 0.04;
            if (pp.autoSpiccato > 0.0 && ioiN < pp.autoSpiccato && detached && dur < 0.2)
                art = 5;
            else if (pp.autoMartele > 0.0 && vel127 >= pp.autoMartele && dur < 0.45 && detached)
                art = 4;
        }
        cutAt = -1.0;
        cutDone = false;
        relTime = pp.releaseTime;
        strokeBite = phraseBite;
        if (art == 3) // staccato: a short stopped stroke, half the written length
        {
            shaped = true;
            if (dur > 0.12)
                cutAt = t + std::max (0.06, 0.5 * dur);
        }
        else if (art == 4 || art == 5) // martele: bite, force released, stopped; spiccato: off the string
        {
            shaped = quick = true;
            fSus = pp.qForceSus;
            fTau = pp.qForceTau;
            fHold = pp.qForceHold;
            sSus = pp.qStrokeSus;
            sTau = pp.qStrokeTau;
            stF = pp.qStopForce;
            stT = pp.qStopTime;
            stA = pp.qStopAccel;
            stD = pp.qStopDamp;
            strokeBite += art == 4 ? 0.5 : 0.2;
            if (art == 5)
            {
                fSus = 0.15;
                fTau = 0.03;
                relTime = 0.015;
                if (dur > 0.0)
                    cutAt = t + std::min (0.6 * dur, 0.12);
            }
            else if (dur > 0.15)
                cutAt = t + std::max (0.08, 0.7 * dur);
        }
    }

    // ---------------------------------------------------------------- events
    void noteOn (int pitch, double vel127)
    {
        if (m5NoteOn (pitch, vel127)) // M5: keyswitches, plucked and harmonic notes
            return;
        m7NoteOn (pitch);
        m7ArticulationNoteOn(); // M7: tremolo, sautille, portato
        const bool anyHeld = nHeld > 0;
        const bool chord = anyHeld && (t - held[nHeld - 1].on) < pp.chordWindow;
        const int style = (int) pp.bowStyle;
        if (! chord)
        {
            phraseNote (pitch, vel127, anyHeld);
            shortRun = t - lastOn < pp.quickIOI ? shortRun + 1 : 0;
        }
        // Legato: a note that follows closely is slurred onto the same bow
        const bool join = ! anyHeld && pendingOff > 0.0;
        pendingOff = -1.0;
        int s;
        if (chord)
        {
            // double stop: another string, adjacent to the chord's
            int used = held[nHeld - 1].str;
            s = chooseString (pitch, used);
            for (int k = 0; k < 4; ++k) // prefer an adjacent string if it can play the note
                if (std::abs (k - used) == 1 && pitch >= openPitch[k] && pitch - openPitch[k] <= 16)
                {
                    if (std::abs (s - used) != 1)
                        s = k;
                    break;
                }
            fingerNote (s, pitch, true);
            st[s].bowed = true;
            st[s].fScale = 1.0;
            if (std::abs (v) > 0.01)
                st[s].landAt = t;
        }
        const bool rebow = anyHeld && ! chord
            && (style == 2 || style >= 3 || cutDone || m7Mode == artSautille
                || (pp.slurMaxNotes > 0 && slurNotes + 1 >= pp.slurMaxNotes)
                || (pp.slurMaxTime > 0 && t - strokeStart > pp.slurMaxTime)
                || (pp.slurAccent > 0 && vel127 - lastVel >= pp.slurAccent));
        if (rebow)
            nHeld = 0; // the held note ends with this bow
        if (chord)
            ;
        else if ((anyHeld && ! rebow) || join)
        {
            ++slurNotes;
            releasing = stopping = false;
            // legato: same bow. The new note replaces what was sounding.
            s = pickString (pitch, 0.0);
            for (int k = 0; k < 4; ++k)
                if (k != s)
                    st[k].bowed = false;
            const bool cross = ! st[s].bowed;
            fingerNote (s, pitch, true);
            st[s].bowed = true;
            if (cross)
                st[s].landAt = t;
            // a slur keeps the bow going; each note's velocity sets where the dynamics go
            noteNow = pitch;
            const double dNew = std::clamp (dynFromVel (vel127) + phraseOff, 0.0, 1.0);
            dTarget = manDyn ? ccDyn : (1.0 - pp.slurFollow) * d + pp.slurFollow * dNew;
            if (mpeDyn >= 0.0 && ! manDyn) // M7: MPE pressure is the note's dynamics
                dTarget = mpeDyn;
            nHeld = 0; // slurred-over notes no longer sound
            m7PulseAt = t; // M7: a portato note's pulse
        }
        else
        {
            // a new stroke
            slurNotes = 0;
            shaped = pp.shapeIOI > 0 && t - lastOn < pp.shapeIOI;
            quick = shaped && shortRun >= (int) pp.quickRun; // a run, not a lone grace note
            fSus = quick ? pp.qForceSus : pp.forceSus;
            fTau = quick ? pp.qForceTau : pp.forceTau;
            fHold = quick ? pp.qForceHold : pp.forceHold;
            sSus = quick ? pp.qStrokeSus : pp.strokeSus;
            sTau = quick ? pp.qStrokeTau : pp.strokeTau;
            stF = quick ? pp.qStopForce : pp.stopForce;
            stT = quick ? pp.qStopTime : pp.stopTime;
            stA = quick ? pp.qStopAccel : pp.stopAccel;
            stD = quick ? pp.qStopDamp : pp.stopDamp;
            s = pickString (pitch, anyHeld ? 0.0 : t - lastOffT);
            for (int k = 0; k < 4; ++k)
                st[k].bowed = false;
            noteNow = pitch;
            setStroke (vel127);
            chooseArt (style, vel127);
            m7Stroke(); // M7: where the stroke starts, the bounce or reversal timing
            const bool bowMoving = std::abs (v) > 0.01;
            if (bowMoving && strokeStart >= 0.0)
            {
                const double len = t - strokeStart;
                strokeEst = strokeEst <= 0.0 ? len : strokeEst + 0.5 * (len - strokeEst);
            }
            if (bowMoving && t - lastBudget < 0.15)
                ; // the bow has just changed at the end of the hair: this note takes that bow
            else if (bowMoving)
                dir = -dir; // bow change
            else if (t - lastStop > 0.8)
                dir = hair > 0.5 * pp.bowLength ? -1.0 : 1.0; // retake: start where the bow is
            else
                dir = -dir;
            // a stopped stroke can go either way: start it towards the longer part of the bow when
            // the turn would leave almost no hair
            const double room = dir > 0 ? pp.bowLength - hair : hair;
            if (! bowMoving && room < 0.12)
                dir = -dir;
            // a bow that has been off the string is set down where the stroke has room
            if (! bowMoving && t - lastStop > pp.retakeAfter && pp.retakeRoom > 0.0)
            {
                const double r = std::min (pp.retakeRoom, pp.bowLength - pp.retakeMin);
                hair = dir > 0 ? std::clamp (hair, pp.retakeMin, pp.bowLength - r)
                               : std::clamp (hair, r, pp.bowLength - pp.retakeMin);
            }
            // the stroke's length, when the host knows it (a score, Studio look-ahead): spread
            // the bow so the note does not run out of hair (no slower than half speed)
            strokeCap = 0.0;
            if (nextDur > 0.0)
                strokeCap = 0.9 * (dir > 0 ? pp.bowLength - hair : hair) / nextDur;
            else if (pp.liveDistribute > 0.0 && strokeEst > 0.0 && strokeEst < pp.taperLiveMax)
                strokeCap = 0.85 * (dir > 0 ? pp.bowLength - hair : hair)
                    / (pp.liveDistribute * std::clamp (strokeEst, 0.1, 3.0));
            strokeLen = nextDur > 0.0 ? nextDur : (strokeEst > 0.0 && strokeEst < pp.taperLiveMax ? strokeEst : 0.0);
            changing = bowMoving;
            releasing = false;
            stopping = false;
            strokeStart = t;
            fingerNote (s, pitch, false);
            st[s].bowed = true;
        }
        if (! chord)
            planShift (s, pitch);
        // leaving a ringing open string for another: a free finger or the hand mutes it
        if (! chord && lastString >= 0 && lastString != s && st[lastString].lifted && ! st[lastString].bowed)
            st[lastString].muteEnv = 1.0;
        lastString = s;
        lastVel = vel127;
        if (log)
            std::fprintf (stderr,
                          "on %.3f p%d s%d %s%s%s hair %.3f dir %+.0f phrase %+.3f art %d\n",
                          t,
                          pitch,
                          s,
                          strokeStart == t ? "stroke" : "slur",
                          shaped ? " shaped" : "",
                          quick ? " quick" : "",
                          hair,
                          dir,
                          phraseOff,
                          art);
        lastOn = t;
        if (nHeld < 16)
            held[nHeld++] = { pitch, vel127, t, s };
    }

    // CC11 expression: a level trim on top of the notes' dynamics, 100 = as played, 0.4 dB per step
    // (bow speed scales the string's amplitude, and force follows the speed). CC 21: tuning of the
    // sounding note in cents, 64 = none, 1 cent per step (per-note intonation from a score editor).
    // CC 22: bow pressure, 64 = as played, moves the force within the playable window by up to
    // +-0.3 of its width. CC 23: extra force at the stroke's start for a quicker catch, 64 = none,
    // 127 = double the force at the very start. CC 24: vibrato width, 64 = as played, 0 = none,
    // 127 = about 8x (cube law). CC 25: how long before the note's end the vibrato relaxes,
    // 10 ms per step (0 = it doesn't), replacing vibTaper.
    void controller (int cc, double v127)
    {
        // M6: pitch bend arrives as controller 128, its value in cents (the plugin scales it by
        // the Bend range parameter); CC121 below centres it again
        if (cc == 128)
        {
            bendCents = v127;
            return;
        }
        if (cc == 11)
            exprTarget = std::pow (10.0, (v127 - 100.0) * 0.4 / 20.0);
        else if (cc == 21)
            centsTrim = v127 - 64.0;
        else if (cc == 22)
            pressTrim = (v127 - 64.0) / 64.0 * 0.3;
        else if (cc == 23)
            biteTrim = std::max (0.0, v127 - 64.0) / 64.0;
        else if (cc == 24)
            vibScale = std::pow (v127 / 64.0, 3.0);
        else if (cc == 25)
            vibEnd = v127 / 100.0;
        // M4 drawn curves (the Curves tab exports the player's own curves on these): once a lane
        // sends, it takes over that dimension. CC1 dynamics (0..127 = d 0..1, replaces velocity
        // and phrasing), CC26 vibrato width (0.5 cents p-p per step), CC19 vibrato rate
        // (4 + 4 * v/127 Hz), CC74 contact point (127 = 2% of the string from the bridge,
        // 0 = 22%). CC121 (reset all controllers) gives every dimension back to the player.
        else if (pp.drawnCurves > 0.0 && cc == 1)
        {
            manDyn = true;
            ccDyn = v127 / 127.0;
            dTarget = ccDyn;
        }
        else if (pp.drawnCurves > 0.0 && cc == 26)
            ccVib = v127 * 0.5;
        else if (pp.drawnCurves > 0.0 && cc == 19)
            ccRate = 4.0 + 4.0 * v127 / 127.0;
        else if (pp.drawnCurves > 0.0 && cc == 74)
            ccContact = 0.02 + 0.2 * (1.0 - v127 / 127.0);
        else if (cc == 121)
        {
            manDyn = false;
            ccVib = ccRate = ccContact = -1.0;
            bendCents = 0.0; // M6
        }
    }

    // Studio look-ahead: a sounding note's end came into view (tEnd in the player's seconds), so
    // its vibrato can relax before the end as it does when the length is known at the start
    void notePlanEnd (int pitch, double tEnd)
    {
        for (int i = 0; i < nHeld; ++i)
            if (held[i].pitch == pitch && held[i].str >= 0 && st[held[i].str].planEnd < 0.0)
                st[held[i].str].planEnd = tEnd;
    }

    void noteOff (int pitch)
    {
        pitch = m5NoteOff (pitch); // M5: plucked notes end here, harmonics map to their finger
        if (pitch < 0)
            return;
        int k = 0;
        bool found = false;
        int str = -1;
        for (int i = 0; i < nHeld; ++i)
        {
            if (! found && held[i].pitch == pitch)
            {
                found = true;
                str = held[i].str;
                continue;
            }
            held[k++] = held[i];
        }
        nHeld = k;
        if (! found)
            return;
        if (nHeld == 0)
        {
            lastOffT = t;
            if (cutDone) // the stroke already ended (staccato, martele, spiccato)
                return;
            if (m7NoteOff()) // M7: portato waits in the bow for the next note, sautille lifts
                return;
            if ((int) pp.bowStyle == 1 && pp.legatoGap > 0.0)
            {
                pendingOff = t + pp.legatoGap; // a note coming within legatoGap is slurred on
                return;
            }
            if (log)
                std::fprintf (stderr,
                              "off %.3f p%d %s\n",
                              t,
                              pitch,
                              pp.stopBelow > 0 && t - strokeStart < pp.stopBelow ? "stop" : "release");
            if (art == 5)
                releasing = true;
            else if (art == 3 || art == 4 || (pp.stopBelow > 0 && t - strokeStart < pp.stopBelow))
            {
                stopping = true;
                stopT = t;
            }
            else
                releasing = true;
            lastStop = t;
        }
        else if (str >= 0)
            st[str].bowed = false; // one note of a double stop ends
    }

    // ---------------------------------------------------------------- per output sample
    // Writes the gestures for this sample: signed bow velocity and force per string.
    void tick (double* vBow, double* force)
    {
        const double dt = 1.0 / fs;
        // M4: a stroke cut short by its articulation; a planned shift leaving late in the old note
        if (cutAt > 0.0 && t >= cutAt)
        {
            cutAt = -1.0;
            if (nHeld > 0 && ! releasing && ! stopping)
            {
                if (art == 5)
                    releasing = true;
                else
                {
                    stopping = true;
                    stopT = t;
                }
                lastStop = t;
                cutDone = true;
            }
        }
        if (pendingOff > 0.0 && t >= pendingOff)
        {
            pendingOff = -1.0;
            releasing = true;
            lastStop = t;
        }
        if (preT > 0.0 && ! preDone && t >= preT - preDur)
        {
            Str& S = st[preString];
            if (nHeld > 0 && lastString == preString && S.bowed && ! releasing && ! stopping && t < preT + 0.02)
            {
                S.slideFrom = S.pitch;
                S.slideT0 = t;
                S.slideDur = preDur;
                S.target = tunedPitch (prePitch);
                preDone = true;
            }
            else
                preT = -1.0;
        }
        updateEnvelope (lastString);
        m7Tick(); // M7: tremolo reversals
        // bow budget: change bow before the hair runs out
        if (! releasing && nHeld > 0)
        {
            const double margin = std::abs (v) * 0.04 + 0.01;
            if ((dir > 0 && hair > pp.bowLength - margin) || (dir < 0 && hair < margin))
            {
                dir = -dir;
                changing = true;
                lastBudget = t;
                if (log)
                    std::fprintf (stderr, "budget change %.3f hair %.3f dir %+.0f v %+.3f\n", t, hair, -dir, v);
            }
        }
        // bow velocity: accelerate towards the target with limited acceleration
        if (std::abs (dTarget - d) > 1e-6 || std::abs (V - speedFor (dEff()) * regTrim (noteNow)) > 1e-9)
        {
            d += (dTarget - d) * std::min (1.0, dt / std::max (1e-4, pp.dynGlide));
            applyDyn();
        }
        const double balance[4] = { pp.speedG, pp.speedD, pp.speedA, pp.speedE };
        double shape = 1.0;
        if (shaped)
        {
            const double sus = shapedSus (sSus);
            shape = sus + (1.0 - sus) * std::exp (-(t - strokeStart) / sTau);
        }
        expr += (exprTarget - expr) * std::min (1.0, dt / 0.015);
        if (pp.strokeTaper > 0.0 && strokeLen > 0.0 && nHeld > 0)
        {
            const double age = t - strokeStart, taper = std::min (pp.strokeTaper, 0.4 * strokeLen);
            const double down = std::clamp ((age - (strokeLen - taper)) / taper, 0.0, 1.0);
            const double up = std::clamp ((age - strokeLen - 0.05) / 0.2, 0.0, 1.0); // the note goes on
            const double k = down * down * (3 - 2 * down) * (1.0 - up * up * (3 - 2 * up));
            shape *= 1.0 - std::min (0.95, pp.taperDepth * pp.shapeAmount) * k;
        }
        shape *= m7Shape(); // M7: portato pulses, the baroque bow's lift-off stroke (1 otherwise)
        vTarget = dir * V * shape * balance[lastString] * expr;
        if (strokeCap > 0.0 && std::abs (vTarget) > strokeCap)
            vTarget *= std::max (0.5, strokeCap / std::abs (vTarget));
        if (m7VCap > 0.0 && std::abs (vTarget) > m7VCap) // M7: sautille's short strokes
            vTarget = vTarget > 0 ? m7VCap : -m7VCap;
        if (pp.budgetSoft > 0.0 && ! releasing && nHeld > 0)
        {
            // slow down in time for the end of the hair
            const double room = std::max (0.0, (dir > 0 ? pp.bowLength - hair : hair) - 0.01);
            const double vMax = std::max (0.05, std::sqrt (2.0 * pp.budgetSoft * room));
            if (std::abs (vTarget) > vMax)
                vTarget = vTarget > 0 ? vMax : -vMax;
        }
        if (releasing)
        {
            // keep moving while the hair leaves the string, then slow down
            bool off = true;
            for (auto& S : st)
                off = off && S.force < 0.02 * S.forceTarget + 1e-4;
            vTarget = off ? 0.0 : v;
        }
        if (stopping)
        {
            vTarget = 0.0;
            if (t - stopT > stT)
            {
                stopping = false;
                releasing = true;
            }
        }
        const double a = (stopping ? stA : std::max (accel, m7AccelMin)) * dt;
        v += std::clamp (vTarget - v, -a, a);
        if (changing && std::abs (v - vTarget) < 1e-4)
            changing = false;
        hair = std::clamp (hair + v * dt, 0.0, pp.bowLength);

        for (int s = 0; s < 4; ++s)
        {
            Str& S = st[s];
            // left hand: slide, vibrato
            double pitch = S.target + (S.bowed ? centsTrim / 100.0 : 0.0);
            if (S.slideT0 >= 0.0)
            {
                const double u = (t - S.slideT0) / S.slideDur;
                if (u >= 1.0)
                {
                    S.slideT0 = -1.0;
                    S.scooping = false;
                }
                else
                    pitch = S.slideFrom + (S.target - S.slideFrom) * 0.5 * (1.0 - std::cos (pi * u));
            }
            if (S.bowed)
                pitch += bendCents / 100.0; // M6: pitch bend moves the bowed notes
            if (S.mpeBend != 0.0 || S.err != 0.0) // M7: MPE bend, landing error the ear is correcting
                pitch += m7PitchAdd (S);
            const bool sliding = S.slideT0 >= 0.0;
            if (S.vibWidthTarget > 0.0 && S.bowed)
            {
                const double age = t - S.noteOn;
                const double q = S.vibSqueeze, delay = pp.vibDelay * pp.vibDelayScale * q;
                const double env = std::clamp ((age - delay) / (pp.vibBloom * pp.vibDelayScale * q), 0.0, 1.0);
                double w = S.vibWidthTarget * vibScale * pp.vibAmount * (1.0 + S.wanderW) * env * env * (3 - 2 * env);
                w *= std::min (pp.vibGrowMax, pp.vibGrowStart + pp.vibGrow * std::max (0.0, age - delay));
                const double taper = (vibEnd >= 0.0 ? vibEnd : pp.vibTaper) * q;
                if (S.planEnd > 0.0 && taper > 0.0)
                {
                    const double k = std::clamp ((S.planEnd - t) / taper * 1.5 - 0.5, 0.0, 1.0);
                    w *= k * k * (3 - 2 * k);
                }
                if (ccVib >= 0.0)
                    w = ccVib;
                S.vibWidth += (w - S.vibWidth) * std::min (1.0, dt / 0.05);
                S.vibPhase += 2 * pi * S.vibRate * dt;
                if (S.vibPhase > 2 * pi)
                {
                    S.vibPhase -= 2 * pi;
                    // per-cycle wander: no two cycles alike
                    S.vibRate = (pp.vibRate + pp.vibRateDyn * d + S.rateAdd + pp.vibRateAdd)
                        * (1.0 + S.wanderR + 0.04 * rng.gauss());
                    if (ccRate > 0.0)
                        S.vibRate = ccRate;
                }
            }
            else
                S.vibWidth *= 1.0 - std::min (1.0, dt / 0.08);
            if (! sliding && S.vibWidth > 0.0)
                pitch += 0.5 * S.vibWidth / 100.0 * std::sin (S.vibPhase);
            if (std::abs (pitch - S.pitch) > 0.001 || (sliding && std::abs (pitch - S.pitch) > 1e-5))
            {
                S.pitch = pitch;
                vn->s[s].setPitch (pitch);
            }
            // idle fingers lift (damped touch), the string then rings open (sympathetic)
            if (S.bowed)
                S.lastBowed = t;
            else if (! S.lifted && t - S.lastBowed > pp.liftAfter && nHeld > 0)
            {
                S.lifted = true;
                S.target = S.pitch = openTuned (s);
                vn->s[s].setNote (S.pitch);
                S.dampEnv = 1.0;
            }
            S.dampEnv *= std::exp (-dt / pp.liftDampTime);
            S.muteEnv *= std::exp (-dt / std::max (1e-4, pp.openMuteTime));
            double dmp = std::max (pp.liftDamp * S.dampEnv, pp.openMute * S.muteEnv);
            if (stopping && S.bowed && std::abs (v) < 0.02)
                dmp = std::max (dmp, stD);
            dmp = std::max (dmp, m5Damp (s)); // M5: plucking finger, pizz note-off
            vn->s[s].damp = dmp;
            {
                const double age = t - std::max (S.noteOn, S.landAt);
                vn->s[s].noiseGain = pp.noiseStart + (1.0 - pp.noiseStart) * std::clamp (age / pp.noiseRise, 0.0, 1.0);
            }

            // bow force on this string
            double ft = 0.0;
            if (S.bowed && ! releasing)
            {
                ft = forceFor (s, std::max (std::abs (v), 0.3 * V * balance[s]));
                ft *= 1.0 + pp.crossBite * std::exp (-(t - S.landAt) / pp.crossBiteTime);
                const double age = t - strokeStart;
                ft *= 1.0
                    + ((pp.bite + pp.biteFF * d * d) * pp.biteScale + biteTrim + strokeBite * pp.biteScale)
                        * std::exp (-age / pp.biteTime);
                if (changing)
                    ft *= 1.0 - pp.changeDip * (1.0 - std::min (1.0, std::abs (v) / std::max (1e-3, V)));
                if (sliding && ! S.scooping)
                    ft *= 1.0 - pp.shiftLighten;
                if (stopping)
                    ft *= stF;
                if (shaped && age > fHold)
                {
                    const double sus = shapedSus (fSus);
                    ft *= sus + (1.0 - sus) * std::exp (-(age - fHold) / fTau);
                }
            }
            // listening: every few ms compare the slip rate with the note's frequency
            if (S.bowed && ! releasing && S.force > 0.0)
            {
                S.earT += dt;
                if (S.earT >= std::max (pp.earWindow, pp.earPeriods / vn->s[s].f1))
                {
                    const long n = vn->s[s].slipTotal - S.slipMark;
                    const double spp = n / (S.earT * vn->s[s].f1);
                    if (! S.captured && spp > 0.8 && spp < 1.25)
                    {
                        S.captured = true;
                        const double c = t - std::max (S.noteOn, S.landAt);
                        ++capN;
                        capSum += c;
                        capSlow += c > 0.05;
                        capVerySlow += c > 0.1;
                        if (log)
                            std::fprintf (stderr, "capture %.3f s at %.3f string %d pitch %.1f\n", c, t, s, S.target);
                    }
                    S.spp = spp;
                    S.earHigh = spp > 1.4 ? S.earHigh + 1 : 0;
                    if (S.earHigh >= 2)
                        S.ear = std::min (pp.earMax, S.ear * (1.0 + pp.earUp));
                    else if (spp < 0.4 && t - std::max (strokeStart, std::max (S.landAt, S.noteOn)) > pp.earWait)
                        S.ear = std::max (pp.earMin, S.ear * (1.0 - pp.earDown));
                    else
                        S.ear += (1.0 - S.ear) * std::min (1.0, S.earT / pp.earRelax);
                    S.slipMark = vn->s[s].slipTotal;
                    S.earT = 0.0;
                }
                ft *= S.ear;
            }
            else
            {
                S.slipMark = vn->s[s].slipTotal;
                S.earT = 0.0;
                S.ear += (1.0 - S.ear) * std::min (1.0, dt / pp.earRelax);
            }
            ft *= m5Force (s); // M5: light bow on a harmonic
            if (S.bowed && ! releasing)
                ft *= m7Force(); // M7: sautille's bounce, tremolo's catches, the baroque bow's light tip
            S.forceTarget = S.bowed ? std::max (ft, S.forceTarget * 0.0) : 0.0;
            const double tau = releasing ? relTime : (S.bowed ? (S.force < 1e-4 ? pp.landTime : 0.01) : pp.crossTime);
            S.force += (ft - S.force) * std::min (1.0, dt / tau);
            if (S.force < 1e-5 && ft == 0.0)
                S.force = 0.0;
            force[s] = S.force;
            vBow[s] = S.force > 0.0 ? v : 0.0;
            vn->s[s].setBeta (m5Beta (s) > 0.0 ? m5Beta (s) : betaFor (s)); // M5: a plucked string keeps its point
            vn->s[s].hairFrac = hair / pp.bowLength;
            vn->s[s].widthScale = pp.tiltPP + (1.0 - pp.tiltPP) * d;
        }
        t += dt;
    }

    // ================================================================ M7 intonation, styles, MPE
    // Intonation: cents on top of 12-TET at A440 for each MIDI note (tuneTable) and each open
    // string (openCents), set by setTuning. tuneOn false (Expressive with no leaning at A440,
    // the default) leaves every pitch exactly as 2.1 played it.
    bool tuneOn = false;
    double tuneTable[128] = {};
    double openCents[4] = {};
    // MPE: per pitch, the bend (cents) and pressure (dynamics 0..1, -1 none) the note's channel
    // carries, and the bow's contact scale from the latest note's timbre (CC74)
    double mpeBendFor[128] = {};
    double mpePressFor[128] = {}; // -1 from init()
    double mpeTimbreFor[128] = {};
    double mpeDyn = -1.0, mpeContact = 1.0;

    double tunedPitch (int pitch) const { return tuneOn ? pitch + tuneTable[pitch & 127] / 100.0 : (double) pitch; }
    double openTuned (int s) const { return tuneOn ? openPitch[s] + openCents[s] / 100.0 : openPitch[s]; }
    // an open string more than 4 cents from the system's pitch for its note is stopped instead
    double m7OpenCost (int s, int pitch) const
    {
        return pitch == (int) openPitch[s] && pitch != 55 && std::abs (tuneTable[pitch & 127] - openCents[s]) > 4.0
            ? 2.0
            : 0.0;
    }

    // system: Intonation; key: tonic pitch class (0 = C); a4: Hz; amount: Expressive leaning
    // (PlayerParams::intonAmount); table: cents per MIDI note for Scala / MTS-ESP (else unused);
    // tableTransposes: the A4 setting moves the table too (a Scala file without a keyboard map).
    void setTuning (int system, int key, double a4, double amount, const double* table, bool tableTransposes)
    {
        double a4c = 1200.0 * std::log2 (std::clamp (a4, 300.0, 600.0) / 440.0);
        if (std::abs (a4c) < 0.01)
            a4c = 0.0;
        const bool ext = (system == intonScala || system == intonMts) && table != nullptr;
        tuneOn = a4c != 0.0 || system == intonJust || system == intonPythagorean || ext
            || (system == intonExpressive && amount > 0.0);
        if (! tuneOn)
            return;
        // A stays at the A4 setting: the tonic moves instead (as a violinist tunes to the A)
        const int aDeg = 9 - key;
        for (int n = 0; n < 128; ++n)
        {
            const int deg = n - key;
            double c = a4c;
            if (ext)
                c = table[n] + (tableTransposes ? a4c : 0.0);
            else if (system == intonJust)
                c += justCents (deg) - justCents (aDeg);
            else if (system == intonPythagorean)
                c += pythagoreanCents (deg) - pythagoreanCents (aDeg);
            else if (system == intonExpressive)
                c += amount * (pythagoreanCents (deg) - pythagoreanCents (aDeg));
            tuneTable[n] = c;
        }
        // open strings in pure fifths from the A (Scala / MTS-ESP: as the scale says)
        static constexpr double fifths[4] = { -3.91, -1.96, 0.0, 1.96 };
        for (int s = 0; s < 4; ++s)
            openCents[s] = ext         ? tuneTable[(int) openPitch[s]]
                : system == intonEqual ? a4c
                                       : a4c + (system == intonExpressive ? amount : 1.0) * fifths[s];
    }
    // after the open strings were retuned: a string ringing open takes its new pitch
    void retuneOpen (int s)
    {
        Str& S = st[s];
        if (S.lifted && S.slideT0 < 0.0)
        {
            S.target = S.pitch = openTuned (s);
            vn->s[s].setNote (S.pitch);
        }
    }

    // a new note's MPE expression (set by the host before its note-on) and the landing error
    void m7NoteOn (int pitch)
    {
        mpeDyn = mpePressFor[pitch & 127];
        if (mpeTimbreFor[pitch & 127] > 0.0)
            mpeContact = mpeTimbreFor[pitch & 127];
    }
    void m7Note (Str& S, int pitch, double semis)
    {
        S.mpeBend = mpeBendFor[pitch & 127];
        S.err = pp.pitchError > 0.0 && semis > 0 ? pp.pitchError * rng.gauss() : 0.0;
    }
    double m7PitchAdd (const Str& S) const
    {
        double c = S.mpeBend;
        if (S.err != 0.0)
            c += S.err * (0.4 + 0.6 * std::exp (-(t - S.noteOn) / std::max (0.01, pp.earFix)));
        return c / 100.0;
    }
    // portamento into a note the hand can reach, on the string the finger is already on
    bool m7Portamento (Str& S, double semis, double jump)
    {
        if (S.lifted || semis <= 0 || jump < pp.slideMin - 0.3 || t - S.lastBowed > 0.15 || S.noteOn > t - 0.05)
            return false;
        if (0.5 + 0.5 * rng.uni() >= pp.slideProb)
            return false;
        S.slideFrom = S.pitch;
        S.slideT0 = t;
        return true;
    }

    // MPE (the plugin's MPE mode): per note, by its pitch. cents: the note's bend; pressure 0..1
    // (-1: none yet) sets the dynamics while that note is the newest; timbre 0..127 (CC74, 64 =
    // the player's own contact point) moves the bow towards the bridge (up) or the fingerboard
    void mpeBend (int pitch, double cents)
    {
        mpeBendFor[pitch & 127] = cents;
        for (int i = 0; i < nHeld; ++i)
            if (held[i].pitch == pitch && held[i].str >= 0)
                st[held[i].str].mpeBend = cents;
    }
    void mpePressure (int pitch, double v)
    {
        mpePressFor[pitch & 127] = v;
        if (nHeld > 0 && held[nHeld - 1].pitch == pitch)
        {
            mpeDyn = v;
            if (v >= 0.0 && ! manDyn)
                dTarget = std::clamp (v, 0.0, 1.0);
        }
    }
    void mpeTimbre (int pitch, double v127)
    {
        const double k = std::pow (2.0, (64.0 - std::clamp (v127, 0.0, 127.0)) / 64.0);
        mpeTimbreFor[pitch & 127] = k;
        if (nHeld > 0 && held[nHeld - 1].pitch == pitch)
            mpeContact = k;
    }

    // ================================================================ M5 articulations
    // Pizzicato (right hand, Bartok snap, left hand) and harmonics (natural, artificial). Plucked
    // notes bypass the bow: the left hand stops the note, the plucking finger touches the string
    // (damping what rings), pulls and lets go (String::pluck). Harmonic notes are bowed: the player
    // picks a string and node that sound the pitch (natural: 1/2, 1/3, 1/4 of the string from the
    // nut), else an artificial harmonic (the note two octaves down stopped, a light finger a fourth
    // above it), and puts the light finger on (String::setTouch).
    enum M5Art : int
    {
        artArco,
        artPizz,
        artBartok,
        artLeftPizz,
        artHarmonic
    };
    int m5Key = -1; // articulation picked by keyswitch (-1: none yet)
    double m5Param = 0.0; // the last PlayerParams::articulation seen (a change wins over the keyswitch)
    int m5String = -1; // forces chooseString while a harmonic is fingered
    bool m5Busy = false;
    int m5Pluck[128] = {}; // pitch -> string + 1 of a plucked note still held
    int m5Harm[128] = {}; // pitch -> finger pitch + 1 of a held harmonic
    struct M5Str
    {
        double pluckAt = -1.0, h = 0.0, tau = 0.0, clickAt = 0.0, clickAmp = 0.0, impulse = 0.0;
        double beta = 0.0; // the plucking point, held while the string rings unbowed
        double offAt = -1.0;
    } m5[4];

    int m5Articulation()
    {
        if (pp.articulation != m5Param)
        {
            m5Param = pp.articulation;
            m5Key = -1;
        }
        return std::clamp (m5Key >= 0 ? m5Key : (int) std::lround (pp.articulation), 0, m7ArtLast);
    }

    bool m5NoteOn (int pitch, double vel127)
    {
        if (m5Busy)
            return false;
        const int ks = (int) pp.keyswitchBase;
        if (ks > 0 && pitch >= ks && pitch <= ks + m7KeyLast)
        {
            if (pitch - ks >= m7ContactKey0) // M7: the contact keyswitches latch on their own
            {
                m7Contact();
                m7ContactKey = pitch - ks - m7ContactKey0;
                return true;
            }
            m5Articulation();
            m5Key = pitch - ks;
            return true;
        }
        const int art = m5Articulation();
        if (art == artPizz || art == artBartok || art == artLeftPizz)
        {
            m5PluckNote (pitch, vel127, art);
            return true;
        }
        if (art == artHarmonic && m5Harmonic (pitch, vel127))
            return true;
        if (art == artColLegno)
        {
            m7Strike (pitch, vel127);
            return true;
        }
        for (int k = 0; k < 4; ++k) // an ordinary note: the light finger is lifted
            if (vn->s[k].touchOn)
                vn->s[k].setTouch (0.0, 0.0);
        return false;
    }

    int m5NoteOff (int pitch)
    {
        if (pitch < 0 || pitch > 127)
            return pitch;
        if (m5Pluck[pitch] > 0)
        {
            m5[m5Pluck[pitch] - 1].offAt = t;
            m5Pluck[pitch] = 0;
            return -1;
        }
        if (m5Harm[pitch] > 0)
        {
            const int fp = m5Harm[pitch] - 1;
            m5Harm[pitch] = 0;
            return fp;
        }
        return pitch;
    }

    void m5PluckNote (int pitch, double vel127, int art)
    {
        int s = -1;
        if (art == artLeftPizz) // the left hand plucks open strings
            for (int k = 0; k < 4; ++k)
                if (pitch == (int) openPitch[k])
                    s = k;
        if (s < 0)
            s = chooseString (pitch);
        Str& S = st[s];
        const double semis = std::max (0.0, pitch - openPitch[s]);
        if (semis > 0 && ! inReach (semis))
            handPos = semis > handPos ? std::max (2.0, semis - 3.0) : std::max (2.0, semis);
        S.slideT0 = -1.0;
        S.target = S.pitch = semis == 0 ? openTuned (s) : tunedPitch (pitch);
        S.vibWidthTarget = 0.0;
        S.lifted = semis == 0;
        S.noteOn = t;
        S.bowed = false;
        S.mpeBend = mpeBendFor[pitch & 127];
        S.err = 0.0;
        vn->s[s].setNote (S.pitch);
        if (vn->s[s].touchOn)
            vn->s[s].setTouch (0.0, 0.0);
        const double dd = dynFromVel (vel127);
        const double L = stringLength * std::pow (2.0, -semis / 12.0);
        M5Str& M = m5[s];
        M.h = pp.pizzAmpPP * std::pow (pp.pizzAmpFF / pp.pizzAmpPP, dd);
        M.tau = pp.pizzTau * (1.0 - pp.pizzBright * dd);
        M.clickAt = M.clickAmp = 0.0;
        double pointM = pp.pizzPointMM * 1e-3;
        if (art == artBartok)
        {
            M.h *= pp.bartokAmp;
            M.tau = pp.bartokTau;
            pointM = pp.bartokPointMM * 1e-3;
            M.clickAt = 0.3 / vn->s[s].f1; // it swings past the middle and hits the board
            M.clickAmp = pp.bartokClick * (0.3 + 0.7 * dd);
        }
        M.beta = std::clamp (pointM / L, 0.04, 0.5);
        if (art == artLeftPizz)
        {
            M.h *= pp.lhAmp;
            M.tau = pp.lhTau;
            M.beta = std::clamp (pp.lhFromNut, 0.04, 0.5); // near the nut: the same comb as near the bridge
        }
        M.impulse = pp.pizzImpulse * vel127 / 127.0;
        M.pluckAt = t + pp.pizzTouch;
        M.offAt = -1.0;
        lastString = s;
        lastOn = t;
        m5Pluck[pitch & 127] = s + 1;
    }

    // per string per sample: runs a due pluck and returns the plucking/lifting finger's loss
    double m5Damp (int s)
    {
        M5Str& M = m5[s];
        double dmp = 0.0;
        if (M.pluckAt >= 0.0)
        {
            if (t < M.pluckAt)
                dmp = pp.pizzTouchDamp;
            else
            {
                M.pluckAt = -1.0;
                vn->s[s].setBeta (M.beta);
                if (pp.pizzModel < 0.5)
                    vn->s[s].pluckImpulse (M.impulse);
                else
                    vn->s[s].pluck (M.h, M.tau, M.clickAt, M.clickAmp, pp.pizzPull, pp.bartokClickTime, pp.bartokKick);
            }
        }
        if (M.beta > 0.0 && ! st[s].lifted) // a stopped plucked note: the fingertip is a lossy stop
            dmp = std::max (dmp, pp.pizzStopLoss);
        if (M.offAt >= 0.0 && ! st[s].lifted)
            dmp = std::max (dmp, pp.pizzOffDamp);
        return dmp;
    }

    // a bowed harmonic takes a light bow: the bow sees only the segment up to the light finger, so the
    // playable force window is lower (Fmax ~ 1/beta). The ear listens for the harmonic, not the
    // string's fundamental, so its multiple-slip correction is held off on a touched string.
    double m5Force (int s)
    {
        if (! vn->s[s].touchOn)
            return 1.0;
        st[s].ear = 1.0;
        st[s].earHigh = 0;
        return pp.harmForce;
    }

    double m5Beta (int s)
    {
        if (st[s].bowed)
            m5[s].beta = 0.0;
        return m5[s].beta;
    }

    // Harmonic: the string and node, then the ordinary bowed note on the finger pitch
    bool m5Harmonic (int pitch, double vel127)
    {
        int s = -1, n = 0;
        for (int k : { 2, 3, 4 })
        {
            for (int q = 0; q < 4 && s < 0; ++q)
            {
                const int c = (q + lastString) % 4; // from the string in use
                if (std::abs (pitch - (openPitch[c] + 12.0 * std::log2 ((double) k))) < 0.5)
                    s = c;
            }
            if (s >= 0)
            {
                n = k;
                break;
            }
        }
        double finger, x;
        if (s >= 0)
        {
            finger = openPitch[s];
            x = 1.0 - 1.0 / n;
        }
        else
        {
            finger = pitch - 24; // artificial: the stopped note, the light finger a fourth above
            if (finger < openPitch[0] + 1)
                return false;
            s = chooseString ((int) finger);
            if (finger - openPitch[s] < 1 || finger - openPitch[s] > 12)
                return false;
            x = 0.75;
        }
        for (int k = 0; k < 4; ++k)
            if (k != s && vn->s[k].touchOn)
                vn->s[k].setTouch (0.0, 0.0);
        m5Busy = true;
        m5String = s;
        noteOn ((int) finger, vel127);
        m5String = -1;
        m5Busy = false;
        vn->s[s].setTouch (x, pp.harmTouch * vn->s[s].d.Z);
        m5Harm[pitch & 127] = (int) finger + 1;
        return true;
    }
    // ================================================================ M7 articulations
    // Tremolo, sautille and portato are bowed through the same physics: the player only changes
    // how the bow moves (reversals, bounce, pulses). Col legno battuto strikes the string with the
    // stick (String::strike). Sul ponticello and sul tasto move the contact point (latched on
    // their own, so they combine with any bowed articulation).
    enum M7Art : int
    {
        artTremolo = 5,
        artSautille,
        artPortato,
        artColLegno
    };
    static constexpr int m7ArtLast = artColLegno;
    static constexpr int m7ContactKey0 = 9; // keyswitchBase + 9..11: ordinario, ponticello, tasto
    static constexpr int m7KeyLast = 11;
    int m7Mode = 0; // the bowed M7 articulation of the current note (0: none)
    mutable int m7ContactKey = -1; // (mutable: a parameter change, seen from the const bow targets, unlatches the key)
    mutable double m7ContactParam = 0.0;
    double m7PulseAt = -10.0, m7NextFlip = -1.0, m7FlipAt = -10.0, m7Bounce = 0.1, m7VCap = 0.0, m7AccelMin = 0.0;
    bool m7Rest = false;

    int m7Contact() const
    {
        if (pp.contact != m7ContactParam)
        {
            m7ContactParam = pp.contact;
            m7ContactKey = -1;
        }
        return std::clamp (m7ContactKey >= 0 ? m7ContactKey : (int) std::lround (pp.contact), 0, 2);
    }
    double m7ContactMM() const
    {
        const int c = m7Contact();
        return c == 1 ? pp.pontMM : c == 2 ? pp.tastoMM : 0.0;
    }
    double m7Press() const
    {
        const int c = m7Contact();
        return c == 1 ? pp.pontPress : c == 2 ? pp.tastoPress : 0.0;
    }

    double m7TremoloRate() const
    {
        static constexpr double perBeat[4] = { 0.0, 4.0, 6.0, 8.0 };
        const int sync = std::clamp ((int) std::lround (pp.tremoloSync), 0, 3);
        const double r = sync > 0 && pp.tempo > 0.0 ? pp.tempo / 60.0 * perBeat[sync] : pp.tremoloRate;
        return std::clamp (r, 2.0, 32.0);
    }

    void m7ArticulationNoteOn()
    {
        const int a = m5Articulation();
        m7Mode = a >= artTremolo && a <= artPortato ? a : 0;
        m7Rest = false;
        m7VCap = m7AccelMin = 0.0;
    }

    // at a new stroke (after chooseArt, before the direction is decided)
    void m7Stroke()
    {
        m7PulseAt = t;
        const bool moving = std::abs (v) > 0.01;
        if (m7Mode == artTremolo)
        {
            shaped = quick = false;
            m7NextFlip = t + 1.0 / m7TremoloRate();
            if (! moving)
                hair = pp.tremoloHair * pp.bowLength;
        }
        else if (m7Mode == artSautille)
        {
            shaped = quick = false;
            const double ioi = t - lastOn;
            const double len = nextDur > 0.0 ? nextDur : (ioi < 0.3 ? ioi : pp.sautMax);
            m7Bounce = std::clamp (len, pp.sautMin, pp.sautMax);
            if (! moving)
                hair = pp.sautHair * pp.bowLength;
        }
    }

    // per sample, before the bow moves
    void m7Tick()
    {
        m7VCap = m7AccelMin = 0.0;
        if (m7Mode == artTremolo && nHeld > 0 && ! releasing && ! stopping)
        {
            const double rate = m7TremoloRate();
            // a reversal takes at most half a stroke
            m7AccelMin = 4.0 * V * rate;
            if (t >= m7NextFlip)
            {
                dir = -dir;
                changing = true;
                m7FlipAt = t;
                m7NextFlip = std::max (m7NextFlip + 1.0 / rate, t + 0.5 / rate);
            }
        }
        else if (m7Mode == artSautille && nHeld > 0)
        {
            m7VCap = 2.0 * pp.sautLen / m7Bounce;
            m7AccelMin = 4.0 * std::min (V, m7VCap) / m7Bounce;
        }
    }

    // the bow speed's shape (x)
    double m7Shape() const
    {
        const int c = m7Contact();
        double k = c == 1 ? pp.pontSpeed : c == 2 ? pp.tastoSpeed : 1.0;
        if (m7Mode == artPortato)
        {
            if (m7Rest)
                return k * pp.portRest;
            const double age = t - m7PulseAt;
            const double r = std::clamp (age / pp.portRise, 0.0, 1.0), sm = r * r * (3 - 2 * r);
            k *= pp.portDip + (1.0 - pp.portDip) * sm + pp.portSwell * sm * std::exp (-age / pp.portDecay);
        }
        else if (pp.liftStroke > 0.0 && m7Mode == 0 && slurNotes == 0 && ! releasing)
            k *= 1.0 - pp.liftStroke * (1.0 - std::exp (-(t - strokeStart) / pp.liftTau));
        return k;
    }

    // the bow force (x), on a bowed string
    double m7Force() const
    {
        double k = 1.0;
        if (pp.tipLight > 0.0)
            k *= 1.0 - pp.tipLight * std::clamp (hair / pp.bowLength, 0.0, 1.0);
        if (m7Mode == artSautille)
        {
            const double x = std::clamp ((t - strokeStart) / m7Bounce, 0.0, 1.0);
            k *= pp.sautFloor + (1.0 - pp.sautFloor) * std::sin (pi * x);
        }
        else if (m7Mode == artTremolo)
            k *= 1.0 + pp.tremoloBite * std::exp (-(t - m7FlipAt) / 0.01);
        else if (m7Mode == artPortato && ! m7Rest)
        {
            const double age = t - m7PulseAt;
            k *= std::sqrt (std::max (0.1, pp.portDip + (1.0 - pp.portDip) * std::clamp (age / pp.portRise, 0.0, 1.0)));
        }
        return k;
    }

    // the last note let go: true when handled here
    bool m7NoteOff()
    {
        if (m7Mode == artPortato && pp.portGap > 0.0)
        {
            pendingOff = t + pp.portGap; // the next note within portGap joins this bow
            m7Rest = true;
            return true;
        }
        if (m7Mode == artSautille)
        {
            releasing = true;
            relTime = 0.02;
            lastStop = t;
            return true;
        }
        return false;
    }

    // col legno battuto: the stick strikes the stopped (or open) string
    void m7Strike (int pitch, double vel127)
    {
        const int s = chooseString (pitch);
        Str& S = st[s];
        const double semis = std::max (0.0, pitch - openPitch[s]);
        if (semis > 0 && ! inReach (semis))
            handPos = semis > handPos ? std::max (2.0, semis - 3.0) : std::max (2.0, semis);
        S.slideT0 = -1.0;
        S.target = S.pitch = pitch;
        S.vibWidthTarget = 0.0;
        S.lifted = semis == 0;
        S.noteOn = t;
        S.bowed = false;
        vn->s[s].setNote (pitch);
        if (vn->s[s].touchOn)
            vn->s[s].setTouch (0.0, 0.0);
        const double dd = dynFromVel (vel127);
        const double L = stringLength * std::pow (2.0, -semis / 12.0);
        M5Str& M = m5[s];
        M.pluckAt = -1.0;
        M.offAt = -1.0;
        M.beta = std::clamp (pp.clMM * 1e-3 / L, 0.04, 0.5);
        vn->s[s].setBeta (M.beta);
        const double speed = pp.clSpeedPP * std::pow (pp.clSpeedFF / pp.clSpeedPP, dd);
        const double tc = std::min (pp.clContact, 0.4 / std::max (1.0, vn->s[s].f1));
        vn->s[s].strike (speed, tc, pp.clKnock * (0.2 + 0.8 * dd));
        lastString = s;
        lastOn = t;
        m5Pluck[pitch & 127] = s + 1;
    }
};
} // namespace o2
