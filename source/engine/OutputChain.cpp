#include "engine/OutputChain.h"

#include <algorithm>
#include <cmath>

namespace violinsynth::engine
{
namespace
{
// The stage fades in with the first 5% of the Room.
float stageFor (float room)
{
    return juce::jlimit (0.0f, 1.0f, room / 0.05f);
}
} // namespace

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
    stageAmount.reset (fs, 0.05);

    const juce::dsp::ProcessSpec stereo { fs, static_cast<juce::uint32> (maxBlockSize), 2 };
    reverb.prepare (stereo);
    limiter.prepare (stereo);
    limiter.setThreshold (-0.3f);
    limiter.setRelease (80.0f);

    prepareStage();
    dormantAfter = std::max (1, static_cast<int> (dormantSeconds * fs));
    appliedSordino = -1.0f;
    appliedRoom = -1.0f;
    setSettings (settings);
    width.setCurrentAndTargetValue (settings.width);
    gain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (settings.gainDb));
    stageAmount.setCurrentAndTargetValue (stageFor (settings.room));
    reset();
}

void OutputChain::reset()
{
    dcBlocker.reset();
    sordinoShelf.reset();
    sordinoLowPass.reset();
    resetPostBody();
}

void OutputChain::resetPostBody()
{
    decorrelateA1.reset();
    decorrelateA2.reset();
    decorrelateB1.reset();
    decorrelateB2.reset();
    for (auto& f : stage.left)
        f.reset();
    for (auto& f : stage.right)
        f.reset();
    stage.reflectionLowPass.reset();
    std::fill (stage.line.begin(), stage.line.end(), 0.0f);
    stage.write = 0;
    reverb.reset();
    limiter.reset();
    postBodyDormant = true;
    quietRun = dormantAfter;
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
    stageAmount.setTargetValue (stageFor (settings.room));

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
    // Read before processing: in mono, the engine passes `mono` as `right`.
    const auto inputSilent
        = std::all_of (mono, mono + numSamples, [] (float x) { return std::abs (x) <= silenceThreshold; });

    if (postBodyDormant)
    {
        if (inputSilent)
        {
            std::fill (left, left + numSamples, 0.0f);
            std::fill (right, right + numSamples, 0.0f);
            width.skip (numSamples);
            gain.skip (numSamples);
            stageAmount.skip (numSamples);
            return;
        }
        postBodyDormant = false;
        quietRun = 0;
    }

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

    processStage (mono, left, right, numSamples);

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

    // Asleep once the input and every tail have been silent for a while;
    // the limiter's release is long over by then, so a reset changes nothing.
    const auto quiet = [] (float x) { return std::abs (x) <= tailThreshold; };
    const auto outputSilent
        = inputSilent && std::all_of (left, left + numSamples, quiet) && std::all_of (right, right + numSamples, quiet);
    quietRun = outputSilent ? quietRun + numSamples : 0;
    if (quietRun >= dormantAfter)
        resetPostBody();
}
void OutputChain::prepareStage()
{
    // Directivity: +-5 dB peaks, at different frequencies in each ear.
    constexpr double leftHz[] = { 1150.0, 2100.0, 3300.0, 5200.0 }, rightHz[] = { 1500.0, 2650.0, 4100.0, 6400.0 };
    constexpr double gainDb[] = { 5.0, -5.0, 5.0, -5.0 };
    for (std::size_t k = 0; k < stage.left.size(); ++k)
    {
        stage.left[k].setPeak (fs, leftHz[k], gainDb[k], 2.5);
        stage.right[k].setPeak (fs, rightHz[k], gainDb[k], 2.5);
    }
    stage.reflectionLowPass.setLowPass (fs, 5000.0);

    // Five reflections per side, 4 to 42 ms.
    constexpr double leftMs[] = { 4.3, 11.7, 19.1, 27.9, 37.3 }, rightMs[] = { 5.9, 13.3, 17.2, 31.1, 41.7 };
    const auto samples = [this] (double ms) { return static_cast<int> (ms * 0.001 * fs); };
    int length = 1;
    while (length <= samples (rightMs[Stage::numReflections - 1]))
        length *= 2;
    stage.line.assign (static_cast<std::size_t> (length), 0.0f);
    stage.mask = length - 1;
    stage.write = 0;
    for (std::size_t k = 0; k < Stage::numReflections; ++k)
    {
        stage.delayLeft[k] = samples (leftMs[k]);
        stage.delayRight[k] = samples (rightMs[k]);
    }
}

void OutputChain::processStage (const float* mono, float* left, float* right, int numSamples)
{
    constexpr std::array<float, Stage::numReflections> reflectionGain { 0.42f, 0.3f, 0.24f, 0.18f, 0.13f };
    for (int i = 0; i < numSamples; ++i)
    {
        auto directLeft = left[i], directRight = right[i];
        for (auto& f : stage.left)
            directLeft = f.process (directLeft);
        for (auto& f : stage.right)
            directRight = f.process (directRight);
        stage.line[static_cast<std::size_t> (stage.write)] = stage.reflectionLowPass.process (mono[i]);
        float earlyLeft = 0.0f, earlyRight = 0.0f;
        for (std::size_t k = 0; k < Stage::numReflections; ++k)
        {
            earlyLeft += reflectionGain[k]
                * stage.line[static_cast<std::size_t> ((stage.write - stage.delayLeft[k]) & stage.mask)];
            earlyRight += reflectionGain[k]
                * stage.line[static_cast<std::size_t> ((stage.write - stage.delayRight[k]) & stage.mask)];
        }
        stage.write = (stage.write + 1) & stage.mask;
        const auto amount = stageAmount.getNextValue();
        left[i] += amount * (0.8f * directLeft + 0.5f * earlyLeft - left[i]);
        right[i] += amount * (0.8f * directRight + 0.5f * earlyRight - right[i]);
    }
}
} // namespace violinsynth::engine
