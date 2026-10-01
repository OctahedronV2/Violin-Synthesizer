// Minimal standard MIDI file reader (from lite/lite.cpp): notes with velocity, CCs, tempo map.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace o2
{
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

inline bool readMidi (const std::string& path, std::vector<NoteEvent>& notes, std::vector<CcEvent>& ccs)
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

} // namespace o2
