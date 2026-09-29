#include "engine/OutputChain.h"
#include "engine/AuthVariant.h"
#include <array>

#include <algorithm>
#include <cmath>

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

    dormantAfter = std::max (1, static_cast<int> (dormantSeconds * fs));
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
    resetPostBody();
}

void OutputChain::resetPostBody()
{
    decorrelateA1.reset();
    decorrelateA2.reset();
    decorrelateB1.reset();
    decorrelateB2.reset();
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

    if (authOn (4))
    {
        // SCRATCH: a violin radiates each frequency in its own direction, so two
        // microphones hear different body peaks. As vibrato sweeps the partials
        // across them, the left and right levels move against each other.
        // Early reflections from a stage floor and nearby walls follow.
        struct Dir
        {
            std::array<Biquad, 12> l, r; // SCRATCH: 4 used unless DENSE
        };
        static Dir dir;
        static std::array<float, 8192> line {};
        static int w = 0;
        static Biquad reflLow;
        static bool ready = false;
        if (! ready)
        {
            const double fl[] = { 1150.0, 2100.0, 3300.0, 5200.0 }, fr[] = { 1500.0, 2650.0, 4100.0, 6400.0 };
            const double g[] = { 5.0, -5.0, 5.0, -5.0 };
            for (int k = 0; k < 4; ++k)
            {
                dir.l[static_cast<size_t> (k)].setPeak (fs, fl[k], g[k], 2.5);
                dir.r[static_cast<size_t> (k)].setPeak (fs, fr[k], g[k], 2.5);
            }
            if (std::getenv ("DENSE"))
            {
                // SCRATCH: a violin's radiation at one microphone has a peak or dip every
                // few hundred hertz above 1 kHz; each ear hears a different pattern.
                unsigned r = 12345u;
                const auto rnd = [&r] { r = r * 1664525u + 1013904223u; return static_cast<double> (r >> 8) / 16777216.0; };
                for (int k = 0; k < 12; ++k)
                {
                    const auto f = 900.0 * std::pow (10000.0 / 900.0, (k + rnd()) / 12.0);
                    dir.l[static_cast<size_t> (k)].setPeak (fs, f, (k % 2 ? 6.0 : -6.0), 5.0);
                    const auto f2 = 900.0 * std::pow (10000.0 / 900.0, (k + rnd()) / 12.0);
                    dir.r[static_cast<size_t> (k)].setPeak (fs, f2, (k % 2 ? -6.0 : 6.0), 5.0);
                }
            }
            reflLow.setLowPass (fs, 5000.0);
            ready = true;
        }
        const auto ms = [this] (double m) { return static_cast<int> (m * 0.001 * fs); };
        const int tl[] = { ms (4.3), ms (11.7), ms (19.1), ms (27.9), ms (37.3) };
        const int tr[] = { ms (5.9), ms (13.3), ms (17.2), ms (31.1), ms (41.7) };
        const float gr[] = { 0.42f, 0.3f, 0.24f, 0.18f, 0.13f };
        for (int i = 0; i < numSamples; ++i)
        {
            auto dl = left[i], dr = right[i];
            for (auto& f : dir.l)
                dl = f.process (dl);
            for (auto& f : dir.r)
                dr = f.process (dr);
            line[static_cast<size_t> (w)] = reflLow.process (mono[i]);
            float el = 0.0f, er = 0.0f;
            for (int k = 0; k < 5; ++k)
            {
                el += gr[k] * line[static_cast<size_t> ((w - tl[k]) & 8191)];
                er += gr[k] * line[static_cast<size_t> ((w - tr[k]) & 8191)];
            }
            w = (w + 1) & 8191;
            left[i] = 0.8f * dl + 0.5f * el;
            right[i] = 0.8f * dr + 0.5f * er;
        }
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

    // Asleep once the input and every tail have been silent for a while;
    // the limiter's release is long over by then, so a reset changes nothing.
    const auto quiet = [] (float x) { return std::abs (x) <= tailThreshold; };
    const auto outputSilent
        = inputSilent && std::all_of (left, left + numSamples, quiet) && std::all_of (right, right + numSamples, quiet);
    quietRun = outputSilent ? quietRun + numSamples : 0;
    if (quietRun >= dormantAfter)
        resetPostBody();
}
} // namespace violinsynth::engine
