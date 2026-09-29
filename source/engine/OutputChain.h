#pragma once

#include <array>
#include <vector>

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
//   after the body:  stereo width, stage, small room, output gain, limiter
//
// The stage (docs/REFERENCE_SOUND.md): a violin radiates each frequency in
// its own direction, so the two microphones hear different body peaks, which
// move against each other as vibrato sweeps the harmonics across them. Early
// reflections from the stage floor and nearby walls follow. It fades in as
// the Room rises from 0 to 5%, so Room at 0 is the dry violin.
//
// Once the body's output and the chain's own tails (all-passes, room,
// limiter) have been silent for a second (below -120 dBFS), the part after the body writes
// silence without computing until sound arrives again.
class OutputChain
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset();
    void setSettings (const OutputSettings& s);

    void processPreBody (float* mono, int numSamples);
    // Writes the stereo result into left/right from the mono body output.
    void processPostBody (const float* mono, float* left, float* right, int numSamples);

    bool isPostBodyDormant() const { return postBodyDormant; } // for tests

private:
    static constexpr float silenceThreshold = 1.0e-10f; // input: about -200 dBFS
    // Output: -120 dBFS. JUCE's reverb settles into a float limit cycle near
    // -140 dBFS instead of decaying to zero, so its tail ends here.
    static constexpr float tailThreshold = 1.0e-6f;
    static constexpr double dormantSeconds = 1.0;

    void resetPostBody();

    double fs = 48000.0;
    OutputSettings settings;

    DcBlocker dcBlocker;
    Biquad sordinoShelf, sordinoLowPass;
    float appliedSordino = -1.0f;
    float appliedRoom = -1.0f;

    AllPass decorrelateA1, decorrelateA2, decorrelateB1, decorrelateB2;
    juce::SmoothedValue<float> width, gain, stageAmount;

    struct Stage
    {
        static constexpr int numReflections = 5;
        std::array<Biquad, 4> left, right; // directivity peaks
        Biquad reflectionLowPass;
        std::vector<float> line; // power-of-two length
        int mask = 0, write = 0;
        std::array<int, numReflections> delayLeft {}, delayRight {};
    };
    void prepareStage();
    void processStage (const float* mono, float* left, float* right, int numSamples);
    Stage stage;

    juce::dsp::Reverb reverb;
    juce::dsp::Limiter<float> limiter;

    bool postBodyDormant = true;
    int quietRun = 0, dormantAfter = 1; // samples of silent input and output
};
} // namespace violinsynth::engine
