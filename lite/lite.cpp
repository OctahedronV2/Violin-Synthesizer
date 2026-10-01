// Octavio Lite: a small, self-contained violin engine for quick sound iteration.
//
//   g++ -O2 -std=c++17 -o lite lite.cpp
//   ./lite in.mid out.wav [name=value ...]
//
// Signal path (everything is in this one file, no dependencies):
//   MIDI -> player (voices, bow strokes, slurs, vibrato)
//        -> bowed waveguide string per voice at 96 kHz (friction junction + loss filter)
//        -> decimate to 48 kHz -> body impulse response -> room impulse response -> wav
//
// Every tunable number is a named parameter (see P(...) calls); pass name=value to change it.

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double FS = 96000.0; // string rate
constexpr double OUT_FS = 48000.0;

std::map<std::string, double> params;
double P (const char* name, double fallback)
{
    auto it = params.find (name);
    return it == params.end() ? fallback : it->second;
}

double midiHz (double n) { return 440.0 * std::pow (2.0, (n - 69.0) / 12.0); }

// ---------------------------------------------------------------- random
struct Rng
{
    uint32_t s;
    double uni() // -1..1
    {
        s = s * 1664525u + 1013904223u;
        return (s >> 8) / double (1u << 23) - 1.0;
    }
    double gauss() { return (uni() + uni() + uni()) * 1.0; } // about unit variance
};

// ---------------------------------------------------------------- MIDI
struct NoteEvent
{
    double on, off;
    int note;
    double vel; // 0..1
};
struct CcEvent
{
    double t;
    int cc;
    double value;
};

bool readMidi (const std::string& path, std::vector<NoteEvent>& notes, std::vector<CcEvent>& ccs)
{
    std::ifstream f (path, std::ios::binary);
    if (! f)
        return false;
    std::vector<uint8_t> d ((std::istreambuf_iterator<char> (f)), {});
    size_t p = 0;
    auto u32 = [&] { uint32_t v = (d[p] << 24) | (d[p + 1] << 16) | (d[p + 2] << 8) | d[p + 3]; p += 4; return v; };
    auto u16 = [&] { uint32_t v = (d[p] << 8) | d[p + 1]; p += 2; return v; };
    if (d.size() < 14 || std::memcmp (&d[0], "MThd", 4) != 0)
        return false;
    p = 4;
    const auto hlen = u32();
    u16();
    const int ntracks = u16();
    const int division = u16();
    p = 8 + hlen;

    struct Raw { uint64_t tick; int order; uint8_t status, a, b; uint32_t tempo; bool isTempo; };
    std::vector<Raw> raw;
    int order = 0;
    for (int t = 0; t < ntracks && p + 8 <= d.size(); ++t)
    {
        if (std::memcmp (&d[p], "MTrk", 4) != 0)
            return false;
        p += 4;
        const auto len = u32();
        const auto end = p + len;
        uint64_t tick = 0;
        uint8_t running = 0;
        while (p < end)
        {
            uint32_t delta = 0;
            uint8_t c;
            do { c = d[p++]; delta = (delta << 7) | (c & 0x7f); } while (c & 0x80);
            tick += delta;
            uint8_t st = d[p];
            if (st & 0x80) ++p; else st = running;
            if (st == 0xff)
            {
                const uint8_t type = d[p++];
                uint32_t l = 0;
                do { c = d[p++]; l = (l << 7) | (c & 0x7f); } while (c & 0x80);
                if (type == 0x51 && l == 3)
                    raw.push_back ({ tick, order++, 0, 0, 0, uint32_t ((d[p] << 16) | (d[p + 1] << 8) | d[p + 2]), true });
                p += l;
            }
            else if (st == 0xf0 || st == 0xf7)
            {
                uint32_t l = 0;
                do { c = d[p++]; l = (l << 7) | (c & 0x7f); } while (c & 0x80);
                p += l;
            }
            else
            {
                running = st;
                const int hi = st & 0xf0;
                const int n = (hi == 0xc0 || hi == 0xd0) ? 1 : 2;
                const uint8_t a = d[p], b = n == 2 ? d[p + 1] : 0;
                p += n;
                raw.push_back ({ tick, order++, st, a, b, 0, false });
            }
        }
        p = end;
    }
    std::stable_sort (raw.begin(), raw.end(), [] (const Raw& x, const Raw& y) { return x.tick < y.tick; });
    double secPerTick = 0.5 / division, sec = 0.0;
    uint64_t lastTick = 0;
    std::map<int, std::pair<double, double>> open; // note -> (on, vel)
    for (const auto& r : raw)
    {
        sec += (r.tick - lastTick) * secPerTick;
        lastTick = r.tick;
        if (r.isTempo) { secPerTick = r.tempo / 1e6 / division; continue; }
        const int hi = r.status & 0xf0;
        if (hi == 0x90 && r.b > 0)
        {
            if (open.count (r.a))
                notes.push_back ({ open[r.a].first, sec, r.a, open[r.a].second });
            open[r.a] = { sec, r.b / 127.0 };
        }
        else if (hi == 0x80 || (hi == 0x90 && r.b == 0))
        {
            if (auto it = open.find (r.a); it != open.end())
            {
                notes.push_back ({ it->second.first, sec, r.a, it->second.second });
                open.erase (it);
            }
        }
        else if (hi == 0xb0)
            ccs.push_back ({ sec, r.a, r.b / 127.0 });
    }
    std::sort (notes.begin(), notes.end(), [] (const NoteEvent& x, const NoteEvent& y) { return x.on < y.on; });
    return true;
}

