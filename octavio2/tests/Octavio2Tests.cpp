// Octavio 2 plugin tests: the engine plays, Studio mode looks ahead, the processor runs at the
// host's rate, notes outside the violin stay silent, state round-trips.

#include "../plugin/PluginEditor.h"
#include "../plugin/PluginProcessor.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <map>
#include <string>

namespace
{
struct Level
{
    double rms = 0, peak = 0;
    bool finite = true;
};

Level measure (const juce::AudioBuffer<float>& b, int from = 0, int to = -1)
{
    Level l;
    if (to < 0)
        to = b.getNumSamples();
    double sum = 0;
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = from; i < to; ++i)
        {
            const float x = b.getSample (c, i);
            l.finite = l.finite && std::isfinite (x);
            sum += x * x;
            l.peak = std::max (l.peak, (double) std::abs (x));
        }
    l.rms = std::sqrt (sum / std::max (1, (to - from) * b.getNumChannels()));
    return l;
}

double dB (double x)
{
    return 20 * std::log10 (x + 1e-30);
}

// Plays one note through the processor (on at the start, off after `noteSeconds`) and returns
// `seconds` of output.
juce::AudioBuffer<float>
play (octavio2::Processor& p, double rate, int note, double noteSeconds, double seconds, int velocity = 100)
{
    const int block = 480;
    p.prepareToPlay (rate, block);
    const int total = (int) (seconds * rate);
    juce::AudioBuffer<float> out (2, total);
    juce::AudioBuffer<float> buf (2, block);
    const int off = (int) (noteSeconds * rate);
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = std::min (block, total - pos);
        buf.setSize (2, n, false, false, true);
        juce::MidiBuffer midi;
        if (pos == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) velocity), 0);
        if (off >= pos && off < pos + n)
            midi.addEvent (juce::MidiMessage::noteOff (1, note), off - pos);
        p.processBlock (buf, midi);
        for (int c = 0; c < 2; ++c)
            out.copyFrom (c, pos, buf, c, 0, n);
    }
    return out;
}

void setParam (octavio2::Processor& p, const juce::String& id, float value)
{
    auto* param = p.getParameters().getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}
} // namespace

TEST_CASE ("Octavio 2 plays a violin note at 48 kHz and at 44.1 kHz", "[octavio2]")
{
    for (const double rate : { 48000.0, 44100.0 })
    {
        octavio2::Processor p;
        setParam (p, "octave", 2.0f); // index 2 = no shift: MIDI 69 sounds A4
        const auto out = play (p, rate, 69, 1.0, 1.5);
        const auto sounding = measure (out, (int) (0.2 * rate), (int) (1.0 * rate));
        INFO ("rate " << rate << ": " << dB (sounding.rms) << " dB RMS, peak " << dB (sounding.peak) << " dB");
        CHECK (sounding.finite);
        CHECK (dB (sounding.rms) > -45.0);
        CHECK (sounding.peak < 1.0);
    }
}

TEST_CASE ("Octavio 2 is silent for notes outside the violin", "[octavio2]")
{
    octavio2::Processor p;
    setParam (p, "octave", 2.0f);
    setParam (p, "room", 0.0f); // no hall, so nothing else can sound
    const auto out = play (p, 48000.0, 40, 0.5, 1.0); // E2, below the G string
    CHECK (measure (out).peak < 1e-6);
}

TEST_CASE ("Octavio 2 plays through every violin, microphone, mute and distance", "[octavio2]")
{
    // each setting loads its own impulse responses or filters: all must sound, none blow up
    struct Setting
    {
        const char* id;
        float value;
    };
    const Setting settings[]
        = { { "violin", 1 }, { "violin", 2 },  { "violin", 3 },     { "mic", 1 },         { "mic", 2 },
            { "mic", 3 },    { "mute", 1 },    { "mute", 2 },       { "distance", 0.5f }, { "distance", 10 },
            { "width", 0 },  { "width", 200 }, { "movement", 100 }, { "bridge", 2400 },   { "bridge", 3600 } };
    for (const auto& s : settings)
    {
        octavio2::Processor p;
        setParam (p, "octave", 2.0f);
        setParam (p, s.id, s.value);
        const auto out = play (p, 48000.0, 69, 1.0, 1.5);
        const auto sounding = measure (out, (int) (0.2 * 48000), 48000);
        INFO (s.id << " = " << s.value << ": " << dB (sounding.rms) << " dB RMS, peak " << dB (sounding.peak) << " dB");
        CHECK (sounding.finite);
        CHECK (dB (sounding.rms) > (std::string (s.id) == "mute" ? -60.0 : -45.0));
        CHECK (sounding.peak < 1.0);
    }
}

TEST_CASE ("Octavio 2 plays every articulation, bow style and bridge setting", "[octavio2]")
{
    struct Setting
    {
        const char* id;
        float value;
        double minDb;
    };
    // plucked notes die away and harmonics are quieter, so they get a lower floor
    const Setting settings[] = { { "articulation", 1, -65 }, { "articulation", 2, -65 },  { "articulation", 3, -75 },
                                 { "articulation", 4, -60 }, { "bowStyle", 1, -45 },      { "bowStyle", 2, -45 },
                                 { "bowStyle", 3, -60 },     { "bowStyle", 4, -60 },      { "bowStyle", 5, -65 },
                                 { "sympathetic", 0, -45 },  { "sympathetic", 100, -45 }, { "wolf", 100, -45 },
                                 { "hold", 0, -45 },         { "hold", 100, -45 },        { "phrasing", 0, -45 },
                                 { "phrasing", 200, -45 },   { "fingerPlan", 1, -45 },    { "mode", 1, -45 } };
    for (const auto& s : settings)
    {
        octavio2::Processor p;
        setParam (p, "octave", 2.0f);
        setParam (p, s.id, s.value);
        const auto out = play (p, 48000.0, 69, 1.0, 2.5);
        const auto sounding = measure (out, 0, (int) (2.5 * 48000));
        INFO (s.id << " = " << s.value << ": " << dB (sounding.rms) << " dB RMS, peak " << dB (sounding.peak) << " dB");
        CHECK (sounding.finite);
        CHECK (dB (sounding.rms) > s.minDb);
        CHECK (sounding.peak < 1.0);
    }
}

