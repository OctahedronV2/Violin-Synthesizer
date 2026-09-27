#include "plugin/PluginProcessor.h"

#include "plugin/PluginEditor.h"

namespace violinsynth
{
ViolinSynthProcessor::ViolinSynthProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "ViolinSynthState", params::createLayout()),
      reader (parameters)
{
    // Loading a convolution body allocates, so body changes are applied on
    // the message thread.
    startTimerHz (20);
}

ViolinSynthProcessor::~ViolinSynthProcessor()
{
    stopTimer();
}

void ViolinSynthProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    keyboardState.reset();
    engine.setSettings (reader.read());
    engine.prepare (sampleRate, samplesPerBlock);
    setLatencySamples (engine.getLatencySamples());
}

void ViolinSynthProcessor::releaseResources()
{
    keyboardState.reset();
}

bool ViolinSynthProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& output = layouts.getMainOutputChannelSet();
    return output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo();
}

void ViolinSynthProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    keyboardState.processNextMidiBuffer (midiMessages, 0, buffer.getNumSamples(), true);
    engine.setSettings (reader.read());
    engine.process (buffer, midiMessages);
    const auto& violin = engine.getViolin();
    activeArticulation.store (static_cast<int> (violin.currentArticulation()));
    for (int s = 0; s < engine::Violin::numStrings; ++s)
    {
        auto& state = stringStates[static_cast<std::size_t> (s)];
        state.note.store (violin.noteOnString (s));
        state.level.store (static_cast<float> (violin.stringLevel (s)));
        state.bowSpeed.store (static_cast<float> (violin.stringBowSpeed (s)));
    }

    // This is an instrument: consume incoming MIDI rather than echoing it.
    midiMessages.clear();
}

juce::AudioProcessorEditor* ViolinSynthProcessor::createEditor()
{
    return new ViolinSynthEditor (*this);
}

void ViolinSynthProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (const auto xml = parameters.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void ViolinSynthProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml != nullptr && xml->hasTagName (parameters.state.getType()))
    {
        parameters.replaceState (juce::ValueTree::fromXml (*xml));
        presets.restoreFromState();
    }
}
} // namespace violinsynth

// Entry point used by every plugin format wrapper to create the processor.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new violinsynth::ViolinSynthProcessor();
}
