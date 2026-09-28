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
    settings.portamentoSeconds = 0.05;
    StringContext context;

    StringVoice voice;
    voice.prepare (internalRate, 2);
    voice.start (69, 0.5f);
    for (int n = 0; n < 10000; ++n)
        voice.processSample (settings, context);

    // One-pole glide in log frequency, time constant a third of the portamento time.
    voice.legato (73, 0.5f);
    const auto coeff = std::exp (-3.0 / (settings.portamentoSeconds * internalRate));
    double logF0 = std::log (440.0), worst = 0.0;
    const auto target = std::log (440.0 * std::pow (2.0, 4.0 / 12.0));
    for (int n = 0; n < static_cast<int> (0.3 * internalRate); ++n)
    {
        voice.processSample (settings, context);
        logF0 = target + coeff * (logF0 - target);
        worst = std::max (worst, std::abs (cents (voice.currentF0(), std::exp (logF0))));
    }
    CHECK (worst < 0.2);
    CHECK (std::abs (cents (voice.currentF0(), std::exp (target))) < 0.01);
}
