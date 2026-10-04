// Octavio 2 engine: MIDI events -> player -> four strings on one bridge -> radiation -> stereo.
//
// The one code path the plugin and the offline renderer (render.cpp sound=) share, so what Jake
// hears from a render is what the plugin plays. Runs at 48 kHz (the strings at 2x inside);
// the plugin resamples to the host's rate.
//
// Events carry a time in engine samples. Live mode plays each at its time; the player never
// knows how long a note will be. Studio mode delays everything by studioLookAhead and reports
// that as latency: the player is told the length of every note that ends within the look-ahead
// when it starts (bow spread, short-note vibrato), and the end of longer notes as soon as it
// comes into view (vibrato relaxing before the end). Allocation only in prepare().

#pragma once
#include "Player.h"
#include "Radiation.h"

#include <atomic>
#include <cstdint>
#include <memory>

namespace o2
{
struct EngineSettings
{
    bool studio = false;
    double brightnessDb = 5.0; // Jake picked +5 dB "brighter + grit" (2026-10-01)
    int hall = 1; // 0 none, 1 Arvedi near seat (the renders' hall), ...
    double reverbDb = 0.0; // hall tail level
    double volumeDb = 0.0;
    double vibrato = 1.0; // width scale
    double velocityCurve = 1.0; // dynamics = velocity ^ this (PlayerParams::velCurve)
    double dynamics = 0.0; // added to every note's dynamics, -0.5..0.5 (PlayerParams::dynBias)
    // radiation and room (M1)
    int violin = 0; // RadiationData::bodies: Stoppani, Klimke, Levaggi, Iowa
    int mic = 0; // RadiationData::mics: front, above, player's ear, side
    double width = 1.0; // stereo width, 0..2
    double movement = 0.5; // player's sway, 0..1 (0.5: the M1 clips' middle setting)
    double distance = 2.0; // m, 0.5..10
    double bridgeHz = 2900.0; // bridge rocking resonance, 2400 (dark) .. 3600 (bright)
    int mute = 0; // 0 off, 1 con sordino, 2 practice mute
    double hiss = 1.0; // bow hiss (Params::slipNoise) scale: 1 natural, 3, 6 (M2)
};

class Engine
{
public:
    static constexpr double rate = 48000.0;
    static constexpr double studioLookAhead = 1.2; // s: every note shorter than PlayerParams::vibShort is known
    static constexpr int lookAheadSamples = (int) (studioLookAhead * rate);

    // Message thread. seed: the strings' and player's random wander (renders use 1).
    void prepare (const RadiationData& data, unsigned seed = 1, double internalRate = 96000.0)
    {
        violin = std::make_unique<Violin>();
        violin->p.fs = internalRate;
        player = std::make_unique<Player>();
        radiation = std::make_unique<Radiation>();
        radiation->prepare (data);
        this->seed = seed;
        reset();
    }

    // Message thread (allocates nothing, but re-initialises every string).
    void reset()
    {
        violin->init();
        for (int i = 0; i < 4; ++i)
        {
            violin->s[i].rng = Rng();
            violin->s[i].rng.s ^= 0x51ED2701ull * (seed + 7 * i);
        }
        const PlayerParams keep = player->pp;
        *player = Player();
        player->pp = keep;
        player->pp.seed = seed;
        player->init (*violin, rate);
        dec = Decim();
        dec2 = Decim();
        radiation->reset();
        clock = 0;
        head = tail = 0;
        for (int p = 0; p < 128; ++p)
            held[p] = sounding[p] = 0;
        applySettings();
    }

    // Audio thread (or before playing). Switching Live/Studio drops what was queued for the
    // other mode's timing and releases the held notes.
    void setSettings (const EngineSettings& s)
    {
        const bool modeChanged = s.studio != settings.studio;
        settings = s;
        applySettings();
        if (modeChanged && player)
        {
            head = tail;
            releaseAll();
        }
    }
    const EngineSettings& getSettings() const { return settings; }
    int latencySamples() const { return settings.studio ? lookAheadSamples : 0; }
    int64_t now() const { return clock; }

