// Phase 7.6: what hosts do to a plugin, played through the processor as a
// host would (docs/PHASE7.md).

#include "ProcessorTestUtilities.h"
#include "TestUtilities.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <random>

using violinsynth::ViolinSynthProcessor;
using namespace violinsynth;
using namespace violinsynth::test;

namespace
{
constexpr int d5 = 74; // stopped on the A string, so its pitch comes from the model, not the tuning
constexpr double d5Hz = 587.3295358348151;

struct Summary
{
    float peak = 0.0f;
    bool finite = true;
};

Summary summarise (const std::vector<float>& x)
{
    Summary s;
    for (auto v : x)
    {
        s.finite = s.finite && std::isfinite (v);
        s.peak = std::max (s.peak, std::abs (v));
    }
    return s;
}

double pitchCents (const std::vector<float>& x, double sampleRate, double from, double to)
{
    const std::vector<double> window (x.begin() + static_cast<std::ptrdiff_t> (from * sampleRate),
                                      x.begin() + static_cast<std::ptrdiff_t> (to * sampleRate));
    return cents (estimateF0 (window, sampleRate, d5Hz), d5Hz);
}

// Seconds from the start of the render to the first sample above a tenth of the peak.
double onsetSeconds (const std::vector<float>& x, double sampleRate)
{
    const auto peak = summarise (x).peak;
    for (std::size_t i = 0; i < x.size(); ++i)
        if (std::abs (x[i]) > 0.1f * peak)
            return static_cast<double> (i) / sampleRate;
    return -1.0;
}
} // namespace

TEST_CASE ("Sample-rate changes mid-session keep the violin in tune", "[host]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    ViolinSynthProcessor processor;
    useSteadySettings (processor);

    for (const auto rate : { 44100.0, 96000.0, 48000.0 })
    {
        INFO ("sample rate " << rate);
        processor.releaseResources();
        processor.prepareToPlay (rate, 512);
        const auto out
            = renderProcessor (processor, rate, 512, 1.5, { { 0.0, juce::MidiMessage::noteOn (1, d5, 0.8f) } });
        const auto s = summarise (out);
        REQUIRE (s.finite);
        REQUIRE (s.peak > 0.01f);
        CHECK (std::abs (pitchCents (out, rate, 0.7, 1.4)) < 2.0);
    }
}

TEST_CASE ("Every block size from 16 to 4096 plays, including blocks larger than announced", "[host]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    ViolinSynthProcessor processor;
    useSteadySettings (processor);
    const std::vector<TimedMidi> note { { 0.0, juce::MidiMessage::noteOn (1, d5, 0.8f) },
                                        { 0.6, juce::MidiMessage::noteOff (1, d5) } };

    for (const auto maxBlock : { 16, 32, 64, 128, 441, 1024, 2048, 4096 })
    {
        for (const auto block : { maxBlock, maxBlock / 2 + 1, 2 * maxBlock + 3 })
        {
            INFO ("announced " << maxBlock << ", played " << block);
            processor.prepareToPlay (48000.0, maxBlock);
            const auto out = renderProcessor (processor, 48000.0, block, 1.0, note);
            const auto s = summarise (out);
            CHECK (s.finite);
            CHECK (s.peak > 0.01f);
            CHECK (s.peak <= 1.0f);
            CHECK (std::abs (pitchCents (out, 48000.0, 0.3, 0.55)) < 2.0);
        }
    }

    // FL Studio splits blocks at automation and note events: sizes vary every block.
    processor.prepareToPlay (48000.0, 512);
    std::mt19937 random (7);
    std::uniform_int_distribution<int> size (1, 512);
    juce::AudioBuffer<float> buffer (2, 512);
    bool finite = true;
    float peak = 0.0f;
    for (int done = 0, n = 0; done < 48000; done += n)
    {
        n = size (random);
        buffer.setSize (2, n, false, false, true);
        juce::MidiBuffer midi;
        if (done == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, d5, 0.8f), 0);
        processor.processBlock (buffer, midi);
        for (int i = 0; i < n; ++i)
        {
            finite = finite && std::isfinite (buffer.getSample (0, i));
            peak = std::max (peak, std::abs (buffer.getSample (0, i)));
        }
    }
    CHECK (finite);
    CHECK (peak > 0.01f);
}

