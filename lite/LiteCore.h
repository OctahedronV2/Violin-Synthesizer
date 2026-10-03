// Octavio Lite: the sound engine.
//
// One header, freestanding C++17: no standard library, no allocation, no
// system calls. The same code runs in the offline renderer (lite.cpp), in the
// browser (compiled to WebAssembly, lite/web) and, later, inside the VST.
// Because it carries its own maths functions, all three produce the same
// samples for the same input.
//
// Signal path, per host sample:
//   notes -> player (voices, strokes, slurs) -> left hand (pitch, vibrato)
//   -> bowed waveguide string per voice at 2x the host rate
//   -> decimate -> body impulse response (partitioned FFT convolution)
//   -> room (8-line feedback delay network) -> gain -> stereo out
//
// Parameters are listed once, in kParams below; every front end (sliders,
// renderer flags, VST parameters) reads that table.

#pragma once

namespace lite
{
// ------------------------------------------------------------------ maths
namespace m
{
constexpr double pi = 3.14159265358979323846;
constexpr double ln2 = 0.69314718055994530942;

inline double abs (double x)
{
    return x < 0 ? -x : x;
}
inline double min (double a, double b)
{
    return a < b ? a : b;
}
inline double max (double a, double b)
{
    return a > b ? a : b;
}
inline double clamp (double x, double lo, double hi)
{
    return x < lo ? lo : x > hi ? hi : x;
}
inline double sqrt (double x)
{
    return __builtin_sqrt (x);
}
inline double floor (double x)
{
    const double t = (double) (long long) x;
    return t > x ? t - 1.0 : t;
}

// 2^x: split into integer and fraction, polynomial for the fraction.
inline double exp2 (double x)
{
    x = clamp (x, -1000.0, 1000.0);
    const double fi = floor (x + 0.5);
    const double f = (x - fi) * ln2; // |f| <= ln2/2
    double s = 1.0, term = 1.0;
    for (int k = 1; k <= 12; ++k)
    {
        term *= f / k;
        s += term;
    }
    const long long e = (long long) fi + 1023;
    if (e <= 0)
        return 0.0;
    const unsigned long long bits = (unsigned long long) e << 52;
    return s * __builtin_bit_cast (double, bits);
}
inline double exp (double x)
{
    return exp2 (x * 1.44269504088896340736);
}

inline double log (double x)
{
    if (x <= 0.0)
        return -1.0e300;
    unsigned long long b = __builtin_bit_cast (unsigned long long, x);
    long long e = (long long) ((b >> 52) & 0x7ff) - 1023;
    b = (b & 0x000fffffffffffffULL) | 0x3ff0000000000000ULL;
    double mant = __builtin_bit_cast (double, b); // [1, 2)
    if (mant > 1.41421356237309504880)
    {
        mant *= 0.5;
        ++e;
    }
    const double s = (mant - 1.0) / (mant + 1.0), s2 = s * s;
    double sum = 0.0, p = s;
    for (int k = 1; k <= 23; k += 2)
    {
        sum += p / k;
        p *= s2;
    }
    return 2.0 * sum + e * ln2;
}
inline double pow (double a, double b)
{
    return exp (b * log (a));
}
inline double pow2 (double x)
{
    return exp2 (x);
}
inline double db (double d)
{
    return exp2 (d * 0.16609640474436811739);
} // 10^(d/20)

inline double sin (double x)
{
    x -= 2.0 * pi * floor (x / (2.0 * pi) + 0.5); // [-pi, pi]
    if (x > pi / 2)
        x = pi - x;
    else if (x < -pi / 2)
        x = -pi - x;
    const double x2 = x * x;
    double term = x, s = x;
    for (int k = 1; k <= 8; ++k)
    {
        term *= -x2 / ((2 * k) * (2 * k + 1));
        s += term;
    }
    return s;
}
inline double cos (double x)
{
    return sin (x + pi / 2);
}

inline double atan (double x)
{
    const bool neg = x < 0;
    if (neg)
        x = -x;
    const bool inv = x > 1.0;
    if (inv)
        x = 1.0 / x;
    // two halvings: atan(x) = 2 atan(x / (1 + sqrt(1 + x^2)))
    x = x / (1.0 + sqrt (1.0 + x * x));
    x = x / (1.0 + sqrt (1.0 + x * x));
    const double x2 = x * x;
    double term = x, s = x;
    for (int k = 1; k <= 10; ++k)
    {
        term *= -x2;
        s += term / (2 * k + 1);
    }
    s *= 4.0;
    if (inv)
        s = pi / 2 - s;
    return neg ? -s : s;
}
inline double atan2 (double y, double x)
{
    if (x > 0)
        return atan (y / x);
    if (x < 0)
        return y >= 0 ? atan (y / x) + pi : atan (y / x) - pi;
    return y > 0 ? pi / 2 : y < 0 ? -pi / 2 : 0.0;
}
inline double tanh (double x)
{
    if (x > 20)
        return 1.0;
    if (x < -20)
        return -1.0;
    const double e = exp (2.0 * x);
    return (e - 1.0) / (e + 1.0);
}
inline double midiHz (double n)
{
    return 440.0 * exp2 ((n - 69.0) / 12.0);
}
} // namespace m

// ------------------------------------------------------------------ parameters
enum Param
{
    pBowSoft,
    pBowLoud,
    pPressure,
    pContact,
    pAttack,
    pRelease,
    pLift,
    pGrain,
    pGrainColour,
    pMuStatic,
    pMuDynamic,
    pFrictionSoft,
    pRing,
    pDark,
    pImpedance,
    pImpedanceSlope,
    pVibDepth,
    pVibRate,
    pVibDelay,
    pVibFade,
    pVibRateJitter,
    pVibDepthJitter,
    pGlide,
    pLandCents,
    pLandTime,
    pIntonation,
    pBody,
    pRoom,
    pRoomTime,
    pRoomDamp,
    pGain,
    numParams
};

struct ParamInfo
{
    const char* id; // stable name: renderer flags, saved settings, VST parameter ids
    const char* group;
    const char* label;
    const char* unit;
    double min, max, def;
};

inline constexpr ParamInfo kParams[numParams] = {
    { "bowSoft", "Bow", "Bow speed, soft notes", "m/s", 0.02, 0.4, 0.08 },
    { "bowLoud", "Bow", "Bow speed, loud notes", "m/s", 0.1, 1.2, 0.53 },
    { "pressure", "Bow", "Bow pressure", "x Fmax", 0.05, 1.5, 0.75 },
    { "contact", "Bow", "Contact point (from bridge)", "of string", 0.04, 0.3, 0.12 },
    { "attack", "Bow", "Attack", "s", 0.005, 0.4, 0.06 },
    { "release", "Bow", "Release", "s", 0.01, 0.6, 0.12 },
    { "lift", "Bow", "Weight lifts on release", "", 0.0, 4.0, 1.0 },
    { "grain", "Rosin", "Grain amount", "", 0.0, 0.5, 0.04 },
    { "grainColour", "Rosin", "Grain colour", "Hz", 300.0, 15000.0, 3000.0 },
    { "muS", "Rosin", "Static friction", "", 0.3, 1.5, 0.8 },
    { "muD", "Rosin", "Sliding friction", "", 0.05, 0.7, 0.3 },
    { "v0", "Rosin", "Friction softness", "m/s", 0.01, 0.6, 0.1 },
    { "ring", "String", "Ring time", "s", 0.2, 10.0, 2.5 },
    { "dark", "String", "Darkness", "", 0.0, 0.85, 0.25 },
    { "impedance", "String", "Impedance", "kg/s", 0.05, 0.6, 0.2 },
    { "impedanceSlope", "String", "Heavier low strings", "", 0.0, 1.0, 0.35 },
    { "vibDepth", "Left hand", "Vibrato width", "cents", 0.0, 100.0, 36.0 },
    { "vibRate", "Left hand", "Vibrato rate", "Hz", 3.0, 9.0, 5.6 },
    { "vibDelay", "Left hand", "Vibrato delay", "s", 0.0, 1.0, 0.25 },
    { "vibFade", "Left hand", "Vibrato fade-in", "s", 0.01, 1.5, 0.35 },
    { "vibRateJitter", "Left hand", "Vibrato rate wobble", "", 0.0, 0.4, 0.06 },
    { "vibDepthJitter", "Left hand", "Vibrato width wobble", "", 0.0, 1.0, 0.15 },
    { "glide", "Left hand", "Slur glide", "s", 0.002, 0.25, 0.03 },
    { "landCents", "Left hand", "Note lands flat by", "cents", -40.0, 0.0, -8.0 },
    { "landTime", "Left hand", "Landing settles in", "s", 0.005, 0.4, 0.06 },
    { "intonation", "Left hand", "Tuning spread", "cents", 0.0, 25.0, 6.0 },
    { "body", "Body & room", "Body", "", 0.0, 1.0, 1.0 },
    { "room", "Body & room", "Room amount", "", 0.0, 1.0, 0.3 },
    { "roomTime", "Body & room", "Room decay", "s", 0.2, 6.0, 1.8 },
    { "roomDamp", "Body & room", "Room darkness", "", 0.0, 0.95, 0.4 },
    { "gain", "Body & room", "Output level", "dB", -24.0, 24.0, 0.0 },
};

// ------------------------------------------------------------------ building blocks
struct Rng
{
    unsigned s = 1;
    double uni() // -1..1
    {
        s = s * 1664525u + 1013904223u;
        return (s >> 8) / double (1u << 23) - 1.0;
    }
    double gauss() { return uni() + uni() + uni(); } // variance 1
};

// Fractional delay line with a cubic Lagrange read.
struct Delay
{
    static constexpr int size = 16384, mask = size - 1; // 0.17 s at 96 kHz: below G1
    double buf[size] = {};
    int w = 0;
    void clear()
    {
        for (auto& x : buf)
            x = 0.0;
        w = 0;
    }
    void push (double x)
    {
        buf[w] = x;
        w = (w + 1) & mask;
    }
    double read (double d) const // d >= 2 samples back
    {
        d = m::clamp (d, 2.0, size - 4.0);
        const int i = (int) d;
        const double f = d - i;
        auto at = [&] (int k) { return buf[(w - k) & mask]; };
        const double xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
        const double c0 = -f * (f - 1) * (f - 2) / 6.0, c1 = (f + 1) * (f - 1) * (f - 2) / 2.0;
        const double c2 = -(f + 1) * f * (f - 2) / 2.0, c3 = (f + 1) * f * (f - 1) / 6.0;
        return c0 * xm1 + c1 * x0 + c2 * x1 + c3 * x2;
    }
};

// Bow-string friction, hyperbolic curve. Returns the string's velocity at the bow.
inline double
friction (double vBow, double vH, double force, double Z, double muS, double muD, double v0, bool& sticking)
{
    if (force <= 0.0)
    {
        sticking = false;
        return vH;
    }
    const double dh = vBow - vH, adh = m::abs (dh), a = 2.0 * Z;
    auto root = [&]
    {
        const double b = a * v0 + force * muD - a * adh, c = (force * muS - a * adh) * v0;
        const double disc = b * b - 4 * a * c;
        if (disc < 0)
            return -1.0;
        const double r = (-b + m::sqrt (disc)) / (2 * a);
        return r > 0 ? r : -1.0;
    };
    double slip;
    if (a * adh <= muS * force)
    {
        if (sticking)
            return vBow;
        slip = root();
        if (slip < 0)
        {
            sticking = true;
            return vBow;
        }
    }
    else
        slip = root();
    sticking = false;
    return vBow - (dh > 0 ? slip : -slip);
}

// ------------------------------------------------------------------ voice: one bowed string
struct Voice
{
    // player state
    int note = -1;
    double fromNote = 60, toNote = 60, glide = 1.0;
    double vel = 0.5, dir = 1.0, env = 0.0;
    bool held = false, active = false;
    double noteTime = 0.0, releaseTime = 0.0, lastStart = -1.0e9, lastUsed = -1.0e9;
    double cents = 0.0, landCents = 0.0;
    double vibPhase = 0.0, vibRateK = 1.0, vibDepthK = 1.0, cycleRate = 1.0, cycleDepth = 1.0;
    // string state
    Delay bridge, nut;
    double lp = 0.0, hair = 0.0, hair2 = 0.0;
    bool stick = false;
    Rng rng;
    // control-rate values
    double N = 100.0, g = 0.99, Z = 0.2;
    int ctl = 0;

