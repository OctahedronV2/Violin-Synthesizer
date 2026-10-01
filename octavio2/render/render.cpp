// Octavio 2 offline renderer: MIDI -> player -> four strings on one bridge -> bridge force.
//
//   g++ -O2 -std=c++17 -o /tmp/o2 octavio2/render/render.cpp
//   /tmp/o2 in.mid force.wav [name=value ...] [start=s] [length=s] [tail=s]
//
// Writes the total force on the bridge at 48 kHz (mono float). octavio2/render/finish.py turns
// it into sound (body, microphones, room). M1 moves body and room into the core.
// Options: any Params field (strings) or PlayerParams field (player), by name, e.g.
//   bowPoints=3 friction=hyperbolic admittance=0 vibWidthHi=40 speedRange=0.3 seed=2
// Also: fs=192000 (internal rate, default 96000), slips=1 prints Helmholtz health per note.

#include "../core/Player.h"
#include "Midi.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>

using namespace o2;

static std::map<std::string, std::string> opts;
static double opt (const char* k, double def)
{
    auto it = opts.find (k);
    return it == opts.end() ? def : std::atof (it->second.c_str());
}

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf (stderr, "usage: o2 in.mid force.wav [name=value ...]\n");
        return 1;
    }
    for (int i = 3; i < argc; ++i)
    {
        const char* eq = std::strchr (argv[i], '=');
        if (eq)
            opts[std::string ((const char*) argv[i], eq)] = eq + 1;
    }
    std::vector<NoteEvent> notes;
    std::vector<CcEvent> ccs;
    if (! readMidi (argv[1], notes, ccs))
    {
        std::fprintf (stderr, "cannot read %s\n", argv[1]);
        return 1;
    }

    auto violin = std::make_unique<Violin>();
    Params& p = violin->p;
    p.fs = opt ("fs", 96000.0);
    if (opts.count ("friction"))
        p.friction = opts["friction"] == "hyperbolic" ? Friction::hyperbolic : opts["friction"] == "thermalHyp" ? Friction::thermalHyp : Friction::thermal;
#define O(name) p.name = opt (#name, p.name)
    O (muS); O (muD); O (v0); O (aT); O (bT); O (cT); O (tauG); O (ya); O (xi);
    O (bowWidth); O (hairStiffness); O (hairDamping); O (grain); O (grainHz); O (grainFade); O (slipNoise); O (slipNoiseHz); O (slipNoiseExp); O (slipNoiseFade); O (slipNoiseOut);
    O (torsionSpeed); O (torsionImpedance); O (torsionQ); O (fingerLoss); O (admScale);
#undef O
    p.bowPoints = (int) opt ("bowPoints", p.bowPoints);
    p.dispersion = opt ("dispersion", p.dispersion) != 0;
    p.torsion = opt ("torsion", p.torsion) != 0;
    p.autoTune = opt ("autoTune", p.autoTune) != 0;
    p.admittance = opt ("admittance", p.admittance) != 0;
    violin->init();
    const unsigned seed = (unsigned) opt ("seed", 1);
    for (int i = 0; i < 4; ++i)
        violin->s[i].rng.s ^= 0x51ED2701ull * (seed + 7 * i);

    auto player = std::make_unique<Player>();
    PlayerParams& q = player->pp;
#define O(name) q.name = opt (#name, q.name)
    O (velLo); O (velHi); O (velCurve); O (speedLo); O (speedRange); O (speedCurve);
    O (contactPP); O (contactFF); O (cLower); O (cUpper); O (posLo); O (posRange);
    O (accel); O (accelFF); O (landTime); O (biteFF); O (biteTime); O (changeDip);
    O (releaseTime); O (crossTime); O (bowLength); O (shiftBase); O (shiftPerSemi);
    O (shiftLighten); O (vibDelay); O (vibBloom); O (vibWidthLo); O (vibWidthHi);
    O (vibRate); O (vibRateDyn); O (vibWander); O (liftAfter); O (liftDamp); O (liftDampTime);
    O (chordWindow); O (speedMap); O (contactFollow); O (speedPP); O (speedFF); O (slurFollow); O (slurMaxNotes); O (slurMaxTime); O (slurAccent); O (shapeIOI); O (strokeSus); O (strokeTau); O (dynGlide); O (reg55); O (reg61); O (reg67); O (reg73); O (reg79); O (reg85); O (reg91); O (reg97); O (speedG); O (speedD); O (speedA); O (speedE); O (crossBite); O (crossBiteTime); O (noiseStart); O (noiseRise); O (bite); O (earUp); O (earDown); O (earMax); O (earMin); O (earRelax); O (earWindow); O (earWait); O (earPeriods);
