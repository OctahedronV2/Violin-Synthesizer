#include "engine/Body.h"

#include "dsp/BodyModes.h"

#include <BinaryData.h>

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <memory>
#include <span>

namespace violinsynth::engine
{
namespace
{
struct BodyData
{
    const char* impulseResponse;
    int impulseResponseSize;
    std::span<const dsp::bodydata::Mode> modes;
    float directGain;
    float outputGain;
};

BodyData bodyData (int index)
{
    using namespace dsp::bodydata;

    switch (index)
    {
        case 1:
            return { BinaryData::klimke_wav,
                     BinaryData::klimke_wavSize,
                     klimkeModes,
                     klimkeDirectGain,
                     klimkeOutputGain };
        case 2:
            return { BinaryData::stoppani_wav,
                     BinaryData::stoppani_wavSize,
                     stoppaniModes,
                     stoppaniDirectGain,
                     stoppaniOutputGain };
        case 3:
            return { BinaryData::iowa_wav, BinaryData::iowa_wavSize, iowaModes, iowaDirectGain, iowaOutputGain };
        default:
            return { BinaryData::levaggi_wav,
                     BinaryData::levaggi_wavSize,
                     levaggiModes,
                     levaggiDirectGain,
                     levaggiOutputGain };
    }
}
} // namespace

int Body::convolutionBlockSize (double sampleRate)
{
    int size = 128;
    while (size < 512 && size * 48000.0 < 128.0 * sampleRate - 1.0)
        size *= 2;
    return size;
}

std::vector<float> Body::impulseResponse (int bodyIndex, double sampleRate)
{
    bodyIndex = juce::jlimit (0, numBodies - 1, bodyIndex);
    const auto data = bodyData (bodyIndex);

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (
        new juce::MemoryInputStream (data.impulseResponse, static_cast<size_t> (data.impulseResponseSize), false),
        true));
    if (reader == nullptr)
        return { 1.0f };

    const auto length = static_cast<int> (reader->lengthInSamples);
    juce::AudioBuffer<float> original (1, length);
    reader->read (&original, 0, length, 0, true, false);
    const auto originalRate = reader->sampleRate;

    // Resampled the way JUCE's Convolution did before Phase 7, so the
    // bodies sound the same at every host rate.
    juce::AudioBuffer<float> resampled;
    if (juce::approximatelyEqual (originalRate, sampleRate))
    {
        resampled = original;
    }
    else
    {
        const auto ratio = originalRate / sampleRate;
        juce::MemoryAudioSource memorySource (original, false);
        juce::ResamplingAudioSource resampler (&memorySource, false, 1);
        const auto finalSize = juce::roundToInt (juce::jmax (1.0, length / ratio));
        resampler.setResamplingRatio (ratio);
        resampler.prepareToPlay (finalSize, originalRate);
        resampled.setSize (1, finalSize);
        resampler.getNextAudioBlock ({ &resampled, 0, finalSize });
    }

    const auto gain = static_cast<float> (originalRate / sampleRate)
        * juce::Decibels::decibelsToGain (targetRmsDb - measuredConvolutionRmsDb[static_cast<size_t> (bodyIndex)]);
    const auto* samples = resampled.getReadPointer (0);
    std::vector<float> ir (static_cast<size_t> (resampled.getNumSamples()));
    for (size_t i = 0; i < ir.size(); ++i)
        ir[i] = gain * samples[i];
    return ir;
}

void Body::prepare (double sampleRate, int bodyIndex)
{
    fs = sampleRate;
    bodyIndex = juce::jlimit (0, numBodies - 1, bodyIndex);

    std::vector<std::vector<float>> irs;
    for (int b = 0; b < numBodies; ++b)
        irs.push_back (impulseResponse (b, fs));
    convolution.prepare (convolutionBlockSize (fs), irs, bodyIndex, static_cast<int> (crossfadeSeconds * fs));

    // The lowest body mode is near 200 Hz; 20 ms of silence in and out
    // covers several of its periods.
    modalDormantAfter = std::max (1, static_cast<int> (0.02 * fs));
    modalBody = -1;
    setModalBody (bodyIndex);
    reset();
}

void Body::reset()
{
    convolution.reset();
    for (auto& r : resonators)
        r.reset();
    modalDormant = true;
    modalQuietRun = modalDormantAfter;
}

void Body::setBody (int bodyIndex)
{
    convolution.selectFilter (bodyIndex);
    setModalBody (bodyIndex);
}

void Body::setQuality (Quality q)
{
    if (q == quality)
        return;

    // The form that stops being used starts from silence when it comes back.
    quality = q;
    convolution.reset();
    for (auto& r : resonators)
        r.reset();
    modalDormant = true;
    modalQuietRun = modalDormantAfter;
}

bool Body::isDormant() const
{
    return quality == Quality::convolution ? convolution.isDormant() : modalDormant;
}

void Body::setModalBody (int bodyIndex)
{
    bodyIndex = juce::jlimit (0, numBodies - 1, bodyIndex);
    if (bodyIndex == modalBody)
        return;

    const auto data = bodyData (bodyIndex);
    numModes = 0;
    for (const auto& mode : data.modes)
    {
        if (numModes == maxModes || mode.freq >= 0.45f * static_cast<float> (fs))
            break;
        resonators[static_cast<size_t> (numModes)].setBandPass (fs, mode.freq, mode.q);
        modeGains[static_cast<size_t> (numModes)] = mode.gain;
        ++numModes;
    }
    directGain = data.directGain;
    modalOutputGain = data.outputGain;
    modalTrim = juce::Decibels::decibelsToGain (targetRmsDb - measuredModalRmsDb[static_cast<size_t> (bodyIndex)]);
    modalBody = bodyIndex;
}

void Body::process (float* samples, int numSamples)
{
    if (quality == Quality::convolution)
        convolution.process (samples, numSamples);
    else
        processModal (samples, numSamples);
}

void Body::processModal (float* samples, int numSamples)
{
    constexpr auto threshold = dsp::PartitionedConvolution::silenceThreshold;

    if (modalDormant)
    {
        int firstSound = 0;
        while (firstSound < numSamples && std::abs (samples[firstSound]) <= threshold)
            ++firstSound;
        std::fill (samples, samples + firstSound, 0.0f);
        if (firstSound == numSamples)
            return;

        // The resonators are at rest: start them on the first sample of sound.
        modalDormant = false;
        modalQuietRun = 0;
        samples += firstSound;
        numSamples -= firstSound;
    }

    for (int i = 0; i < numSamples; ++i)
    {
        const auto x = samples[i];
        auto y = directGain * x;
        for (int m = 0; m < numModes; ++m)
            y += modeGains[static_cast<size_t> (m)] * resonators[static_cast<size_t> (m)].process (x);
        samples[i] = modalTrim * modalOutputGain * y;

        const auto quiet = std::abs (x) <= threshold && std::abs (samples[i]) <= threshold;
        modalQuietRun = quiet ? modalQuietRun + 1 : 0;
    }

    if (modalQuietRun >= modalDormantAfter)
    {
        for (auto& r : resonators)
            r.reset();
        modalDormant = true;
    }
}
} // namespace violinsynth::engine
