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
// together with the strings (all strings push on the same bridge point).
struct Bridge
{
    struct Mode
    {
        double b0, b2, a1, a2, z1 = 0, z2 = 0;
    };
    std::vector<Mode> modes;
    double Yd = 0.0;
    void add (double fs, double f, double Q, double mass)
    {
        const double w = 2 * pi * f, K = w / std::tan (w / (2 * fs)), a = w / Q;
        const double d0 = K * K + a * K + w * w;
        Mode m;
        m.b0 = K / (mass * d0);
        m.b2 = -m.b0;
        m.a1 = (2 * w * w - 2 * K * K) / d0;
        m.a2 = (K * K - a * K + w * w) / d0;
        modes.push_back (m);
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
        for (auto& m : modes)
        {
            const double y = m.b0 * F + m.z1;
            m.z1 = -m.a1 * y + m.z2;
            m.z2 = m.b2 * F - m.a2 * y;
        }
    }
    void clear()
    {
        for (auto& m : modes)
            m.z1 = m.z2 = 0;
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
    // bow
    int bowPoints = 4;
    double slipNoise = 0.08, slipNoiseHz = 3000.0, slipNoiseExp = 0.5, slipNoiseFade = 0.0;
    // 0: the rough friction acts on the string (couples into the slip timing: jitter);
    // 1: the same force fluctuation goes straight to the bridge (hiss without jitter)
    double slipNoiseOut = 1.0;
    double bowWidth = 0.01, hairStiffness = 110000.0, hairDamping = 10.0, grain = 0.03, grainHz = 3000.0, grainFade = 0.05;
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
    double admScale = 0.5;
    bool sympathetic = true; // open strings not bowed are free to ring
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
    double noiseGain = 1.0; // set by the player: how settled the stroke is (less hiss while the note starts)
    double damp = 0.0; // extra loss per round trip from a finger touching or lifting (0..1), set by the player
    double dBr = 2, dN = 2; // one-way bow->bridge, nut round trip
    // bridge wave
    double aBr = 0.0;
    // bow contact state
    bool stick[4] = {};
    double hairY[4] = {}, grainLp[4] = {}, xFast[4] = {}, xSlow[4] = {}, tau[4] = {};
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
    void fitDispersion()
    {
        dispM = 0;
        dispA = 0.0;
        const double f = d.f0, Bn = d.B;
        if (! P->dispersion || Bn <= 0)
            return;
        double best = 1e30;
        const int nmax = std::min (40, (int) (0.45 * fs / f));
        for (int m = 1; m <= P->maxAllpass; ++m)
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

    // A new note: the finger lands, the ear's correction starts again from zero.
    void setNote (double n) { setPitch (n, true); }

    // Continuous finger position (vibrato, slides). keepEar keeps the ear's running correction.
    void setPitch (double n, bool resetEar = false)
    {
        note = n;
        f1 = 440.0 * std::pow (2.0, (n - 69.0) / 12.0);
        // intrinsic losses, set from decay times in seconds (so the same at any rate)
        const double lo = P->perString ? d.t60lo : 2.5, hi = P->perString ? d.t60hi : 0.25, fh = P->perString ? d.fhi : 4000.0;
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
        const double fp1 = f1 * std::sqrt (1 + Bn);
        const double w = 2 * pi * fp1 / fs;
        const double pdLoss = std::atan2 (dark * std::sin (w), 1.0 - dark * std::cos (w)) / w;
        const double pdAp = M > 0 ? M * apPhaseDelay (apA, w) : 0.0;
        N = fs / fp1 - pdLoss - pdAp;
        period = fs / fp1;
        if (resetEar || ! fingered)
            fingerCents = 0.0;
        setBeta (beta);
    }

    void setBeta (double b)
    {
        beta = b;
        const int K = bowPointsNow();
        const double gap = gapSamples();
        dBr = std::max (1.0, beta * period / 2.0);
        // the nut side loses the extra delay of the bow-width gaps
        dN = std::max (1.0, N - 2.0 * dBr - 2.0 * (K - 1) * gap - period * (1.0 - std::pow (2.0, -fingerCents / 1200.0)));
    }

    int bowPointsNow() const { return std::clamp (P->bowPoints, 1, 4); }
    double gapSamples() const
    {
        const int K = bowPointsNow();
        if (K <= 1)
            return 0.0;
        const double c = 2.0 * 0.325 * d.f0; // wave speed of this string, m/s
        return std::max (1.0, P->bowWidth * fs / c / (K - 1));
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
            auto root = [&] {
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

    // Friction at one contact point. a = junction impedance seen by the friction force
    // (force f gives velocity change f / a). Returns the contact point's velocity.
    double contact (int k, double vBow, double vh, double force, double a)
    {
        if (force <= 0.0)
        {
            stick[k] = false;
            tau[k] *= 0.999;
            return vh;
        }
        if (P->friction == Friction::thermalHyp)
        {
            const double lineLen = std::max (1e-4, P->bowWidth / std::max (1, bowPointsNow()));
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
        // thermal friction after van Walstijn et al. (Acta Acustica 2026) with mu_d = mu_s (their best
        // fit) and the rosin's pre-sliding elasticity left out: friction limit mu_s * y(tau), where
        // tau is the contact temperature above ambient and y falls from 1 to ya through the
        // glass transition tauG. Heat balance per unit length of contact (their eq. 22):
        //   aT Fb dtau/dt + (bT sqrt(|v| / Fb) + cT Fb) tau = Qf = f_line |v_rel|
        const double lineLen = std::max (1e-4, P->bowWidth / std::max (1, bowPointsNow()));
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
                const double r = fadeN * P->slipNoise * std::pow (std::abs (dh) / 0.1, P->slipNoiseExp) * (wn - slipLp[k]);
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
        double x = nutLoop.read (dN);
        lp = (1 - dark) * x + dark * lp;
        x = g * fingerG * (1.0 - damp) * lp;
        for (int i = 0; i < M; ++i)
        {
            const double y = apA * x + apX[i] - apA * apY[i];
            apX[i] = x;
            apY[i] = y;
            x = y;
        }
        const double vinN = -x;

        const int K = bowPointsNow();
        const double gap = gapSamples();
        const double Zs = P->perString ? d.Z : 0.2;
        slipped = false;
        hiss = 0.0;
        const bool wasStick = stickAll;
        // Torsion: the bow drags the string's surface, so it also twists the string. Twist
        // waves travel torsionSpeed times faster with impedance torsionImpedance * Z (as seen
        // at the surface) and lose exp(-pi / Q) of their amplitude per round trip.
        const bool tors = P->torsion;
        const double Zt = Zs * P->torsionImpedance;
        const double tPeriod = period / P->torsionSpeed;
        const double tGap = K > 1 ? std::max (1.0, gap / P->torsionSpeed) : 0.0;
        const double tR = std::exp (-pi / (2.0 * P->torsionQ)); // per reflection
        const double tdB = std::max (1.0, beta * tPeriod), tdN = std::max (1.0, (1 - beta) * tPeriod - 2.0 * (K - 1) * tGap);
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
                const double c = P->hairDamping / K, kk = P->hairStiffness / K;
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
            const double dv = f / (2.0 * Zs);
            const double v = vh + f * Ys;
            if (k == 0)
                toBr.push (fromN[k] + dv);
            else
                gapL[k - 1].push (fromN[k] + dv);
            if (k == K - 1)
                nutLoop.push (fromB[k] + dv);
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

    // Pluck: set an initial triangular displacement at fraction p from the bridge
    // (as velocity waves: a step in each direction), amplitude in m/s-equivalent.
    void pluck (double p, double amp)
    {
        // simplest: an impulse of velocity at the pluck point, both directions
        // (a 'plucked' spectrum ~ sin(n pi p)/n after integration through the body is close enough)
        const double d1 = std::max (1.0, p * N / 2.0);
        (void) d1;
        for (int i = 0; i < 1; ++i)
        {
            toBr.push (amp);
            nutLoop.push (amp);
        }
    }

    void clear()
    {
        toBr.clear();
        fromBr.clear();
        nutLoop.clear();
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
struct ModeData
{
    double f, Q, m;
};
static const ModeData kGenericModes[] = {
    { 275.0, 20.0, 0.385830 },
    { 405.0, 30.0, 0.589463 },
    { 470.0, 40.0, 0.169314 },
    { 540.0, 40.0, 0.147366 },
    { 630.0, 30.0, 0.252627 },
    { 741.5, 20.2, 0.665660 },
    { 789.2, 15.0, 0.131346 },
    { 813.6, 21.9, 0.203290 },
    { 987.4, 19.1, 0.859700 },
    { 1096.6, 15.0, 0.121965 },
    { 1150.9, 21.7, 0.281240 },
    { 1264.1, 47.7, 3.433160 },
    { 1328.8, 54.4, 5.799740 },
    { 1425.7, 42.1, 1.626120 },
    { 1480.0, 54.3, 2.137150 },
    { 1535.2, 18.4, 0.375290 },
    { 1651.7, 40.1, 0.789210 },
    { 1720.9, 53.0, 2.200850 },
    { 1782.3, 30.1, 0.783300 },
    { 1891.0, 50.6, 0.645450 },
    { 1958.9, 33.2, 0.359480 },
    { 2078.9, 31.9, 0.144790 },
    { 2170.8, 38.4, 0.247660 },
    { 2271.9, 38.3, 0.221230 },
    { 2347.5, 23.7, 0.070150 },
    { 2459.3, 37.9, 0.149470 },
    { 2626.1, 33.3, 0.185440 },
    { 2751.9, 52.6, 0.520100 },
    { 2936.1, 15.0, 0.008093 },
    { 3109.2, 29.8, 0.061600 },
    { 3214.8, 50.0, 0.367640 },
    { 3524.9, 22.7, 0.055620 },
    { 3736.9, 59.1, 0.215670 },
    { 3904.8, 15.0, 0.013355 },
    { 4297.4, 33.9, 0.120830 },
    { 4503.5, 58.1, 0.291340 },
    { 4642.4, 47.0, 0.256360 },
    { 5504.6, 15.0, 0.016383 },
    { 6099.4, 109.5, 2.963900 },
    { 6497.7, 22.5, 0.770950 }
};

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
    static StringData defaults (int i)
    {
        static const StringData sd[4] = {
            // Pickering impedances and bending stiffness; intrinsic loss fitted to the Iowa
            // pizzicato decays with the bridge at admScale 0.5 (string-physics strings_bridge0.5.txt)
            { "G", 196.00, 0.350, 1.6e-5, 5.904, 0.274, 2000.0 },
            { "D", 293.66, 0.303, 1.4e-5, 2.099, 0.273, 2000.0 },
            { "A", 440.00, 0.203, 1.3e-5, 1.689, 0.599, 3000.0 },
            { "E", 659.26, 0.173, 4.7e-5, 3.567, 1.209, 4000.0 },
        };
        return sd[i];
    }

    void init()
    {
        fs = p.fs;
        for (int i = 0; i < 4; ++i)
        {
            s[i].init (defaults (i), p);
            s[i].clear();
        }
        bridge.clear();
        bridge.modes.clear();
        bridge.Yd = 0.0;
        if (p.admittance)
            for (const auto& m : kGenericModes)
                if (m.f < 0.45 * fs)
                    bridge.add (fs, m.f, m.Q, m.m / p.admScale);
    }
    void setString (int i, const StringData& d)
    {
        s[i].init (d, p);
    }

    // One sample. bowString < 0: no bow. Returns the total force on the bridge.
    double tick (const double* vBow, const double* force)
    {
        double sumZA = 0, sumZ = 0;
        for (int i = 0; i < 4; ++i)
        {
            s[i].readBridge();
            const double Z = p.perString ? s[i].d.Z : 0.2;
            sumZA += Z * s[i].aBr;
            sumZ += Z;
        }
        double F = 0, hiss = 0;
        if (p.admittance && ! bridge.modes.empty())
        {
            const double Yd = bridge.Yd;
            const double v = (2 * Yd * sumZA + bridge.past()) / (1 + Yd * sumZ);
            for (int i = 0; i < 4; ++i)
            {
                const double Z = p.perString ? s[i].d.Z : 0.2;
                const double Fi = Z * (2 * s[i].aBr - v);
                F += Fi;
                s[i].tick (v - s[i].aBr, vBow[i], force[i]);
                hiss += s[i].hiss;
            }
            bridge.update (F);
        }
        else
        {
            for (int i = 0; i < 4; ++i)
            {
                const double Z = p.perString ? s[i].d.Z : 0.2;
                F += 2 * Z * s[i].aBr;
                s[i].tick (-s[i].aBr, vBow[i], force[i]);
                hiss += s[i].hiss;
            }
        }
        F += hiss; // rough-friction hiss reaches the bridge directly (string delay ignored)
        fBridge = F;
        return F;
    }
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
