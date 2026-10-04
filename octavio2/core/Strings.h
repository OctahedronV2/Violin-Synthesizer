// Octavio 2 strings: four bowed strings on one bridge.
// Started from the string-physics lab (research/string-physics/lab/lab.h), which measured each
// ingredient against the Schelleng limits, Guettler attacks and the Iowa violin. Changes for 2.0:
// thermal rosin + 4-point compliant hair by default, rosin grain faded in over each stroke's
// first 50 ms, a cheap continuous finger (setPitch) for vibrato and slides, ear intonation on.
// Double precision, clarity before speed (Jake: optimise later). Allocation only at init.

#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "BridgeData.h"

namespace o2
{
constexpr double pi = 3.14159265358979323846;

struct Rng
{
    uint64_t s = 0x9E3779B97F4A7C15ull;
    double uni() // [-1, 1)
    {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return (double) (s >> 11) / (double) (1ull << 52) - 1.0;
    }
    double gauss() { return (uni() + uni() + uni()); } // variance 1
};

struct Delay
{
    std::vector<double> buf = std::vector<double> (1 << 15, 0.0);
    int w = 0, mask = (1 << 15) - 1;
    void clear() { std::fill (buf.begin(), buf.end(), 0.0); }
    void push (double x)
    {
        buf[w] = x;
        w = (w + 1) & mask;
    }
    double read (double d) const // cubic Lagrange; d >= 1 sample back (d = 1: last pushed)
    {
        d = std::clamp (d, 1.0, (double) mask - 4.0);
        const int i = (int) d;
        const double f = d - i;
        auto at = [&] (int k) { return buf[(w - k) & mask]; };
        if (i < 2) // too short for the cubic: linear
            return (1 - f) * at (i) + f * at (i + 1);
        const double xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
        const double c0 = -f * (f - 1) * (f - 2) / 6.0, c1 = (f + 1) * (f - 1) * (f - 2) / 2.0;
        const double c2 = -(f + 1) * f * (f - 2) / 2.0, c3 = (f + 1) * f * (f - 1) / 6.0;
        return c0 * xm1 + c1 * x0 + c2 * x1 + c3 * x2;
    }
};

// ------------------------------------------------------------------ string data
struct StringData
{
    const char* name;
    double f0; // open
    double Z; // transverse impedance, kg/s  (sqrt(T * mu))
    double B; // inharmonicity f_n = n f0 sqrt(1 + B n^2)
    double t60lo, t60hi, fhi; // intrinsic string decay (s) low, and at fhi Hz
};

// ------------------------------------------------------------------ bridge admittance (modal)
// Bridge velocity per unit string force, as a sum of resonant modes
//   Y(s) = sum_k (1/m_k) s / (s^2 + (w_k/Q_k) s + w_k^2),
// discretised with the bilinear transform. The delay-free part Yd is solved
// together with the strings (all strings push on the same bridge point). Every mode has a
// positive residue (m_k > 0) and Q_k > 0, so Re Y >= 0 at every frequency: the bridge can only
// take energy from the strings, never give more back than it took (passive, unconditionally
// stable however the modes are retuned).
struct Bridge
{
    struct Mode
    {
        double b0, b2, a1, a2, z1 = 0, z2 = 0;
        double f = 0, Q = 1, mass = 1; // as last set
        double ra = 0, rb = 0; // how it radiates (modal body): ra v, and rb w v (see BridgeData.h)
    };
    std::vector<Mode> modes;
    double Yd = 0.0;
    double radA = 0.0, radB = 0.0; // sum_k ra_k v_k and sum_k rb_k w_k v_k after the last update
    static void design (Mode& m, double fs, double f, double Q, double mass)
    {
        const double w = 2 * pi * f, K = w / std::tan (w / (2 * fs)), a = w / Q;
        const double d0 = K * K + a * K + w * w;
        m.b0 = K / (mass * d0);
        m.b2 = -m.b0;
        m.a1 = (2 * w * w - 2 * K * K) / d0;
        m.a2 = (K * K - a * K + w * w) / d0;
        m.f = f;
        m.Q = Q;
        m.mass = mass;
    }
    void add (double fs, double f, double Q, double mass)
    {
        Mode m;
        design (m, fs, f, Q, mass);
        modes.push_back (m);
        Yd += m.b0;
    }
    // retune mode k in place (keeps its state; no allocation)
    void set (int k, double fs, double f, double Q, double mass)
    {
        design (modes[(size_t) k], fs, f, Q, mass);
        Yd = 0.0;
        for (auto& m : modes)
            Yd += m.b0;
    }
    double past() const // velocity from the modes' memory
    {
        double s = 0;
        for (auto& m : modes)
            s += m.z1;
        return s;
    }
    void update (double F)
    {
        double ra = 0.0, rb = 0.0;
        for (auto& m : modes)
        {
            const double y = m.b0 * F + m.z1;
            m.z1 = -m.a1 * y + m.z2;
            m.z2 = m.b2 * F - m.a2 * y;
            ra += m.ra * y;
            rb += m.rb * y;
        }
        radA = ra;
        radB = rb;
    }
    void clear()
    {
        for (auto& m : modes)
            m.z1 = m.z2 = 0;
        radA = radB = 0.0;
    }
};

// ------------------------------------------------------------------ friction laws
enum class Friction
{
    hyperbolic, // mu(dv): Smith & Woodhouse fit to steady sliding
    thermal, // mu(T): temperature of the rosin at the contact (Woodhouse 2003), plastic
    thermalHyp // mu = y(T) * hyperbolic(dv): temperature scales a rate curve (rate-and-state, WG25 / vW26 eq. 26)
};

struct Params
{
    double fs = 96000.0;
    Friction friction = Friction::thermal;
    double muS = 1.05, muD = 0.3, v0 = 0.1; // muS also scales the thermal law
    // thermal: mu = muD + (muS - muD) * exp(-theta); theta = kFast*xFast + kSlow*xSlow,
    // x follow heat power q = |f dv| (W) with time constants tauFast/tauSlow.
    double tauFast = 3e-4, tauSlow = 0.2, kFast = 8.0, kSlow = 1.0; // (old lumped variant, unused)
    double aT = 1.0e-6, bT = 0.22, cT = 1.0e-4, tauG = 25.0, ya = 0.4, xi = 2.0; // van Walstijn 2026, Table 1
    // elasto-plastic pre-sliding layer (Dupont et al. 2002, as in vW26): the rosin shears
    // elastically by z before it slides; friction = force * (sigma0 z + sigma1 dz/dt). 0 = off.
    // zba: fraction of the sliding deflection below which the layer is purely elastic. The layer is
    // stiffer under more force, so a light bow rounds the Helmholtz corner and a heavy one keeps it
    // sharp: brightness follows force (M2, 2026-10-03: sigma0 3e5 and zba 0.5 halve the gap to
    // Iowa's pp-mf-ff brightening and bring the Schelleng limits at 10-20 cm/s closer).
    double sigma0 = 3.0e5, sigma1 = 0.0, zba = 0.5;
    // the hair is a ribbon held at the frog and the tip, so it is stiffest near either end: its
    // stiffness under the string goes as 1 / (x (1 - x)) along the bow (x = 0 frog, 1 tip),
    // hairStiffness being the value at the middle. hairEnds 0 = uniform, 1 = the full law.
    double hairEnds = 1.0;
    int epIters = 3;
    // bow
    int bowPoints = 4;
    // the player tunes the strings while bowing, so the bow's flattening is tuned out: every
    // string (open or stopped) sits tuneCents sharp of its unbowed pitch
    double tuneCents = 6.0, earCarry = 0.7;
    double slipNoise = 0.04, slipNoiseHz = 3000.0, slipNoiseExp = 0.5, slipNoiseFade = 0.0;
    // 0: the rough friction acts on the string (couples into the slip timing: jitter);
    // 1: the same force fluctuation goes straight to the bridge (hiss without jitter)
    double slipNoiseOut = 1.0;
    double bowWidth = 0.01, hairStiffness = 110000.0, hairDamping = 10.0, grain = 0.03, grainHz = 3000.0,
           grainFade = 0.05;
    int grainMode = 0;
    double grainLen = 1e-4;
    // string
    bool dispersion = true;
    int maxAllpass = 6;
    bool perString = true; // real impedance/damping per string, else every string = generic A (old engine)
    bool torsion = false;
    double torsionSpeed = 5.0, torsionImpedance = 3.0, torsionQ = 2.0;
    bool autoTune = true;
    double fingerLoss = 0.0; // extra loss per reflection at a stopping finger (fraction)
    // bridge
    bool admittance = true;
    int bridgeModes = 1; // 0: the M0 generic set (kGenericModes), 1: the passive fit to CNSM (kCnsmModes, M3)
    double admScale = -1.0; // admittance scale; < 0: the mode set's own calibration (generic 0.5, CNSM 1)
    // Sympathetic: how freely the open strings nobody is playing join in, 0..1. An idle open
    // string meets the bridge through an ideal transformer (coupling c, lossless, passive). At the
    // default 0.5, c = symCoupling: a string at its own notch feels the bridge less than the
    // bowed point does (the cross-admittance is smaller than the driving point's), and at c = 1
    // an open string a unison or octave under the note absorbs it (dynamic absorber, up to -7 dB
    // on E5 stopped on the A string with the CNSM bridge). Below 0.5 the player's spare fingers
    // quiet them (c down to 0, damping up to symDamp per round trip); above, c rises to 1 and
    // they lose up to symLossCut of their own low-frequency loss (in dB; still passive).
    double sympathetic = 0.5, symCoupling = 0.5, symLossCut = 0.6, symIdleAfter = 2.0, symDamp = 0.2;
    // Wolf: makes the strongest body mode below 700 Hz lighter and sharper, 0..1 (0 = as
    // measured). wolfHz > 0 picks the mode nearest that frequency instead.
    double wolf = 0.0, wolfHz = 0.0, wolfMass = 12.0, wolfQ = 2.5;
    // Hold: the chin and the hand damp the low modes, 0 (free, as the violin was measured hanging)
    // .. 1 (held firmly): Q of the modes below holdHz falls to 1 / (1 + holdMax) of the free value,
    // fading out over the next holdFade Hz.
    double hold = 0.5, holdMax = 1.5, holdHz = 500.0, holdFade = 300.0;
};

// ------------------------------------------------------------------ one string
struct String
{
    StringData d;
    const Params* P = nullptr;
    double fs = 96000.0;
    // geometry
    double note = 0, f1 = 0, N = 0; // N: delay-line part of the round trip
    double period = 218; // true period in samples (fs / partial 1)
    double beta = 0.1;
    // waves
    Delay toBr, fromBr, nutLoop;
    Delay gapR[3], gapL[3];
    Delay tBr, tNut, tGapR[3], tGapL[3]; // torsion
    // nut-side loop filter: one-pole loss, M first-order allpasses (dispersion)
    double g = 0.99, dark = 0.2, lp = 0.0;
    int M = 0;
    double apA = 0.0, apX[8] = {}, apY[8] = {};
    double fingerG = 1.0;
    double slipLp[4] = {}, slipNoiseA = 0.0;
    double hiss = 0.0; // rough-friction force sent straight to the bridge this sample
    double direct = 0.0; // M5: sound radiated straight from the string/fingerboard (Bartok slap), not via the bridge
    double noiseGain = 1.0; // set by the player: how settled the stroke is (less hiss while the note starts)
    double damp = 0.0; // extra loss per round trip from a finger touching or lifting (0..1), set by the player
    double dBr = 2, dN = 2; // one-way bow->bridge, nut round trip
    // bridge wave
    double aBr = 0.0;
    // bow contact state
    bool stick[4] = {};
    double hairY[4] = {}, grainLp[4] = {}, xFast[4] = {}, xSlow[4] = {}, tau[4] = {};
    double zEP[4] = {}, dvEP[4] = {}; // elasto-plastic layer deflection (m) and last relative velocity
    double hairFrac = 0.5; // where on the bow the string is, 0 frog .. 1 tip (set by the player)
    double widthScale = 1.0; // the hair's width on the string against bowWidth (the player tilts the bow)
    Rng rng;
    // diagnostics
    double vBowPt = 0.0;
    bool stickAll = false, slipped = false;
    long slipTotal = 0;
    int stuckFor = 0;
    long samples = 0, lastSlip = 0;
    double fingerCents = 0.0;
    bool fingered = false;
    // stroke tracking (grain fade-in): samples since the bow landed or changed direction
    double strokeAge = 0.0, lastVBow = 0.0;
    // dispersion fit made once per string at the open pitch, reused by setPitch
    int dispM = 0;
    double dispA = 0.0;
    // M5: shaped pluck (see pluck()) and the light finger (setTouch)
    bool plOn = false;
    double plU0 = 0.0, plPull = 0.0, plTau = 1.0, plT = 0.0, plClickAt = -1.0, plClickAmp = 0.0, clickHp = 0.0,
           clickLp = 0.0, plClickTau = 1.0, plKick = 0.5;
    bool touchOn = false;
    double touchX = 0.0, touchR = 0.0, touchA = 1.0;
    Delay hTo, hBack;

