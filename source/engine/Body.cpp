#include "engine/Body.h"

#include "dsp/BodyModes.h"

#include <BinaryData.h>

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

void Body::prepare (double sampleRate, int maxBlockSize, int bodyIndex)
{
    fs = sampleRate;
    maxBlock = maxBlockSize;
    convolution.prepare ({ sampleRate, static_cast<juce::uint32> (maxBlockSize), 1 });
    loadedConvolutionBody = -1;
    loadConvolutionBody (bodyIndex);
    modalBody = -1;
    setModalBody (bodyIndex);
    reset();
}

void Body::reset()
{
    convolution.reset();
    for (auto& r : resonators)
        r.reset();
}

void Body::loadConvolutionBody (int bodyIndex)
{
    bodyIndex = juce::jlimit (0, numBodies - 1, bodyIndex);
    if (bodyIndex == loadedConvolutionBody)
        return;

    const auto data = bodyData (bodyIndex);
    // The impulse responses are 48 kHz; JUCE resamples them to the processing rate.
    convolution.loadImpulseResponse (data.impulseResponse,
                                     static_cast<size_t> (data.impulseResponseSize),
                                     juce::dsp::Convolution::Stereo::no,
                                     juce::dsp::Convolution::Trim::no,
                                     0,
                                     juce::dsp::Convolution::Normalise::no);
    loadedConvolutionBody = bodyIndex;
    convolutionTrim
        = juce::Decibels::decibelsToGain (targetRmsDb - measuredConvolutionRmsDb[static_cast<size_t> (bodyIndex)]);
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
    {
        float* channels[] = { samples };
        juce::dsp::AudioBlock<float> block (channels, 1, static_cast<size_t> (numSamples));
        convolution.process (juce::dsp::ProcessContextReplacing<float> (block));
        block.multiplyBy (convolutionTrim);
        return;
    }

    for (int i = 0; i < numSamples; ++i)
    {
        const auto x = samples[i];
        auto y = directGain * x;
        for (int m = 0; m < numModes; ++m)
            y += modeGains[static_cast<size_t> (m)] * resonators[static_cast<size_t> (m)].process (x);
        samples[i] = modalTrim * modalOutputGain * y;
    }
}

int Body::getLatencySamples() const
{
    return quality == Quality::convolution ? convolution.getLatency() : 0;
}
} // namespace violinsynth::engine