TEST_CASE ("Offline rendering gives the same output as real time", "[host]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    const std::vector<TimedMidi> phrase {
        { 0.0, juce::MidiMessage::noteOn (1, 67, 0.7f) }, { 0.4, juce::MidiMessage::noteOn (1, 71, 0.7f) },
        { 0.45, juce::MidiMessage::noteOff (1, 67) },     { 0.8, juce::MidiMessage::noteOn (1, 74, 0.9f) },
        { 0.8, juce::MidiMessage::noteOn (1, 62, 0.9f) }, { 1.2, juce::MidiMessage::noteOff (1, 71) },
        { 1.6, juce::MidiMessage::noteOff (1, 74) },      { 1.6, juce::MidiMessage::noteOff (1, 62) },
    };

    auto render = [&] (bool offline)
    {
        ViolinSynthProcessor processor;
        setParameter (processor, params::id::bodyQuality, 1.0f);
        processor.setNonRealtime (offline);
        processor.prepareToPlay (48000.0, 256);
        return renderProcessor (processor, 48000.0, 256, 2.5, phrase);
    };

    const auto realtime = render (false);
    const auto offline = render (true);
    REQUIRE (summarise (realtime).peak > 0.01f);
    CHECK (realtime == offline);
}

TEST_CASE ("Reported latency matches the delay through the engine", "[host]")
{
    // A plucked note starts sharply. Once the reported latency is taken off,
    // it must start at the same moment at every sample rate, so a host's delay
    // compensation lines it up with other tracks. The measured bodies start
    // about 1 ms later than the modal ones: that is the leading part of the
    // impulse responses, the same at every rate, not processing latency.
    juce::ScopedJuceInitialiser_GUI juce;

    for (const auto modal : { true, false })
    {
        std::vector<double> onsets;
        for (const auto rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            ViolinSynthProcessor processor;
            useSteadySettings (processor, modal);
            setParameter (processor, params::id::articulation, static_cast<float> (engine::Articulation::pizzicato));
            processor.prepareToPlay (rate, 256);
            if (! modal)
                waitForBody (processor, rate, 256);

            const auto out
                = renderProcessor (processor, rate, 256, 0.3, { { 0.0, juce::MidiMessage::noteOn (1, d5, 0.8f) } });
            const auto latency = processor.getLatencySamples();
            const auto onset = onsetSeconds (out, rate) - latency / rate;
            UNSCOPED_INFO ("rate " << rate << (modal ? " modal" : " convolution") << ": latency " << latency
                                   << " samples, onset " << onset * 1000.0 << " ms");
            REQUIRE (onset >= 0.0);
            CHECK (onset < 0.002);
            onsets.push_back (onset);
        }

        const auto [lo, hi] = std::minmax_element (onsets.begin(), onsets.end());
        CHECK (*hi - *lo < 0.0001); // 5 samples at 48 kHz
    }
}

namespace
{
const juce::File phase6State { juce::String (VIOLINSYNTH_TEST_DATA_DIR) + "/fixtures/state-phase6.bin" };
constexpr int phase6Preset = 1; // Romantic Soloist
} // namespace