    void reset()
    {
        bridge.clear();
        nut.clear();
        lp = hair = hair2 = 0.0;
        stick = false;
        env = 0.0;
        active = held = false;
        note = -1;
    }

    void start (int n, double v, bool slur, double now, const double* p)
    {
        if (slur && active)
        {
            fromNote = currentNote();
            glide = 0.0;
        }
        else
        {
            fromNote = n;
            glide = 1.0;
            dir = -dir; // a new stroke turns the bow
            vibPhase = 0.5 + 0.5 * rng.uni();
            vibRateK = 1.0 + 0.05 * rng.uni();
            vibDepthK = 1.0 + 0.15 * rng.uni();
        }
        toNote = n;
        note = n;
        vel = v;
        noteTime = 0.0;
        cents = p[pIntonation] * rng.gauss();
        landCents = p[pLandCents] * (1.0 + 0.4 * rng.uni());
        held = active = true;
        releaseTime = 0.0;
        lastStart = lastUsed = now;
        ctl = 0;
    }
    void stop() { held = false; }
    double currentNote() const
    {
        const double c = 0.5 - 0.5 * m::cos (m::pi * m::min (glide, 1.0));
        return fromNote + (toNote - fromNote) * c;
    }

    // One sample at the internal rate fs.
    double process (const double* p, double fs, double dyn)
    {
        if (! active)
            return 0.0;
        const double dt = 1.0 / fs;
        noteTime += dt;
        if (glide < 1.0)
            glide += dt / p[pGlide];
        if (held)
            env = m::min (1.0, env + dt / p[pAttack]);
        else
        {
            releaseTime += dt;
            env = m::max (0.0, env - dt / p[pRelease]);
        }

        // Vibrato phase advances every sample; each cycle gets its own rate and width.
        const double vibAmt = m::clamp ((noteTime - p[pVibDelay]) / p[pVibFade], 0.0, 1.0);
        vibPhase += dt * p[pVibRate] * vibRateK * cycleRate;
        if (vibPhase >= 1.0)
        {
            vibPhase -= 1.0;
            cycleRate = 1.0 + p[pVibRateJitter] * rng.uni();
            cycleDepth = m::max (0.0, 1.0 + p[pVibDepthJitter] * rng.uni());
        }

        // Pitch and string constants at control rate (every 16 samples).
        if (ctl-- <= 0)
        {
            ctl = 15;
            const double shape = 0.5 - 0.5 * m::cos (m::pi * vibAmt);
            const double vib = 0.5 * p[pVibDepth] * vibDepthK * cycleDepth * shape * m::sin (2 * m::pi * vibPhase);
            const double land = landCents * m::exp (-noteTime / p[pLandTime]);
            const double f0 = m::midiHz (currentNote()) * m::pow2 ((cents + vib + land) / 1200.0);
            const double a = p[pDark];
            g = m::pow (10.0, -3.0 / (p[pRing] * f0));
            const double w = 2 * m::pi * f0 / fs;
            const double pd = m::atan2 (a * m::sin (w), 1.0 - a * m::cos (w)) / w; // loss filter's delay
            N = fs / f0 - pd;
            Z = p[pImpedance] * m::pow (440.0 / f0, p[pImpedanceSlope]);
        }

        const double beta = p[pContact], a = p[pDark];
        const double muS = p[pMuStatic], muD = m::min (p[pMuDynamic], muS - 0.01);
        const double vb = (p[pBowSoft] + (p[pBowLoud] - p[pBowSoft]) * vel) * dyn * env;
        const double lift = held ? 1.0 : m::pow (m::max (env, 1.0e-9), p[pLift]);
        const double force = p[pPressure] * 2.0 * Z * vb / (beta * (muS - muD)) * lift;

        // Rosin grain: band-limited noise on the bow's motion, in proportion to its speed.
        const double hc = m::exp (-2 * m::pi * p[pGrainColour] / fs);
        hair = hc * hair + (1 - hc) * rng.uni();
        hair2 = hc * hair2 + (1 - hc) * hair;
        const double grain = p[pGrain] * vb * (hair - hair2) * 12.0;

        const double inB = bridge.read (beta * N), inN = nut.read ((1.0 - beta) * N);
        lp = (1 - a) * inB + a * lp;
        const double vinB = -g * lp, vinN = -inN;
        const double vh = vinB + vinN;
        const double v = friction (dir * vb + grain, vh, force, Z, muS, muD, p[pFrictionSoft], stick);
        const double dv = v - vh;
        bridge.push (vinN + dv);
        nut.push (vinB + dv);

        if (! held && releaseTime > p[pRing] * 2.5)
            reset();
        return inB - vinB; // force on the bridge (scaled later)
    }
};

// ------------------------------------------------------------------ body: partitioned FFT convolution
struct Body
{
    static constexpr int B = 128, F = 256, maxParts = 192; // up to 24576 taps (0.5 s at 48 kHz)
    struct C
    {
        float re, im;
    };
    C tw[F / 2];
    int rev[F];
    C H[maxParts][F / 2 + 1];
    C X[maxParts][F / 2 + 1];
    float last[B] = {}, inBlock[B] = {}, outBlock[B] = {};
    int parts = 0, head = 0, pos = 0;
    float irBuf[maxParts * B] = {}; // loaded by the host
    int irLength = 0;

