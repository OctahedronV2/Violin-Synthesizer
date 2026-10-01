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
    double slurMaxNotes = 4, slurMaxTime = 1.5, slurAccent = 12;
    // detache in quick passages (the previous note started less than shapeIOI s before): each
    // stroke speaks, then the bow eases to strokeSus of its speed (time constant strokeTau),
    // so the notes are shaped and separated instead of an even organ-like line. 0 = off.
    double shapeIOI = 0.0, strokeSus = 0.4, strokeTau = 0.12; // off: it didn't deepen the dips
    double dynGlide = 0.04; // s: speed, contact and force move to a new dynamic this smoothly
    // register: dB of extra bow speed by pitch (G3 .. E7 every 6 semitones), so a velocity plays
    // about equally loud anywhere on the instrument (fitted on the dry render)
    double reg55 = 4.7, reg61 = 1.4, reg67 = 3.5, reg73 = -2.8, reg79 = 4.1, reg85 = 8.7, reg91 = 9.5, reg97 = 9.0;
    // contact point, mm from the bridge: pp .. ff
    double contactPP = 32.0, contactFF = 14.0;
    double contactFollow = 0.0; // contact distance scales with the stopped length ^ this
    // force inside the Schelleng window: F = Fmin^(1-p) Fmax^p, p = posLo + posRange * d
    double cLower = 0.0042, cUpper = 0.75; // measured coefficients (SGA08, D string, kg/s)
    double posLo = 0.45, posRange = 0.40;
    double accel = 8.0; // bow acceleration limit, m/s^2 (higher at ff)
    double accelFF = 20.0;
    double landTime = 0.006; // bow lands on the string (force rise), s
    double biteFF = 0.35, biteTime = 0.03; // extra force at the start of loud strokes
    double changeDip = 0.6; // force reduction at a bow change
    double releaseTime = 0.05; // lift-off force time constant, s
    double crossTime = 0.02; // string crossing: force moves to the new string, s
    double bowLength = 0.62; // hair, m
    // left hand
    double shiftBase = 0.045, shiftPerSemi = 0.006; // slide time, s
    double shiftLighten = 0.2; // bow force reduction during a slide
    double vibDelay = 0.14, vibBloom = 0.3; // s
    double vibWidthLo = 8.0, vibWidthHi = 30.0; // cents peak-to-peak at d = 0 / 1
    double vibRate = 5.6, vibRateDyn = 0.6; // Hz, plus per unit d
    double vibWander = 0.15; // relative random wander of rate and width
    double liftAfter = 0.35; // an idle string's finger lifts after this long, s
    double liftDamp = 0.25, liftDampTime = 0.03;
    double chordWindow = 0.03; // s
    // loudness balance across strings: a violinist plays the low strings with less bow so a melody
    // stays even. Fitted so mf notes match TinySOL's register balance (G -1.5, D -2.3, E -5.6 dB vs A)
    double speedG = 0.43, speedD = 0.50, speedA = 1.0, speedE = 0.75;
    // a string the bow lands on while already moving (crossing, double stop) gets a short extra
    // force so it is captured into Helmholtz motion at once instead of multiple slipping
    double crossBite = 0.8, crossBiteTime = 0.05;
    double noiseStart = 0.3, noiseRise = 0.08; // slip hiss while a note starts, then full
    double bite = 0.25; // extra force at the start of every stroke (Guettler: capture needs force)
    // the player's ear: Helmholtz health from the strings (slips per period). Multiple slipping
    // -> more force; a string that sticks silent -> less force. Imperfection will scale this.
    double earUp = 0.25, earDown = 0.15, earMax = 2.5, earMin = 0.4, earRelax = 0.3, earWindow = 0.005, earWait = 0.05, earPeriods = 6.0;
    unsigned seed = 1;
};

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
    bool changing = false, releasing = false;
    double strokeStart = -1.0, lastStop = -10.0;
    double d = 0.6; // dynamics of the current stroke
    double dTarget = 0.6, noteNow = 69.0;
    int slurNotes = 0;
    double lastOn = -10.0;
    bool shaped = false;
    double lastVel = 64.0;
    double contactMM = 22.0;

    struct Str
    {
        bool bowed = false; // the bow is (or should be) on this string
        double force = 0.0, forceTarget = 0.0;
        double pitch = 0.0; // finger, semitones (MIDI)
        double target = 0.0;
        double slideFrom = 0.0, slideT0 = -1.0, slideDur = 0.0;
        double noteOn = -1.0;
        double vibPhase = 0.0, vibRate = 5.6, vibWidth = 0.0, vibWidthTarget = 0.0;
        double wanderR = 0.0, wanderW = 0.0;
        double lastBowed = -10.0;
        double setPitchAt = -1e9;
        bool lifted = true;
        double dampEnv = 0.0;
        double fScale = 1.0;
        double landAt = -10.0; // when the bow last landed on this string while moving
        double ear = 1.0; // force correction from listening
        long slipMark = 0;
        bool captured = true;
        int earHigh = 0;
        double earT = 0.0;
    } st[4];

    double handSemis = 0.0; // hand position: semitones above the open string of the first finger
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
    }

    double dynFromVel (double vel127) const
    {
        const double x = std::clamp ((vel127 - pp.velLo) / (pp.velHi - pp.velLo), 0.0, 1.0);
        return std::pow (x, pp.velCurve);
    }

    // ---------------------------------------------------------------- string choice
    int chooseString (int pitch, int avoid1 = -1, int avoid2 = -1) const
    {
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
            double c = 0.12 * semis + 0.6 * std::max (0.0, semis - 7.0);
            c += 0.9 * std::abs (s - lastString);
            if (semis > 0)
                c += 0.15 * std::abs (semis - handSemis);
            if (semis == 0 && pitch != 55)
                c += 0.8; // open strings can't vibrate: a violinist mostly stops the note
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

    // ---------------------------------------------------------------- bow targets from dynamics
    double betaFor (int s) const
    {
        const double L = stringLength * std::pow (2.0, -(st[s].pitch - openPitch[s]) / 12.0);
        // on a shorter (stopped) string the player moves the bow towards the bridge too
        const double c = contactMM * std::pow (L / stringLength, pp.contactFollow);
        return std::clamp (c * 1e-3 / L, 0.02, 0.3);
    }

    double forceFor (int s, double speed) const
    {
        const double beta = betaFor (s);
        const double z = vn->s[s].d.Z / 0.303; // the measured window is for a D string
        const double fMax = pp.cUpper * speed / beta * z;
        const double fMin = pp.cLower * speed / (beta * beta) * z * z;
        const double p = pp.posLo + pp.posRange * d;
        return std::exp ((1 - p) * std::log (fMin) + p * std::log (fMax));
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
        dTarget = dynFromVel (vel127);
        if (jump)
            d = dTarget;
        applyDyn();
        accel = pp.accel + (pp.accelFF - pp.accel) * dTarget;
    }
    void applyDyn()
    {
        V = speedFor (d) * regTrim (noteNow);
        contactMM = pp.contactPP + (pp.contactFF - pp.contactPP) * d;
    }

    // ---------------------------------------------------------------- left hand
    void fingerNote (int s, int pitch, bool slurred)
    {
        Str& S = st[s];
        const double semis = pitch - openPitch[s];
        const double from = S.lifted ? openPitch[s] : S.pitch;
        const double jump = std::abs (pitch - from);
        // a shift: same string, finger already down, the hand moves more than a tone
        const bool shift = ! S.lifted && semis > 0 && jump > 2.0 && std::abs (semis - handSemis) > 2.0;
        S.target = pitch;
        if (shift)
        {
            S.slideFrom = S.pitch;
            S.slideT0 = t;
            S.slideDur = pp.shiftBase + pp.shiftPerSemi * jump;
        }
        else
        {
            S.slideT0 = -1.0;
            S.pitch = pitch;
            vn->s[s].setNote (pitch);
            S.setPitchAt = t;
        }
        if (semis > 0)
            handSemis = semis;
        S.lifted = semis == 0;
        S.noteOn = t;
        S.captured = false;
        // vibrato restarts on a new bow, continues (phase kept) over a slur in one position
        if (! slurred || shift)
            S.vibWidth = 0.0;
        S.vibWidthTarget = semis > 0 ? pp.vibWidthLo + (pp.vibWidthHi - pp.vibWidthLo) * d : 0.0;
        S.wanderR = pp.vibWander * rng.gauss() * 0.5;
        S.wanderW = pp.vibWander * rng.gauss() * 0.5;
        S.vibRate = (pp.vibRate + pp.vibRateDyn * d) * (1.0 + S.wanderR);
    }

    // ---------------------------------------------------------------- events
    void noteOn (int pitch, double vel127)
    {
        const bool anyHeld = nHeld > 0;
        const bool chord = anyHeld && (t - held[nHeld - 1].on) < pp.chordWindow;
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
                           && ((pp.slurMaxNotes > 0 && slurNotes + 1 >= pp.slurMaxNotes)
                               || (pp.slurMaxTime > 0 && t - strokeStart > pp.slurMaxTime)
                               || (pp.slurAccent > 0 && vel127 - lastVel >= pp.slurAccent));
        if (rebow)
            nHeld = 0; // the held note ends with this bow
        if (chord)
            ;
        else if (anyHeld && ! rebow)
        {
            ++slurNotes;
            // legato: same bow. The new note replaces what was sounding.
            s = chooseString (pitch);
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
            const double dNew = dynFromVel (vel127);
            dTarget = (1.0 - pp.slurFollow) * d + pp.slurFollow * dNew;
            nHeld = 0; // slurred-over notes no longer sound
        }
        else
        {
            // a new stroke
            slurNotes = 0;
            shaped = pp.shapeIOI > 0 && t - lastOn < pp.shapeIOI;
            s = chooseString (pitch);
            for (int k = 0; k < 4; ++k)
                st[k].bowed = false;
            noteNow = pitch;
            setStroke (vel127);
            const bool bowMoving = std::abs (v) > 0.01;
            if (bowMoving)
                dir = -dir; // bow change
            else if (t - lastStop > 0.8)
                dir = hair > 0.5 * pp.bowLength ? -1.0 : 1.0; // retake: start where the bow is
            else
                dir = -dir;
            changing = bowMoving;
            releasing = false;
            strokeStart = t;
            fingerNote (s, pitch, false);
            st[s].bowed = true;
        }
        lastString = s;
        lastVel = vel127;
        lastOn = t;
        if (nHeld < 16)
            held[nHeld++] = { pitch, vel127, t, s };
    }

    void noteOff (int pitch)
    {
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
        // bow budget: change bow before the hair runs out
        if (! releasing && nHeld > 0)
        {
            const double margin = std::abs (v) * 0.04 + 0.01;
            if ((dir > 0 && hair > pp.bowLength - margin) || (dir < 0 && hair < margin))
            {
                dir = -dir;
                changing = true;
                if (log)
                    std::fprintf (stderr, "budget change %.3f\n", t);
            }
        }
        // bow velocity: accelerate towards the target with limited acceleration
        if (std::abs (dTarget - d) > 1e-6 || std::abs (V - speedFor (d) * regTrim (noteNow)) > 1e-9)
        {
            d += (dTarget - d) * std::min (1.0, dt / std::max (1e-4, pp.dynGlide));
            applyDyn();
        }
        const double balance[4] = { pp.speedG, pp.speedD, pp.speedA, pp.speedE };
        double shape = 1.0;
        if (shaped)
            shape = pp.strokeSus + (1.0 - pp.strokeSus) * std::exp (-(t - strokeStart) / pp.strokeTau);
        vTarget = dir * V * shape * balance[lastString];
        if (releasing)
        {
            // keep moving while the hair leaves the string, then slow down
            bool off = true;
            for (auto& S : st)
                off = off && S.force < 0.02 * S.forceTarget + 1e-4;
            vTarget = off ? 0.0 : v;
        }
        const double a = accel * dt;
        v += std::clamp (vTarget - v, -a, a);
        if (changing && std::abs (v - vTarget) < 1e-4)
            changing = false;
        hair = std::clamp (hair + v * dt, 0.0, pp.bowLength);

        for (int s = 0; s < 4; ++s)
        {
            Str& S = st[s];
            // left hand: slide, vibrato
            double pitch = S.target;
            if (S.slideT0 >= 0.0)
            {
                const double u = (t - S.slideT0) / S.slideDur;
                if (u >= 1.0)
                    S.slideT0 = -1.0;
                else
                    pitch = S.slideFrom + (S.target - S.slideFrom) * 0.5 * (1.0 - std::cos (pi * u));
            }
            const bool sliding = S.slideT0 >= 0.0;
            if (S.vibWidthTarget > 0.0 && S.bowed)
            {
                const double age = t - S.noteOn;
                const double env = std::clamp ((age - pp.vibDelay) / pp.vibBloom, 0.0, 1.0);
                const double w = S.vibWidthTarget * (1.0 + S.wanderW) * env * env * (3 - 2 * env);
                S.vibWidth += (w - S.vibWidth) * std::min (1.0, dt / 0.05);
                S.vibPhase += 2 * pi * S.vibRate * dt;
                if (S.vibPhase > 2 * pi)
                {
                    S.vibPhase -= 2 * pi;
                    // per-cycle wander: no two cycles alike
                    S.vibRate = (pp.vibRate + pp.vibRateDyn * d) * (1.0 + S.wanderR + 0.04 * rng.gauss());
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
                S.target = S.pitch = openPitch[s];
                vn->s[s].setNote (openPitch[s]);
                S.dampEnv = 1.0;
            }
            S.dampEnv *= std::exp (-dt / pp.liftDampTime);
            vn->s[s].damp = pp.liftDamp * S.dampEnv;
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
                ft *= 1.0 + (pp.bite + pp.biteFF * d * d) * std::exp (-age / pp.biteTime);
                if (changing)
                    ft *= 1.0 - pp.changeDip * (1.0 - std::min (1.0, std::abs (v) / std::max (1e-3, V)));
                if (sliding)
                    ft *= 1.0 - pp.shiftLighten;
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
                        if (log && c > 0.1)
                            std::fprintf (stderr, "slow capture %.3f s at %.2f string %d pitch %.1f\n", c, t, s, S.target);
                    }
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
            S.forceTarget = S.bowed ? std::max (ft, S.forceTarget * 0.0) : 0.0;
            const double tau = releasing ? pp.releaseTime : (S.bowed ? (S.force < 1e-4 ? pp.landTime : 0.01) : pp.crossTime);
            S.force += (ft - S.force) * std::min (1.0, dt / tau);
            if (S.force < 1e-5 && ft == 0.0)
                S.force = 0.0;
            force[s] = S.force;
            vBow[s] = S.force > 0.0 ? v : 0.0;
            vn->s[s].setBeta (betaFor (s));
        }
        t += dt;
    }
};
} // namespace o2