TEST_CASE ("Write the saved-state fixture", "[.fixtures]")
{
    // Run once, on the version whose projects must keep loading:
    //   ViolinSynthTests "[.fixtures]"
    // then copy the printed values into the test below.
    juce::ScopedJuceInitialiser_GUI juce;
    ViolinSynthProcessor processor;
    processor.getPresetManager().load (phase6Preset);
    setParameter (processor, params::id::bodyQuality, 1.0f); // edits on top of the preset
    setParameter (processor, params::id::room, 0.5f);
    setParameter (processor, params::id::articulation, 1.0f);

    juce::MemoryBlock state;
    processor.getStateInformation (state);
    phase6State.getParentDirectory().createDirectory();
    REQUIRE (phase6State.replaceWithData (state.getData(), state.getSize()));

    for (auto* p : processor.getParameters().processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            std::printf ("{ \"%s\", %.6ff },\n",
                         ranged->getParameterID().toRawUTF8(),
                         static_cast<double> (ranged->convertFrom0to1 (ranged->getValue())));
}

TEST_CASE ("Projects saved by Phase 6 still load and sound the same", "[host][state]")
{
    // Saved by the Phase 6 plugin: the Romantic Soloist preset with the Light
    // body, Room at 50% and the Detache articulation.
    const std::vector<std::pair<const char*, float>> saved {
        { "bowPosition", 0.1f },   { "bowPressure", 0.55f },  { "attack", 0.12f },       { "release", 0.3f },
        { "vibratoRate", 6.0f },   { "vibratoDepth", 38.0f }, { "vibratoDelay", 0.18f }, { "portamento", 0.09f },
        { "bendRange", 2.0f },     { "body", 0.0f },          { "bodyQuality", 1.0f },   { "sordino", 0.0f },
        { "width", 0.6f },         { "room", 0.5f },          { "outputGain", -4.5f },   { "playMode", 0.0f },
        { "resonance", 0.4f },     { "humanise", 0.6f },      { "autoBowChange", 1.0f }, { "mpe", 0.0f },
        { "mpeBendRange", 48.0f }, { "articulation", 1.0f },
    };

    juce::ScopedJuceInitialiser_GUI juce;
    juce::MemoryBlock state;
    REQUIRE (phase6State.loadFileAsData (state));

    ViolinSynthProcessor restored;
    restored.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    CHECK (restored.getPresetManager().getCurrentIndex() == phase6Preset);
    CHECK (restored.getPresetManager().isModified());

    // Parameters added since Phase 6 keep their defaults, which must play as Phase 6 did.
    ViolinSynthProcessor expected;
    for (auto* p : restored.getParameters().processor.getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p);
        REQUIRE (ranged != nullptr);
        const auto id = ranged->getParameterID();
        const auto it = std::find_if (saved.begin(), saved.end(), [&] (const auto& v) { return id == v.first; });
        const auto want = it != saved.end() ? it->second : ranged->convertFrom0to1 (ranged->getDefaultValue());
        INFO ("parameter " << id);
        CHECK (std::abs (ranged->convertFrom0to1 (ranged->getValue()) - want) < 1.0e-4f);
        if (it != saved.end())
            setParameter (expected, juce::ParameterID { id, 1 }, want);
    }

    // The restored plugin sounds exactly like one set up by hand.
    const std::vector<TimedMidi> phrase { { 0.0, juce::MidiMessage::noteOn (1, 69, 0.7f) },
                                          { 0.5, juce::MidiMessage::noteOn (1, 76, 0.7f) },
                                          { 0.55, juce::MidiMessage::noteOff (1, 69) },
                                          { 1.0, juce::MidiMessage::noteOff (1, 76) } };
    restored.prepareToPlay (48000.0, 256);
    expected.prepareToPlay (48000.0, 256);
    const auto a = renderProcessor (restored, 48000.0, 256, 1.5, phrase);
    const auto b = renderProcessor (expected, 48000.0, 256, 1.5, phrase);
    REQUIRE (summarise (a).peak > 0.01f);
    CHECK (a == b);
}

TEST_CASE ("Bypass, reset and 100 release and prepare cycles leave the plugin working", "[host]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    ViolinSynthProcessor processor;
    useSteadySettings (processor);
    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;

    // Hosts call these in every order, from any thread but the audio thread.
    constexpr std::array rates { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 };
    constexpr std::array blocks { 32, 64, 256, 441, 1024 };
    for (int cycle = 0; cycle < 100; ++cycle)
    {
        processor.releaseResources();
        const auto rate = rates[static_cast<std::size_t> (cycle) % rates.size()];
        processor.prepareToPlay (rate, blocks[static_cast<std::size_t> (cycle / 5) % blocks.size()]);
        const auto out
            = renderProcessor (processor, rate, 256, 0.02, { { 0.0, juce::MidiMessage::noteOn (1, d5, 0.8f) } });
        REQUIRE (summarise (out).finite);
        if (cycle % 10 == 0)
            processor.reset();
    }

    processor.prepareToPlay (48000.0, 256);
    renderProcessor (processor, 48000.0, 256, 0.2, { { 0.0, juce::MidiMessage::noteOn (1, d5, 0.8f) } });

    // Bypassed, an instrument is silent; unbypassed, the held note carries on.
    midi.clear();
    buffer.setSize (2, 256);
    for (int i = 0; i < 50; ++i)
    {
        processor.processBlockBypassed (buffer, midi);
        CHECK (buffer.getMagnitude (0, 256) == 0.0f);
    }
    const auto out = renderProcessor (processor, 48000.0, 256, 1.0);
    const auto s = summarise (out);
    CHECK (s.finite);
    CHECK (s.peak > 0.01f);
    CHECK (std::abs (pitchCents (out, 48000.0, 0.3, 0.9)) < 2.0);
}
