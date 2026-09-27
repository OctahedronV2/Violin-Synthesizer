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