// ---------------------------------------------------------------- wav
bool readWav (const std::string& path, std::vector<std::vector<float>>& ch, double& rate)
{
    std::ifstream f (path, std::ios::binary);
    if (! f)
        return false;
    std::vector<uint8_t> d ((std::istreambuf_iterator<char> (f)), {});
    if (d.size() < 12 || std::memcmp (&d[0], "RIFF", 4) != 0)
        return false;
    size_t p = 12;
    int fmt = 1, nch = 1, bits = 16;
    while (p + 8 <= d.size())
    {
        uint32_t len;
        std::memcpy (&len, &d[p + 4], 4);
        if (std::memcmp (&d[p], "fmt ", 4) == 0)
        {
            uint16_t v; uint32_t r;
            std::memcpy (&v, &d[p + 8], 2); fmt = v;
            std::memcpy (&v, &d[p + 10], 2); nch = v;
            std::memcpy (&r, &d[p + 12], 4); rate = r;
            std::memcpy (&v, &d[p + 22], 2); bits = v;
            if (fmt == 0xfffe) { std::memcpy (&v, &d[p + 32], 2); fmt = v; }
        }
        else if (std::memcmp (&d[p], "data", 4) == 0)
        {
            const size_t bps = bits / 8, frames = len / (bps * nch);
            ch.assign (nch, std::vector<float> (frames));
            const uint8_t* s = &d[p + 8];
            for (size_t i = 0; i < frames; ++i)
                for (int c = 0; c < nch; ++c, s += bps)
                {
                    float x = 0;
                    if (fmt == 3 && bits == 32) std::memcpy (&x, s, 4);
                    else if (bits == 16) { int16_t v; std::memcpy (&v, s, 2); x = v / 32768.0f; }
                    else if (bits == 24) { int32_t v = (s[0] << 8) | (s[1] << 16) | (s[2] << 24); x = v / 2147483648.0f; }
                    else if (bits == 32) { int32_t v; std::memcpy (&v, s, 4); x = v / 2147483648.0f; }
                    ch[c][i] = x;
                }
            return true;
        }
        p += 8 + len + (len & 1);
    }
    return false;
}

void writeWav (const std::string& path, const std::vector<float>& l, const std::vector<float>& r, double rate)
{
    std::ofstream f (path, std::ios::binary);
    const uint32_t n = l.size(), data = n * 4, riff = 36 + data, sr = rate, br = rate * 4, fmtLen = 16;
    const uint16_t pcm = 1, ch = 2, align = 4, bits = 16;
    f.write ("RIFF", 4); f.write ((const char*) &riff, 4); f.write ("WAVEfmt ", 8);
    f.write ((const char*) &fmtLen, 4); f.write ((const char*) &pcm, 2); f.write ((const char*) &ch, 2);
    f.write ((const char*) &sr, 4); f.write ((const char*) &br, 4); f.write ((const char*) &align, 2);
    f.write ((const char*) &bits, 2); f.write ("data", 4); f.write ((const char*) &data, 4);
    for (uint32_t i = 0; i < n; ++i)
        for (float x : { l[i], r[i] })
        {
            const int16_t v = (int16_t) std::lround (std::clamp (x, -1.0f, 1.0f) * 32767.0f);
            f.write ((const char*) &v, 2);
        }
}

