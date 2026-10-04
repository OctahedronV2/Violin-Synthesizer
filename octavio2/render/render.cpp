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
// M5: articulation=0..4 (arco, pizz, Bartok pizz, left-hand pizz, harmonic; keyswitches MIDI 24-28
// do the same inside the file), pizzModel=0 (the old placeholder pluck), pizz*/bartok*/lh*/harm*.
// Bridge (M3): bridgeModes=0 (M0 generic set) | 1 (CNSM fit), sympathetic=0..1 (0.5), wolf=0..1,
// hold=0..1 (0.5), admScale=x, modalBody=0 (measured body over the whole band).
//
// sound=out.wav renders through o2::Engine instead, the plugin's own code path (player, strings,
// body, mics, hall), and writes stereo float at 48 kHz:
//   mode=planned (default: every note's length known, as the score renders), live (the plugin's
//   Live mode: no lengths), studio (the plugin's Studio mode: look-ahead)
//   hall=0..8 (0 none, 1 Arvedi near seat), bright=dB (5), reverb=dB (0), volume=dB (0),
//   vibrato=x (1), velCurve=x (1), data=octavio2/data

#include "../core/Engine.h"
#include "../core/Wav.h"
#include "Midi.h"

#include <algorithm>
#include <array>
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

static void writeStereo (const std::string& path, const std::vector<float>& l, const std::vector<float>& r)
{
    std::vector<float> x (2 * l.size());
    for (size_t i = 0; i < l.size(); ++i)
    {
        x[2 * i] = l[i];
        x[2 * i + 1] = r[i];
    }
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
    w16 (2);
    w32 (48000);
    w32 (48000 * 8);
    w16 (8);
    w16 (32);
    std::fwrite ("data", 1, 4, f);
    w32 ((uint32_t) x.size() * 4);
    std::fwrite (x.data(), 4, x.size(), f);
    std::fclose (f);
}

static const char* halls[] = { "arvedi-near",  "arvedi-far",   "detmold",          "church",
                               "maida-vale-4", "maida-vale-5", "wdr-control-room", "wdr-studio" };

// experiments: any of the strings' and bow's continuous Params
static void stringOpts (Params& p)
{
#define O(name) p.name = opt (#name, p.name)
    O (muS);
    O (muD);
    O (v0);
    O (aT);
    O (bT);
    O (cT);
    O (tauG);
    O (ya);
    O (xi);
    O (bowWidth);
    O (hairStiffness);
    O (hairEnds);
    O (sigma0);
    O (sigma1);
    O (zba);
    p.epIters = (int) opt ("epIters", p.epIters);
    O (hairDamping);
    O (grain);
    O (grainHz);
    O (grainFade);
    O (tuneCents);
    O (earCarry);
    O (slipNoise);
    O (slipNoiseHz);
    O (slipNoiseExp);
    O (slipNoiseFade);
    O (slipNoiseOut);
    O (torsionSpeed);
    O (torsionImpedance);
    O (torsionQ);
    O (fingerLoss);
    O (admScale);
    O (sympathetic);
    O (symLossCut);
    O (symIdleAfter);
    O (symDamp);
    O (symCoupling);
    O (wolf);
    O (wolfHz);
    O (wolfMass);
    O (wolfQ);
    O (hold);
    O (holdMax);
    O (holdHz);
    O (holdFade);
    p.bridgeModes = (int) opt ("bridgeModes", p.bridgeModes);
#undef O
}

