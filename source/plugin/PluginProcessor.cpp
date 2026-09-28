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
    // the message thread. The timer also shows the host's notes on the keyboard.
    keyboardState.addListener (this);
    startTimerHz (20);
}

ViolinSynthProcessor::~ViolinSynthProcessor()
{
    stopTimer();
    keyboardState.removeListener (this);
}

bool ViolinSynthProcessor::KeyEventQueue::push (const KeyEvent& e)
{
    const auto scope = fifo.write (1);
    if (scope.blockSize1 + scope.blockSize2 == 0)
        return false;
    events[static_cast<std::size_t> (scope.blockSize1 > 0 ? scope.startIndex1 : scope.startIndex2)] = e;
    return true;
}

template <typename Fn>
void ViolinSynthProcessor::KeyEventQueue::popAll (Fn&& fn)
{
    const auto scope = fifo.read (fifo.getNumReady());
    scope.forEach ([&] (int index) { fn (events[static_cast<std::size_t> (index)]); });
}

void ViolinSynthProcessor::handleNoteOn (juce::MidiKeyboardState*, int channel, int note, float velocity)
{
    if (! showingHostNotes)
        keysToAudio.push ({ channel, note, std::max (velocity, 1.0f / 127.0f) });
}

void ViolinSynthProcessor::handleNoteOff (juce::MidiKeyboardState*, int channel, int note, float)
{
    if (! showingHostNotes)
        keysToAudio.push ({ channel, note, 0.0f });
}

void ViolinSynthProcessor::updateKeyboardState()
{
    showingHostNotes = true;
    keysToDisplay.popAll (
        [this] (const KeyEvent& e)
        {
            if (e.note < 0)
                keyboardState.allNotesOff (e.channel);
            else if (e.velocity > 0.0f)
                keyboardState.noteOn (e.channel, e.note, e.velocity);
            else
                keyboardState.noteOff (e.channel, e.note, 0.0f);
        });
    showingHostNotes = false;

    // MidiKeyboardState queues every note it is given for a processNextMidiBuffer
    // call; the notes already reached the audio thread above, so drop the copies.
    juce::MidiBuffer none;
    keyboardState.processNextMidiBuffer (none, 0, 1, false);
}

void ViolinSynthProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    keyboardState.reset();
    mergedMidi.ensureSize (8192);
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

    // Host notes are shown on the on-screen keyboard; its notes join the host's.
    for (const auto metadata : midiMessages)
    {
        if (metadata.numBytes > 3)
            continue; // SysEx: copying it into a MidiMessage would allocate
        const auto m = metadata.getMessage();
        if (m.isNoteOn())
            keysToDisplay.push ({ m.getChannel(), m.getNoteNumber(), m.getFloatVelocity() });
        else if (m.isNoteOff())
            keysToDisplay.push ({ m.getChannel(), m.getNoteNumber(), 0.0f });
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            keysToDisplay.push ({ m.getChannel(), -1, 0.0f });
    }

    const juce::MidiBuffer* midi = &midiMessages;
    bool merged = false;
    keysToAudio.popAll (
        [&] (const KeyEvent& e)
        {
            if (! merged)
            {
                mergedMidi.clear();
                mergedMidi.addEvents (midiMessages, 0, -1, 0);
                midi = &mergedMidi;
                merged = true;
            }
            mergedMidi.addEvent (e.velocity > 0.0f ? juce::MidiMessage::noteOn (e.channel, e.note, e.velocity)
                                                   : juce::MidiMessage::noteOff (e.channel, e.note),
                                 0);
        });

    engine.setSettings (reader.read());
    engine.process (buffer, *midi);
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