TEST_CASE ("Octavio 2 keyswitches pick the articulation whatever the Octave", "[octavio2]")
{
    // C#1 (MIDI 25) = pizzicato: the plucked note has died well before a bowed one would
    auto lateLevel = [] (bool keyswitch)
    {
        octavio2::Processor p;
        setParam (p, "room", 0.0f);
        const int block = 480;
        p.prepareToPlay (48000.0, block);
        juce::AudioBuffer<float> buf (2, block);
        double late = 0;
        for (int pos = 0, k = 0; pos < 3 * 48000; pos += block, ++k)
        {
            juce::MidiBuffer midi;
            if (k == 0 && keyswitch)
                midi.addEvent (juce::MidiMessage::noteOn (1, 25, (juce::uint8) 100), 0);
            if (k == 10)
                midi.addEvent (juce::MidiMessage::noteOn (1, 57, (juce::uint8) 100), 0); // A4 at Octave +1
            p.processBlock (buf, midi);
            if (pos > 2 * 48000)
                late = std::max (late, (double) buf.getMagnitude (0, block));
        }
        return late;
    };
    const double bowed = lateLevel (false), plucked = lateLevel (true);
    INFO ("bowed " << dB (bowed) << " dB, plucked " << dB (plucked) << " dB");
    CHECK (dB (plucked) < dB (bowed) - 10.0);
}

TEST_CASE ("Octavio 2's bridge stays stable at every Sympathetic, Wolf and Hold extreme", "[octavio2]")
{
    // M3 soak, short: the strings and the passive bridge alone, random bowing on all strings
    for (const double wolf : { 0.0, 1.0 })
        for (const double hold : { 0.0, 1.0 })
        {
            o2::Violin v;
            v.p.sympathetic = 1.0;
            v.p.wolf = wolf;
            v.p.hold = hold;
            v.init();
            o2::Rng rng;
            auto uniform = [&rng] { return 0.5 * (rng.uni() + 1.0); };
            double vb[4] = {}, fb[4] = {}, peak = 0, after = 0, tail = 0;
            bool finite = true;
            const int n = (int) (12 * v.fs);
            for (int i = 0; i < n; ++i)
            {
                if (i % 9600 == 0)
                    for (int s = 0; s < 4; ++s)
                    {
                        const bool bowing = i < n - (int) (4 * v.fs) && uniform() < 0.6;
                        vb[s] = bowing ? 0.05 + 0.4 * uniform() : 0.0;
                        fb[s] = bowing ? 0.2 + 1.8 * uniform() : 0.0;
                    }
                const double F = v.tick (vb, fb);
                finite = finite && std::isfinite (F);
                peak = std::max (peak, std::abs (F));
                if (i > n - (int) (4 * v.fs) + 4800 && i < n - (int) (3 * v.fs))
                    after = std::max (after, std::abs (F)); // just after the bows lift
                if (i > n - (int) v.fs)
                    tail = std::max (tail, std::abs (F));
            }
            INFO ("wolf " << wolf << ", hold " << hold << ": peak " << peak << " N, after the bows lift " << after
                          << " N, 3 s later " << tail << " N");
            CHECK (finite);
            CHECK (peak < 50.0);
            CHECK (tail < 0.3 * after); // the open strings ring on, but die away
        }
}

TEST_CASE ("Octavio 2 Studio mode plays its look-ahead later and reports it as latency", "[octavio2]")
{
    octavio2::Processor p;
    setParam (p, "octave", 2.0f);
    setParam (p, "room", 0.0f);
    setParam (p, "mode", 1.0f);
    const double rate = 48000.0;
    const auto out = play (p, rate, 69, 0.5, 2.2);
    CHECK (p.getLatencySamples() == o2::Engine::lookAheadSamples);
    const int la = o2::Engine::lookAheadSamples;
    CHECK (measure (out, 0, la - 100).peak < 1e-6);
    CHECK (dB (measure (out, la + 2400, la + 24000).rms) > -45.0);
}

TEST_CASE ("Octavio 2 renders the same performance identically", "[octavio2]")
{
    octavio2::Processor a, b;
    const auto x = play (a, 48000.0, 76, 0.6, 1.0);
    const auto y = play (b, 48000.0, 76, 0.6, 1.0);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < x.getNumSamples(); ++i)
            REQUIRE (x.getSample (c, i) == y.getSample (c, i));
}

TEST_CASE ("Octavio 2 saves and restores its parameters", "[octavio2]")
{
    octavio2::Processor a;
    setParam (a, "vibrato", 1.5f);
    setParam (a, "room", 3.0f);
    juce::MemoryBlock state;
    a.getStateInformation (state);
    octavio2::Processor b;
    b.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (std::abs (b.getParameters().getRawParameterValue ("vibrato")->load() - 1.5f) < 1e-4f);
    CHECK (juce::roundToInt (b.getParameters().getRawParameterValue ("room")->load()) == 3);
}

