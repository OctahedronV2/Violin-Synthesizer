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
    // bridge (M3): Params::sympathetic, wolf, hold, each 0..1
    double sympathetic = 0.5; // idle open strings: 0 quiet, 0.5 natural (half-coupled), 1 full and long
    double wolf = 0.0; // 0: the body as measured; 1: a strong wolf near the main body mode
    double hold = 0.5; // 0: free (hanging); 0.5: held; 1: held firmly (low modes damped)
    // player (M4, M5)
    int articulation = 0; // PlayerParams::articulation: arco, pizz, Bartok pizz, left-hand pizz, harmonic
    int bowStyle = 0; // PlayerParams::bowStyle: auto, legato, detache, staccato, martele, spiccato
    double phrasing = 1.0; // PlayerParams::phrase
    bool fingerPlan = false; // Viterbi fingering and anticipated shifts (Studio look-ahead)
    bool drawnCurves = true; // CC lanes take over the player's dimension
    bool modalBody = true; // the bridge modes radiate below 1.5 kHz (Radiation::setModalBody)
    // M6 views: the Play, Bow and Left hand tabs' controls (PlayerParams' M6 block)
    double portamento = 1.0; // PlayerParams::slideScale
    double stringPreference = 0.0; // PlayerParams::stringPref, -1 bright .. +1 dark
    double vibratoRate = 0.0; // Hz, PlayerParams::vibRateAdd
    double vibratoDelay = 1.0; // PlayerParams::vibDelayScale
    double bowChange = 1.0; // PlayerParams::accelScale
    double strokeShaping = 1.0; // PlayerParams::shapeAmount
    double bite = 1.0; // PlayerParams::biteScale
    double contact = 1.0; // PlayerParams::contactScale
    // M7 player: style (o2::PlayerStyle), intonation (o2::Intonation), Key (tonic pitch class
    // for Just, Pythagorean and Expressive, 0 = C), A4 reference (Hz)
    int playerStyle = 0;
    int intonation = 0;
    int tuningKey = 0;
    double a4 = 440.0;
    // M7 instrument: parts and extended articulations
    int strings = 0; // Violin::setStringSet: 0 synthetic, 1 gut, 2 steel
    int rosin = 1; // Violin::rosin: 0 light, 1 standard, 2 dark, 3 baroque
    int bow = 0; // 0 modern, 1 baroque (Engine::applyBow)
    int contactStyle = 0; // PlayerParams::contact: 0 ordinario, 1 sul ponticello, 2 sul tasto
    double tremoloRate = 12.0; // strokes per second (free tremolo)
    int tremoloSync = 0; // 0 free, 1 16ths, 2 16th triplets, 3 32nds (needs tempo)
    double tempo = 0.0; // the host's tempo, bpm (0 = unknown)
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
        rosinApplied = 1;
        bowApplied = 0;
        this->seed = seed;
        reset();
    }

    // Message thread (allocates nothing, but re-initialises every string).
    void reset()
    {
        violin->init();
        tuningDirty = true; // M7: the strings are back at their own tuning
        for (int i = 0; i < 4; ++i)
        {
            baseF0[i] = violin->s[i].d.f0;
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
        lowDec = Decim();
        lowDec2 = Decim();
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
    // M7 MPE, per note by its pitch: bend in cents, pressure 0..1 (-1: none), timbre (CC74) 0..127.
    // Sent before the note-on, they set how the note starts.
    void noteBend (int64_t when, int pitch, double cents) { push ({ when, Ev::bend, pitch, cents, 0 }); }
    void notePressure (int64_t when, int pitch, double v) { push ({ when, Ev::pressure, pitch, v, 0 }); }
    void noteTimbre (int64_t when, int pitch, double v127) { push ({ when, Ev::timbre, pitch, v127, 0 }); }

    // M7: a tuning table (cents on 12-TET at A440 per MIDI note) for the Scala and MTS-ESP
    // systems. transposes: the A4 setting moves it too. Audio thread (or before playing); copies.
    void setTuningTable (const double* cents, bool transposes)
    {
        bool same = transposes == tableTransposes && hasTable;
        for (int n = 0; n < 128 && same; ++n)
            same = std::abs (cents[n] - table[n]) < 0.01;
        if (same)
            return;
        std::copy (cents, cents + 128, table);
        tableTransposes = transposes;
        hasTable = true;
        tuningDirty = true;
        applyTuning();
    }
    void clearTuningTable()
    {
        if (hasTable)
        {
            hasTable = false;
            tuningDirty = true;
            applyTuning();
        }
    }

    // Audio thread: n samples of stereo at 48 kHz.
    void render (float* outL, float* outR, int n)
    {
        while (n > 0)
        {
            const int m = std::min (n, Radiation::maxBlock);
            double force[Radiation::maxBlock], direct[Radiation::maxBlock], low[Radiation::maxBlock];
            for (int i = 0; i < m; ++i)
            {
                dispatch();
                double vb[4], fb[4];
                player->tick (vb, fb);
                const int over = (int) std::lround (violin->p.fs / rate);
                direct[i] = 0.0;
                for (int k = 0; k < over; ++k)
                {
                    const double F = violin->tick (vb, fb);
                    for (int s = 0; s < 4; ++s) // M5: the Bartok slap heard from the fingerboard
                        direct[i] += violin->s[s].direct / over;
                    if (over == 4)
                    {
                        dec2.push (F);
                        lowDec2.push (violin->radLow);
                        if (k & 1)
                        {
                            dec.push (dec2.out());
                            lowDec.push (lowDec2.out());
                        }
                    }
                    else
                    {
                        dec.push (F);
                        lowDec.push (violin->radLow);
                    }
                }
                force[i] = dec.out();
                low[i] = lowDec.out();
                scope[(size_t) (scopeWrite++ & (scopeSize - 1))] = (float) force[i];
                if (--traceLeft <= 0)
                {
                    traceLeft = traceEvery;
                    traceNow();
                }
                ++clock;
            }
            scopeWritten.store (scopeWrite, std::memory_order_release);
            radiation->process (force, violin->hasModalBody() ? low : nullptr, outL, outR, m);
            const double dg = directGain * std::pow (10.0, settings.volumeDb / 20.0);
            for (int i = 0; i < m; ++i) // M5: sound that does not come through the bridge (0 unless a Bartok slap)
            {
                outL[i] += (float) (dg * direct[i]);
                outR[i] += (float) (dg * direct[i]);
            }
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

    // M6: the bow and the left hand every 5 ms of engine time (the Bow and Left hand tabs). Read
    // as the note log: a reader keeps its own count, at most traceSize - 64 behind the writer.
    struct Trace
    {
        float t = 0; // engine seconds
        float speed = 0; // bow speed, m/s, + down-bow
        float force = 0; // N on the sounding string
        float hair = 0; // where the string is on the hair, 0 frog .. 1 tip
        float contact = 0; // bow-bridge distance / string length
        float dynamics = 0; // 0..1
        float pitch = 0; // finger pitch on the sounding string (MIDI, with slide and vibrato)
        float target = 0; // the note the finger goes to
        float handPos = 2; // semitones above the open string where the first finger sits
        float vibWidth = 0; // cents peak to peak
        int string = 2; // 0 G .. 3 E
        bool sounding = false, sliding = false, changing = false;
    };
    static constexpr uint64_t traceSize = 4096; // 20 s
    static constexpr int traceEvery = 240; // samples (5 ms)
    const Trace& traceEntry (uint64_t i) const { return trace[i & (traceSize - 1)]; }
    uint64_t traceCount() const { return traceWritten.load (std::memory_order_acquire); }

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
            allOff,
            bend, // M7 MPE
            pressure,
            timbre
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
        applyStyle();
        radiation->setBrightness (settings.brightnessDb);
        radiation->setHall (settings.hall);
        radiation->setReverbGain (std::pow (10.0, settings.reverbDb / 20.0));
        radiation->setOutputGain (std::pow (10.0, settings.volumeDb / 20.0));
        radiation->setViolin (settings.violin);
        violin->setBody (settings.violin);
        radiation->setModalBody (settings.modalBody, violin->modalDelay());
        radiation->setMic (settings.mic);
        radiation->setWidth (settings.width);
        radiation->setMovement (settings.movement);
        radiation->setDistance (settings.distance);
        radiation->setBridge (settings.bridgeHz, settings.mute);
        violin->p.slipNoise = hissBase * settings.hiss;
        violin->p.sympathetic = settings.sympathetic;
        if (violin->p.wolf != settings.wolf || violin->p.hold != settings.hold)
        {
            violin->p.wolf = settings.wolf;
            violin->p.hold = settings.hold;
            violin->applyBody();
        }
        player->pp.articulation = settings.articulation;
        player->pp.bowStyle = settings.bowStyle;
        player->pp.phrase = settings.phrasing;
        player->pp.fingerPlan = player->pp.anticipate = settings.fingerPlan ? 1.0 : 0.0;
        player->pp.drawnCurves = settings.drawnCurves ? 1.0 : 0.0;
        player->pp.vibAmount = settings.vibrato;
        player->pp.velCurve = settings.velocityCurve;
        player->pp.dynBias = settings.dynamics;
        player->pp.slideScale = settings.portamento;
        player->pp.stringPref = settings.stringPreference;
        player->pp.vibRateAdd = settings.vibratoRate;
        player->pp.vibDelayScale = settings.vibratoDelay;
        player->pp.accelScale = settings.bowChange;
        player->pp.shapeAmount = settings.strokeShaping;
        player->pp.biteScale = settings.bite;
        player->pp.contactScale = settings.contact;
        // M7 instrument (each part is written only when it changes, so the renderer's experiments
        // on the same fields survive)
        violin->setStringSet (settings.strings);
        if (settings.rosin != rosinApplied)
        {
            rosinApplied = settings.rosin;
            violin->setRosin (settings.rosin);
        }
        if (settings.bow != bowApplied)
        {
            bowApplied = settings.bow;
            applyBow (settings.bow);
        }
        player->pp.contact = settings.contactStyle;
        player->pp.tremoloRate = settings.tremoloRate;
        player->pp.tremoloSync = settings.tremoloSync;
        player->pp.tempo = settings.tempo;
        applyTuning();
    }

    // M7: the player style, as offsets on the parameters it had in Modern soloist (the base,
    // kept while another style plays, so switching back restores them exactly)
    void applyStyle()
    {
        const int style = std::clamp (settings.playerStyle, 0, (int) styleCount - 1);
        if (style == styleNow)
            return;
        if (styleNow == styleModern)
            styleBase = player->pp;
        player->pp = styleBase;
        o2::applyStyle (player->pp, style);
        player->pp.seed = seed;
        styleNow = style;
        tuningDirty = true;
        bowApplied = -1; // the style reset the bow's player fields: write them again
    }

    // M7: the intonation system -> the player's pitch table and the open strings' tuning
    void applyTuning()
    {
        const int sys = std::clamp (settings.intonation, 0, (int) intonCount - 1);
        if (! tuningDirty && sys == tunedSys && settings.tuningKey == tunedKey && settings.a4 == tunedA4
            && player->pp.intonAmount == tunedAmount)
            return;
        tuningDirty = false;
        tunedSys = sys;
        tunedKey = settings.tuningKey;
        tunedA4 = settings.a4;
        tunedAmount = player->pp.intonAmount;
        player->setTuning (sys,
                           ((settings.tuningKey % 12) + 12) % 12,
                           settings.a4,
                           player->pp.intonAmount,
                           hasTable ? table : nullptr,
                           tableTransposes);
        for (int i = 0; i < 4; ++i)
        {
            const double f0 = baseF0[i] * std::pow (2.0, player->openCents[i] / 1200.0 * (player->tuneOn ? 1.0 : 0.0));
            if (f0 != violin->s[i].d.f0)
            {
                violin->s[i].d.f0 = f0;
                player->retuneOpen (i);
            }
        }
    }

    // M7: the bow. Modern = the defaults. Baroque: shorter (56 cm of hair) and lighter (the player
    // presses at most 1.4 N), fewer hairs (8 mm ribbon) at a lower tension (softer hair), light at
    // the tip, and the baroque player's strokes: each separate note breathes out (lift-off stroke)
    // and short notes lift off the string and ring instead of stopping on it.
    void applyBow (int b)
    {
        const Params P;
        PlayerParams Q; // the modern bow: as the current player style plays it
        o2::applyStyle (Q, styleNow);
        auto& p = violin->p;
        auto& q = player->pp;
        const bool baroque = b == 1;
        p.bowWidth = baroque ? 0.008 : P.bowWidth;
        p.hairStiffness = baroque ? 80000.0 : P.hairStiffness;
        q.bowLength = baroque ? 0.56 : Q.bowLength;
        q.forceCap = baroque ? 1.4 : Q.forceCap;
        q.tipLight = baroque ? 0.3 : Q.tipLight;
        q.liftStroke = baroque ? 0.3 : Q.liftStroke;
        q.stopBelow = baroque ? 0.0 : Q.stopBelow;
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
                case Ev::bend:
                    player->mpeBend (e.a, e.b);
                    break;
                case Ev::pressure:
                    player->mpePressure (e.a, e.b);
                    break;
                case Ev::timbre:
                    player->mpeTimbre (e.a, e.b);
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

    void traceNow()
    {
        const Player& p = *player;
        const int s = std::clamp (p.lastString, 0, 3);
        const auto& S = p.st[s];
        Trace e;
        e.t = (float) seconds();
        e.speed = (float) p.v;
        e.force = (float) S.force;
        e.hair = (float) (p.hair / p.pp.bowLength);
        e.contact = (float) p.betaFor (s);
        e.dynamics = (float) p.dEff();
        e.pitch = (float) S.pitch;
        e.target = (float) S.target;
        e.handPos = (float) p.handPos;
        e.vibWidth = (float) S.vibWidth;
        e.string = s;
        e.sounding = p.nHeld > 0 && ! p.releasing;
        e.sliding = S.slideT0 >= 0.0;
        e.changing = p.changing;
        const uint64_t w = traceWritten.load (std::memory_order_relaxed);
        trace[w & (traceSize - 1)] = e;
        traceWritten.store (w + 1, std::memory_order_release);
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

    double directGain = 0.1; // M5: N of slap -> output units (fitted to the Philharmonia snap pizz)
    std::unique_ptr<Violin> violin;
    double hissBase = Params {}.slipNoise;
    std::unique_ptr<Player> player;
    std::unique_ptr<Radiation> radiation;
    Decim dec, dec2, lowDec, lowDec2;
    EngineSettings settings;
    unsigned seed = 1;
    // M7 styles and intonation
    PlayerParams styleBase;
    int styleNow = styleModern;
    bool tuningDirty = true, hasTable = false, tableTransposes = true;
    int tunedSys = -1, tunedKey = -1;
    double tunedA4 = 0.0, tunedAmount = -1.0;
    double table[128] = {};
    double baseF0[4] = {};
    int rosinApplied = 1, bowApplied = 0; // M7: the parts last written into the strings and player
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
    Trace trace[traceSize] = {};
    std::atomic<uint64_t> traceWritten { 0 };
    int traceLeft = traceEvery;
};
} // namespace o2
