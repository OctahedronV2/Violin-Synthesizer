#include "engine/OutputChain.h"

namespace violinsynth::engine
{
void OutputChain::prepare (double sampleRate, int maxBlockSize)
{
    fs = sampleRate;
    dcBlocker.prepare (fs);

    auto ms = [this] (double milliseconds) { return static_cast<int> (milliseconds * 0.001 * fs); };
    decorrelateA1.prepare (ms (3.1), 0.6f);
    decorrelateA2.prepare (ms (7.3), 0.5f);
    decorrelateB1.prepare (ms (4.3), 0.6f);
    decorrelateB2.prepare (ms (9.7), 0.5f);

    width.reset (fs, 0.05);
    gain.reset (fs, 0.05);

    const juce::dsp::ProcessSpec stereo { fs, static_cast<juce::uint32> (maxBlockSize), 2 };
    reverb.prepare (stereo);
    limiter.prepare (stereo);
    limiter.setThreshold (-0.3f);
    limiter.setRelease (80.0f);

    appliedSordino = -1.0f;
    appliedRoom = -1.0f;
    setSettings (settings);
    width.setCurrentAndTargetValue (settings.width);
    gain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (settings.gainDb));
    reset();
}

void OutputChain::reset()
{
    dcBlocker.reset();
    sordinoShelf.reset();
    sordinoLowPass.reset();
    decorrelateA1.reset();
    decorrelateA2.reset();
    decorrelateB1.reset();
    decorrelateB2.reset();
    reverb.reset();
    limiter.reset();
}

void OutputChain::setSettings (const OutputSettings& s)
{
    settings = s;

    // A mute clamps extra mass onto the bridge, which mainly takes the
    // bridge hill and everything above it down.
    if (std::abs (settings.sordino - appliedSordino) > 1.0e-3f)
    {
        sordinoShelf.setHighShelf (fs, 1200.0, -16.0 * static_cast<double> (settings.sordino));
        sordinoLowPass.setLowPass (fs, 20000.0 * std::pow (0.2, static_cast<double> (settings.sordino)));
        appliedSordino = settings.sordino;
    }

    width.setTargetValue (juce::jlimit (0.0f, 1.0f, settings.width));
    gain.setTargetValue (juce::Decibels::decibelsToGain (settings.gainDb));

    if (std::abs (settings.room - appliedRoom) > 1.0e-4f)
    {
        juce::dsp::Reverb::Parameters p;
        p.roomSize = 0.45f;
        p.damping = 0.55f;
        p.width = 1.0f;
        p.wetLevel = 0.5f * juce::jlimit (0.0f, 1.0f, settings.room);
        p.dryLevel = 1.0f;
        reverb.setParameters (p);
        appliedRoom = settings.room;
    }
}

void OutputChain::processPreBody (float* mono, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        auto x = dcBlocker.process (mono[i]);
        if (appliedSordino > 0.0f)
            x = sordinoLowPass.process (sordinoShelf.process (x));
        mono[i] = x;
    }
}

void OutputChain::processPostBody (const float* mono, float* left, float* right, int numSamples)
{
    // Mid/side widening with two decorrelating all-pass chains. The side
    // signal cancels in a mono sum, so mono compatibility is preserved.
    for (int i = 0; i < numSamples; ++i)
    {
        const auto m = mono[i];
        const auto a = decorrelateA2.process (decorrelateA1.process (m));
        const auto b = decorrelateB2.process (decorrelateB1.process (m));
        const auto side = 0.5f * width.getNextValue() * (a - b);
        left[i] = m + side;
        right[i] = m - side;
    }

    float* channels[] = { left, right };
    juce::dsp::AudioBlock<float> block (channels, 2, static_cast<size_t> (numSamples));
    const juce::dsp::ProcessContextReplacing<float> context (block);
    reverb.process (context);

    for (int i = 0; i < numSamples; ++i)
    {
        const auto g = gain.getNextValue();
        left[i] *= g;
        right[i] *= g;
    }

    limiter.process (context);
}
} // namespace violinsynth::engine