TEST_CASE ("Octavio 2's interface paints every tab while it plays", "[octavio2]")
{
    // OCTAVIO2_SNAPSHOTS=<folder> saves each tab as a PNG (Live, then Studio)
    const juce::ScopedJuceInitialiser_GUI gui;
    const auto folder = juce::SystemStats::getEnvironmentVariable ("OCTAVIO2_SNAPSHOTS", {});
    for (const bool studio : { false, true })
    {
        octavio2::Processor p;
        setParam (p, "mode", studio ? 1.0f : 0.0f);
        // a little run, the last note held
        const double rate = 48000.0;
        const int block = 480;
        p.prepareToPlay (rate, block);
        juce::AudioBuffer<float> buf (2, block);
        const int notes[] = { 62, 64, 66, 67, 69, 71, 73, 74, 76 };
        const int step = (int) (0.35 * rate / block);
        const int blocks = (int) (4.5 * rate / block);
        for (int b = 0; b < blocks; ++b)
        {
            juce::MidiBuffer midi;
            if (b % step == 0 && b / step < 9)
            {
                const int k = b / step;
                if (k > 0)
                    midi.addEvent (juce::MidiMessage::noteOff (1, notes[k - 1] - 12), 0);
                midi.addEvent (juce::MidiMessage::noteOn (1, notes[k] - 12, (juce::uint8) (80 + 4 * k)), 1);
            }
            p.processBlock (buf, midi);
        }
        for (int tab = 0; tab < 7; ++tab)
        {
            if (studio && tab != 0)
                break;
            p.editorTab = tab;
            octavio2::Editor editor (p);
            const auto image = editor.createComponentSnapshot (editor.getLocalBounds());
            CHECK (image.getWidth() == octavio2::ui::designWidth);
            if (folder.isNotEmpty())
            {
                juce::File file (folder + "/" + (studio ? "studio" : "live") + "-" + juce::String (tab) + ".png");
                file.deleteFile();
                juce::FileOutputStream out (file);
                juce::PNGImageFormat().writeImageToStream (image, out);
            }
        }
    }
}

// ---------------------------------------------------------------- M6 views (Play, Bow, Left hand)
TEST_CASE ("Octavio 2 plays at every extreme of the Bow and Left hand controls", "[octavio2]")
{
    const std::pair<const char*, float> settings[] = { { "portamento", 0 },
                                                       { "portamento", 300 },
                                                       { "stringPreference", -100 },
                                                       { "stringPreference", 100 },
                                                       { "vibratoRate", -1.5f },
                                                       { "vibratoRate", 1.5f },
                                                       { "vibratoDelay", 25 },
                                                       { "vibratoDelay", 300 },
                                                       { "bowChange", 50 },
                                                       { "bowChange", 200 },
                                                       { "strokeShaping", 0 },
                                                       { "strokeShaping", 150 },
                                                       { "bite", 0 },
                                                       { "bite", 200 },
                                                       { "contact", -50 },
                                                       { "contact", 50 } };
    for (const auto& [id, value] : settings)
    {
        octavio2::Processor p;
        setParam (p, "octave", 2.0f);
        setParam (p, id, value);
        const auto out = play (p, 48000.0, 69, 1.0, 2.0);
        const auto l = measure (out);
        INFO (id << " = " << value << ": " << dB (l.rms) << " dB RMS, peak " << dB (l.peak) << " dB");
        CHECK (l.finite);
        CHECK (dB (l.rms) > -45);
        CHECK (l.peak < 1.0);
    }
}

namespace
{
// a legato climb (each note overlaps the next: slurs), then separate notes coming down (strokes)
void playPhrase (octavio2::Processor& p, double seconds)
{
    const double rate = 48000.0;
    const int block = 480;
    p.prepareToPlay (rate, block);
    juce::AudioBuffer<float> buf (2, block);
    const int notes[] = { 62, 66, 69, 74, 78, 81, 79, 76, 72, 69, 67, 74 };
    const double step = 0.4;
    for (int b = 0; b < (int) (seconds * rate / block); ++b)
    {
        juce::MidiBuffer midi;
        const double t0 = b * block / rate, t1 = (b + 1) * block / rate;
        for (int k = 0; k < 12; ++k)
        {
            const double on = k * step, off = k < 11 ? on + step * (k < 6 ? 1.1 : 0.7) : seconds + 1;
            if (on >= t0 && on < t1)
                midi.addEvent (juce::MidiMessage::noteOn (1, notes[k], (juce::uint8) 96), (int) ((on - t0) * rate));
            if (off >= t0 && off < t1)
                midi.addEvent (juce::MidiMessage::noteOff (1, notes[k]), (int) ((off - t0) * rate));
        }
        p.processBlock (buf, midi);
    }
}
} // namespace

TEST_CASE ("Octavio 2 traces the bow and the left hand for the Bow and Left hand tabs", "[octavio2]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    int highest[2] = {};
    for (const int pref : { 0, 100 })
    {
        octavio2::Processor p;
        setParam (p, "octave", 2.0f);
        setParam (p, "stringPreference", (float) pref);
        playPhrase (p, 5.2);
        auto& e = p.getEngine();
        // the trace: one point every 5 ms, the bow both ways, the hair moving
        const auto n = e.traceCount();
        REQUIRE (n >= 1000);
        bool down = false, up = false, slid = false, finite = true;
        float hairLo = 1, hairHi = 0;
        for (uint64_t i = n - 1000; i < n; ++i)
        {
            const auto& t = e.traceEntry (i);
            finite = finite && std::isfinite (t.speed) && std::isfinite (t.pitch);
            down = down || t.speed > 0.05f;
            up = up || t.speed < -0.05f;
            slid = slid || t.sliding;
            hairLo = std::min (hairLo, t.hair);
            hairHi = std::max (hairHi, t.hair);
        }
        CHECK (finite);
        CHECK (down);
        CHECK (up);
        CHECK (hairHi - hairLo > 0.2f);
        // the highest note (A5): which string the player took
        for (uint64_t i = 0; i < e.logCount(); ++i)
            if (e.logEntry (i).pitch == 81 && e.logEntry (i).kind != o2::Engine::NoteLog::off)
                highest[pref != 0 ? 1 : 0] = e.logEntry (i).string;
        if (pref == 100)
            CHECK (slid); // the dark preference stays on lower strings and shifts up them
        // the Play, Bow and Left hand tabs mid-phrase
        const auto folder = juce::SystemStats::getEnvironmentVariable ("OCTAVIO2_SNAPSHOTS", {});
        for (const int tab : { 0, 2, 3 })
        {
            p.editorTab = tab;
            octavio2::Editor editor (p);
            const auto image = editor.createComponentSnapshot (editor.getLocalBounds());
            CHECK (image.getWidth() == octavio2::ui::designWidth);
            if (folder.isNotEmpty())
            {
                juce::File file (folder + "/phrase-pref" + juce::String (pref) + "-" + juce::String (tab) + ".png");
                file.deleteFile();
                juce::FileOutputStream out (file);
                juce::PNGImageFormat().writeImageToStream (image, out);
            }
        }
    }
    CHECK (highest[1] < highest[0]); // dark: A5 on a lower string
}