// PlayerParams fields given on the command line
static void playerOpts (PlayerParams& q)
{
#define O(name) q.name = opt (#name, q.name)
    O (velLo);
    O (velHi);
    O (velCurve);
    O (speedLo);
    O (speedRange);
    O (speedCurve);
    O (contactPP);
    O (contactFF);
    O (cLower);
    O (cUpper);
    O (posLo);
    O (posRange);
    O (forceCap);
    O (tiltPP);
    O (accel);
    O (accelFF);
    O (landTime);
    O (biteFF);
    O (biteTime);
    O (changeDip);
    O (liveDistribute);
    O (retakeAfter);
    O (retakeRoom);
    O (retakeMin);
    O (budgetSoft);
    O (strokeTaper);
    O (taperDepth);
    O (taperLiveMax);
    O (releaseTime);
    O (crossTime);
    O (bowLength);
    O (shiftBase);
    O (shiftPerSemi);
    O (shiftLighten);
    O (vibDelay);
    O (vibBloom);
    O (vibWidthLo);
    O (vibWidthHi);
    O (vibRate);
    O (vibRateDyn);
    O (vibGrowStart);
    O (vibGrow);
    O (vibGrowMax);
    O (vibTaper);
    O (vibPosition);
    O (vibShort);
    O (vibWander);
    O (vibAmount);
    O (liftAfter);
    O (liftDamp);
    O (liftDampTime);
    O (chordWindow);
    O (speedMap);
    O (contactFollow);
    O (speedPP);
    O (speedFF);
    O (slurFollow);
    O (slurMaxNotes);
    O (slurMaxTime);
    O (slurAccent);
    O (shapeIOI);
    O (stopBelow);
    O (forceSus);
    O (forceTau);
    O (forceHold);
    O (stopAccel);
    O (stopForce);
    O (stopTime);
    O (strokeSus);
    O (quickIOI);
    O (quickRun);
    O (quickP);
    O (qForceSus);
    O (qForceTau);
    O (qForceHold);
    O (qStrokeSus);
    O (qStrokeTau);
    O (qStopForce);
    O (qStopTime);
    O (qStopAccel);
    O (stopDamp);
    O (openMute);
    O (openMuteTime);
    O (qStopDamp);
    O (quickContact);
    O (strokeTau);
    O (dynGlide);
    O (reg55);
    O (reg61);
    O (reg67);
    O (reg73);
    O (reg79);
    O (reg85);
    O (reg91);
    O (reg97);
    O (speedG);
    O (speedD);
    O (speedA);
    O (speedE);
    O (crossBite);
    O (crossBiteTime);
    O (noiseStart);
    O (noiseRise);
    O (bite);
    O (earUp);
    O (earDown);
    O (earMax);
    O (earMin);
    O (earRelax);
    O (earWindow);
    O (earWait);
    O (earPeriods);
    // M5 articulations
    O (articulation);
    O (keyswitchBase);
    O (pizzModel);
    O (pizzImpulse);
    O (pizzPointMM);
    O (pizzAmpPP);
    O (pizzAmpFF);
    O (pizzTau);
    O (pizzBright);
    O (pizzTouch);
    O (pizzPull);
    O (pizzStopLoss);
    O (pizzTouchDamp);
    O (pizzOffDamp);
    O (bartokAmp);
    O (bartokTau);
    O (bartokPointMM);
    O (bartokClick);
    O (bartokClickTime);
    O (bartokKick);
    O (lhAmp);
    O (lhTau);
    O (lhFromNut);
    O (harmTouch);
    O (harmForce);
#undef O
}