// ---------------------------------------------------------------- FFT convolution
void fft (std::vector<std::complex<double>>& a, bool inverse)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = 2 * kPi / len * (inverse ? 1 : -1);
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1);
            for (size_t k = 0; k < len / 2; ++k, w *= wl)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
            }
        }
    }
    if (inverse)
        for (auto& x : a) x /= double (n);
}

std::vector<float> convolve (const std::vector<float>& x, const std::vector<float>& h)
{
    size_t n = 1;
    while (n < x.size() + h.size()) n <<= 1;
    std::vector<std::complex<double>> a (n), b (n);
    for (size_t i = 0; i < x.size(); ++i) a[i] = x[i];
    for (size_t i = 0; i < h.size(); ++i) b[i] = h[i];
    fft (a, false);
    fft (b, false);
    for (size_t i = 0; i < n; ++i) a[i] *= b[i];
    fft (a, true);
    std::vector<float> y (x.size());
    for (size_t i = 0; i < y.size(); ++i) y[i] = float (a[i].real());
    return y;
}

// ---------------------------------------------------------------- the string
// Fractional delay line, cubic Lagrange read.
struct Delay
{
    std::vector<double> buf = std::vector<double> (8192, 0.0);
    int w = 0;
    void push (double x) { buf[w] = x; w = (w + 1) & 8191; }
    double read (double d) const // d >= 1 samples back (1 = most recent)
    {
        d = std::max (d, 1.0);
        const int i = int (d);
        const double f = d - i;
        auto at = [&] (int k) { return buf[(w - k + 8192 * 4) & 8191]; };
        const double xm1 = at (i - 1 < 1 ? 1 : i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
        // Lagrange around x0 (offset f in [0,1))
        const double c0 = -f * (f - 1) * (f - 2) / 6.0, c1 = (f + 1) * (f - 1) * (f - 2) / 2.0;
        const double c2 = -(f + 1) * f * (f - 2) / 2.0, c3 = (f + 1) * f * (f - 1) / 6.0;
        return c0 * xm1 + c1 * x0 + c2 * x1 + c3 * x2;
    }
};

// Bow-string friction (hyperbolic curve), returns string velocity at the bow.
double friction (double vBow, double vH, double force, double Z, bool& sticking)
{
    const double muS = P ("muS", 0.8), muD = P ("muD", 0.3), v0 = P ("v0", 0.1);
    if (force <= 0.0) { sticking = false; return vH; }
    const double dh = vBow - vH, adh = std::abs (dh), a = 2.0 * Z;
    auto root = [&] {
        const double b = a * v0 + force * muD - a * adh, c = (force * muS - a * adh) * v0;
        const double disc = b * b - 4 * a * c;
        if (disc < 0) return -1.0;
        const double r = (-b + std::sqrt (disc)) / (2 * a);
        return r > 0 ? r : -1.0;
    };
    double slip;
    if (a * adh <= muS * force)
    {
        if (sticking) return vBow;
        slip = root();
        if (slip < 0) { sticking = true; return vBow; }
    }
    else
        slip = root();
    sticking = false;
    return vBow - (dh > 0 ? slip : -slip);
}

struct Voice
{
    // pitch
    double note = -1, fromNote = -1, glide = 1.0;
    double cents = 0.0; // per-note tuning offset
    // bow
    bool held = false, active = false;
    double vel = 0.5, dir = 1.0;
    double env = 0.0; // 0..1 bow speed envelope
    double noteTime = 0, strokeTime = 0, releaseTime = -1;
    double lastStart = -1e9, lastUsed = -1e9;
    // vibrato
    double vibPhase = 0, vibRate = 1, vibDepth = 1;
    // string
    Delay bridge, nut;
    double lp = 0.0;
    bool stick = false;
    double hair = 0.0, hair2 = 0.0;
    Rng rng { 1 };