// M6: MIDI mapping, slur pedal, pitch bend, presets ------------------------------------------
#include "../plugin/ui/MidiView.h"

namespace
{
int parameterIndex (octavio2::Processor& p, const juce::String& id)
{
    const auto& ps = p.AudioProcessor::getParameters();
    for (int i = 0; i < ps.size(); ++i)
        if (auto* w = dynamic_cast<juce::AudioProcessorParameterWithID*> (ps[i]))
            if (w->paramID == id)
                return i;
    return -1;
}

void block (octavio2::Processor& p, std::initializer_list<juce::MidiMessage> messages, int n = 480)
{
    juce::AudioBuffer<float> buf (2, n);
    juce::MidiBuffer midi;
    for (const auto& m : messages)
        midi.addEvent (m, 0);
    p.processBlock (buf, midi);
}
} // namespace

TEST_CASE ("Octavio 2 saves its MIDI map with the project", "[octavio2][midi]")
{
    using octavio2::MidiMap;
    octavio2::Processor a;
    auto e = a.getMidiMap().getEntries();
    CHECK (a.getMidiMap().matchingPreset() == 0); // the Octavio 2 map by default
    e.push_back ({ 20, MidiMap::parameterBase + parameterIndex (a, "reverb"), 0.25f, 0.75f, true });
    e.erase (e.begin()); // CC1 -> dynamics removed
    a.getMidiMap().setEntries (e);
    CHECK (a.getMidiMap().matchingPreset() == -1);
    setParam (a, "bendRange", 7.0f);
    juce::MemoryBlock state;
    a.getStateInformation (state);

    octavio2::Processor b;
    b.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (b.getMidiMap().getEntries() == a.getMidiMap().getEntries());
    CHECK (juce::roundToInt (b.getParameters().getRawParameterValue ("bendRange")->load()) == 7);

    // a project saved before M6 (no map in it) gets the Octavio 2 map
    octavio2::Processor c;
    c.getMidiMap().loadPreset (MidiMap::Preset::none);
    const auto old = a.getParameters().copyState(); // the parameters only, as 2.1 saved them
    juce::MemoryBlock oldState;
    juce::AudioProcessor::copyXmlToBinary (*old.createXml(), oldState);
    c.setStateInformation (oldState.getData(), (int) oldState.getSize());
    CHECK (c.getMidiMap().matchingPreset() == 0);
}

TEST_CASE ("Octavio 2 mapped controllers move parameters and the player", "[octavio2][midi]")
{
    using octavio2::MidiMap;
    octavio2::Processor p;
    p.prepareToPlay (48000.0, 480);
    auto e = p.getMidiMap().getEntries();
    e.push_back ({ 20, MidiMap::parameterBase + parameterIndex (p, "reverb") });
    e.push_back ({ 20, MidiMap::parameterBase + parameterIndex (p, "width"), 0.0f, 0.5f, true });
    p.getMidiMap().setEntries (e);
    block (p, { juce::MidiMessage::controllerEvent (1, 20, 127) });
    // in this block already: the engine has the new settings
    CHECK (std::abs (p.getEngine().getSettings().reverbDb - 6.0) < 1e-3);
    CHECK (std::abs (p.getEngine().getSettings().width - 0.0) < 1e-3); // inverted: 127 -> range start
    block (p, { juce::MidiMessage::controllerEvent (1, 20, 0) });
    CHECK (std::abs (p.getParameters().getRawParameterValue ("reverb")->load() - -24.0f) < 1e-3f);
    CHECK (std::abs (p.getParameters().getRawParameterValue ("width")->load() - 100.0f) < 1e-2f);

    // the default map: CC1 is the player's dynamics (a drawn curve takes over)
    block (p, { juce::MidiMessage::controllerEvent (1, 1, 32) });
    block (p, {});
    CHECK (p.getEngine().getPlayer().manDyn);
    CHECK (std::abs (p.getEngine().getPlayer().ccDyn - 32.0 / 127.0) < 1e-6);
    block (p, { juce::MidiMessage::controllerEvent (1, 121, 0) });
    block (p, {});
    CHECK (! p.getEngine().getPlayer().manDyn);

    // the legacy map: CC1 is vibrato, dynamics stay the player's
    p.getMidiMap().loadPreset (MidiMap::Preset::legacy);
    block (p, { juce::MidiMessage::controllerEvent (1, 1, 0) });
    block (p, {});
    CHECK (! p.getEngine().getPlayer().manDyn);
    CHECK (p.getEngine().getPlayer().vibScale < 1e-6);

    // no map: controllers do nothing
    p.getMidiMap().loadPreset (MidiMap::Preset::none);
    block (p, { juce::MidiMessage::controllerEvent (1, 1, 100) });
    block (p, {});
    CHECK (! p.getEngine().getPlayer().manDyn);
}

