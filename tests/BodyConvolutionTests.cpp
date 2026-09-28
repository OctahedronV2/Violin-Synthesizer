#include "dsp/PartitionedConvolution.h"
#include "engine/ViolinEngine.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

using namespace violinsynth;

namespace
{
std::vector<float> noise (std::size_t length, unsigned seed, float decaySamples = 0.0f)
{
    std::mt19937 rng (seed);
    std::normal_distribution<float> dist (0.0f, 0.3f);
    std::vector<float> x (length);
    for (std::size_t i = 0; i < length; ++i)
        x[i] = dist (rng) * (decaySamples > 0.0f ? std::exp (-static_cast<float> (i) / decaySamples) : 1.0f);
    return x;
}

std::vector<float> directConvolution (const std::vector<float>& x, const std::vector<float>& h)
{
    std::vector<float> y (x.size(), 0.0f);
    for (std::size_t n = 0; n < x.size(); ++n)
    {
        double sum = 0.0;
        for (std::size_t k = 0; k < h.size() && k <= n; ++k)
            sum += static_cast<double> (h[k]) * static_cast<double> (x[n - k]);
        y[n] = static_cast<float> (sum);
    }
    return y;
}

// Runs `x` through `c` in host blocks from `blockSizes`, cycling.
std::vector<float> runBlocks (dsp::PartitionedConvolution& c, std::vector<float> x, const std::vector<int>& blockSizes)
{
    std::size_t pos = 0;
    for (std::size_t b = 0; pos < x.size(); ++b)
    {
        const auto n = std::min (static_cast<std::size_t> (blockSizes[b % blockSizes.size()]), x.size() - pos);
        c.process (x.data() + pos, static_cast<int> (n));
        pos += n;
    }
    return x;
}

float maxAbsDifference (const std::vector<float>& a, const std::vector<float>& b)
{
    float d = 0.0f;
    for (std::size_t i = 0; i < std::min (a.size(), b.size()); ++i)
        d = std::max (d, std::abs (a[i] - b[i]));
    return d;
}
} // namespace

TEST_CASE ("Partitioned convolution matches direct convolution with zero latency", "[dsp][body]")
{
    const auto blockSizes = GENERATE (std::vector<int> { 1 },
                                      std::vector<int> { 32 },
                                      std::vector<int> { 128 },
                                      std::vector<int> { 1000 },
                                      std::vector<int> { 7, 300, 64, 1, 129 });
    const auto irLength = GENERATE (1, 100, 128, 1500);
    CAPTURE (blockSizes, irLength);

    const auto h = noise (static_cast<std::size_t> (irLength), 1, 300.0f);
    auto x = noise (6000, 2);
    // A gap longer than the impulse response lets the convolver go dormant
    // and wake up mid-stream.
    std::fill (x.begin() + 2000, x.begin() + 4200, 0.0f);

    dsp::PartitionedConvolution c;
    c.prepare (128, { h }, 0, 64);
    const auto y = runBlocks (c, x, blockSizes);
    CHECK (maxAbsDifference (y, directConvolution (x, h)) < 2.0e-5f);
}

TEST_CASE ("Partitioned convolution crossfades between impulse responses", "[dsp][body]")
{
    const auto h0 = noise (900, 3, 200.0f);
    const auto h1 = noise (700, 4, 200.0f);
    const auto x = noise (12000, 5);
    const auto y0 = directConvolution (x, h0);
    const auto y1 = directConvolution (x, h1);

    dsp::PartitionedConvolution c;
    constexpr int fade = 1000;
    c.prepare (128, { h0, h1 }, 0, fade);

    auto y = x;
    constexpr int switchAt = 3000 + 50; // mid-block: the fade starts on the next block boundary
    c.process (y.data(), switchAt);
    c.selectFilter (1);
    CHECK (c.selectedFilter() == 1);
    c.process (y.data() + switchAt, static_cast<int> (y.size()) - switchAt);

    const auto fadeStart = static_cast<std::size_t> ((switchAt / 128 + 1) * 128);
    for (std::size_t i = 0; i < fadeStart; ++i)
        REQUIRE (std::abs (y[i] - y0[i]) < 2.0e-5f);

    // During the fade the output is a blend of the two full convolutions:
    // the new body starts with its whole tail, not from silence.
    for (std::size_t i = fadeStart; i < fadeStart + fade; ++i)
    {
        const auto g = static_cast<float> (i - fadeStart + 1) / fade;
        REQUIRE (std::abs (y[i] - ((1.0f - g) * y0[i] + g * y1[i])) < 2.0e-5f);
    }
    for (std::size_t i = fadeStart + fade; i < y.size(); ++i)
        REQUIRE (std::abs (y[i] - y1[i]) < 2.0e-5f);
}