    void init (const StringData& sd, const Params& p)
    {
        d = sd;
        P = &p;
        fs = p.fs;
        slipNoiseA = std::exp (-2 * pi * p.slipNoiseHz / fs);
        fitDispersion();
        setNote (12.0 * std::log2 (d.f0 / 440.0) + 69.0);
    }

    static double apPhaseDelay (double a, double w) // first-order allpass (a + z^-1)/(1 + a z^-1)
    {
        const double ph = -w + 2.0 * std::atan2 (a * std::sin (w), 1.0 + a * std::cos (w));
        return -ph / w;
    }

    // Fit M first-order allpasses with one shared coefficient to the stiff-string partials
    // f_n = n f1 sqrt(1 + B n^2) of the open string. Done once; setPitch reuses it (a fingered
    // string is stiffer, B ~ 1/L^2, but the difference is about a cent at partial 10).
    void fitDispersion() { fitAllpass (d.f0, d.B, fs, *P, dispM, dispA); }
    static void fitAllpass (double f, double Bn, double fs, const Params& p, int& dispM, double& dispA)
    {
        dispM = 0;
        dispA = 0.0;
        if (! p.dispersion || Bn <= 0)
            return;
        double best = 1e30;
        const int nmax = std::min (40, (int) (0.45 * fs / f));
        for (int m = 1; m <= p.maxAllpass; ++m)
            for (double a = -0.02; a > -0.95; a -= 0.005)
            {
                double err = 0;
                const double ref = m * apPhaseDelay (a, 2 * pi * f * std::sqrt (1 + Bn) / fs);
                for (int k = 2; k <= nmax; ++k)
                {
                    const double fk = k * f * std::sqrt (1 + Bn * k * k);
                    const double want = fs / (f * std::sqrt (1 + Bn * k * k)) - fs / (f * std::sqrt (1 + Bn));
                    const double got = m * apPhaseDelay (a, 2 * pi * fk / fs) - ref;
                    err += (got - want) * (got - want) / k;
                }
                if (err < best * 0.95)
                {
                    best = err;
                    dispM = m;
                    dispA = a;
                }
            }
    }