    void initTables()
    {
        for (int k = 0; k < F / 2; ++k)
            tw[k] = { (float) m::cos (-2 * m::pi * k / F), (float) m::sin (-2 * m::pi * k / F) };
        for (int i = 0; i < F; ++i)
        {
            int r = 0;
            for (int b = 0; b < 8; ++b)
                if (i & (1 << b))
                    r |= 1 << (7 - b);
            rev[i] = r;
        }
    }
    void fft (C* a, bool inverse) const
    {
        for (int i = 0; i < F; ++i)
            if (i < rev[i])
            {
                const C t = a[i];
                a[i] = a[rev[i]];
                a[rev[i]] = t;
            }
        for (int len = 2; len <= F; len <<= 1)
        {
            const int step = F / len;
            for (int i = 0; i < F; i += len)
                for (int k = 0; k < len / 2; ++k)
                {
                    C w = tw[k * step];
                    if (inverse)
                        w.im = -w.im;
                    const C u = a[i + k], x = a[i + k + len / 2];
                    const C v = { x.re * w.re - x.im * w.im, x.re * w.im + x.im * w.re };
                    a[i + k] = { u.re + v.re, u.im + v.im };
                    a[i + k + len / 2] = { u.re - v.re, u.im - v.im };
                }
        }
    }
    // The IR is in irBuf[0..length), at the host rate.
    void load (int length)
    {
        irLength = length < 0 ? 0 : length > maxParts * B ? maxParts * B : length;
        parts = (irLength + B - 1) / B;
        for (int pIdx = 0; pIdx < parts; ++pIdx)
        {
            C a[F];
            for (int i = 0; i < F; ++i)
            {
                const int j = pIdx * B + i;
                a[i] = { i < B && j < irLength ? irBuf[j] : 0.0f, 0.0f };
            }
            fft (a, false);
            for (int k = 0; k <= F / 2; ++k)
                H[pIdx][k] = a[k];
        }
        clear();
    }
    void clear()
    {
        for (auto& part : X)
            for (auto& c : part)
                c = { 0, 0 };
        for (int i = 0; i < B; ++i)
            last[i] = inBlock[i] = outBlock[i] = 0.0f;
        head = pos = 0;
    }
    void block()
    {
        C a[F];
        for (int i = 0; i < B; ++i)
        {
            a[i] = { last[i], 0 };
            a[B + i] = { inBlock[i], 0 };
            last[i] = inBlock[i];
        }
        fft (a, false);
        head = (head + maxParts - 1) % maxParts;
        for (int k = 0; k <= F / 2; ++k)
            X[head][k] = a[k];
        C y[F];
        for (int k = 0; k <= F / 2; ++k)
        {
            float re = 0, im = 0;
            for (int pIdx = 0; pIdx < parts; ++pIdx)
            {
                const C& x = X[(head + pIdx) % maxParts][k];
                const C& h = H[pIdx][k];
                re += x.re * h.re - x.im * h.im;
                im += x.re * h.im + x.im * h.re;
            }
            y[k] = { re, im };
        }
        for (int k = 1; k < F / 2; ++k)
            y[F - k] = { y[k].re, -y[k].im };
        fft (y, true);
        for (int i = 0; i < B; ++i)
            outBlock[i] = y[B + i].re / F;
    }
    // One sample in, one out, B samples late.
    float process (float x)
    {
        if (parts == 0)
            return x;
        inBlock[pos] = x;
        const float y = outBlock[pos];
        if (++pos == B)
        {
            block();
            pos = 0;
        }
        return y;
    }
};

// ------------------------------------------------------------------ room: feedback delay network
struct Room
{
    static constexpr int lines = 8, size = 16384, mask = size - 1;
    float buf[lines][size] = {};
    int w = 0, len[lines] = {};
    float lpState[lines] = {}, gain[lines] = {};
    float pre[4096] = {};
    int preW = 0, preLen = 480;
    double fs = 48000.0, appliedTime = -1.0;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        constexpr double ms[lines] = { 29.7, 37.1, 41.1, 43.7, 53.3, 59.9, 67.7, 79.3 };
        for (int i = 0; i < lines; ++i)
            len[i] = (int) m::min (ms[i] * 0.001 * fs, size - 1.0);
        preLen = (int) m::min (0.012 * fs, 4095.0);
        appliedTime = -1.0;
        clear();
    }
    void clear()
    {
        for (auto& l : buf)
            for (auto& x : l)
                x = 0.0f;
        for (auto& x : pre)
            x = 0.0f;
        for (auto& s : lpState)
            s = 0.0f;
    }
    void process (float x, double time, double damp, float& l, float& r)
    {
        if (time != appliedTime)
        {
            for (int i = 0; i < lines; ++i)
                gain[i] = (float) m::pow (10.0, -3.0 * len[i] / (time * fs));
            appliedTime = time;
        }
        pre[preW] = x;
        const float in = pre[(preW - preLen) & 4095];
        preW = (preW + 1) & 4095;
        float o[lines];
        for (int i = 0; i < lines; ++i)
        {
            float y = buf[i][(w - len[i]) & mask];
            lpState[i] = (float) ((1.0 - damp) * y + damp * lpState[i]);
            o[i] = lpState[i] * gain[i];
        }
        // 8-point Hadamard mix, energy preserving
        for (int h = 1; h < lines; h <<= 1)
            for (int i = 0; i < lines; i += h << 1)
                for (int j = i; j < i + h; ++j)
                {
                    const float a = o[j], b = o[j + h];
                    o[j] = a + b;
                    o[j + h] = a - b;
                }
        const float norm = 0.35355339f; // 1/sqrt(8)
        for (int i = 0; i < lines; ++i)
            buf[i][w] = o[i] * norm + ((i & 1) ? -in : in) * 0.5f;
        w = (w + 1) & mask;
        l = r = 0.0f;
        for (int i = 0; i < lines; ++i)
            (i & 1 ? r : l) += buf[i][(w - 1 - len[i] / 2) & mask];
        l *= 0.5f;
        r *= 0.5f;
    }
};