TEST_CASE ("Partitioned convolution sleeps on silence and wakes on the next sample", "[dsp][body]")
{
    const auto h = noise (1000, 6, 200.0f);
    dsp::PartitionedConvolution c;
    c.prepare (128, { h }, 0, 64);
    CHECK (c.isDormant());

    std::vector<float> x (5000, 0.0f);
    x[10] = 1.0f;
    c.process (x.data(), 100);
    CHECK_FALSE (c.isDormant());
    CHECK (x[10] == h[0]);
    c.process (x.data() + 100, 4900);
    CHECK (c.isDormant()); // the tail (1000 samples) has been played

    // A single sample of sound after the sleep is convolved from its first sample.
    std::vector<float> y (256, 0.0f);
    y[77] = 0.5f;
    c.process (y.data(), 256);
    for (std::size_t i = 0; i < 256; ++i)
        REQUIRE (std::abs (y[i] - (i >= 77 ? 0.5f * h[i - 77] : 0.0f)) < 1.0e-6f);
}

TEST_CASE ("Body impulse responses are prepared for every host rate", "[engine][body]")
{
    for (auto fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        CAPTURE (fs);
        const auto blockSize = engine::Body::convolutionBlockSize (fs);
        CHECK (blockSize * 48000.0 >= 128.0 * fs - 1.0);
        CHECK (blockSize <= 512);

        for (int b = 0; b < engine::Body::numBodies; ++b)
        {
            const auto ir = engine::Body::impulseResponse (b, fs);
            REQUIRE (ir.size() > 100);
            const auto peak = *std::max_element (ir.begin(),
                                                 ir.end(),
                                                 [] (float x, float y) { return std::abs (x) < std::abs (y); });
            CHECK (std::isfinite (peak));
            CHECK (std::abs (peak) > 1.0e-3f);
        }
    }
}

TEST_CASE ("Engine stops computing once a note has died away, and plays on the next sample", "[engine][body]")
{
    const auto quality = GENERATE (engine::Body::Quality::convolution, engine::Body::Quality::modal);
    CAPTURE (quality == engine::Body::Quality::modal);

    constexpr double fs = 48000.0;
    constexpr int block = 128;
    engine::ViolinEngine e;
    engine::EngineSettings s; // default: sympathetic resonance, room and width on
    s.bodyQuality = quality;
    e.setSettings (s);
    e.prepare (fs, block);

    auto idle = [&e]
    { return e.getBody().isDormant() && e.getSympathetic().isDormant() && e.getOutputChain().isPostBodyDormant(); };
    CHECK (idle());

    juce::AudioBuffer<float> buffer (2, block);
    auto play = [&] (double seconds, const juce::MidiBuffer& midi)
    {
        float peak = 0.0f;
        for (int i = 0; i < static_cast<int> (seconds * fs / block); ++i)
        {
            e.process (buffer, i == 0 ? midi : juce::MidiBuffer {});
            peak = std::max (peak, buffer.getMagnitude (0, block));
        }
        return peak;
    };

    juce::MidiBuffer on, off;
    on.addEvent (juce::MidiMessage::noteOn (1, 62, 0.6f), 0); // D string, so G and A resonate
    off.addEvent (juce::MidiMessage::noteOff (1, 62), 0);
    CHECK (play (1.0, on) > 0.01f);
    CHECK_FALSE (idle());
    play (10.0, off);
    CHECK (play (1.0, {}) == 0.0f);
    CHECK (idle());

    // The next note sounds from its first block.
    juce::MidiBuffer again;
    again.addEvent (juce::MidiMessage::noteOn (1, 69, 0.6f), 5);
    e.process (buffer, again);
    CHECK (buffer.getMagnitude (0, block) > 0.0f);
    CHECK (play (0.3, {}) > 0.01f);
    CHECK_FALSE (idle());
}