    // M7: another string set on this string while it plays: new impedance, stiffness and loss
    // (dispersion fit made beforehand), same open pitch, finger and ear
    void setData (const StringData& sd, int apM, double apCoef)
    {
        d = sd;
        dispM = apM;
        dispA = apCoef;
        setPitch (note);
    }

    // M7: col legno battuto. The stick strikes the string at the bow point (the caller sets beta
    // first): the string there is pushed at the stick's speed v (m/s) for the contact time tc (s,
    // a half sine), then the stick bounces off and the string rings. knock (N at the board): the
    // wood's own click, heard straight from the stick (String::direct), decaying over knockTime.
    void strike (double v, double tc, double knock, double knockTime = 0.003)
    {
        stkV = v;
        stkTc = std::max (1.0, tc * fs);
        stkT = 0.0;
        stkKnock = knock;
        stkKnockTau = std::max (1.0, knockTime * fs);
        stkOn = true;
    }
    double strikeWave()
    {
        if (! stkOn)
            return 0.0;
        const double u = stkT < stkTc ? stkV * std::sin (pi * stkT / stkTc) : 0.0;
        if (stkKnock > 0.0)
        {
            const double s = std::exp (-stkT / stkKnockTau), wn = rng.uni();
            stkHp = 0.9 * stkHp + 0.1 * wn; // band-passed near 1-4 kHz: wood, not metal
            stkLp = 0.75 * stkLp + 0.25 * (wn - stkHp);
            direct += stkKnock * s * stkLp * 4.0;
        }
        stkT += 1.0;
        if (stkT > std::max (stkTc, 10.0 * stkKnockTau))
            stkOn = false;
        return u;
    }
    bool stkOn = false;
    double stkV = 0.0, stkT = 0.0, stkTc = 1.0, stkKnock = 0.0, stkKnockTau = 1.0, stkHp = 0.0, stkLp = 0.0;

    // A new note: the finger lands, the ear's correction starts again from zero.
    void setNote (double n) { setPitch (n, true); }

    // Continuous finger position (vibrato, slides). keepEar keeps the ear's running correction.
    void setPitch (double n, bool resetEar = false)
    {
        note = n;
        f1 = 440.0 * std::pow (2.0, (n - 69.0) / 12.0);
        // intrinsic losses, set from decay times in seconds (so the same at any rate)
        const double lo = P->perString ? d.t60lo : 2.5, hi = P->perString ? d.t60hi : 0.25,
                     fh = P->perString ? d.fhi : 4000.0;
        g = std::pow (10.0, -3.0 / (lo * f1));
        {
            const double r = std::pow (10.0, -3.0 / f1 * (1.0 / hi - 1.0 / lo));
            const double cw = std::cos (2 * pi * fh / fs), r2 = r * r;
            const double A = 1.0 - r2, Bq = 1.0 - r2 * cw;
            dark = A <= 1e-12 ? 0.0 : (Bq - std::sqrt (std::max (Bq * Bq - A * A, 0.0))) / A;
        }
        fingered = std::abs (f1 - d.f0) > 0.5;
        fingerG = fingered ? 1.0 - P->fingerLoss : 1.0;
        M = dispM;
        apA = dispA;
        const double Bn = P->dispersion ? d.B * (f1 / d.f0) * (f1 / d.f0) : 0.0;
        // total round trip: N = fs / (frequency of partial 1)
        const double fp1 = f1 * std::pow (2.0, P->tuneCents / 1200.0) * std::sqrt (1 + Bn);
        const double w = 2 * pi * fp1 / fs;
        const double pdLoss = std::atan2 (dark * std::sin (w), 1.0 - dark * std::cos (w)) / w;
        const double pdAp = M > 0 ? M * apPhaseDelay (apA, w) : 0.0;
        N = fs / fp1 - pdLoss - pdAp;
        period = fs / fp1;
        if (! fingered)
            fingerCents = 0.0;
        else if (resetEar) // the finger lands where the last correction left it (earCarry of it)
            fingerCents *= P->earCarry;
        setBeta (beta);
    }

    void setBeta (double b)
    {
        beta = b;
        const int K = bowPointsNow();
        const double gap = gapSamples();
        dBr = std::max (1.0, beta * period / 2.0);
        // the nut side loses the extra delay of the bow-width gaps
        dN = std::max (1.0,
                       N - 2.0 * dBr - 2.0 * (K - 1) * gap - period * (1.0 - std::pow (2.0, -fingerCents / 1200.0)));
        if (touchOn) // bow point to the touching finger, one way (at least 2 samples left beyond it)
            touchA = std::clamp (touchX * period / 2.0 - dBr - (K - 1) * gap, 1.0, std::max (1.0, 0.5 * dN - 1.0));
    }

    int bowPointsNow() const { return std::clamp (P->bowPoints, 1, 4); }
    double gapSamples() const
    {
        const int K = bowPointsNow();
        if (K <= 1)
            return 0.0;
        const double c = 2.0 * 0.325 * d.f0; // wave speed of this string, m/s
        return std::max (1.0, P->bowWidth * widthScale * fs / c / (K - 1));
    }

    double grainFactor (int k, double vBow = 0.0)
    {
        if (P->grain <= 0.0)
            return 1.0;
        const double fade = P->grainFade > 0.0 ? std::min (1.0, strokeAge / (P->grainFade * fs)) : 1.0;
        if (P->grainMode == 1 && stick[k])
            return 1.0; // slip only: the friction wobbles while the rosin is shearing
        // grainMode 2: the irregularity is laid along the bow hair, so it passes the string at
        // the bow's speed: bandwidth = speed / grainLen
        const double hz = P->grainMode == 2 ? std::max (20.0, std::abs (vBow) / P->grainLen) : P->grainHz;
        const double a = std::exp (-2 * pi * hz / fs);
        grainLp[k] = a * grainLp[k] + (1 - a) * rng.gauss();
        return std::max (0.0, 1.0 + fade * P->grain * grainLp[k] * std::sqrt ((1 + a) / (1 - a)));
    }

    double hyperbolic (int k, double vBow, double vh, double force, double a)
    {
        const double muS = P->muS, muD = P->muD, v0 = P->v0;
        const double dh = vBow - vh, adh = std::abs (dh);
        auto root = [&]
        {
            const double b = a * v0 + force * muD - a * adh, c = (force * muS - a * adh) * v0;
            const double disc = b * b - 4 * a * c;
            if (disc < 0)
                return -1.0;
            const double r = (-b + std::sqrt (disc)) / (2 * a);
            return r > 0 ? r : -1.0;
        };
        double slip;
        if (a * adh <= muS * force)
        {
            if (stick[k])
                return vBow;
            slip = root();
            if (slip < 0)
            {
                stick[k] = true;
                return vBow;
            }
        }
        else
            slip = root();
        stick[k] = false;
        return vBow - (dh > 0 ? slip : -slip);
    }