// ------------------------------------------------------------------ the engine
struct Engine
{
    static constexpr int maxVoices = 4;
    static constexpr double chordWindow = 0.03; // notes starting this close together form a chord
    static constexpr double outputScale = 0.45; // calibrated in lite.cpp: default A4 at about -20 dBFS RMS

    double params[numParams];
    double hostRate = 48000.0, fs = 96000.0, now = 0.0, dynamics = 1.0;
    Voice voices[maxVoices];
    int noteVoice[128];
    // decimator: 2x -> 1x, half-band low-pass
    static constexpr int decTaps = 63;
    double decH[decTaps];
    double decBuf[decTaps] = {};
    int decW = 0;
    Body body;
    Room room;
    float dryDelay[Body::B] = {};
    int dryPos = 0;

    void prepare (double sampleRate)
    {
        hostRate = sampleRate;
        fs = 2.0 * sampleRate;
        for (int i = 0; i < numParams; ++i)
            params[i] = kParams[i].def;
        for (int i = 0; i < maxVoices; ++i)
        {
            voices[i].rng.s = 0x9e3779b9u * (unsigned) (i + 1);
            voices[i].reset();
        }
        for (auto& v : noteVoice)
            v = -1;
        for (int i = 0; i < decTaps; ++i)
        {
            const double k = i - (decTaps - 1) / 2.0, fc = 0.21; // 20 kHz at 96 kHz
            const double sinc = k == 0 ? 2 * fc : m::sin (2 * m::pi * fc * k) / (m::pi * k);
            decH[i] = sinc
                * (0.42 - 0.5 * m::cos (2 * m::pi * i / (decTaps - 1)) + 0.08 * m::cos (4 * m::pi * i / (decTaps - 1)));
        }
        body.initTables();
        body.load (body.irLength);
        room.prepare (sampleRate);
        now = 0.0;
        dynamics = 1.0;
    }

