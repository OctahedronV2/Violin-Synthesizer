// Octavio 2 plugin tests: the engine plays, Studio mode looks ahead, the processor runs at the
// host's rate, notes outside the violin stay silent, state round-trips.

#include "../plugin/PluginEditor.h"
#include "../plugin/PluginProcessor.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
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
