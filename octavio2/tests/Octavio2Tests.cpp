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