    // Thermal friction with the elasto-plastic pre-sliding layer (vW26 / Dupont 2002). The layer's
    // deflection z obeys dz/dt = dv (1 - alpha(z, dv) z / zss), zss = mu(tau) / sigma0, with
    // alpha 0 (elastic) below zba * zss and 1 (sliding) above zss. The friction force is
    // force * (sigma0 z + sigma1 dz/dt). Solved implicitly: backward Euler in z (alpha from the
    // last step), Newton on the relative velocity dv = vBow - v, where v = vh + f / a.
    double elastoPlastic (int k, double vBow, double vh, double force, double a)
    {
        const double lineLen = std::max (1e-4, P->bowWidth * widthScale / std::max (1, bowPointsNow()));
        const double Fb = force / lineLen;
        const double tq = std::pow (std::max (0.0, tau[k]) / P->tauG, P->xi);
        const double muT = P->muS * (1 + P->ya * tq) / (1 + tq);
        // the sliding rosin's roughness (see the plastic law below): while the layer slides, the
        // friction limit fluctuates, or (slipNoiseOut) the same fluctuation goes to the bridge
        double r = 0.0;
        if (P->slipNoise > 0.0 && ! stick[k])
        {
            const double an = slipNoiseA, wn = rng.gauss();
            slipLp[k] = an * slipLp[k] + (1 - an) * wn;
            r = noiseGain * P->slipNoise * std::pow (std::abs (dvEP[k]) / 0.1, P->slipNoiseExp) * (wn - slipLp[k]);
        }
        const double mu = P->slipNoiseOut > 0.0 ? muT : muT * std::max (0.05, 1.0 + r);
        const double s0 = P->sigma0, s1 = P->sigma1, T = 1.0 / fs, zp = zEP[k];
        const double zss = mu / s0;
        const double D = vBow - vh; // relative velocity if no friction acted
        auto alphaOf = [&] (double dv)
        {
            if (zp * dv <= 0.0) // unloading or reversing: elastic
                return 0.0;
            const double r = std::abs (zp) / zss, lo = P->zba;
            if (r <= lo)
                return 0.0;
            if (r >= 1.0)
                return 1.0;
            return 0.5 + 0.5 * std::sin (pi * (r - 0.5 * (1 + lo)) / (1 - lo));
        };
        // f(dv) and h(dv) = dv - D + f / a (zero at the solution)
        auto eval = [&] (double dv, double& f, double& dfd)
        {
            const double al = alphaOf (dv), c = al * T * s0 / mu; // implicit: z = (zp + T dv) / (1 + c |dv|)
            const double den = 1.0 + c * std::abs (dv);
            const double z = (zp + T * dv) / den;
            const double dz = (z - zp) / T;
            const double dzd = (T * den - (zp + T * dv) * c * (dv >= 0 ? 1.0 : -1.0)) / (den * den);
            f = force * (s0 * z + s1 * dz);
            dfd = force * (s0 + s1 / T) * dzd;
            return z;
        };
        double dv = dvEP[k], f = 0, dfd = 0;
        for (int it = 0; it < P->epIters; ++it)
        {
            eval (dv, f, dfd);
            const double h = dv - D + f / a, dh = 1.0 + dfd / a;
            double step = h / std::max (dh, 1e-3);
            dv -= step;
        }
        const double z = eval (dv, f, dfd);
        // the force a stuck contact can carry is bounded by the layer: keep f consistent with dv
        zEP[k] = z;
        dvEP[k] = dv;
        stick[k] = std::abs (z) < P->zba * zss;
        if (P->slipNoiseOut > 0.0 && ! stick[k])
            hiss += P->slipNoiseOut * (dv > 0 ? 1.0 : -1.0) * muT * force * r;
        const double v = vBow - dv;
        const double q = std::abs (f * dv) / lineLen; // W/m
        const double cool = (P->bT * std::sqrt (std::abs (dv) / Fb) + P->cT) * Fb;
        const double rate = cool / (P->aT * Fb), e = std::exp (-rate / fs);
        const double target = cool > 0 ? q / cool : 0.0;
        tau[k] = target + (tau[k] - target) * e;
        return v;
    }

    // Friction at one contact point. a = junction impedance seen by the friction force
    // (force f gives velocity change f / a). Returns the contact point's velocity.
    double contact (int k, double vBow, double vh, double force, double a)
    {
        if (force <= 0.0)
        {
            stick[k] = false;
            tau[k] *= 0.999;
            zEP[k] = 0.0;
            dvEP[k] = 0.0;
            return vh;
        }
        if (P->friction == Friction::thermalHyp)
        {
            const double lineLen = std::max (1e-4, P->bowWidth * widthScale / std::max (1, bowPointsNow()));
            const double Fb = force / lineLen;
            const double tq = std::pow (std::max (0.0, tau[k]) / P->tauG, P->xi);
            const double y = (1 + P->ya * tq) / (1 + tq);
            const double v = hyperbolic (k, vBow, vh, force * y, a);
            const double f = a * (v - vh);
            const double q = stick[k] ? 0.0 : std::abs (f * (vBow - v)) / lineLen;
            const double cool = (P->bT * std::sqrt (std::abs (vBow - v) / Fb) + P->cT) * Fb;
            const double e = std::exp (-cool / (P->aT * Fb) / fs), target = q / cool;
            tau[k] = target + (tau[k] - target) * e;
            return v;
        }
        if (P->friction == Friction::hyperbolic)
            return hyperbolic (k, vBow, vh, force, a);
        if (P->sigma0 > 0.0)
            return elastoPlastic (k, vBow, vh, force, a);
        // thermal friction after van Walstijn et al. (Acta Acustica 2026) with mu_d = mu_s (their best
        // fit) and the rosin's pre-sliding elasticity left out: friction limit mu_s * y(tau), where
        // tau is the contact temperature above ambient and y falls from 1 to ya through the
        // glass transition tauG. Heat balance per unit length of contact (their eq. 22):
        //   aT Fb dtau/dt + (bT sqrt(|v| / Fb) + cT Fb) tau = Qf = f_line |v_rel|
        const double lineLen = std::max (1e-4, P->bowWidth * widthScale / std::max (1, bowPointsNow()));
        const double Fb = force / lineLen; // N/m
        const double tq = std::pow (std::max (0.0, tau[k]) / P->tauG, P->xi);
        const double y = (1 + P->ya * tq) / (1 + tq);
        const double mu = P->muS * y;
        const double dh = vBow - vh;
        double v, q = 0.0;
        if (std::abs (a * dh) <= mu * force)
        {
            stick[k] = true;
            v = vBow;
        }
        else
        {
            stick[k] = false;
            // sliding rosin is rough: the friction force fluctuates broadband while it slides
            // (hair and rosin asperities), more the faster it slides. slipNoise is the relative
            // rms at 0.1 m/s of sliding speed.
            double rough = 1.0;
            if (P->slipNoise > 0.0)
            {
                // high-passed at slipNoiseHz: the hiss of sliding hair, kept out of the band
                // that would jitter the slip timing
                const double a = slipNoiseA, wn = rng.gauss();
                slipLp[k] = a * slipLp[k] + (1 - a) * wn;
                const double fadeN = noiseGain;
                const double r
                    = fadeN * P->slipNoise * std::pow (std::abs (dh) / 0.1, P->slipNoiseExp) * (wn - slipLp[k]);
                if (P->slipNoiseOut > 0.0)
                    hiss += P->slipNoiseOut * (dh > 0 ? 1.0 : -1.0) * mu * force * r;
                else
                    rough = std::max (0.0, 1.0 + r);
            }
            const double f = (dh > 0 ? 1.0 : -1.0) * mu * force * rough;
            v = vh + f / a;
            q = std::abs (f * (vBow - v)) / lineLen; // W/m
        }
        const double cool = (P->bT * std::sqrt (std::abs (vBow - v) / Fb) + P->cT) * Fb;
        // exact step of the linear relaxation over one sample
        const double rate = cool / (P->aT * Fb), e = std::exp (-rate / fs);
        const double target = cool > 0 ? q / cool : 0.0;
        tau[k] = target + (tau[k] - target) * e;
        return v;
    }

    // Step 1 of a sample: the wave arriving at the bridge.
    void readBridge() { aBr = toBr.read (dBr); }

