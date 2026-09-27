#include "plugin/Parameters.h"
#include "plugin/PluginProcessor.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>

using violinsynth::ViolinSynthProcessor;

namespace
{
struct RenderResult
{
    float peak = 0.0f;
    bool allFinite = true;
};

RenderResult render (ViolinSynthProcessor& processor, const juce::MidiBuffer& midi, int numSamples, int blockSize)
{
    RenderResult result;
    juce::AudioBuffer<float> buffer (processor.getTotalNumOutputChannels(), blockSize);

    for (int start = 0; start < numSamples; start += blockSize)
    {
        const auto length = std::min (blockSize, numSamples - start);
        buffer.setSize (buffer.getNumChannels(), length, false, false, true);

        juce::MidiBuffer blockMidi;
        blockMidi.addEvents (midi, start, length, -start);
        processor.processBlock (buffer, blockMidi);

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            for (int i = 0; i < length; ++i)
            {
                const auto sample = buffer.getSample (channel, i);
                result.allFinite = result.allFinite && std::isfinite (sample);
                result.peak = std::max (result.peak, std::abs (sample));
            }
        }
    }

    return result;
}

juce::MidiBuffer noteOnAtStart()
{
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 69, 0.8f), 0);
    return midi;
}
} // namespace

TEST_CASE ("Processor describes itself as an instrument", "[processor]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    ViolinSynthProcessor processor;

    CHECK (processor.getName() == "Violin Synthesizer");
    CHECK (processor.acceptsMidi());
    CHECK_FALSE (processor.producesMidi());
    CHECK_FALSE (processor.isMidiEffect());
    CHECK (processor.getTotalNumInputChannels() == 0);
    CHECK (processor.getTotalNumOutputChannels() == 2);
}

TEST_CASE ("Processor is silent without MIDI input", "[processor]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    ViolinSynthProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    const auto result = render (processor, {}, 48000, 512);

    CHECK (result.allFinite);
    CHECK (result.peak == 0.0f);
}

TEST_CASE ("Processor produces bounded, finite audio for a note", "[processor]")
{
    const auto sampleRate = GENERATE (44100.0, 48000.0, 96000.0, 192000.0);
    const auto blockSize = GENERATE (1, 64, 512, 4096);
    CAPTURE (sampleRate, blockSize);

    juce::ScopedJuceInitialiser_GUI juce;
    ViolinSynthProcessor processor;
    processor.setRateAndBufferSizeDetails (sampleRate, blockSize);
    processor.prepareToPlay (sampleRate, blockSize);

    const auto result = render (processor, noteOnAtStart(), static_cast<int> (sampleRate / 4), blockSize);

    CHECK (result.allFinite);
    CHECK (result.peak > 0.01f);
    CHECK (result.peak <= 1.0f);
}

TEST_CASE ("Output gain parameter attenuates the signal", "[processor][parameters]")
{
    juce::ScopedJuceInitialiser_GUI juce;

    ViolinSynthProcessor loud;
    loud.prepareToPlay (48000.0, 256);
    const auto loudPeak = render (loud, noteOnAtStart(), 24000, 256).peak;

    ViolinSynthProcessor quiet;
    auto* gain = quiet.getParameters().getParameter (violinsynth::params::id::outputGain.getParamID());
    REQUIRE (gain != nullptr);
    gain->setValueNotifyingHost (gain->convertTo0to1 (-20.0f));
    quiet.prepareToPlay (48000.0, 256);
    const auto quietPeak = render (quiet, noteOnAtStart(), 24000, 256).peak;

    REQUIRE (loudPeak > 0.0f);
    CHECK (quietPeak < loudPeak * 0.2f);
}

TEST_CASE ("State round-trips through get/setStateInformation", "[processor][state]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    const auto gainId = violinsynth::params::id::outputGain.getParamID();

    ViolinSynthProcessor source;
    auto* sourceGain = source.getParameters().getParameter (gainId);
    sourceGain->setValueNotifyingHost (sourceGain->convertTo0to1 (-12.5f));

    juce::MemoryBlock state;
    source.getStateInformation (state);
    REQUIRE (state.getSize() > 0);

    ViolinSynthProcessor restored;
    restored.setStateInformation (state.getData(), static_cast<int> (state.getSize()));

    const auto* restoredGain = restored.getParameters().getRawParameterValue (gainId);
    CHECK (std::abs (restoredGain->load() - -12.5f) < 0.05f);
}

TEST_CASE ("Invalid state data is ignored", "[processor][state]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    ViolinSynthProcessor processor;

    const char garbage[] = "not a valid plugin state";
    processor.setStateInformation (garbage, static_cast<int> (sizeof (garbage)));

    const auto gainId = violinsynth::params::id::outputGain.getParamID();
    CHECK (std::abs (processor.getParameters().getRawParameterValue (gainId)->load()) < 0.05f);
}

