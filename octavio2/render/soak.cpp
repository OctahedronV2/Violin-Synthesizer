// Octavio 2 stability soak (M3): the player and the four strings on the passive modal bridge,
// run offline for a long stretch of music with the bridge controls swept to their extremes.
//
//   g++ -O2 -std=c++17 -o /tmp/o2soak octavio2/render/soak.cpp
//   /tmp/o2soak [seconds=3600] [seed=1] [fs=96000]
//
// Plays seeded random music: single notes over the whole range, double stops, open-string
// drones, long notes sitting on the wolf (the strongest body mode) with vibrato, fast runs,
// pp to ff. Every 60 s it changes Sympathetic (0, 0.5, 1), Wolf (0, 1) and Hold (0, 0.5, 1),
// cycling through all 18 combinations, and every 5 minutes it stops for 15 s of silence.
// Fails (exit 1) if any bridge force or bridge-mode state is NaN or infinite, if a minute's
// peak force grows past 20x the median minute's, or if a silence doesn't decay (the last
// second of every rest must be 80 dB under the music's level). Prints one line per minute.

#include "../core/Player.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace o2;

int main (int argc, char** argv)
{
    std::map<std::string, std::string> opts;
    for (int i = 1; i < argc; ++i)
    {
        const char* eq = std::strchr (argv[i], '=');
        if (eq)
            opts[std::string ((const char*) argv[i], eq)] = eq + 1;
    }
    auto opt = [&] (const char* k, double d) { return opts.count (k) ? std::atof (opts[k].c_str()) : d; };
    const double seconds = opt ("seconds", 3600.0);
    const unsigned seed = (unsigned) opt ("seed", 1);
    const double sr = 48000.0;

    auto violin = std::make_unique<Violin>();
    violin->p.fs = opt ("fs", 96000.0);
    violin->init();
    for (int i = 0; i < 4; ++i)
        violin->s[i].rng.s ^= 0x51ED2701ull * (seed + 7 * i);
    auto player = std::make_unique<Player>();
    player->pp.seed = seed;
    player->init (*violin, sr);
    const int over = (int) std::lround (violin->p.fs / sr);

    Rng rng;
    rng.s ^= 0x9E37ull * (seed + 3);
    auto uni01 = [&] { return 0.5 * (rng.uni() + 1.0); };

    const double sym[3] = { 0.0, 0.5, 1.0 }, wolf[2] = { 0.0, 1.0 }, hold[3] = { 0.0, 0.5, 1.0 };
    int combo = -1;
    auto setCombo = [&] (int c)
    {
        combo = c;
        violin->p.sympathetic = sym[c % 3];
        violin->p.wolf = wolf[(c / 3) % 2];
        violin->p.hold = hold[(c / 6) % 3];
        violin->applyBody();
    };
    setCombo (0);

    // the music: notes are scheduled as (on, off, pitch, velocity)
    struct Note
    {
        double on, off;
        int pitch;
        double vel;
    };
    std::vector<Note> pending;
    double tNext = 0.5; // when the next phrase element starts
    auto schedule = [&] (double t)
    {
        const double r = uni01();
        const double vel = 20 + 107 * uni01();
        if (r < 0.35) // single note anywhere
        {
            const int p = 55 + (int) (uni01() * 45);
            const double d = 0.1 + 3.0 * uni01() * uni01();
            pending.push_back ({ t, t + d, p, vel });
            return t + d + (uni01() < 0.3 ? 0.4 * uni01() : 0.0);
        }
        if (r < 0.55) // double stop: a third to a sixth over a lower note
        {
            const int p = 55 + (int) (uni01() * 25), iv = 3 + (int) (uni01() * 7);
            const double d = 0.4 + 2.5 * uni01();
            pending.push_back ({ t, t + d, p, vel });
            pending.push_back ({ t, t + d, p + iv, vel });
            return t + d + 0.3 * uni01();
        }
        if (r < 0.65) // open-string drone, then a stopped note over it
        {
            static const int open[4] = { 55, 62, 69, 76 };
            const int s = (int) (uni01() * 3.999);
            const double d = 1.0 + 3.0 * uni01();
            pending.push_back ({ t, t + d, open[s], vel });
            pending.push_back ({ t + 0.3, t + d, open[s] + 7 + (int) (uni01() * 5), vel });
            return t + d + 0.5;
        }
        if (r < 0.8) // sitting on the wolf (the strongest body mode, ~534 Hz)
        {
            const double d = 1.5 + 3.0 * uni01();
            const int p = 71 + (int) (uni01() * 3);
            pending.push_back ({ t, t + d, p, 60 + 67 * uni01() });
            return t + d + 0.3;
        }
        // a fast run
        double tt = t;
        int p = 55 + (int) (uni01() * 30);
        for (int k = 0; k < 16; ++k)
        {
            const double d = 0.06 + 0.1 * uni01();
            pending.push_back ({ tt, tt + d * 0.95, p, vel });
            p = std::clamp (p + (uni01() < 0.5 ? -1 : 1) * (1 + (int) (uni01() * 3)), 55, 100);
            tt += d;
        }
        return tt + 0.2;
    };

    const long n = (long) (seconds * sr);
    std::vector<double> minutePeak;
    double peak = 0.0, musicPow = 0.0, silencePow = 0.0;
    long musicN = 0, silenceN = 0;
    bool ok = true, inRest = false;
    int rests = 0, restFails = 0;
    double restStart = -1.0;
    const auto t0 = std::chrono::steady_clock::now();
    for (long i = 0; i < n && ok; ++i)
    {
        const double t = i / sr;
        // every 5 minutes: 15 s of silence (notes released at the start)
        const double inCycle = std::fmod (t, 300.0);
        const bool rest = inCycle > 285.0;
        if (rest && ! inRest)
        {
            for (auto& q : pending)
                q.off = std::min (q.off, t);
            restStart = t;
            silencePow = 0.0;
            silenceN = 0;
        }
        if (! rest && inRest)
        {
            ++rests;
            const double music = musicPow / std::max (1L, musicN), sil = silencePow / std::max (1L, silenceN);
            const double db = 10 * std::log10 ((sil + 1e-300) / (music + 1e-300));
            std::printf ("  rest %d: last second %.1f dB re the music\n", rests, db);
            if (db > -80.0)
            {
                ++restFails;
                ok = false;
            }
            tNext = t + 0.2;
        }
        inRest = rest;
        if (! rest)
            while (tNext <= t + 0.5)
                tNext = schedule (tNext);
        // events
        for (size_t k = 0; k < pending.size();)
        {
            Note& q = pending[k];
            if (q.on >= 0.0 && q.on <= t)
            {
                if (! rest)
                {
                    player->nextDur = q.off - q.on;
                    player->noteOn (q.pitch, q.vel);
                }
                q.on = -1.0;
            }
            if (q.on < 0.0 && q.off <= t)
            {
                player->noteOff (q.pitch);
                pending[k] = pending.back();
                pending.pop_back();
                continue;
            }
            ++k;
        }
        double vb[4], fb[4];
        player->tick (vb, fb);
        for (int k = 0; k < over; ++k)
        {
            const double F = violin->tick (vb, fb);
            if (! std::isfinite (F))
            {
                std::printf ("FAIL: non-finite bridge force at %.3f s\n", t);
                ok = false;
                break;
            }
            peak = std::max (peak, std::abs (F));
            if (rest && t > restStart + 14.0)
            {
                silencePow += F * F;
                ++silenceN;
            }
            else if (! rest)
            {
                musicPow += F * F;
                ++musicN;
            }
        }
        if (i % 48000 == 0)
            for (auto& m : violin->bridge.modes)
                if (! std::isfinite (m.z1) || ! std::isfinite (m.z2))
                {
                    std::printf ("FAIL: non-finite bridge mode state at %.3f s\n", t);
                    ok = false;
                    break;
                }
        if ((i + 1) % (long) (60 * sr) == 0)
        {
            minutePeak.push_back (peak);
            std::vector<double> sorted = minutePeak;
            std::sort (sorted.begin(), sorted.end());
            const double med = sorted[sorted.size() / 2];
            const double el = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
            std::printf ("min %3zu  sym %.1f wolf %.0f hold %.1f  peak %.3f N (median %.3f)  %.1fx realtime\n",
                         minutePeak.size(),
                         violin->p.sympathetic,
                         violin->p.wolf,
                         violin->p.hold,
                         peak,
                         med,
                         (i + 1) / sr / el);
            std::fflush (stdout);
            if (minutePeak.size() > 3 && peak > 20.0 * med)
            {
                std::printf ("FAIL: growth (peak %.3f > 20 x median %.3f)\n", peak, med);
                ok = false;
            }
            peak = 0.0;
            setCombo ((combo + 1) % 18);
        }
    }
    std::printf ("%s: %.0f s of music, %d rests decayed (%d failed)\n",
                 ok ? "PASS" : "FAIL",
                 seconds,
                 rests,
                 restFails);
    return ok ? 0 : 1;
}