TEST_CASE ("Octavio 2 slurs everything while the sustain pedal is down, and bends", "[octavio2][midi]")
{
    for (const bool pedal : { false, true })
    {
        octavio2::Processor p;
        p.prepareToPlay (48000.0, 480);
        if (pedal)
            block (p, { juce::MidiMessage::controllerEvent (1, 64, 127) });
        CHECK (p.isPedalDown() == pedal);
        // detached notes: the first ends well before the second starts
        block (p, { juce::MidiMessage::noteOn (1, 69 - 12, (juce::uint8) 90) });
        for (int i = 0; i < 40; ++i)
            block (p, {});
        block (p, { juce::MidiMessage::noteOff (1, 69 - 12) });
        for (int i = 0; i < 10; ++i)
            block (p, {});
        block (p, { juce::MidiMessage::noteOn (1, 71 - 12, (juce::uint8) 90) });
        block (p, {});
        CHECK ((p.getEngine().getPlayer().slurNotes > 0) == pedal);
        block (p, { juce::MidiMessage::pitchWheel (1, 16383) });
        block (p, {});
        CHECK (std::abs (p.getEngine().getPlayer().bendCents - 200.0 * 8191.0 / 8192.0) < 0.01); // +-2 st default
        block (p, { juce::MidiMessage::controllerEvent (1, 64, 0) });
        CHECK (! p.isPedalDown());
    }
}

TEST_CASE ("Octavio 2 Learn assigns the controller that moves", "[octavio2][midi]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    using octavio2::MidiMap;
    octavio2::Processor p;
    p.prepareToPlay (48000.0, 480);
    octavio2::ui::MidiView view (p);
    const int target = MidiMap::parameterBase + parameterIndex (p, "brightness");
    const auto before = p.getMidiMap().getEntries().size();
    view.startLearn (target);
    CHECK (view.isLearning());
    view.poll();
    block (p, { juce::MidiMessage::controllerEvent (1, 85, 100) });
    view.poll();
    CHECK (! view.isLearning());
    const auto e = p.getMidiMap().getEntries();
    REQUIRE (e.size() == before + 1);
    CHECK (e.back().cc == 85);
    CHECK (e.back().target == target);
    // and it plays
    block (p, { juce::MidiMessage::controllerEvent (1, 85, 0) });
    CHECK (std::abs (p.getParameters().getRawParameterValue ("brightness")->load() - -6.0f) < 1e-3f);
}

TEST_CASE ("Octavio 2 factory presets load and play cleanly", "[octavio2][presets]")
{
    octavio2::Processor p;
    auto& presets = p.getPresets();
    presets.setUserFolder (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("o2-no-presets"));
    REQUIRE (presets.numFactory() >= 8);
    CHECK (presets.currentName() == "Concert Soloist");
    CHECK (! presets.isModified());
    for (int i = 0; i < presets.numFactory(); ++i)
    {
        INFO (presets.list()[(size_t) i].name);
        REQUIRE (presets.load (i));
        CHECK (! presets.isModified());
        // a phrase (Octave +1): a soft low note, a loud one, a high one, a fast run. Not double
        // stops: two strings at ff reach +4..+6 dBFS in the default sound too (the engine's level,
        // unchanged by M6; see the M6 report)
        const double rate = 48000.0;
        const int n = 480;
        p.prepareToPlay (rate, n);
        juce::AudioBuffer<float> buf (2, n);
        double peak = 0, sum = 0;
        bool finite = true;
        long count = 0;
        double segPeak[5] = {};
        const int total = (int) (5.0 * rate / n);
        for (int b = 0; b < total; ++b)
        {
            juce::MidiBuffer midi;
            const double t = b * n / rate;
            auto at = [&] (double when, const juce::MidiMessage& m)
            {
                const int s = (int) std::lround (when * rate) - b * n;
                if (s >= 0 && s < n)
                    midi.addEvent (m, s);
            };
            at (0.0, juce::MidiMessage::noteOn (1, 55, (juce::uint8) 40));
            at (1.0, juce::MidiMessage::noteOff (1, 55));
            at (1.05, juce::MidiMessage::noteOn (1, 64, (juce::uint8) 127));
            at (2.0, juce::MidiMessage::noteOff (1, 64));
            at (2.05, juce::MidiMessage::noteOn (1, 88, (juce::uint8) 110));
            at (2.9, juce::MidiMessage::noteOff (1, 88));
            for (int k = 0; k < 8; ++k)
            {
                const int note = 50 + (k * 5) % 14;
                at (3.0 + 0.12 * k, juce::MidiMessage::noteOn (1, note, (juce::uint8) 100));
                at (3.0 + 0.12 * k + 0.1, juce::MidiMessage::noteOff (1, note));
            }
            at (4.0, juce::MidiMessage::allNotesOff (1));
            p.processBlock (buf, midi);
            const auto l = measure (buf);
            finite = finite && l.finite;
            peak = std::max (peak, l.peak);
            segPeak[std::min (4, (int) t)] = std::max (segPeak[std::min (4, (int) t)], l.peak);
            sum += l.rms * l.rms;
            ++count;
        }
        CHECK (finite);
        CHECK (peak < 1.0);
        CHECK (dB (std::sqrt (sum / count)) > -60.0); // it plays
        UNSCOPED_INFO (presets.list()[(size_t) i].name
                       << ": peak " << dB (peak) << " dBFS, rms " << dB (std::sqrt (sum / count)) << " dBFS; by second "
                       << dB (segPeak[0]) << " " << dB (segPeak[1]) << " " << dB (segPeak[2]) << " " << dB (segPeak[3])
                       << " " << dB (segPeak[4]));
    }
    // the modified marker, next and previous
    presets.load (0);
    setParam (p, "vibrato", 1.9f);
    CHECK (presets.isModified());
    presets.loadNext();
    CHECK (presets.currentIndex() == 1);
    presets.loadPrevious();
    presets.loadPrevious();
    CHECK (presets.currentIndex() == presets.numFactory() - 1);
}