    // Step 2: given the wave leaving the bridge, run the bow junction and the nut loop.
    void tick (double bBr, double vBow, double force)
    {
        if (force <= 0.0 || vBow * lastVBow < 0.0 || (lastVBow == 0.0 && vBow != 0.0))
            strokeAge = 0.0;
        else
            strokeAge += 1.0;
        lastVBow = vBow;
        const double vinB = fromBr.read (dBr);
        fromBr.push (bBr);
        // nut loop: loss, dispersion, inverting reflection at the nut / finger
        double x = nutLoop.read (touchOn ? std::max (2.0, dN - 2.0 * touchA) : dN);
        lp = (1 - dark) * x + dark * lp;
        x = g * fingerG * (1.0 - damp) * lp;
        for (int i = 0; i < M; ++i)
        {
            const double y = apA * x + apX[i] - apA * apY[i];
            apX[i] = x;
            apY[i] = y;
            x = y;
        }
        const double Zs = P->perString ? d.Z : 0.2;
        double vinN = -x;
        if (touchOn) // the light finger: a dashpot junction between the bow and the nut
        {
            const double a = hTo.read (touchA), b = vinN;
            const double vH = 2.0 * Zs * (a + b) / (2.0 * Zs + touchR);
            nutLoop.push (vH - b);
            vinN = hBack.read (touchA);
            hBack.push (vH - a);
        }

        const int K = bowPointsNow();
        const double gap = gapSamples();
        slipped = false;
        hiss = 0.0;
        direct = 0.0;
        const double plW = pluckWave() + strikeWave(); // M7: strikeWave is 0 unless col legno
        const bool wasStick = stickAll;
        // Torsion: the bow drags the string's surface, so it also twists the string. Twist
        // waves travel torsionSpeed times faster with impedance torsionImpedance * Z (as seen
        // at the surface) and lose exp(-pi / Q) of their amplitude per round trip.
        const bool tors = P->torsion;
        const double Zt = Zs * P->torsionImpedance;
        const double tPeriod = period / P->torsionSpeed;
        const double tGap = K > 1 ? std::max (1.0, gap / P->torsionSpeed) : 0.0;
        const double tR = std::exp (-pi / (2.0 * P->torsionQ)); // per reflection
        const double tdB = std::max (1.0, beta * tPeriod),
                     tdN = std::max (1.0, (1 - beta) * tPeriod - 2.0 * (K - 1) * tGap);
        double fromB[4], fromN[4], tfromB[4] = {}, tfromN[4] = {};
        for (int k = 0; k < K; ++k)
        {
            fromB[k] = k == 0 ? vinB : gapR[k - 1].read (gap);
            fromN[k] = k == K - 1 ? vinN : gapL[k].read (gap);
            if (tors)
            {
                tfromB[k] = k == 0 ? -tR * tBr.read (tdB) : tGapR[k - 1].read (tGap);
                tfromN[k] = k == K - 1 ? -tR * tNut.read (tdN) : tGapL[k].read (tGap);
            }
        }
        // admittance of the string surface to a point force: transverse plus twist
        const double Ys = 1.0 / (2.0 * Zs) + (tors ? 1.0 / (2.0 * Zt) : 0.0);
        double vs = 0;
        for (int k = 0; k < K; ++k)
        {
            const double vh = fromB[k] + fromN[k] + tfromB[k] + tfromN[k];
            const double fk = force / K * grainFactor (k, vBow);
            double f;
            if (P->hairStiffness > 0.0)
            {
                // hair spring kk with damper c in parallel, in series with the string surface
                const double x = std::clamp (hairFrac, 0.04, 0.96);
                const double ends = 1.0 + P->hairEnds * (0.25 / (x * (1.0 - x)) - 1.0);
                const double c = P->hairDamping / K, kk = P->hairStiffness * ends / K;
                const double a = 1.0 / (Ys + 1.0 / c);
                const double vb = vBow - kk * hairY[k] / c;
                const double vv = contact (k, vb, vh, fk, a);
                f = a * (vv - vh);
                hairY[k] += -(f + kk * hairY[k]) / c / fs;
                if (force <= 0)
                    hairY[k] *= 0.99;
            }
            else
            {
                const double a = 1.0 / Ys;
                f = a * (contact (k, vBow, vh, fk, a) - vh);
            }
            const double dv = f / (2.0 * Zs) + (k == 0 ? plW : 0.0);
            const double v = vh + f * Ys;
            if (k == 0)
                toBr.push (fromN[k] + dv);
            else
                gapL[k - 1].push (fromN[k] + dv);
            if (k == K - 1)
                (touchOn ? hTo : nutLoop).push (fromB[k] + dv);
            else
                gapR[k].push (fromB[k] + dv);
            if (tors)
            {
                const double dt = f / (2.0 * Zt);
                if (k == 0)
                    tBr.push (tfromN[k] + dt);
                else
                    tGapL[k - 1].push (tfromN[k] + dt);
                if (k == K - 1)
                    tNut.push (tfromB[k] + dt);
                else
                    tGapR[k].push (tfromB[k] + dt);
            }
            vs += v;
        }
        vBowPt = vs / K;
        if (K == 1)
            stickAll = stick[0];
        else if (vBowPt < 0.0)
            stickAll = false;
        else if (vBowPt > 0.5 * vBow)
            stickAll = true;
        // a slip counts once the string has stuck for at least 3% of a period (debounce)
        if (stickAll && ! wasStick)
            stuckFor = 0;
        if (stickAll)
            ++stuckFor;
        if (force > 0 && wasStick && ! stickAll && stuckFor >= 0.03 * period)
        {
            slipped = true;
            ++slipTotal;
            // The player's ear: on a stopped note the finger creeps to cancel the pitch the bow
            // pulls (flattening). Open strings cannot be corrected.
            const double iv = (double) (samples - lastSlip);
            const double cur = period * std::pow (2.0, -fingerCents / 1200.0);
            if (P->autoTune && fingered && std::abs (iv / cur - 1.0) < 0.1)
            {
                const double err = 1200.0 * std::log2 (iv / period);
                fingerCents = std::clamp (fingerCents + 0.05 * err, -40.0, 40.0);
                setBeta (beta);
            }
            lastSlip = samples;
        }
        ++samples;
    }

    // ---------------------------------------------------------------- M5: plucks and the light finger
    // The old placeholder (kept for before/after comparisons, PlayerParams::pizzModel 0): one
    // velocity impulse pushed towards the bridge and the nut at the bow point.
    void pluckImpulse (double amp)
    {
        toBr.push (amp);
        nutLoop.push (amp);
    }

    // A shaped pluck at the junction (bow point 0, so the caller sets beta to the plucking point
    // first). The finger pulls the string aside by h metres with a force applied at the point
    // (velocity wave u = F / 2Z into both directions), then lets go:
    //  - the pull is a raised-cosine ramp lasting a whole number of periods (at least two, about
    //    `pull` seconds), whose spectrum then has zeros at every
    //    partial, so the string follows it quasi-statically and nothing rings before the release
    //    (an instant initial shape would hit the bridge and body with a step of the static force);
    //  - the release is the force falling as (1 + t/tau) exp(-t/tau): a soft fingertip rolling off
    //    the string (tau ~ 0.5 ms) is a 12 dB/octave low-pass on the pluck above 1/(2 pi tau), a
    //    nail (tau ~ 0.05 ms) lets the whole spectrum through. The plucking point (beta) carves
    //    the sin(n pi beta) comb, the bridge and body do the rest.
    // clickAt > 0 (s after the release): a Bartok snap, the string slaps the fingerboard: a bright
    // knock (clickAmp, decaying over clickTime) heard straight from the fingerboard (String::direct,
    // added by the Engine after the body) and a little through the bridge, and the board stopping
    // the swing for an instant on each of the next four periods (kick x the pluck's wave, halving).
    // The pull adds latency: the release comes about pull seconds after the call.
    void pluck (double h,
                double tau,
                double clickAt = 0.0,
                double clickAmp = 0.0,
                double pull = 0.0,
                double clickTime = 0.0015,
                double kick = 0.5)
    {
        const double L = f1 > 0.0 ? 0.325 * d.f0 / f1 : 0.325; // vibrating length, m
        const double c = 2.0 * 0.325 * d.f0; // wave speed, m/s
        const double b = std::clamp (beta, 0.02, 0.98);
        plU0 = c * h / (2.0 * b * (1.0 - b) * L);
        plPull = std::max (2.0, std::ceil (pull * fs / period)) * period; // whole periods: zeros on every partial
        plTau = std::max (0.5, tau * fs);
        plT = 0.0;
        plOn = true;
        plClickAt = clickAt > 0.0 ? plPull + clickAt * fs : -1.0;
        plClickAmp = clickAmp;
        plClickTau = std::max (1.0, clickTime * fs);
        plKick = kick;
    }
    bool plucking() const { return plOn; }
    // samples until the release starts (for timing), and the time the pull takes
    double pluckLatency() const { return plOn ? std::max (0.0, plPull - plT) : 0.0; }