    // Events, in time order. dur: the note's length in seconds when the caller knows it
    // (the renderer's score mode), 0 = unknown.
    void noteOn (int64_t when, int pitch, double vel127, double dur = 0.0)
    {
        push ({ when, Ev::on, pitch, vel127, dur });
    }
    void noteOff (int64_t when, int pitch) { push ({ when, Ev::off, pitch, 0, 0 }); }
    void controller (int64_t when, int cc, double v127) { push ({ when, Ev::cc, cc, v127, 0 }); }
    void allNotesOff (int64_t when) { push ({ when, Ev::allOff, 0, 0, 0 }); }

    // Audio thread: n samples of stereo at 48 kHz.
    void render (float* outL, float* outR, int n)
    {
        while (n > 0)
        {
            const int m = std::min (n, Radiation::maxBlock);
            double force[Radiation::maxBlock];
            for (int i = 0; i < m; ++i)
            {
                dispatch();
                double vb[4], fb[4];
                player->tick (vb, fb);
                const int over = (int) std::lround (violin->p.fs / rate);
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
                }
                force[i] = dec.out();
                scope[(size_t) (scopeWrite++ & (scopeSize - 1))] = (float) force[i];
                ++clock;
            }
            scopeWritten.store (scopeWrite, std::memory_order_release);
            radiation->process (force, outL, outR, m);
            outL += m;
            outR += m;
            n -= m;
        }
    }

    // ---------------------------------------------------------------- for the plugin's displays
    // What the player did, for the plan lane and the curves (written by the audio thread, read by
    // the editor: a reader keeps its own count and reads entries newer than it, at most
    // logSize - 64 behind the writer).
    struct NoteLog
    {
        enum Kind : int
        {
            stroke,
            slur,
            off,
            planned
        };
        double t; // engine seconds when it sounds (planned: when it will)
        int pitch, string, kind;
        double dir, dur; // dir: +1 down-bow, -1 up-bow; planned: the length if known
    };
    static constexpr uint64_t logSize = 1024;
    const NoteLog& logEntry (uint64_t i) const { return noteLog[i & (logSize - 1)]; }
    uint64_t logCount() const { return logWritten.load (std::memory_order_acquire); }
    // the most recent bridge force (48 kHz), for the string-motion scope
    static constexpr int scopeSize = 4096;
    float scopeSample (int64_t i) const { return scope[(size_t) (i & (scopeSize - 1))]; }
    int64_t scopeCount() const { return scopeWritten.load (std::memory_order_acquire); }
    double seconds() const { return (double) clock / rate; }

    // for tests and the renderer
    Player& getPlayer() { return *player; }
    Violin& getViolin() { return *violin; }
    Radiation& getRadiation() { return *radiation; }

private:
    struct Ev
    {
        enum Type : int
        {
            on,
            off,
            cc,
            allOff
        };
        int64_t t;
        int type;
        int a;
        double b, dur;
    };

    void applySettings()
    {
        if (! radiation)
            return;
        radiation->setBrightness (settings.brightnessDb);
        radiation->setHall (settings.hall);
        radiation->setReverbGain (std::pow (10.0, settings.reverbDb / 20.0));
        radiation->setOutputGain (std::pow (10.0, settings.volumeDb / 20.0));
        radiation->setViolin (settings.violin);
        radiation->setMic (settings.mic);
        radiation->setWidth (settings.width);
        radiation->setMovement (settings.movement);
        radiation->setDistance (settings.distance);
        radiation->setBridge (settings.bridgeHz, settings.mute);
        violin->p.slipNoise = hissBase * settings.hiss;
        player->pp.vibAmount = settings.vibrato;
        player->pp.velCurve = settings.velocityCurve;
        player->pp.dynBias = settings.dynamics;
    }

    void push (Ev e)
    {
        if (settings.studio)
        {
            e.t += lookAheadSamples;
            if (e.type == Ev::on)
                log ({ (double) e.t / rate, e.a, -1, NoteLog::planned, 0, 0 });
            // a note that is already sounding learns where it ends
            if (e.type == Ev::off && e.a >= 0 && e.a < 128 && sounding[e.a])
            {
                player->notePlanEnd (e.a, player->t + (double) (e.t - clock) / rate);
                sounding[e.a] = 0;
            }
        }
        if (tail > head && e.t < queue[(tail - 1) & qmask].t)
            e.t = queue[(tail - 1) & qmask].t; // keep the queue in time order
        if (tail - head > qmask)
        {
            if (e.type == Ev::cc)
                return; // full: controllers are dropped before notes
            ++head; // drop the oldest
        }
        queue[tail & qmask] = e;
        ++tail;
    }