TEST_CASE ("Octavio 2 saves and loads user presets", "[octavio2][presets]")
{
    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("o2-presets-test");
    folder.deleteRecursively();
    octavio2::Processor p;
    auto& presets = p.getPresets();
    presets.setUserFolder (folder);
    setParam (p, "vibrato", 0.3f);
    setParam (p, "room", 4.0f);
    setParam (p, "octave", 4.0f); // a performance setting: not in presets
    REQUIRE (presets.save ("My Church").wasOk());
    CHECK (presets.currentName() == "My Church");
    CHECK (! presets.isModified());
    CHECK (presets.save ("Concert Soloist").failed()); // a factory name
    presets.load (0);
    CHECK (std::abs (p.getParameters().getRawParameterValue ("vibrato")->load() - 1.0f) < 1e-4f);
    CHECK (juce::roundToInt (p.getParameters().getRawParameterValue ("octave")->load()) == 4);
    REQUIRE ((int) presets.list().size() == presets.numFactory() + 1);
    presets.load (presets.numFactory());
    CHECK (std::abs (p.getParameters().getRawParameterValue ("vibrato")->load() - 0.3f) < 1e-4f);
    CHECK (juce::roundToInt (p.getParameters().getRawParameterValue ("room")->load()) == 4);
    // the preset's name comes back with the project
    juce::MemoryBlock state;
    p.getStateInformation (state);
    octavio2::Processor q;
    q.getPresets().setUserFolder (folder);
    q.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (q.getPresets().currentName() == "My Church");
    CHECK (! q.getPresets().isModified());
    REQUIRE (presets.remove (presets.currentIndex()).wasOk());
    CHECK ((int) presets.list().size() == presets.numFactory());
    folder.deleteRecursively();
}

// ---------------------------------------------------------------- M7 player: styles, intonation, MPE
namespace
{
// processes `blocks` blocks of 480 samples at 48 kHz, the MIDI at the start of the first block
void run (octavio2::Processor& p, int blocks, const std::vector<juce::MidiMessage>& first = {})
{
    juce::AudioBuffer<float> buf (2, 480);
    for (int b = 0; b < blocks; ++b)
    {
        juce::MidiBuffer midi;
        if (b == 0)
            for (const auto& m : first)
                midi.addEvent (m, 0);
        p.processBlock (buf, midi);
    }
}
} // namespace

TEST_CASE ("Octavio 2 M7: Modern soloist is the default player, and every style plays", "[octavio2][m7]")
{
    octavio2::Processor a, b;
    // b's engine goes through every style and back: Modern soloist restores the player exactly
    auto settings = b.getEngine().getSettings();
    for (int style = o2::styleCount - 1; style >= 0; --style)
    {
        settings.playerStyle = style;
        b.getEngine().setSettings (settings);
    }
    const auto x = play (a, 48000.0, 76, 0.6, 1.0);
    const auto y = play (b, 48000.0, 76, 0.6, 1.0);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < x.getNumSamples(); ++i)
            REQUIRE (x.getSample (c, i) == y.getSample (c, i));

    for (int style = 1; style < o2::styleCount; ++style)
    {
        octavio2::Processor p;
        setParam (p, "playerStyle", (float) style);
        setParam (p, "octave", 2.0f);
        const auto out = play (p, 48000.0, 74, 1.2, 1.6);
        const auto l = measure (out, 9600, 57600);
        INFO ("style " << style << ": " << dB (l.rms) << " dB RMS");
        CHECK (l.finite);
        CHECK (dB (l.rms) > -45.0);
        CHECK (l.peak < 1.0);
    }
}

TEST_CASE ("Octavio 2 M7: styles change the player's habits", "[octavio2][m7]")
{
    const o2::PlayerParams modern;
    for (int s = 0; s < o2::styleCount; ++s)
    {
        o2::PlayerParams p;
        o2::applyStyle (p, s);
        if (s == o2::styleModern)
            CHECK ((p.vibRate == modern.vibRate && p.vibWidthHi == modern.vibWidthHi && p.slideProb == 0.0
                    && p.scoop == 0.0 && p.pitchError == 0.0 && p.intonAmount == 0.0 && p.mdvPeak == 0.5
                    && p.openPenalty == modern.openPenalty && p.bite == modern.bite));
        if (s == o2::styleRomantic)
            CHECK ((p.vibWidthHi > modern.vibWidthHi && p.vibRate < modern.vibRate && p.slideProb > 0.0));
        if (s == o2::styleHungarian)
            CHECK ((p.vibRate > modern.vibRate && p.bite > modern.bite && p.vibDelay < modern.vibDelay));
        if (s == o2::styleBaroque)
            CHECK ((p.vibWidthHi < 0.5 * modern.vibWidthHi && p.mdvDepth > modern.mdvDepth));
        if (s == o2::styleMaqam)
            CHECK ((p.scoop > 0.0 && p.slideProb > 0.0));
        if (s == o2::styleFiddle)
            CHECK ((p.openPenalty < modern.openPenalty && p.vibWidthHi < modern.vibWidthHi));
        if (s == o2::styleStudent)
            CHECK ((p.pitchError > 0.0 && p.accel < modern.accel));
    }
    // portamento: a leap in reach on the string the finger is on slides when slideProb is 1
    o2::Violin vn;
    vn.init();
    o2::Player pl;
    pl.pp.slideProb = 1.0;
    pl.init (vn, 48000.0);
    double vb[4], fb[4];
    pl.noteOn (71, 90); // B4 on the A string
    for (int i = 0; i < 9600; ++i)
        pl.tick (vb, fb);
    pl.noteOn (74, 90); // D5 slurred on (the B is still held): 3 semitones up the A string, in reach
    CHECK (pl.lastString == 2);
    CHECK (pl.st[2].slideT0 >= 0.0);
}