#undef O
    q.seed = seed;
    player->log = opt ("log", 0) != 0;
    const double sr = 48000.0;
    player->init (*violin, sr);

    // events: offs before ons at the same time
    struct Ev
    {
        double t;
        int type, pitch;
        double vel;
    };
    std::vector<Ev> ev;
    double tEnd = 0;
    for (auto& n : notes)
    {
        ev.push_back ({ n.on, 1, n.note, n.vel * 127.0 });
        ev.push_back ({ n.off, 0, n.note, 0 });
        tEnd = std::max (tEnd, n.off);
    }
    std::stable_sort (ev.begin(), ev.end(), [] (const Ev& a, const Ev& b) { return a.t < b.t || (a.t == b.t && a.type < b.type); });

    const double start = opt ("start", 0.0);
    const double length = opt ("length", tEnd + opt ("tail", 2.5) - start);
    const long n = (long) ((start + length) * sr);
    const int over = (int) std::lround (p.fs / sr);
    std::vector<float> out;
    out.reserve ((size_t) (length * sr) + 1);
    Decim dec; // 2x; for 4x a second stage runs first
    Decim dec2;
    size_t e = 0;
    const auto t0 = std::chrono::steady_clock::now();
    const bool slips = opt ("slips", 0) != 0;
    long slipCount[4] = {};
    for (long i = 0; i < n; ++i)
    {
        const double tt = i / sr;
        while (e < ev.size() && ev[e].t <= tt)
        {
            if (ev[e].type == 1)
                player->noteOn (ev[e].pitch, ev[e].vel);
            else
                player->noteOff (ev[e].pitch);
            ++e;
        }
        double vb[4], fb[4];
        player->tick (vb, fb);
        for (int k = 0; k < over; ++k)
        {
            const double F = violin->tick (vb, fb);
            if (over == 4)
            {
                dec2.push (F);
                if (k & 1)
                    dec.push (dec2.out());
            }
            else
                dec.push (F);
            for (int s = 0; s < 4; ++s)
                slipCount[s] += violin->s[s].slipped;
        }
        if (tt >= start)
            out.push_back ((float) dec.out());
        static long win[4] = {};
        for (int s = 0; s < 4; ++s)
            win[s] += 0;
        if (opt ("trace", 0) != 0 && i % 480 == 0 && tt >= opt ("traceFrom", 0) && tt < opt ("traceTo", 1e9))
        {
            std::fprintf (stderr, "%.2f v %+.3f", tt, player->v);
            for (int s = 0; s < 4; ++s)
                std::fprintf (stderr, " | %c F %.3f b%d p %.2f slips %ld", "GDAE"[s], fb[s], (int) player->st[s].bowed, player->st[s].pitch, slipCount[s] - win[s]);
            for (int s = 0; s < 4; ++s)
                win[s] = slipCount[s];
            std::fprintf (stderr, "\n");
        }
    }
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    std::fprintf (stderr, "rendered %.1f s in %.1f s (%.1fx realtime)\n", length, secs, length / secs);
    std::fprintf (stderr, "capture: %ld notes, mean %.1f ms, %.1f%% over 50 ms, %.1f%% over 100 ms\n", player->capN, 1000 * player->capSum / std::max (1L, player->capN), 100.0 * player->capSlow / std::max (1L, player->capN), 100.0 * player->capVerySlow / std::max (1L, player->capN));
    if (slips)
        std::fprintf (stderr, "slips G %ld D %ld A %ld E %ld\n", slipCount[0], slipCount[1], slipCount[2], slipCount[3]);
    writeWav (argv[2], out, (int) sr);
    return 0;
}
