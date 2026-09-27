#pragma once

#include "engine/Filters.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

namespace violinsynth::engine
{
struct OutputSettings
{
    float sordino = 0.0f; // 0..1
    float width = 0.5f; // 0..1
    float room = 0.15f; // reverb wet level 0..1
    float gainDb = 0.0f;
};

// Everything after the string except the body:
//   before the body: DC blocker, sordino (mute on the bridge)
//   after the body:  stereo width, small room, output gain, limiter
class OutputChain
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset();
    void setSettings (const OutputSettings& s);

    void processPreBody (float* mono, int numSamples);
    // Writes the stereo result into left/right from the mono body output.
    void processPostBody (const float* mono, float* left, float* right, int numSamples);

private:
    double fs = 48000.0;
    OutputSettings settings;

    DcBlocker dcBlocker;
    Biquad sordinoShelf, sordinoLowPass;
    float appliedSordino = -1.0f;
    float appliedRoom = -1.0f;

    AllPass decorrelateA1, decorrelateA2, decorrelateB1, decorrelateB2;
    juce::SmoothedValue<float> width, gain;

    juce::dsp::Reverb reverb;
    juce::dsp::Limiter<float> limiter;
};
} // namespace violinsynth::engine