    // The pluck's velocity wave for this sample (called once per tick)
    double pluckWave()
    {
        if (! plOn)
            return 0.0;
        double u;
        if (plT < plPull)
            u = plU0 * 0.5 * (1.0 - std::cos (pi * plT / plPull));
        else
        {
            const double x = (plT - plPull) / plTau;
            u = plU0 * (1.0 + x) * std::exp (-x);
            if (x > 30.0)
            {
                u = 0.0;
                if (plClickAt < 0.0 || plT > plClickAt + std::max (8.0 * plClickTau, 4.0 * period))
                    plOn = false;
            }
        }
        if (plClickAt > 0.0)
        {
            // the board stops the swing (0.12 ms contact), and the string comes back to slap it
            // again on each of the next few periods, each time softer: a rattle that clips the
            // string's swing and makes the slapped string itself bright
            const double k = plT - plClickAt, w = 0.00012 * fs;
            const int cyc = k >= 0.0 ? (int) (k / period) : -1;
            const double kk = k - cyc * period;
            if (cyc >= 0 && cyc < 4 && kk < w)
                u -= plKick * plU0 * std::pow (0.5, cyc) * std::sin (pi * kk / w);
            if (k >= 0.0 && k < 8.0 * plClickTau) // the knock and rattle: a decaying bright burst
            {
                const double s = std::exp (-k / plClickTau);
                const double wn = rng.uni();
                clickHp = 0.94 * clickHp + 0.06 * wn; // high-passed near 1 kHz
                clickLp = 0.82 * clickLp + 0.18 * (wn - clickHp); // ... and low-passed near 3 kHz
                const double knock = plClickAmp * s * clickLp * 4.0;
                hiss += 0.01 * knock; // a little through the bridge and body
                direct = knock; // and straight from the fingerboard into the air (Engine adds it)
            }
        }
        plT += 1.0;
        return u;
    }

    // Light finger touching the string at fraction x from the bridge (harmonics): a dashpot of
    // resistance R (kg/s) at that point, v = 2Z (a + b) / (2Z + R). Partials with a node there
    // pass untouched, all others lose energy at every pass. The nut-side loop is split there
    // into point->finger, finger->nut->finger and finger->point. x = 0 takes the finger away.
    // R is a pressure: 0 = not touching (transparent), ~Z = harmonic, very large = a stop.
    void setTouch (double x, double R)
    {
        const bool on = x > 0.0;
        if (on && ! touchOn)
        {
            hTo.clear();
            hBack.clear();
        }
        touchOn = on;
        touchX = x;
        touchR = std::max (0.0, R);
        setBeta (beta);
    }

    void clear()
    {
        toBr.clear();
        fromBr.clear();
        nutLoop.clear();
        hTo.clear();
        hBack.clear();
        for (auto& x : gapR)
            x.clear();
        for (auto& x : gapL)
            x.clear();
        tBr.clear();
        tNut.clear();
        for (auto& x : tGapR)
            x.clear();
        for (auto& x : tGapL)
            x.clear();
        lp = 0;
        for (auto& x : apX)
            x = 0;
        for (auto& x : apY)
            x = 0;
    }
};

// Generic modal bridge set (string-physics modes_generic.txt): signature modes A0, CBR, B1-, B1+
// plus modes fitted from the Iowa body IR. f (Hz), Q, modal mass (kg). To be replaced by the
// passive fit to the CNSM admittance (plan M3).
static const ModeData kGenericModes[] = {
    { 275.0, 20.0, 0.385830 },  { 405.0, 30.0, 0.589463 },  { 470.0, 40.0, 0.169314 },   { 540.0, 40.0, 0.147366 },
    { 630.0, 30.0, 0.252627 },  { 741.5, 20.2, 0.665660 },  { 789.2, 15.0, 0.131346 },   { 813.6, 21.9, 0.203290 },
    { 987.4, 19.1, 0.859700 },  { 1096.6, 15.0, 0.121965 }, { 1150.9, 21.7, 0.281240 },  { 1264.1, 47.7, 3.433160 },
    { 1328.8, 54.4, 5.799740 }, { 1425.7, 42.1, 1.626120 }, { 1480.0, 54.3, 2.137150 },  { 1535.2, 18.4, 0.375290 },
    { 1651.7, 40.1, 0.789210 }, { 1720.9, 53.0, 2.200850 }, { 1782.3, 30.1, 0.783300 },  { 1891.0, 50.6, 0.645450 },
    { 1958.9, 33.2, 0.359480 }, { 2078.9, 31.9, 0.144790 }, { 2170.8, 38.4, 0.247660 },  { 2271.9, 38.3, 0.221230 },
    { 2347.5, 23.7, 0.070150 }, { 2459.3, 37.9, 0.149470 }, { 2626.1, 33.3, 0.185440 },  { 2751.9, 52.6, 0.520100 },
    { 2936.1, 15.0, 0.008093 }, { 3109.2, 29.8, 0.061600 }, { 3214.8, 50.0, 0.367640 },  { 3524.9, 22.7, 0.055620 },
    { 3736.9, 59.1, 0.215670 }, { 3904.8, 15.0, 0.013355 }, { 4297.4, 33.9, 0.120830 },  { 4503.5, 58.1, 0.291340 },
    { 4642.4, 47.0, 0.256360 }, { 5504.6, 15.0, 0.016383 }, { 6099.4, 109.5, 2.963900 }, { 6497.7, 22.5, 0.770950 }
};

// The passive bridge (M3): kCnsmModes (BridgeData.h), fitted to the CNSM Stoppani violin's
// measured bridge admittance. kCnsmScale is the admittance level the Iowa pizzicato decays ask
// for (bridgefit: 0.35 fits best, 0.5 within 5 %, 1.0 is 25 % worse).
static constexpr double kCnsmScale = 0.5;

// ------------------------------------------------------------------ violin: four strings, one bridge
struct Violin
{
    Params p;
    String s[4];
    Bridge bridge;
    double fBridge = 0.0;
    double fs = 96000.0;

