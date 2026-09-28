// The voice computes pitch at control rate and interpolates it (docs/PHASE7.md, 7.2).
// These check the interpolated pitch against the per-sample curves it replaces.

#include "engine/StringVoice.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

using namespace violinsynth::engine;

namespace
{
constexpr double internalRate = 192000.0;

double cents (double f, double reference)
{
    return 1200.0 * std::log2 (f / reference);
}
} // namespace

TEST_CASE ("Control-rate vibrato follows the per-sample vibrato curve", "[engine][controlrate]")
{
    VoiceSettings settings;
    settings.humanise = 0.0;
    settings.vibratoDepthCents = 40.0;
    settings.vibratoRateHz = 7.0;
    StringContext context;

    for (int note : { 57, 69, 88 })
    {
        INFO ("note " << note);
        StringVoice voice;
        voice.prepare (internalRate, note >= 76 ? 3 : note >= 69 ? 2 : 0);

        const int startAt = 1000;
        double worst = 0.0;
        for (int n = 0; n < static_cast<int> (1.5 * internalRate); ++n)
        {
            if (n == startAt)
                voice.start (note, 0.5f);
            voice.processSample (settings, context);
            if (n < startAt)
                continue;

            // The per-sample reference; the vibrato runs while the string is played.
            const auto sinceNote = (n - startAt + 1) / internalRate;
            const auto onset = std::clamp ((sinceNote - settings.vibratoDelaySeconds) / 0.25, 0.0, 1.0);
            const auto vibrato = 0.5 * settings.vibratoDepthCents * onset
                * std::sin (2.0 * std::numbers::pi * settings.vibratoRateHz * sinceNote);
            const auto expected = 440.0 * std::pow (2.0, (note - 69) / 12.0 + vibrato / 1200.0);
            worst = std::max (worst, std::abs (cents (voice.currentF0(), expected)));
        }
        CHECK (worst < 0.2);
    }
}

TEST_CASE ("Control-rate glide follows the per-sample glide", "[engine][controlrate]")
{
    VoiceSettings settings;
    settings.vibratoDepthCents = 0.0;
    settings.humanise = 0.0;
    settings.portamentoSeconds = 0.05;
    StringContext context;

    // A slur from B4 to F#5 on the A string shifts the hand: the pitch eases
    // (raised cosine in log frequency) over the portamento time.
    StringVoice voice;
    voice.prepare (internalRate, 2);
    voice.start (71, 0.5f);
    for (int n = 0; n < 10000; ++n)
        voice.processSample (settings, context);

    voice.legato (78, 0.5f);
    const auto from = std::log (440.0 * std::pow (2.0, 2.0 / 12.0));
    const auto target = std::log (440.0 * std::pow (2.0, 9.0 / 12.0));
    double worst = 0.0;
    for (int n = 0; n < static_cast<int> (0.3 * internalRate); ++n)
    {
        voice.processSample (settings, context);
        const auto progress = std::min (1.0, (n + 1) / (settings.portamentoSeconds * internalRate));
        const auto logF0 = from + (target - from) * (0.5 - 0.5 * std::cos (std::numbers::pi * progress));
        worst = std::max (worst, std::abs (cents (voice.currentF0(), std::exp (logF0))));
    }
    CHECK (worst < 0.2);
    CHECK (std::abs (cents (voice.currentF0(), std::exp (target))) < 0.01);
}

TEST_CASE ("A slur within the hand changes finger instead of sliding", "[engine][controlrate]")
{
    VoiceSettings settings;
    settings.vibratoDepthCents = 0.0;
    settings.humanise = 0.0;
    settings.portamentoSeconds = 0.05;
    StringContext context;

    // From the open A, and from B4 up to D5: both in first position.
    for (const auto [from, to] : { std::pair { 69, 74 }, std::pair { 71, 74 }, std::pair { 74, 71 } })
    {
        StringVoice voice;
        voice.prepare (internalRate, 2);
        voice.start (from, 0.5f);
        for (int n = 0; n < 10000; ++n)
            voice.processSample (settings, context);
        voice.legato (to, 0.5f);
        const auto expected = 440.0 * std::pow (2.0, (to - 69) / 12.0);
        for (int n = 0; n < static_cast<int> (0.012 * internalRate); ++n)
            voice.processSample (settings, context);
        CAPTURE (from, to);
        CHECK (std::abs (cents (voice.currentF0(), expected)) < 0.5);
    }
}