    void dispatch()
    {
        while (head < tail && queue[head & qmask].t <= clock)
        {
            const Ev& e = queue[head & qmask];
            ++head;
            switch (e.type)
            {
                case Ev::on:
                {
                    double dur = e.dur;
                    if (settings.studio && dur <= 0.0)
                    {
                        // the note's end, if it is already queued
                        for (uint64_t k = head; k < tail; ++k)
                        {
                            const Ev& f = queue[k & qmask];
                            if (f.a == e.a && (f.type == Ev::off || f.type == Ev::on))
                            {
                                dur = (double) (f.t - e.t) / rate;
                                break;
                            }
                            if (f.type == Ev::allOff)
                            {
                                dur = (double) (f.t - e.t) / rate;
                                break;
                            }
                        }
                        if (e.a >= 0 && e.a < 128)
                            sounding[e.a] = dur <= 0.0;
                    }
                    held[e.a & 127] = true;
                    lookAhead (e, dur);
                    player->nextDur = dur;
                    player->noteOn (e.a, e.b);
                    log ({ seconds(),
                           e.a,
                           player->lastString,
                           player->strokeStart == player->t ? NoteLog::stroke : NoteLog::slur,
                           player->dir,
                           dur });
                    break;
                }
                case Ev::off:
                    held[e.a & 127] = false;
                    player->noteOff (e.a);
                    log ({ seconds(), e.a, -1, NoteLog::off, 0, 0 });
                    break;
                case Ev::cc:
                    player->controller (e.a, e.b);
                    break;
                case Ev::allOff:
                    releaseAll();
                    break;
            }
        }
    }

    // M4: the notes queued after e (Studio look-ahead, or a score whose lengths are known) for
    // the player's fingering and phrase plan. Live mode sees none: it never waits.
    void lookAhead (const Ev& e, double dur)
    {
        player->nAhead = 0;
        player->aheadValid = settings.studio || dur > 0.0;
        if (! player->aheadValid)
            return;
        for (uint64_t k = head; k < tail && player->nAhead < Player::maxAhead; ++k)
        {
            const Ev& f = queue[k & qmask];
            if (f.t - e.t > lookAheadSamples || f.type == Ev::allOff)
                break;
            if (f.type != Ev::on)
                continue;
            double fd = f.dur;
            for (uint64_t j = k + 1; fd <= 0.0 && j < tail; ++j)
            {
                const Ev& g = queue[j & qmask];
                if ((g.a == f.a && (g.type == Ev::off || g.type == Ev::on)) || g.type == Ev::allOff)
                    fd = (double) (g.t - f.t) / rate;
            }
            player->ahead[player->nAhead++] = { (double) (f.t - e.t) / rate, f.a, fd, f.b };
        }
    }

    void log (const NoteLog& n)
    {
        const uint64_t w = logWritten.load (std::memory_order_relaxed);
        noteLog[w & (logSize - 1)] = n;
        logWritten.store (w + 1, std::memory_order_release);
    }

    void releaseAll()
    {
        for (int p = 0; p < 128; ++p)
            if (held[p])
            {
                held[p] = false;
                player->noteOff (p);
            }
        for (auto& h : sounding)
            h = 0;
    }

    std::unique_ptr<Violin> violin;
    double hissBase = Params {}.slipNoise;
    std::unique_ptr<Player> player;
    std::unique_ptr<Radiation> radiation;
    Decim dec, dec2;
    EngineSettings settings;
    unsigned seed = 1;
    int64_t clock = 0;

    static constexpr uint64_t qmask = 8191;
    Ev queue[qmask + 1];
    uint64_t head = 0, tail = 0;
    bool held[128] = {};
    char sounding[128] = {}; // Studio: started without a known end
    NoteLog noteLog[logSize] = {};
    std::atomic<uint64_t> logWritten { 0 };
    float scope[scopeSize] = {};
    int64_t scopeWrite = 0;
    std::atomic<int64_t> scopeWritten { 0 };
};
} // namespace o2