    void setParam (int i, double v)
    {
        if (i >= 0 && i < numParams)
            params[i] = m::clamp (v, kParams[i].min, kParams[i].max);
    }

    void noteOn (int n, double velocity)
    {
        if (n < 55 || n > 103)
            return; // violin range; lower notes are keyswitches
        if (velocity <= 0.0)
        {
            noteOff (n);
            return;
        }
        if (noteVoice[n] >= 0)
            noteOff (n);
        // A note starting with another one is a chord: its own voice. Otherwise a note
        // that is still held means a slur: the new note takes over that voice's bow.
        bool chord = false;
        int newest = -1;
        for (int k = 0; k < maxVoices; ++k)
            if (voices[k].held)
            {
                if (now - voices[k].lastStart < chordWindow)
                    chord = true;
                if (newest < 0 || voices[k].lastStart > voices[newest].lastStart)
                    newest = k;
            }
        int v = -1;
        bool slur = false;
        if (! chord && newest >= 0)
        {
            v = newest;
            slur = true;
            for (int k = 0; k < 128; ++k)
                if (noteVoice[k] == v)
                    noteVoice[k] = -1;
        }
        if (v < 0)
        {
            // The line stays on the most recent free voice; chord notes take the oldest.
            for (int k = 0; k < maxVoices; ++k)
                if (! voices[k].held
                    && (v < 0
                        || (chord ? voices[k].lastUsed < voices[v].lastUsed : voices[k].lastUsed > voices[v].lastUsed)))
                    v = k;
            if (v < 0)
                v = 0;
        }
        voices[v].start (n, velocity, slur, now, params);
        noteVoice[n] = v;
    }