// MIDI -> o2::Engine (the plugin's path) -> stereo
static int renderSound (const std::vector<NoteEvent>& notes, const std::vector<CcEvent>& ccs)
{
    std::string dir = opts.count ("data")
        ? opts["data"]
        : std::string (__FILE__).substr (0, std::string (__FILE__).rfind ('/')) + "/../data";
    RadiationData data;
    auto mono = [&] (const std::string& name)
    {
        const WavData w = readWavFile (dir + "/" + name);
        return w.channels.empty() ? std::vector<float>() : w.channels[0];
    };
    for (const char* b : { "stoppani", "klimke", "levaggi", "iowa" })
        data.bodies.push_back (mono (std::string ("bodies/") + b + "-48k.wav"));
    for (const char* m : { "front", "above", "ear", "side" })
    {
        const WavData w = readWavFile (dir + "/mics/" + m + "-48k.wav");
        if (w.channels.size() != 6)
        {
            std::fprintf (stderr, "cannot read %s/mics/%s-48k.wav\n", dir.c_str(), m);
            return 1;
        }
        std::array<std::vector<float>, 6> irs;
        for (size_t k = 0; k < 6; ++k)
            irs[k] = w.channels[k];
        data.mics.push_back (irs);
    }
    for (const char* h : halls)
    {
        const WavData w = readWavFile (dir + "/halls/" + h + ".wav");
        if (w.channels.size() != 2)
        {
            std::fprintf (stderr, "cannot read %s/halls/%s.wav\n", dir.c_str(), h);
            return 1;
        }
        data.halls.push_back ({ w.channels[0], w.channels[1] });
    }
    if (std::any_of (data.bodies.begin(), data.bodies.end(), [] (const auto& b) { return b.empty(); }))
    {
        std::fprintf (stderr, "cannot read the bodies in %s\n", dir.c_str());
        return 1;
    }
    auto engine = std::make_unique<Engine>();
    engine->prepare (data, (unsigned) opt ("seed", 1), opt ("fs", 96000.0));
    EngineSettings es;
    const std::string mode = opts.count ("mode") ? opts["mode"] : "planned";
    es.studio = mode == "studio";
    es.hall = (int) opt ("hall", es.hall);
    es.brightnessDb = opt ("bright", es.brightnessDb);
    es.reverbDb = opt ("reverb", es.reverbDb);
    es.volumeDb = opt ("volume", es.volumeDb);
    es.vibrato = opt ("vibrato", es.vibrato);
    es.velocityCurve = opt ("velCurve", es.velocityCurve);
    es.violin = (int) opt ("violin", es.violin);
    es.mic = (int) opt ("mic", es.mic);
    es.width = opt ("width", es.width);
    es.movement = opt ("movement", es.movement);
    es.distance = opt ("distance", es.distance);
    es.bridgeHz = opt ("bridge", es.bridgeHz);
    es.mute = (int) opt ("mute", es.mute);
    es.sympathetic = opt ("sympathetic", es.sympathetic);
    es.wolf = opt ("wolf", es.wolf);
    es.hold = opt ("hold", es.hold);
    es.modalBody = opt ("modalBody", es.modalBody) != 0;
    if (opts.count ("size"))
        engine->getRadiation().setBodySize (opt ("size", 1.0));
    engine->setSettings (es);
    playerOpts (engine->getPlayer().pp); // experiments: any PlayerParams field
    stringOpts (engine->getViolin().p); // and any continuous strings Params field
    engine->reset(); // the strings and bridge re-made with those (same state as prepare's)
    engine->getPlayer().log = opt ("log", 0) != 0;

    const double sr = Engine::rate;
    auto at = [&] (double t)
    {
        // the first sample at or after t, as the force renderer applies events
        int64_t i = (int64_t) std::ceil (t * sr);
        while (i > 0 && (double) (i - 1) / sr >= t)
            --i;
        while ((double) i / sr < t)
            ++i;
        return i;
    };
    struct Ev
    {
        double t;
        int type, pitch;
        double vel, dur;
    };
    std::vector<Ev> ev;
    double tEnd = 0;
    for (auto& n : notes)
    {
        ev.push_back ({ n.on, 1, n.note, n.vel * 127.0, n.off - n.on });
        ev.push_back ({ n.off, 0, n.note, 0, 0 });
        tEnd = std::max (tEnd, n.off);
    }
    for (auto& c : ccs)
        ev.push_back ({ c.t, 2, c.cc, c.value * 127.0, 0 });
    std::stable_sort (ev.begin(),
                      ev.end(),
                      [] (const Ev& a, const Ev& b) { return a.t < b.t || (a.t == b.t && a.type < b.type); });

    const double start = opt ("start", 0.0);
    const double length = opt ("length", tEnd + opt ("tail", 2.5) - start);
    const int64_t latency = engine->latencySamples();
    const int64_t n = (int64_t) ((start + length) * sr) + latency;
    std::vector<float> L, R;
    L.reserve ((size_t) (length * sr) + 1);
    R.reserve ((size_t) (length * sr) + 1);
    size_t e = 0;
    const auto t0 = std::chrono::steady_clock::now();
    const int block = 512;
    float bl[block], br[block];
    for (int64_t i = 0; i < n; i += block)
    {
        const int m = (int) std::min<int64_t> (block, n - i);
        // events are handed over a block ahead, as a host does; the engine plays them on time
        while (e < ev.size() && at (ev[e].t) < i + m)
        {
            const int64_t when = at (ev[e].t);
            if (ev[e].type == 2)
                engine->controller (when, ev[e].pitch, ev[e].vel);
            else if (ev[e].type == 1)
                engine->noteOn (when, ev[e].pitch, ev[e].vel, mode == "planned" ? ev[e].dur : 0.0);
            else
                engine->noteOff (when, ev[e].pitch);
            ++e;
        }
        engine->render (bl, br, m);
        for (int k = 0; k < m; ++k)
            if (i + k - latency >= (int64_t) (start * sr))
            {
                L.push_back (bl[k]);
                R.push_back (br[k]);
            }
    }
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    double sum = 0, peak = 0;
    for (size_t k = 0; k < L.size(); ++k)
    {
        sum += 0.5 * (L[k] * L[k] + R[k] * R[k]);
        peak = std::max (peak, (double) std::max (std::abs (L[k]), std::abs (R[k])));
    }
    std::fprintf (stderr,
                  "rendered %.1f s in %.1f s (%.1fx realtime), %s mode, %.1f dB RMS, peak %.1f dB\n",
                  length,
                  secs,
                  length / secs,
                  mode.c_str(),
                  10 * std::log10 (sum / std::max<size_t> (1, L.size()) + 1e-30),
                  20 * std::log10 (peak + 1e-30));
    writeStereo (opts["sound"], L, R);
    return 0;
}

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf (stderr,
                      "usage: o2 in.mid force.wav [name=value ...]  |  o2 in.mid - sound=out.wav [mode=live ...]\n");
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

    if (opts.count ("sound"))
        return renderSound (notes, ccs);

    auto violin = std::make_unique<Violin>();
    Params& p = violin->p;
    p.fs = opt ("fs", 96000.0);
    if (opts.count ("friction"))
        p.friction = opts["friction"] == "hyperbolic" ? Friction::hyperbolic
            : opts["friction"] == "thermalHyp"        ? Friction::thermalHyp
                                                      : Friction::thermal;
    stringOpts (p);
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
    playerOpts (q);
    q.seed = seed;
    player->log = opt ("log", 0) != 0;
    const double sr = 48000.0;
    player->init (*violin, sr);

    // events: offs before ons at the same time
    struct Ev
    {
        double t;
        int type, pitch;
        double vel, dur;
    };
    std::vector<Ev> ev;
    double tEnd = 0;
    for (auto& n : notes)
    {
        ev.push_back ({ n.on, 1, n.note, n.vel * 127.0, n.off - n.on });
        ev.push_back ({ n.off, 0, n.note, 0, 0 });
        tEnd = std::max (tEnd, n.off);
    }
    for (auto& c : ccs)
        ev.push_back ({ c.t, 2, c.cc, c.value * 127.0, 0 });
    std::stable_sort (ev.begin(),
                      ev.end(),
                      [] (const Ev& a, const Ev& b) { return a.t < b.t || (a.t == b.t && a.type < b.type); });

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
    const int solo = (int) opt ("solo", -1);
    long slipCount[4] = {};
    for (long i = 0; i < n; ++i)
    {
        const double tt = i / sr;
        while (e < ev.size() && ev[e].t <= tt)
        {
            if (ev[e].type == 2)
                player->controller (ev[e].pitch, ev[e].vel);
            else if (ev[e].type == 1)
            {
                // the note's length, so the player can spread its bow (planned=0: Live, unknown)
                double dur = ev[e].dur;
                if (opt ("planned", 1) == 0)
                    dur = 0.0;
                player->nextDur = dur;
                player->noteOn (ev[e].pitch, ev[e].vel);
            }
            else
                player->noteOff (ev[e].pitch);
            ++e;
        }
        double vb[4], fb[4];
        player->tick (vb, fb);
        for (int k = 0; k < over; ++k)
        {
            double F = violin->tick (vb, fb);
            if (solo >= 0)
                F = violin->Fs[solo]; // debug: one string's own bridge force
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
                std::fprintf (stderr,
                              " | %c F %.3f b%d p %.2f slips %ld",
                              "GDAE"[s],
                              fb[s],
                              (int) player -> st[s].bowed,
                              player->st[s].pitch,
                              slipCount[s] - win[s]);
            for (int s = 0; s < 4; ++s)
                win[s] = slipCount[s];
            std::fprintf (stderr, "\n");
        }
    }
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    std::fprintf (stderr, "rendered %.1f s in %.1f s (%.1fx realtime)\n", length, secs, length / secs);
    std::fprintf (stderr,
                  "capture: %ld notes, mean %.1f ms, %.1f%% over 50 ms, %.1f%% over 100 ms\n",
                  player->capN,
                  1000 * player->capSum / std::max (1L, player->capN),
                  100.0 * player->capSlow / std::max (1L, player->capN),
                  100.0 * player->capVerySlow / std::max (1L, player->capN));
    if (slips)
        std::fprintf (stderr,
                      "slips G %ld D %ld A %ld E %ld\n",
                      slipCount[0],
                      slipCount[1],
                      slipCount[2],
                      slipCount[3]);
    writeWav (argv[2], out, (int) sr);
    return 0;
}
