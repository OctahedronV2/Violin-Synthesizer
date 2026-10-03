#include "PluginProcessor.h"

#include "../core/Wav.h"
#include "Octavio2BinaryData.h"
#include "PluginEditor.h"

namespace octavio2
{
namespace
{
// The engine's sound data, from the files in octavio2/data built into the plugin.
o2::WavData resource (const char* filename)
{
    for (int i = 0; i < Octavio2BinaryData::namedResourceListSize; ++i)
    {
        const char* name = Octavio2BinaryData::namedResourceList[i];
        if (juce::String (Octavio2BinaryData::getNamedResourceOriginalFilename (name)) == filename)
        {
            int size = 0;
            const char* data = Octavio2BinaryData::getNamedResource (name, size);
            return o2::readWav (data, static_cast<size_t> (size));
        }
    }
    jassertfalse;
    return {};
}

o2::RadiationData loadRadiationData()
{
    o2::RadiationData data;
    auto mono = [] (const char* f)
    {
        auto w = resource (f);
        return w.channels.empty() ? std::vector<float> (1, 0.0f) : w.channels[0];
    };
    data.body = mono ("body-fullband-balanced-48k.wav");
    data.bodyLeft = mono ("body-directional-left-48k.wav");
    data.bodyRight = mono ("body-directional-right-48k.wav");
    // the Room choices after "None", in order (params::roomNames)
    for (const char* hall : { "arvedi-near.wav",
                              "arvedi-far.wav",
                              "detmold.wav",
                              "church.wav",
                              "maida-vale-4.wav",
                              "maida-vale-5.wav",
                              "wdr-control-room.wav",
                              "wdr-studio.wav" })
    {
        auto w = resource (hall);
        if (w.channels.size() == 2)
            data.halls.push_back ({ std::move (w.channels[0]), std::move (w.channels[1]) });
        else
            data.halls.push_back ({ std::vector<float> (1, 0.0f), std::vector<float> (1, 0.0f) });
    }
    return data;
}
} // namespace

Processor::Processor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "Octavio2State", params::createLayout()),
      reader (parameters),
      engine (std::make_unique<o2::Engine>())
{
    engine->prepare (loadRadiationData());
    clickedKeys.fill (-1);
    for (auto& channel : sentNotes)
        channel.fill (-1);
    keyboardState.addListener (this);
    startTimerHz (10);
}

Processor::~Processor()
{
    stopTimer();
    keyboardState.removeListener (this);
}

bool Processor::KeyQueue::push (const KeyEvent& e)
{
    const auto scope = fifo.write (1);
    if (scope.blockSize1 + scope.blockSize2 == 0)
        return false;
    events[static_cast<size_t> (scope.blockSize1 > 0 ? scope.startIndex1 : scope.startIndex2)] = e;
    return true;
}

template <typename Fn>
void Processor::KeyQueue::popAll (Fn&& fn)
{
    const auto scope = fifo.read (fifo.getNumReady());
    scope.forEach ([&] (int index) { fn (events[static_cast<size_t> (index)]); });
}

void Processor::handleNoteOn (juce::MidiKeyboardState*, int, int note, float velocity)
{
    const int sent = note - 12 * reader.octaveShift();
    if (sent < 0 || sent > 127)
        return;
    clickedKeys[static_cast<size_t> (note)] = static_cast<std::int8_t> (sent);
    keysToAudio.push ({ sent, std::max (velocity, 1.0f / 127.0f) });
}

void Processor::handleNoteOff (juce::MidiKeyboardState*, int, int note, float)
{
    auto& sent = clickedKeys[static_cast<size_t> (note)];
    if (sent >= 0)
        keysToAudio.push ({ sent, 0.0f });
    sent = -1;
}

void Processor::timerCallback()
{
    updateLatency();
    // the keyboard's own copy of its notes is not needed: they reached the audio thread already
    juce::MidiBuffer none;
    keyboardState.processNextMidiBuffer (none, 0, 1, false);
}

void Processor::updateLatency()
{
    // Studio mode's look-ahead is latency the host compensates. Set from the message thread:
    // hosts are told through a callback that is not safe on the audio thread.
    const double interp
        = resampling ? static_cast<double> (juce::WindowedSincInterpolator::getBaseLatency()) / ratio : 0.0;
    const int samples = juce::roundToInt ((reader.studio() ? o2::Engine::lookAheadSamples : 0) / ratio + interp);
    if (samples != latencyShown.exchange (samples))
        setLatencySamples (samples);
}