    // Per-string data. Impedances from typical synthetic-core tensions (G 44 N, D 43 N,
    // A 53 N, E 77 N; length 0.325 m): Z = T / c, c = 2 L f0. B from Iowa pizzicato.
    // Intrinsic decay from Iowa pizzicato fits (see findings); overwritten by fits.
    static StringData defaults (int i, int bridgeModes = 1)
    {
        static const StringData sd[4] = {
            // Pickering impedances and bending stiffness; intrinsic loss fitted to the Iowa
            // pizzicato partial decays with the CNSM bridge at admScale 0.5, Hold 0.5 (M3,
            // string-physics tools/bridgefit.py run on this violin: octavio2/data/bridge/)
            { "G", 196.00, 0.350, 1.6e-5, 6.035, 0.310, 2000.0 },
            { "D", 293.66, 0.303, 1.4e-5, 1.962, 0.281, 2000.0 },
            { "A", 440.00, 0.203, 1.3e-5, 1.853, 0.679, 3000.0 },
            { "E", 659.26, 0.173, 4.7e-5, 6.247, 1.191, 4000.0 },
        };
        static const StringData generic[4] = {
            // the same fit with the M0 generic bridge at admScale 0.5 (string-physics strings_bridge0.5.txt)
            { "G", 196.00, 0.350, 1.6e-5, 5.904, 0.274, 2000.0 },
            { "D", 293.66, 0.303, 1.4e-5, 2.099, 0.273, 2000.0 },
            { "A", 440.00, 0.203, 1.3e-5, 1.689, 0.599, 3000.0 },
            { "E", 659.26, 0.173, 4.7e-5, 3.567, 1.209, 4000.0 },
        };
        return bridgeModes == 1 ? sd[i] : generic[i];
    }

    // ---------------------------------------------------------------- M7 instrument parts
    // String sets, as scales on the synthetic set's fitted data (0 synthetic = the data above,
    // unchanged). Z = T / (2 L f0) at a fixed pitch, so it follows the tension.
    //  Gut (baroque, plain A and E, plain or wound D, wound G): about 15-25% less tension, more
    //  internal loss, most at the top (the gut's own damping: darker, quicker ring), plain gut is
    //  thick so stiffer (B ~ E d^4 / T: gut's lower modulus is more than made up by its diameter).
    //  Steel (steel core, fiddle): a little more tension, much less internal loss (bright, long ring),
    //  stiffer core.
    struct StringSet
    {
        double Z[4], B[4], lo[4], hi[4];
    };
    static const StringSet& stringSet (int set)
    {
        static const StringSet sets[3] = {
            { { 1, 1, 1, 1 }, { 1, 1, 1, 1 }, { 1, 1, 1, 1 }, { 1, 1, 1, 1 } },
            { { 0.85, 0.82, 0.8, 0.75 }, { 1.0, 2.0, 1.5, 1.2 }, { 0.7, 0.7, 0.7, 0.7 }, { 0.4, 0.4, 0.45, 0.5 } },
            { { 1.05, 1.05, 1.08, 1.05 }, { 1.3, 1.3, 1.3, 1.1 }, { 1.3, 1.3, 1.3, 1.2 }, { 1.6, 1.6, 1.5, 1.3 } },
        };
        return sets[std::clamp (set, 0, 2)];
    }
    static StringData stringData (int i, int set, int bridgeModes)
    {
        StringData d = defaults (i, bridgeModes);
        if (set == 0)
            return d;
        const StringSet& k = stringSet (set);
        d.Z *= k.Z[i];
        d.B *= k.B[i];
        d.t60lo *= k.lo[i];
        d.t60hi *= k.hi[i];
        return d;
    }
    // Switch string set (audio thread safe: no allocation; the dispersion fits are made in init)
    void setStringSet (int set)
    {
        set = std::clamp (set, 0, 2);
        if (set == strings)
            return;
        strings = set;
        for (int i = 0; i < 4; ++i)
        {
            StringData d = stringData (i, set, p.bridgeModes);
            d.f0 = s[i].d.f0; // keep the tuning
            s[i].setData (d, setM[set][i], setA[set][i]);
        }
    }
    int strings = 0;
    int setM[3][4] = {};
    double setA[3][4] = {};

    // Rosin (thermal friction law, vW26): how much it grips cold (muS), how hot it must get to
    // soften (tauG, K above ambient), how much grip is left when hot (ya), the pre-sliding layer's
    // stiffness (sigma0) and its unevenness (grain). 1 Standard = the Params defaults.
    struct Rosin
    {
        double muS, tauG, ya, sigma0, grain;
    };
    static Rosin rosin (int k)
    {
        static const Rosin r[4] = {
            { 0.95, 32.0, 0.45, 3.5e5, 0.025 }, // light: hard, pale, smooth
            { 1.05, 25.0, 0.4, 3.0e5, 0.03 }, // standard
            { 1.12, 19.0, 0.36, 2.6e5, 0.04 }, // dark: soft, sticky, grips and bites
            { 1.15, 17.0, 0.33, 2.3e5, 0.045 }, // baroque: softest, most grip and grit
        };
        return r[std::clamp (k, 0, 3)];
    }
    void setRosin (int k)
    {
        const Rosin r = rosin (k);
        p.muS = r.muS;
        p.tauG = r.tauG;
        p.ya = r.ya;
        p.sigma0 = r.sigma0;
        p.grain = r.grain;
    }

    void init()
    {
        fs = p.fs;
        for (int set = 0; set < 3; ++set)
            for (int i = 0; i < 4; ++i)
            {
                const StringData d = stringData (i, set, p.bridgeModes);
                String::fitAllpass (d.f0, d.B, fs, p, setM[set][i], setA[set][i]);
            }
        for (int i = 0; i < 4; ++i)
        {
            s[i].init (stringData (i, strings, p.bridgeModes), p);
            s[i].clear();
            coupling[i] = 1.0;
            reflect[i] = 1.0;
            unbowed[i] = 0.0;
            energy[i] = 0.0;
        }
        bridge.clear();
        bridge.modes.clear();
        bridge.Yd = 0.0;
        base.clear();
        source.clear();
        radPrev = radLow = 0.0;
        if (p.admittance)
        {
            const bool cnsm = p.bridgeModes == 1;
            const double scale = p.admScale > 0.0 ? p.admScale : (cnsm ? kCnsmScale : 0.5);
            auto addSet = [&] (const ModeData* m, size_t n)
            {
                for (size_t k = 0; k < n; ++k)
                    if (m[k].f < 0.45 * fs)
                    {
                        base.push_back ({ m[k].f, m[k].Q, m[k].m / scale });
                        source.push_back ((int) k);
                    }
            };
            if (cnsm)
                addSet (kCnsmModes, (size_t) kCnsmModeCount);
            else
                addSet (kGenericModes, sizeof (kGenericModes) / sizeof (kGenericModes[0]));
            bridge.modes.reserve (base.size());
            for (const auto& m : base)
                bridge.add (fs, m.f, m.Q, m.m);
            // the wolf's mode: the strongest (peak |Y| = Q / (m w)) below 700 Hz, or the one nearest wolfHz
            wolfMode = -1;
            double best = -1.0;
            for (size_t k = 0; k < base.size(); ++k)
            {
                const double score = p.wolfHz > 0.0 ? -std::abs (std::log (base[k].f / p.wolfHz))
                    : base[k].f < 700.0             ? base[k].Q / (base[k].m * 2 * pi * base[k].f)
                                                    : -1.0;
                if (score > best)
                {
                    best = score;
                    wolfMode = (int) k;
                }
            }
            applyBody();
        }
    }
    void setString (int i, const StringData& d) { s[i].init (d, p); }