TEST_CASE ("Octavio 2 M7: intonation systems and the A4 reference", "[octavio2][m7]")
{
    o2::Violin vn;
    vn.init();
    o2::Player pl;
    pl.init (vn, 48000.0);
    pl.setTuning (o2::intonExpressive, 0, 440.0, 0.0, nullptr, true);
    CHECK_FALSE (pl.tuneOn); // the default: exactly the 2.1 pitches
    pl.setTuning (o2::intonEqual, 0, 415.0, 0.0, nullptr, true);
    CHECK (pl.tuneOn);
    CHECK (std::abs (pl.tuneTable[69] - 1200.0 * std::log2 (415.0 / 440.0)) < 1e-9);
    CHECK (std::abs (pl.openCents[2] - pl.tuneTable[69]) < 1e-9);
    // Pythagorean in D: A at the reference, F# a Pythagorean third above D
    pl.setTuning (o2::intonPythagorean, 2, 440.0, 0.0, nullptr, true);
    CHECK (std::abs (pl.tuneTable[69]) < 1e-9);
    CHECK (std::abs ((pl.tuneTable[66] - pl.tuneTable[62]) - (1200.0 * std::log2 (81.0 / 64.0) - 400.0)) < 0.02);
    CHECK (std::abs (pl.openCents[0] - (-3.91)) < 1e-9); // G two pure fifths below A
    // Just in C: E a pure major third above C, G a pure fifth
    pl.setTuning (o2::intonJust, 0, 440.0, 0.0, nullptr, true);
    CHECK (std::abs ((pl.tuneTable[64] - pl.tuneTable[60]) - (1200.0 * std::log2 (5.0 / 4.0) - 400.0)) < 0.02);
    CHECK (std::abs ((pl.tuneTable[67] - pl.tuneTable[60]) - (1200.0 * std::log2 (3.0 / 2.0) - 700.0)) < 0.02);
    // the processor: A415 retunes the open strings and the notes
    octavio2::Processor p;
    setParam (p, "a4", 415.0f);
    setParam (p, "octave", 2.0f);
    p.prepareToPlay (48000.0, 480);
    run (p, 20, { juce::MidiMessage::noteOn (1, 71, (juce::uint8) 90) });
    auto& player = p.getEngine().getPlayer();
    CHECK (std::abs (p.getEngine().getViolin().s[2].d.f0 - 415.0) < 0.01);
    CHECK (std::abs (player.st[player.lastString].target - (71.0 + 1200.0 * std::log2 (415.0 / 440.0) / 100.0)) < 1e-3);
}

TEST_CASE ("Octavio 2 M7: Scala files load, play and are saved with the project", "[octavio2][m7]")
{
    // 12-TET: nothing moves; 24-EDO quarter tones; a keyboard map with A = 432 Hz
    const std::string et = "! et.scl\n12-TET\n12\n100.\n200.\n300.\n400.\n500.\n600.\n700.\n800.\n900.\n1000.\n"
                           "1100.\n2/1\n";
    auto t = o2::parseScala (et);
    REQUIRE (t.ok);
    for (int n = 0; n < 128; ++n)
        CHECK (std::abs (t.cents[n]) < 1e-9);
    // 24-EDO, every key the next quarter tone, key 69 at 440: key 61 is 8 quarter tones below A
    std::string q = "24-EDO\n24\n";
    for (int i = 1; i < 24; ++i)
        q += std::to_string (50 * i) + ".0\n";
    q += "2/1\n";
    t = o2::parseScala (q);
    REQUIRE (t.ok);
    CHECK (std::abs (t.cents[61] - 400.0) < 1e-9);
    CHECK (std::abs (t.cents[74] - (-250.0)) < 1e-9);
    const std::string kbm = "! a432.kbm\n0\n0\n127\n60\n69\n432.0\n0\n";
    t = o2::parseScala (et, kbm);
    REQUIRE (t.ok);
    CHECK (t.hasKeyboardMap);
    CHECK (std::abs (t.cents[69] - 1200.0 * std::log2 (432.0 / 440.0)) < 1e-9);
    CHECK_FALSE (o2::parseScala ("nonsense").ok);

    octavio2::Processor a;
    CHECK (a.loadScalaText ("quarter.scl", q, {}).isEmpty());
    setParam (a, "intonation", (float) o2::intonScala);
    setParam (a, "octave", 2.0f);
    a.prepareToPlay (48000.0, 480);
    run (a, 10, { juce::MidiMessage::noteOn (1, 74, (juce::uint8) 90) });
    auto& pl = a.getEngine().getPlayer();
    CHECK (pl.tuneOn);
    CHECK (std::abs (pl.st[pl.lastString].target - 71.5) < 1e-6); // 5 quarter tones above A4
    juce::MemoryBlock state;
    a.getStateInformation (state);
    octavio2::Processor b;
    b.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (b.getScalaName() == "quarter.scl");
    b.prepareToPlay (48000.0, 480);
    run (b, 2);
    CHECK (std::abs (b.getEngine().getPlayer().tuneTable[74] - (-250.0)) < 1e-6);
}

TEST_CASE ("Octavio 2 M7: MPE bends, presses and brightens each note; off it changes nothing", "[octavio2][m7]")
{
    for (const bool on : { false, true })
    {
        octavio2::Processor p;
        setParam (p, "octave", 2.0f);
        setParam (p, "mpe", on ? 1.0f : 0.0f);
        p.prepareToPlay (48000.0, 480);
        // a semitone up of the 48-semitone range, sent before the note as MPE controllers do
        const int semitone = 8192 + juce::roundToInt (8192.0 / 48.0);
        run (p,
             10,
             { juce::MidiMessage::pitchWheel (2, semitone),
               juce::MidiMessage::noteOn (2, 69, (juce::uint8) 90),
               juce::MidiMessage::channelPressureChange (2, 127),
               juce::MidiMessage::controllerEvent (2, 74, 127) });
        auto& pl = p.getEngine().getPlayer();
        const auto& S = pl.st[pl.lastString];
        if (on)
        {
            CHECK (std::abs (S.mpeBend - 100.0) < 0.5);
            CHECK (pl.mpeDyn == 1.0);
            CHECK (pl.mpeContact < 0.6);
        }
        else
        {
            CHECK (S.mpeBend == 0.0);
            CHECK (pl.mpeDyn < 0.0);
            CHECK (pl.mpeContact == 1.0);
        }
    }
}