TEST_CASE ("Editor can be created and destroyed", "[editor]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    ViolinSynthProcessor processor;

    std::unique_ptr<juce::AudioProcessorEditor> editor { processor.createEditorAndMakeActive() };
    REQUIRE (editor != nullptr);
    CHECK (editor->getWidth() > 0);
    CHECK (editor->getHeight() > 0);
}

// Not run by default: `ViolinSynthTests "[.screenshot]"` writes editor.png to the working directory.
TEST_CASE ("Editor screenshot", "[.screenshot]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    ViolinSynthProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor { processor.createEditorAndMakeActive() };
    const auto image = editor->createComponentSnapshot (editor->getLocalBounds());
    juce::FileOutputStream stream (juce::File::getCurrentWorkingDirectory().getChildFile ("editor.png"));
    stream.setPosition (0);
    stream.truncate();
    juce::PNGImageFormat().writeImageToStream (image, stream);
}

// Not run by default: `ViolinSynthTests "[.render]"` writes demo WAVs rendered
// by the complete plugin to the working directory.
TEST_CASE ("Render demo phrases through the plugin", "[.render]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    const auto fs = 48000.0;
    const int block = 256;

    struct Phrase
    {
        const char* name;
        int body;
        std::vector<std::tuple<double, double, int, float>> notes; // start s, end s, midi note, velocity
    };

    std::vector<std::tuple<double, double, int, float>> scale; // A major, one bow per note
    const int aMajor[] = { 69, 71, 73, 74, 76, 78, 80, 81, 80, 78, 76, 74, 73, 71, 69 };
    for (int i = 0; i < 15; ++i)
        scale.emplace_back (i * 0.45, i * 0.45 + 0.42, aMajor[i], 0.7f);

    std::vector<std::tuple<double, double, int, float>> legato; // overlapping notes glide
    const int melody[] = { 67, 71, 74, 79, 78, 76, 74, 71, 72, 74 };
    const double lengths[] = { 0.6, 0.6, 0.6, 1.2, 0.4, 0.4, 0.6, 0.6, 0.6, 1.6 };
    double t = 0.0;
    for (int i = 0; i < 10; ++i)
    {
        legato.emplace_back (t, t + lengths[i] + 0.05, melody[i], 0.75f);
        t += lengths[i];
    }

    const std::vector<Phrase> phrases {
        { "cpp_A_major_detache_levaggi", 0, scale },
        { "cpp_A_major_detache_klimke", 1, scale },
        { "cpp_legato_melody_levaggi", 0, legato },
        { "cpp_G3_long_note_levaggi", 0, { { 0.0, 3.0, 55, 0.8f } } },
    };

    for (const auto& phrase : phrases)
    {
        ViolinSynthProcessor processor;
        auto* bodyParam = processor.getParameters().getParameter (violinsynth::params::id::body.getParamID());
        bodyParam->setValueNotifyingHost (bodyParam->convertTo0to1 (static_cast<float> (phrase.body)));
        processor.setRateAndBufferSizeDetails (fs, block);
        processor.prepareToPlay (fs, block);

        // Let the convolution body load (it is installed on a background thread).
        juce::AudioBuffer<float> buffer (2, block);
        juce::MidiBuffer none;
        for (int i = 0; i < 100; ++i)
        {
            processor.processBlock (buffer, none);
            juce::Thread::sleep (5);
        }

        double end = 0.0;
        for (const auto& n : phrase.notes)
            end = std::max (end, std::get<1> (n));
        const auto total = static_cast<int> ((end + 1.5) * fs);

        juce::AudioBuffer<float> output (2, total);
        for (int start = 0; start < total; start += block)
        {
            const auto len = std::min (block, total - start);
            buffer.setSize (2, len, false, false, true);
            juce::MidiBuffer midi;
            for (const auto& [on, off, number, velocity] : phrase.notes)
            {
                const auto onSample = static_cast<int> (on * fs);
                const auto offSample = static_cast<int> (off * fs);
                if (onSample >= start && onSample < start + len)
                    midi.addEvent (juce::MidiMessage::noteOn (1, number, velocity), onSample - start);
                if (offSample >= start && offSample < start + len)
                    midi.addEvent (juce::MidiMessage::noteOff (1, number), offSample - start);
            }
            processor.processBlock (buffer, midi);
            for (int ch = 0; ch < 2; ++ch)
                output.copyFrom (ch, start, buffer, ch, 0, len);
        }

        const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (juce::String (phrase.name) + ".wav");
        file.deleteFile();
        auto stream = std::unique_ptr<juce::OutputStream> (file.createOutputStream());
        juce::WavAudioFormat wav;
        const auto options
            = juce::AudioFormatWriterOptions {}.withSampleRate (fs).withNumChannels (2).withBitsPerSample (16);
        auto writer = wav.createWriterFor (stream, options);
        REQUIRE (writer != nullptr);
        writer->writeFromAudioSampleBuffer (output, 0, total);
    }
}