    void noteOff (int n)
    {
        if (n < 0 || n > 127 || noteVoice[n] < 0)
            return;
        voices[noteVoice[n]].stop();
        noteVoice[n] = -1;
    }

    void allNotesOff()
    {
        for (int n = 0; n < 128; ++n)
            noteOff (n);
    }

    void controller (int cc, double value) // value 0..1
    {
        if (cc == 11 || cc == 2)
            dynamics = 0.25 + 0.75 * value;
        else if (cc == 123 || cc == 120)
            allNotesOff();
    }

    void process (float* left, float* right, int n)
    {
        const double* p = params;
        const double bodyMix = p[pBody], wet = p[pRoom];
        const double out = m::db (p[pGain]) * outputScale;
        for (int i = 0; i < n; ++i)
        {
            double y = 0.0;
            for (int s = 0; s < 2; ++s)
            {
                double x = 0.0;
                for (auto& v : voices)
                    x += v.process (p, fs, dynamics);
                decBuf[decW] = x;
                decW = (decW + 1) % decTaps;
                if (s == 1)
                    for (int k = 0; k < decTaps; ++k)
                        y += decH[k] * decBuf[(decW + k) % decTaps];
            }
            now += 1.0 / hostRate;
            const float dry = (float) y;
            const float b = body.process (dry);
            const float delayedDry = body.parts > 0 ? dryDelay[dryPos] : dry; // lines the dry sound up with the body's
            dryDelay[dryPos] = dry;
            dryPos = (dryPos + 1) % Body::B;
            const float mono = (float) (bodyMix * b + (1.0 - bodyMix) * delayedDry * 0.25);
            float rl, rr;
            room.process (mono, p[pRoomTime], p[pRoomDamp], rl, rr);
            left[i] = (float) (out * ((1.0 - wet) * mono + wet * rl));
            right[i] = (float) (out * ((1.0 - wet) * mono + wet * rr));
        }
    }
};
} // namespace lite