// ---------------------------------------------------------------- M7 instrument
TEST_CASE ("Octavio 2's M7 articulations and instrument parts play stably at every velocity on every string",
           "[octavio2][m7]")
{
    struct Setting
    {
        const char* id;
        float value;
        double minDb; // at velocity 127
    };
    const Setting settings[] = { { "articulation", 5, -50 }, { "articulation", 6, -60 }, { "articulation", 7, -55 },
                                 { "articulation", 8, -80 }, { "contactStyle", 1, -50 }, { "contactStyle", 2, -55 },
                                 { "strings", 1, -50 },      { "strings", 2, -50 },      { "rosin", 0, -50 },
                                 { "rosin", 2, -50 },        { "rosin", 3, -50 },        { "bow", 1, -50 },
                                 { "tremoloSync", 3, -50 } };
    // the 2.1 violin's own peaks: already above full scale on the open A at velocity 127 (+2.5 dB),
    // so each setting is held to the default's peak + 3 dB (no runaway), not to 0 dBFS
    std::map<int, double> reference;
    for (const int note : { 55, 62, 69, 76 })
        for (const int velocity : { 1, 127 })
        {
            octavio2::Processor p;
            setParam (p, "octave", 2.0f);
            reference[note * 1000 + velocity] = measure (play (p, 48000.0, note, 0.8, 1.2, velocity)).peak;
        }
    for (const auto& s : settings)
        for (const int note : { 55, 62, 69, 76 })
            for (const int velocity : { 1, 127 })
            {
                octavio2::Processor p;
                setParam (p, "octave", 2.0f);
                setParam (p, s.id, s.value);
                if (std::string (s.id) == "tremoloSync")
                    setParam (p, "articulation", 5);
                const auto out = play (p, 48000.0, note, 0.8, 1.2, velocity);
                const auto l = measure (out);
                const double ref = reference[note * 1000 + velocity];
                INFO (s.id << " = " << s.value << ", note " << note << ", velocity " << velocity << ": " << dB (l.rms)
                           << " dB RMS, peak " << dB (l.peak) << " dB (default " << dB (ref) << " dB)");
                CHECK (l.finite);
                CHECK (dB (l.peak) < std::max (dB (ref), -6.0) + 3.0);
                if (velocity == 127)
                    CHECK (dB (l.rms) > s.minDb);
            }
}

TEST_CASE ("Octavio 2's M7 keyswitches pick tremolo, col legno and the contact point", "[octavio2][m7]")
{
    // A4 (MIDI 57 at Octave +1) after the keyswitches; returns the peak level after 2 s
    auto run = [] (std::initializer_list<int> keys, juce::AudioBuffer<float>& all)
    {
        octavio2::Processor p;
        setParam (p, "room", 0.0f);
        const int block = 480;
        p.prepareToPlay (48000.0, block);
        juce::AudioBuffer<float> buf (2, block);
        all.setSize (2, 3 * 48000);
        double late = 0;
        for (int pos = 0, k = 0; pos < 3 * 48000; pos += block, ++k)
        {
            juce::MidiBuffer midi;
            if (k == 0)
                for (int key : keys)
                    midi.addEvent (juce::MidiMessage::noteOn (1, key, (juce::uint8) 100), 0);
            if (k == 10)
                midi.addEvent (juce::MidiMessage::noteOn (1, 57, (juce::uint8) 100), 0);
            p.processBlock (buf, midi);
            for (int c = 0; c < 2; ++c)
                all.copyFrom (c, pos, buf, c, 0, block);
            if (pos > 2 * 48000)
                late = std::max (late, (double) buf.getMagnitude (0, block));
        }
        return late;
    };
    juce::AudioBuffer<float> arco, tremolo, pont, colLegno;
    const double bowed = run ({}, arco), struck = run ({ 32 }, colLegno);
    run ({ 29 }, tremolo);
    run ({ 34 }, pont);
    INFO ("bowed " << dB (bowed) << " dB, col legno " << dB (struck) << " dB late");
    CHECK (dB (struck) < dB (bowed) - 10.0); // the struck note has died away
    auto differs = [&] (const juce::AudioBuffer<float>& x)
    {
        double d = 0;
        for (int i = 24000; i < 96000; ++i)
            d = std::max (d, (double) std::abs (x.getSample (0, i) - arco.getSample (0, i)));
        return d;
    };
    CHECK (differs (tremolo) > 1e-3);
    CHECK (differs (pont) > 1e-3);
    CHECK (measure (tremolo).finite);
    CHECK (measure (pont).finite);
}

TEST_CASE ("Octavio 2's M7 parts default to the 2.1 violin", "[octavio2][m7]")
{
    octavio2::Processor p;
    const auto e = octavio2::params::Reader (p.getParameters()).read();
    const o2::EngineSettings d;
    CHECK (e.strings == d.strings);
    CHECK (e.rosin == d.rosin);
    CHECK (e.bow == d.bow);
    CHECK (e.contactStyle == 0);
    CHECK (e.articulation == 0);
    CHECK (d.strings == 0);
    CHECK (d.rosin == 1);
    CHECK (d.bow == 0);
    // the standard rosin is the strings' own defaults
    const auto r = o2::Violin::rosin (1);
    const o2::Params P;
    CHECK (r.muS == P.muS);
    CHECK (r.tauG == P.tauG);
    CHECK (r.ya == P.ya);
    CHECK (r.sigma0 == P.sigma0);
    CHECK (r.grain == P.grain);
}
