#include "plugin/PluginProcessor.h"

#include "plugin/Parameters.h"
#include "plugin/PlaceholderVoice.h"
#include "plugin/PluginEditor.h"

namespace violinsynth
{
ViolinSynthProcessor::ViolinSynthProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "ViolinSynthState", params::createLayout()),
      outputGainDb (parameters.getRawParameterValue (params::id::outputGain.getParamID()))
{
    for (int i = 0; i < numPlaceholderVoices; ++i)
        synth.addVoice (new PlaceholderVoice());

    synth.addSound (new PlaceholderSound());
}

void ViolinSynthProcessor::prepareToPlay (double sampleRate, int)
{
    synth.setCurrentPlaybackSampleRate (sampleRate);
    keyboardState.reset();

    outputGain.reset (sampleRate, 0.05);
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outputGainDb->load()));
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

    buffer.clear();
    keyboardState.processNextMidiBuffer (midiMessages, 0, buffer.getNumSamples(), true);
    synth.renderNextBlock (buffer, midiMessages, 0, buffer.getNumSamples());

    outputGain.setTargetValue (juce::Decibels::decibelsToGain (outputGainDb->load()));
    outputGain.applyGain (buffer, buffer.getNumSamples());

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
        parameters.replaceState (juce::ValueTree::fromXml (*xml));
}
} // namespace violinsynth

// Entry point used by every plugin format wrapper to create the processor.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new violinsynth::ViolinSynthProcessor();
}
