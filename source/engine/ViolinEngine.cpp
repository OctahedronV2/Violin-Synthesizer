#include "engine/ViolinEngine.h"

#include "engine/HurdyFix.h"

#include <algorithm>
#include <cmath>

namespace violinsynth::engine
{
int ViolinEngine::oversamplingOrderFor (double hostSampleRate)
{
    int order = 0;
    while (order < 3 && hostSampleRate * (1 << order) < minInternalRate - 1.0)
        ++order;
    return order;
}

void ViolinEngine::prepare (double hostSampleRate, int maxBlockSize)
{
    hostRate = hostSampleRate;
    maxBlock = std::max (1, maxBlockSize);
    oversamplingOrder = oversamplingOrderFor (hostRate);

    if (oversamplingOrder > 0)
    {
        oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
            1,
            static_cast<size_t> (oversamplingOrder),
            juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
            true,
            true);
        oversampling->initProcessing (static_cast<size_t> (maxBlock));

        // The string writes straight into the oversampler's internal-rate
        // buffer, and only the downsampler runs. JUCE's processSamplesDown()
        // reads the block that processSamplesUp() returns, which is the same
        // buffer every time, so upsample once here to find it. The
        // up-filters' state is never used on the way down.
        juce::AudioBuffer<float> silence (1, maxBlock);
        silence.clear();
        internalBlock = oversampling->processSamplesUp (juce::dsp::AudioBlock<const float> (silence));
    }
    else
    {
        oversampling.reset();
        internalBlock = {};
    }

    violin.prepare (getInternalSampleRate());
    sympathetic.prepare (hostRate);
    body.prepare (hostRate, acoustic() ? Body::guitarBody : settings.body);
    amp.prepare (getInternalSampleRate(), hostRate);
    output.prepare (hostRate, maxBlock);
    radiation.prepare (hostRate, maxBlock);
    mono.setSize (1, maxBlock);
    reset();
}

void ViolinEngine::reset()
{
    violin.reset();
    sympathetic.reset();
    body.reset();
    radiation.reset();
    amp.reset();
    output.reset();
    sordino = settings.output.sordino;
    if (oversampling != nullptr)
        oversampling->reset();
}

void ViolinEngine::setSettings (const EngineSettings& s)
{
    settings = s;
    violin.setSettings (s.performance);
    output.setSettings (s.output);
    body.setQuality (s.bodyQuality);
    body.setBody (acoustic() ? Body::guitarBody : s.body);
    amp.setSettings (s.drive, s.performance.pickup);
}

int ViolinEngine::getLatencySamples() const
{
    const auto os = oversampling != nullptr ? oversampling->getLatencyInSamples() : 0.0f;
    return static_cast<int> (std::lround (os)) + body.getLatencySamples();
}

void ViolinEngine::renderString (int start, int numSamples)
{
    if (numSamples <= 0)
        return;

    float* hostSamples = mono.getWritePointer (0) + start;

    if (oversampling == nullptr)
    {
        violin.render (hostSamples, numSamples);
        if (amplified())
            amp.processPreamp (hostSamples, numSamples);
        return;
    }

    float* channels[] = { hostSamples };
    juce::dsp::AudioBlock<float> hostBlock (channels, 1, static_cast<size_t> (numSamples));
    violin.render (internalBlock.getChannelPointer (0), numSamples * getOversamplingFactor());
    if (amplified())
        amp.processPreamp (internalBlock.getChannelPointer (0), numSamples * getOversamplingFactor());
    oversampling->processSamplesDown (hostBlock);
}

void ViolinEngine::process (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi)
{
    // Some hosts send blocks larger than announced in prepare(); process
    // those in chunks rather than overrunning the internal buffers.
    const auto numSamples = buffer.getNumSamples();
    auto events = midi.cbegin();

    for (int chunkStart = 0; chunkStart < numSamples; chunkStart += maxBlock)
    {
        const auto chunkLength = std::min (maxBlock, numSamples - chunkStart);
        int position = 0;

        for (; events != midi.cend() && (*events).samplePosition < chunkStart + chunkLength; ++events)
        {
            const auto metadata = *events;
            const auto eventPosition = juce::jlimit (0, chunkLength, metadata.samplePosition - chunkStart);
            renderString (position, eventPosition - position);
            position = eventPosition;
            // Only channel messages matter here; copying SysEx into a MidiMessage would allocate.
            if (metadata.numBytes <= 3)
                violin.handleMidi (metadata.getMessage());
        }
        renderString (position, chunkLength - position);

        // Con sordino: put the mute on over about 150 ms.
        const auto sordinoTarget
            = violin.currentArticulation() == Articulation::conSordino ? 1.0f : settings.output.sordino;
        const auto step = static_cast<float> (chunkLength / (0.15 * hostRate));
        sordino = sordinoTarget + std::clamp (sordino - sordinoTarget, -step, step);
        auto outputSettings = settings.output;
        outputSettings.sordino = sordino;
        output.setSettings (outputSettings);

        float* samples = mono.getWritePointer (0);
        if (amplified())
        {
            output.processPreBody (samples, chunkLength);
            amp.processCabinet (samples, chunkLength);
        }
        else if (acoustic())
        {
            // No sympathetic strings: the flat bow's drones stand in for them.
            output.processPreBody (samples, chunkLength);
            body.process (samples, chunkLength);
        }
        else
        {
            sympathetic.process (samples,
                                 chunkLength,
                                 violin.openStrings(),
                                 (hg::on (hg::cleanEnds) ? 0.0 : 1.0) * settings.performance.voice.resonance
                                     * instrumentSpec (settings.performance.instrument).sympatheticScale);
            output.processPreBody (samples, chunkLength);
            radiation.setBodyPeaks (instrumentSpec (settings.performance.instrument).bodyPeaksDb);
            radiation.processPreBody (samples, chunkLength);
            body.process (samples, chunkLength);
            radiation.processPostBody (samples, chunkLength);
        }

        auto* left = buffer.getWritePointer (0, chunkStart);
        auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1, chunkStart) : nullptr;

        if (right != nullptr)
        {
            output.processPostBody (samples, left, right, chunkLength);
        }
        else
        {
            // Mono output: the scratch buffer doubles as the right channel
            // (each sample is read before it is overwritten), then sum.
            output.processPostBody (samples, left, samples, chunkLength);
            for (int i = 0; i < chunkLength; ++i)
                left[i] = 0.5f * (left[i] + samples[i]);
        }
    }
}
} // namespace violinsynth::engine
