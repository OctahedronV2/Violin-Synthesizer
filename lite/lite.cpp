// Octavio Lite offline renderer: plays a MIDI file through the engine in
// LiteCore.h (the same code the web page and the plugin run) and writes a wav.
//
//   g++ -O2 -std=c++17 -o lite lite/lite.cpp
//   ./lite in.mid out.wav [param=value ...] [start=s] [length=s] [norm=dB]
//
// Run it from the repo root so it finds resources/bodies/stoppani.wav (or set
// LITE_BODY). param names are the ids in kParams (LiteCore.h). norm=-20
// normalises the result to that RMS level, for side-by-side listening; by
// default the output is exactly what the engine produces.

#include "LiteCore.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace
{
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

} // namespace

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf (stderr, "usage: lite in.mid out.wav [param=value ...]\n");
        return 1;
    }
    auto engine = std::make_unique<lite::Engine>();
    constexpr double fs = 48000.0;

    // Body impulse response, at the host rate.
    {
        const char* env = std::getenv ("LITE_BODY");
        std::vector<std::vector<float>> ir;
        double rate = 0;
        if (readWav (env ? env : "resources/bodies/stoppani.wav", ir, rate) && ! ir.empty())
        {
            const int n = (int) std::min<size_t> (ir[0].size(), sizeof (engine->body.irBuf) / sizeof (float));
            std::copy (ir[0].begin(), ir[0].begin() + n, engine->body.irBuf);
            engine->body.irLength = n;
        }
        else
            std::fprintf (stderr, "no body impulse response found; playing without a body\n");
    }
    engine->prepare (fs);

    double start = 0, length = 1e9, norm = 0;
    for (int i = 3; i < argc; ++i)
    {
        const std::string a = argv[i];
        const auto eq = a.find ('=');
        if (eq == std::string::npos)
            continue;
        const auto name = a.substr (0, eq);
        const double v = std::atof (a.c_str() + eq + 1);
        if (name == "start") start = v;
        else if (name == "length") length = v;
        else if (name == "norm") norm = v;
        else
        {
            int found = -1;
            for (int k = 0; k < lite::numParams; ++k)
                if (name == lite::kParams[k].id) found = k;
            if (found < 0)
            {
                std::fprintf (stderr, "unknown parameter: %s\n", name.c_str());
                return 1;
            }
            engine->setParam (found, v);
        }
    }

    std::vector<NoteEvent> notes;
    std::vector<CcEvent> ccs;
    if (! readMidi (argv[1], notes, ccs))
    {
        std::fprintf (stderr, "cannot read %s\n", argv[1]);
        return 1;
    }
    struct Ev { double t; int type, a; double b; }; // 0 off, 1 on, 2 cc
    std::vector<Ev> evs;
    double endT = 0;
    for (const auto& n : notes)
    {
        evs.push_back ({ n.on, 1, n.note, n.vel });
        evs.push_back ({ n.off, 0, n.note, 0 });
        endT = std::max (endT, n.off);
    }
    for (const auto& c : ccs)
        evs.push_back ({ c.t, 2, c.cc, c.value });
    std::stable_sort (evs.begin(), evs.end(), [] (const Ev& x, const Ev& y) { return x.t < y.t || (x.t == y.t && x.type < y.type); });
    endT = std::min (endT + 2.5, start + length);

    const size_t total = (size_t) (endT * fs), skip = (size_t) (start * fs);
    std::vector<float> L (total), R (total);
    size_t pos = 0, ei = 0;
    while (pos < total)
    {
        while (ei < evs.size() && (size_t) (evs[ei].t * fs) <= pos)
        {
            const auto& e = evs[ei++];
            if (e.type == 1) engine->noteOn (e.a, e.b);
            else if (e.type == 0) engine->noteOff (e.a);
            else engine->controller (e.a, e.b);
        }
        size_t next = total;
        if (ei < evs.size()) next = std::min (next, std::max (pos + 1, (size_t) (evs[ei].t * fs)));
        const int n = (int) std::min<size_t> (next - pos, 512);
        engine->process (&L[pos], &R[pos], n);
        pos += n;
    }
    std::vector<float> lo (L.begin() + std::min (skip, total), L.end()), ro (R.begin() + std::min (skip, total), R.end());
    double e = 0, peak = 0;
    for (size_t i = 0; i < lo.size(); ++i)
    {
        e += lo[i] * lo[i] + ro[i] * ro[i];
        peak = std::max<double> (peak, std::max (std::abs (lo[i]), std::abs (ro[i])));
    }
    const double rms = std::sqrt (e / std::max<size_t> (1, 2 * lo.size()));
    if (norm != 0.0)
    {
        const double g = std::pow (10.0, norm / 20.0) / std::max (rms, 1e-9);
        for (size_t i = 0; i < lo.size(); ++i) { lo[i] = (float) std::tanh (lo[i] * g); ro[i] = (float) std::tanh (ro[i] * g); }
    }
    if (const char* raw = std::getenv ("LITE_RAW")) // float dump of the left channel, for comparing builds
        std::ofstream (raw, std::ios::binary).write ((const char*) lo.data(), (std::streamsize) (lo.size() * sizeof (float)));
    writeWav (argv[2], lo, ro, fs);
    std::printf ("%s: %.1f s, %zu notes, rms %.1f dBFS, peak %.1f dBFS\n", argv[2], lo.size() / fs, notes.size(),
                 20 * std::log10 (std::max (rms, 1e-12)), 20 * std::log10 (std::max (peak, 1e-12)));
    return 0;
}