    void start (int n, double v, bool slur, double t)
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
            dir = -dir; // new stroke
            strokeTime = 0.0;
            vibPhase = 0.5 + 0.5 * rng.uni();
            vibRate = 1.0 + P ("vibRateVar", 0.05) * rng.uni();
            vibDepth = 1.0 + P ("vibDepthVar", 0.15) * rng.uni();
        }
        note = n;
        vel = v;
        noteTime = 0.0;
        cents = P ("intonation", 6.0) * rng.gauss() * 0.577;
        held = active = true;
        releaseTime = -1;
        lastStart = lastUsed = t;
    }
    void stop() { held = false; releaseTime = 0.0; }
    double currentNote() const
    {
        const double c = 0.5 - 0.5 * std::cos (kPi * std::min (glide, 1.0));
        return fromNote + (note - fromNote) * c;
    }

    double process (double dt, double dyn)
    {
        if (! active) return 0.0;
        noteTime += dt;
        strokeTime += dt;
        if (glide < 1.0) glide += dt / P ("glide", 0.03);

        // Bow envelope: rise on a stroke, fall on release.
        if (held)
            env = std::min (1.0, env + dt / P ("attack", 0.06));
        else
        {
            releaseTime += dt;
            env = std::max (0.0, env - dt / P ("release", 0.12));
        }

        // Vibrato: starts after a delay, fades in, irregular rate.
        const double vd = P ("vibDelay", 0.25), vf = P ("vibFade", 0.35);
        const double vibAmt = std::clamp ((noteTime - vd) / vf, 0.0, 1.0);
        vibPhase += dt * P ("vibRate", 5.6) * vibRate * (1.0 + P ("vibJitter", 0.06) * rng.uni());
        const double depthC = 0.5 * P ("vibDepth", 36.0) * vibDepth * vibAmt * (0.5 - 0.5 * std::cos (kPi * vibAmt));
        const double vib = depthC * std::sin (2 * kPi * vibPhase);
        // Landing: notes start a little flat and settle.
        const double land = P ("landC", -8.0) * std::exp (-noteTime / P ("landT", 0.06));
        const double f0 = midiHz (currentNote()) * std::pow (2.0, (cents + vib + land) / 1200.0);

        // String: round-trip delay split by the bow position.
        const double beta = P ("beta", 0.12);
        const double t60 = P ("t60", 2.5);
        const double g = std::pow (10.0, -3.0 / (t60 * f0));
        const double a = P ("damp", 0.25); // loss pole: higher = darker
        // phase delay of the loss filter at f0, so the pitch stays in tune
        const double w = 2 * kPi * f0 / FS;
        const double pd = std::atan2 (a * std::sin (w), 1.0 - a * std::cos (w)) / w;
        const double N = FS / f0 - pd;
        const double Z = P ("Z", 0.2) * std::pow (440.0 / f0, P ("Zslope", 0.35));

        // Bow speed and force (force follows speed, Schelleng window fraction).
        const double vb = (P ("vMin", 0.08) + P ("vMax", 0.45) * vel) * dyn * env;
        const double force = P ("force", 0.45) * 2.0 * Z * vb / (beta * (P ("muS", 0.8) - P ("muD", 0.3)))
                             * (held ? 1.0 : env); // lift the weight faster than the speed on release
        // Rosin grain on the bow velocity.
        const double hc = std::exp (-2 * kPi * P ("hairHz", 3000.0) / FS);
        hair = hc * hair + (1 - hc) * rng.uni();
        hair2 = hc * hair2 + (1 - hc) * hair;
        const double grain = P ("grain", 0.04) * vb * (hair - hair2) * 12.0;

        const double inB = bridge.read (beta * N), inN = nut.read ((1.0 - beta) * N);
        lp = (1 - a) * inB + a * lp;
        const double vinB = -g * lp, vinN = -inN;
        const double vh = vinB + vinN;
        const double v = friction (dir * vb + grain, vh, force, Z, stick);
        const double dv = v - vh;
        bridge.push (vinN + dv);
        nut.push (vinB + dv);

        if (! held && releaseTime > 6.0) active = false;
        return inB - vinB; // force on the bridge (up to a constant)
    }
};
} // namespace

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf (stderr, "usage: lite in.mid out.wav [name=value ...]\n");
        return 1;
    }
    for (int i = 3; i < argc; ++i)
    {
        std::string s = argv[i];
        const auto eq = s.find ('=');
        if (eq != std::string::npos) params[s.substr (0, eq)] = std::atof (s.c_str() + eq + 1);
    }
    std::vector<NoteEvent> notes;
    std::vector<CcEvent> ccs;
    if (! readMidi (argv[1], notes, ccs)) { std::fprintf (stderr, "cannot read %s\n", argv[1]); return 1; }
    // Only playable violin notes (keyswitches and the like are dropped).
    notes.erase (std::remove_if (notes.begin(), notes.end(), [] (const NoteEvent& n) { return n.note < 55 || n.note > 103; }), notes.end());
    const double start = P ("start", 0.0), length = P ("length", 1e9);
    double endT = 0;
    for (auto& n : notes) endT = std::max (endT, n.off);
    endT = std::min (endT + 2.5, start + length);

    // CC11 dynamics, held between events (1 until the first one).
    std::vector<CcEvent> dyn;
    for (auto& c : ccs) if (c.cc == 11) dyn.push_back (c);

    // ---- the player: events in time order
    struct Ev { double t; int type; size_t idx; }; // 0 off, 1 on
    std::vector<Ev> evs;
    for (size_t i = 0; i < notes.size(); ++i)
    {
        evs.push_back ({ notes[i].on, 1, i });
        evs.push_back ({ std::max (notes[i].off - P ("early", 0.0), notes[i].on + 0.02), 0, i });
    }
    std::sort (evs.begin(), evs.end(), [] (const Ev& a, const Ev& b) { return a.t < b.t || (a.t == b.t && a.type < b.type); });

    const int maxVoices = 4;
    std::vector<Voice> voices (maxVoices);
    for (int i = 0; i < maxVoices; ++i) voices[i].rng.s = 0x9e3779b9u * (i + 1);
    std::vector<int> voiceOf (notes.size(), -1), heldNote (maxVoices, -1);

    const double dt = 1.0 / FS;
    const size_t total = size_t (endT * FS);
    std::vector<float> out96 (total);
    size_t ei = 0, di = 0;
    double dynamics = 1.0;
    const double chordWindow = P ("chord", 0.03), slurGap = P ("slurGap", 0.0);
    for (size_t s = 0; s < total; ++s)
    {
        const double t = s * dt;
        while (di < dyn.size() && dyn[di].t <= t) dynamics = 0.25 + 0.75 * dyn[di++].value;
        while (ei < evs.size() && evs[ei].t <= t)
        {
            const auto& e = evs[ei++];
            const auto& n = notes[e.idx];
            if (e.type == 0)
            {
                const int v = voiceOf[e.idx];
                if (v >= 0 && heldNote[v] == int (e.idx)) { voices[v].stop(); heldNote[v] = -1; }
                continue;
            }
            // Chord: another note started just now -> its own voice.
            // Otherwise, if a note is still held (overlap) -> slur on that voice.
            int v = -1;
            bool slur = false, chord = false;
            for (int k = 0; k < maxVoices; ++k)
                if (heldNote[k] >= 0 && t - voices[k].lastStart < chordWindow) chord = true;
            if (! chord)
            {
                int best = -1;
                for (int k = 0; k < maxVoices; ++k)
                    if (heldNote[k] >= 0 && (best < 0 || voices[k].lastStart > voices[best].lastStart)) best = k;
                if (best >= 0) { v = best; slur = true; }
                else
                {
                    // just released (within slurGap): treat as slur too
                    for (int k = 0; k < maxVoices; ++k)
                        if (voices[k].active && voices[k].releaseTime >= 0 && voices[k].releaseTime < slurGap) { v = k; slur = true; }
                }
            }
            if (v < 0)
            {
                // most recently used free voice keeps the line on one "string"; chords take the oldest
                for (int k = 0; k < maxVoices; ++k)
                    if (heldNote[k] < 0 && (v < 0 || (chord ? voices[k].lastUsed < voices[v].lastUsed : voices[k].lastUsed > voices[v].lastUsed))) v = k;
                if (v < 0) v = 0;
            }
            voices[v].start (n.note, n.vel, slur, t);
            voiceOf[e.idx] = v;
            heldNote[v] = int (e.idx);
        }
        double y = 0;
        for (auto& vc : voices) y += vc.process (dt, dynamics);
        out96[s] = float (y);
    }

    // ---- decimate 96k -> 48k (windowed sinc, 20 kHz)
    const int taps = 95;
    std::vector<double> h (taps);
    for (int i = 0; i < taps; ++i)
    {
        const double m = i - (taps - 1) / 2.0, fc = 20000.0 / FS;
        const double sinc = m == 0 ? 2 * fc : std::sin (2 * kPi * fc * m) / (kPi * m);
        h[i] = sinc * (0.42 - 0.5 * std::cos (2 * kPi * i / (taps - 1)) + 0.08 * std::cos (4 * kPi * i / (taps - 1)));
    }
    std::vector<float> dry (total / 2);
    for (size_t i = 0; i < dry.size(); ++i)
    {
        double acc = 0;
        for (int k = 0; k < taps; ++k)
        {
            const long j = long (2 * i) - k + taps / 2;
            if (j >= 0 && j < long (total)) acc += h[k] * out96[j];
        }
        dry[i] = float (acc);
    }

    // ---- body
    std::vector<std::vector<float>> ir;
    double irRate = 0;
    const char* bodyEnv = std::getenv ("LITE_BODY");
    const std::string bodyPath = bodyEnv ? bodyEnv : "resources/bodies/stoppani.wav";
    if (P ("body", 1.0) > 0.5 && readWav (bodyPath, ir, irRate))
        dry = convolve (dry, ir[0]);

    // ---- room (stereo impulse response, mixed with the dry body sound)
    std::vector<float> L = dry, R = dry;
    const char* roomPath = std::getenv ("LITE_ROOM");
    const double wet = P ("room", 0.3);
    if (roomPath && wet > 0)
    {
        std::vector<std::vector<float>> rir;
        double rr = 0;
        if (readWav (roomPath, rir, rr))
        {
            auto& rl = rir[0];
            auto& rrt = rir.size() > 1 ? rir[1] : rir[0];
            // drop the direct sound's lead-in, normalise energy
            double e = 0;
            for (float x : rl) e += x * x;
            const float sc = float (1.0 / std::sqrt (std::max (e, 1e-12)));
            std::vector<float> a = rl, b = rrt;
            for (auto& x : a) x *= sc;
            for (auto& x : b) x *= sc;
            const auto wl = convolve (dry, a), wr = convolve (dry, b);
            for (size_t i = 0; i < L.size(); ++i)
            {
                L[i] = float ((1 - wet) * dry[i] + wet * wl[i]);
                R[i] = float ((1 - wet) * dry[i] + wet * wr[i]);
            }
        }
    }

    // ---- trim, normalise to a fixed loudness, soft-limit
    const size_t s0 = size_t (start * OUT_FS);
    std::vector<float> lo (L.begin() + std::min (s0, L.size()), L.end()), ro (R.begin() + std::min (s0, R.size()), R.end());
    double e = 0;
    for (size_t i = 0; i < lo.size(); ++i) e += lo[i] * lo[i] + ro[i] * ro[i];
    const double rms = std::sqrt (e / std::max<size_t> (1, 2 * lo.size()));
    const double gain = std::pow (10.0, P ("level", -20.0) / 20.0) / std::max (rms, 1e-9);
    for (size_t i = 0; i < lo.size(); ++i)
    {
        lo[i] = float (std::tanh (lo[i] * gain));
        ro[i] = float (std::tanh (ro[i] * gain));
    }
    writeWav (argv[2], lo, ro, OUT_FS);
    std::printf ("%s: %.1f s, %zu notes\n", argv[2], lo.size() / OUT_FS, notes.size());
    return 0;
}