void Processor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    hostRate = sampleRate;
    ratio = o2::Engine::rate / sampleRate;
    resampling = std::abs (ratio - 1.0) > 1e-9;
    hostClock = 0;
    engine->setSettings (reader.read());
    engine->reset();
    for (auto& channel : sentNotes)
        channel.fill (-1);
    const auto chunk = static_cast<size_t> (std::ceil (512.0 * std::max (1.0, ratio))) + 256;
    for (auto& f : fifo)
        f.assign (chunk, 0.0f);
    fifoFill = 0;
    for (auto& i : interpolators)
        i.reset();
    scratchL.assign (static_cast<size_t> (std::max (samplesPerBlock, 512)), 0.0f);
    scratchR.assign (scratchL.size(), 0.0f);
    latencyShown = -1;
    updateLatency();
}

void Processor::releaseResources()
{
}

bool Processor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

int64_t Processor::engineTime (int hostOffset) const
{
    const auto host = hostClock + hostOffset;
    return resampling ? static_cast<int64_t> (std::llround (static_cast<double> (host) * ratio)) : host;
}

void Processor::handleMidi (const juce::MidiMessage& m, int64_t when)
{
    const int channel = juce::jlimit (1, 16, m.getChannel()) - 1;
    auto& sent = sentNotes[static_cast<size_t> (channel)];
    if (m.isNoteOn())
    {
        // notes outside the violin (G3 to E7, after Octave) stay silent, as in Octavio 1
        const int pitch = m.getNoteNumber() + 12 * reader.octaveShift();
        if (pitch < 55 || pitch > 104)
            return;
        if (sent[static_cast<size_t> (m.getNoteNumber())] >= 0)
            engine->noteOff (when, sent[static_cast<size_t> (m.getNoteNumber())]);
        sent[static_cast<size_t> (m.getNoteNumber())] = static_cast<std::int8_t> (pitch);
        engine->noteOn (when, pitch, m.getVelocity());
    }
    else if (m.isNoteOff())
    {
        // the pitch it started on, even if Octave changed while it was held
        auto& s = sent[static_cast<size_t> (m.getNoteNumber())];
        if (s >= 0)
            engine->noteOff (when, s);
        s = -1;
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        engine->allNotesOff (when);
        sent.fill (-1);
    }
    else if (m.isController())
        engine->controller (when, m.getControllerNumber(), m.getControllerValue());
}

void Processor::renderEngine (float* left, float* right, int numSamples)
{
    if (! resampling)
    {
        engine->render (left, right, numSamples);
        return;
    }
    float* out[2] = { left, right };
    while (numSamples > 0)
    {
        const int n = std::min (numSamples, 512);
        const int need = static_cast<int> (std::ceil (n * ratio)) + 4;
        while (fifoFill < need)
        {
            const int m = std::min (64, static_cast<int> (fifo[0].size()) - fifoFill);
            engine->render (fifo[0].data() + fifoFill, fifo[1].data() + fifoFill, m);
            fifoFill += m;
        }
        int used = 0;
        for (size_t c = 0; c < 2; ++c)
            used = interpolators[c].process (ratio, fifo[c].data(), out[c], n);
        for (auto& f : fifo)
            std::copy (f.begin() + used, f.begin() + fifoFill, f.begin());
        fifoFill -= used;
        out[0] += n;
        out[1] += n;
        numSamples -= n;
    }
}

void Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    engine->setSettings (reader.read());

    keysToAudio.popAll (
        [this] (const KeyEvent& e)
        {
            const auto when = engineTime (0);
            if (e.velocity > 0.0f)
                engine->noteOn (when, e.note, e.velocity * 127.0f);
            else
                engine->noteOff (when, e.note);
        });
    for (const auto metadata : midi)
    {
        if (metadata.numBytes > 3)
            continue; // SysEx: copying it into a MidiMessage would allocate
        handleMidi (metadata.getMessage(), engineTime (metadata.samplePosition));
    }
    midi.clear();

    // in pieces, in case the host sends a block larger than it announced
    const bool stereo = buffer.getNumChannels() > 1;
    for (int done = 0; done < n;)
    {
        const int m = std::min (n - done, static_cast<int> (scratchL.size()));
        renderEngine (scratchL.data(), scratchR.data(), m);
        if (stereo)
        {
            buffer.copyFrom (0, done, scratchL.data(), m);
            buffer.copyFrom (1, done, scratchR.data(), m);
        }
        else
        {
            buffer.copyFrom (0, done, scratchL.data(), m, 0.5f);
            buffer.addFrom (0, done, scratchR.data(), m, 0.5f);
        }
        done += m;
    }
    for (int c = 2; c < buffer.getNumChannels(); ++c)
        buffer.clear (c, 0, n);
    hostClock += n;
}

juce::AudioProcessorEditor* Processor::createEditor()
{
    return new Editor (*this);
}

void Processor::getStateInformation (juce::MemoryBlock& destData)
{
    if (const auto xml = parameters.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void Processor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml != nullptr && xml->hasTagName (parameters.state.getType()))
        parameters.replaceState (juce::ValueTree::fromXml (*xml));
}
} // namespace octavio2

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new octavio2::Processor();
}