    // Hold and Wolf retune the bridge modes (keeps their state, no allocation): call after
    // changing p.hold or p.wolf. Sympathetic is read every sample.
    void applyBody()
    {
        for (size_t k = 0; k < base.size() && k < bridge.modes.size(); ++k)
        {
            double Q = base[k].Q, m = base[k].m;
            const double f = base[k].f;
            const double x = std::clamp ((f - p.holdHz) / std::max (1.0, p.holdFade), 0.0, 1.0);
            const double reach = 0.5 + 0.5 * std::cos (pi * x); // 1 below holdHz, 0 above holdHz + holdFade
            Q /= 1.0 + std::clamp (p.hold, 0.0, 1.0) * p.holdMax * reach;
            double radScale = 1.0;
            if ((int) k == wolfMode && p.wolf > 0.0)
            {
                const double w = std::clamp (p.wolf, 0.0, 1.0);
                m /= 1.0 + w * p.wolfMass;
                Q *= 1.0 + w * p.wolfQ;
                // the wolf is the string fighting the light, sharp mode; its radiated peak stays
                // where it was (only narrower), so Wolf doesn't also turn the body's volume up
                radScale = 1.0 / ((1.0 + w * p.wolfMass) * (1.0 + w * p.wolfQ));
            }
            bridge.set ((int) k, fs, f, std::max (0.5, Q), m);
            // the modal body: how this mode radiates for the chosen violin (fitted at Hold 0, Wolf 0)
            auto& md = bridge.modes[k];
            md.ra = md.rb = 0.0;
            if (hasModalBody())
            {
                const ModalBody& mb = kModalBody[std::clamp (bodyIndex, 0, 3)];
                md.ra = mb.a[source[k]] * radScale;
                md.rb = mb.b[source[k]] * 2 * pi * f * radScale;
            }
        }
    }

    // The modal body (M3): the bridge modes also radiate the sound below the crossover, so bridge
    // and body share their poles (Maestre, Scavone & Smith 2017) and Hold and Wolf are heard
    // in the sound, not only in the strings. Each Violin choice has its own weights over the same
    // modes. radLow is the radiated signal (in the body IR's units per newton) after each tick.
    bool hasModalBody() const { return p.admittance && p.bridgeModes == 1 && ! base.empty(); }
    void setBody (int violinChoice)
    {
        if (bodyIndex == violinChoice)
            return;
        bodyIndex = violinChoice;
        applyBody();
    }
    int modalDelay() const { return kModalBody[std::clamp (bodyIndex, 0, 3)].delay; }

    // One sample. bowString < 0: no bow. Returns the total force on the bridge.
    //
    // Each string meets the bridge through an ideal transformer of ratio c (coupling: the string
    // sees c times the bridge's velocity and pushes it with c times its force, so no energy is
    // made or lost in it, whatever c does over time) after a reflection loss r <= 1 (a damping
    // finger). c = r = 1 is the plain junction. Sympathetic sets them for the idle open strings.
    double Fs[4] = { 0, 0, 0, 0 }; // each string's force on the bridge this sample (debug stems)
    double tick (const double* vBow, const double* force)
    {
        // idle open strings: not fingered, not bowed for symIdleAfter seconds, and well under the
        // loudest string (so a plucked or just-released string rings on as it is: it is the one
        // making the sound, not one answering it)
        const double sym = std::clamp (p.sympathetic, 0.0, 1.0);
        const double glide = 1.0 - std::exp (-1.0 / (0.3 * fs)); // a released string fades into idleness
        const double envA = 1.0 - std::exp (-1.0 / (0.05 * fs));
        double loudest = 0.0;
        for (int i = 0; i < 4; ++i)
        {
            energy[i] += (s[i].d.Z * s[i].aBr * s[i].aBr - energy[i]) * envA;
            loudest = std::max (loudest, energy[i]);
        }
        for (int i = 0; i < 4; ++i)
        {
            unbowed[i] = force[i] > 0.0 ? 0.0 : unbowed[i] + 1.0 / fs;
            const bool idle = ! s[i].fingered && unbowed[i] > p.symIdleAfter && energy[i] < 0.25 * loudest;
            double c = 1.0, r = 1.0;
            if (idle && sym <= 0.5)
            {
                const double x = sym / 0.5;
                c = x * p.symCoupling;
                r = 1.0 - (1.0 - x) * p.symDamp;
            }
            else if (idle)
            {
                // up to full coupling, and the idle strings lose less of their own: r g stays
                // below 1, so seen from the bridge the string is still a passive termination
                const double x = (sym - 0.5) / 0.5;
                c = p.symCoupling + x * (1.0 - p.symCoupling);
                r = std::pow (std::max (1e-6, s[i].g), -p.symLossCut * x);
            }
            coupling[i] += (c - coupling[i]) * glide;
            reflect[i] += (r - reflect[i]) * glide;
        }
        double sumZA = 0, sumZ = 0, a[4];
        for (int i = 0; i < 4; ++i)
        {
            s[i].readBridge();
            a[i] = reflect[i] * s[i].aBr;
            const double Z = p.perString ? s[i].d.Z : 0.2, c = coupling[i];
            sumZA += c * Z * a[i];
            sumZ += c * c * Z;
        }
        double F = 0, hiss = 0;
        if (p.admittance && ! bridge.modes.empty())
        {
            const double Yd = bridge.Yd;
            const double v = (2 * Yd * sumZA + bridge.past()) / (1 + Yd * sumZ);
            for (int i = 0; i < 4; ++i)
            {
                const double Z = p.perString ? s[i].d.Z : 0.2, c = coupling[i];
                const double Fi = c * Z * (2 * a[i] - c * v);
                Fs[i] = Fi;
                F += Fi;
                s[i].tick (c * v - a[i], vBow[i], force[i]);
                hiss += s[i].hiss;
            }
            bridge.update (F);
            radLow = (bridge.radA - radPrev) * fs + bridge.radB;
            radPrev = bridge.radA;
        }
        else
        {
            for (int i = 0; i < 4; ++i)
            {
                const double Z = p.perString ? s[i].d.Z : 0.2;
                Fs[i] = 2 * Z * s[i].aBr;
                F += Fs[i];
                s[i].tick (-s[i].aBr, vBow[i], force[i]);
                hiss += s[i].hiss;
            }
        }
        F += hiss; // rough-friction hiss reaches the bridge directly (string delay ignored)
        fBridge = F;
        return F;
    }

    // the bridge modes before Hold and Wolf (f, Q, mass after the set's scale)
    std::vector<ModeData> base;
    std::vector<int> source; // each mode's index in its table
    int wolfMode = -1, bodyIndex = 0;
    double radLow = 0.0, radPrev = 0.0;
    double coupling[4] = { 1, 1, 1, 1 }, reflect[4] = { 1, 1, 1, 1 }, unbowed[4] = {}, energy[4] = {};
};

// ------------------------------------------------------------------ helpers
inline void writeWav (const std::string& path, const std::vector<float>& x, int sr)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f)
        return;
    auto w32 = [&] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto w16 = [&] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f);
    w32 (36 + (uint32_t) x.size() * 4);
    std::fwrite ("WAVEfmt ", 1, 8, f);
    w32 (16);
    w16 (3);
    w16 (1);
    w32 (sr);
    w32 (sr * 4);
    w16 (4);
    w16 (32);
    std::fwrite ("data", 1, 4, f);
    w32 ((uint32_t) x.size() * 4);
    std::fwrite (x.data(), 4, x.size(), f);
    std::fclose (f);
}

// 2x decimator (windowed sinc, 63 taps), keeps every other sample
struct Decim
{
    double h[63], buf[63] = {};
    int w = 0;
    Decim()
    {
        for (int i = 0; i < 63; ++i)
        {
            const double k = i - 31, fc = 0.23;
            const double s = k == 0 ? 2 * fc : std::sin (2 * pi * fc * k) / (pi * k);
            h[i] = s * (0.42 - 0.5 * std::cos (2 * pi * i / 62) + 0.08 * std::cos (4 * pi * i / 62));
        }
    }
    void push (double x)
    {
        buf[w] = x;
        w = (w + 1) % 63;
    }
    double out() const
    {
        double y = 0;
        for (int k = 0; k < 63; ++k)
            y += h[k] * buf[(w + k) % 63];
        return y;
    }
};
} // namespace o2
