// The voice computes pitch at control rate and interpolates it (docs/PHASE7.md, 7.2).
// These check the interpolated pitch against the per-sample curves it replaces.

#include "engine/StringVoice.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

using namespace violinsynth::engine;

namespace
{
constexpr double internalRate = 192000.0;

double cents (double f, double reference)
{
    return 1200.0 * std::log2 (f / reference);
}
} // namespace

TEST_CASE ("Control-rate vibrato is smooth and blooms to the set depth and rate", "[engine][controlrate]")
{
    VoiceSettings settings;
    settings.humanise = 0.0;
    settings.intonation = 0.0;
    settings.vibratoDepthCents = 40.0;
    settings.vibratoRateHz = 7.0;
    StringContext context;

    for (int note : { 57, 69, 88 })
    {
        INFO ("note " << note);
        StringVoice voice;
        voice.prepare (internalRate, note >= 76 ? 3 : note >= 69 ? 2 : 0);
        voice.setIntonation (0.0);
        const auto f0 = 440.0 * std::pow (2.0, (note - 69) / 12.0);

        const int startAt = 1000;
        std::vector<double> pitch;
        for (int n = 0; n < static_cast<int> (2.0 * internalRate); ++n)
        {
            if (n == startAt)
                voice.start (note, 0.5f);
            voice.processSample (settings, context);
            if (n > startAt)
                pitch.push_back (cents (voice.currentF0(), f0));
        }
        const auto at = [] (double seconds) { return static_cast<std::size_t> (seconds * internalRate); };

        // Still until the bloom starts, a tenth of a second before the delay.
        double before = 0.0;
        for (std::size_t i = 0; i < at (settings.vibratoDelaySeconds - 0.1); ++i)
            before = std::max (before, std::abs (pitch[i]));
        CHECK (before < 0.05);

        // Smooth: the control-rate ramps leave no steps or corners a per-sample
        // curve would not have.
        double bend = 0.0;
        for (std::size_t i = 1; i + 1 < pitch.size(); ++i)
            bend = std::max (bend, std::abs (pitch[i + 1] - 2.0 * pitch[i] + pitch[i - 1]));
        CHECK (bend < 1.0e-3);

        // Bloomed: about the set depth (0.8 of it, a little different on every
        // note and wider when louder) at about the set rate (quickened by 5%).
        const auto from = at (settings.vibratoDelaySeconds + 0.6), to = pitch.size();
        const auto [lo, hi] = std::minmax_element (pitch.begin() + static_cast<std::ptrdiff_t> (from), pitch.end());
        const auto swing = *hi - *lo;
        CHECK (swing > 0.5 * settings.vibratoDepthCents);
        CHECK (swing < 1.2 * settings.vibratoDepthCents);
        int crossings = 0;
        for (auto i = from + 1; i < to; ++i)
            crossings += (pitch[i - 1] < 0.0) != (pitch[i] < 0.0) ? 1 : 0;
        const auto rate = 0.5 * crossings / (static_cast<double> (to - from) / internalRate);
        CHECK (std::abs (rate / (1.05 * settings.vibratoRateHz) - 1.0) < 0.12);
    }
}

TEST_CASE ("Control-rate glide follows the per-sample glide", "[engine][controlrate]")
{
    VoiceSettings settings;
    settings.vibratoDepthCents = 0.0;
    settings.humanise = 0.0;
    settings.intonation = 0.0;
    settings.portamentoSeconds = 0.05;
    StringContext context;

    // A slur from B4 to F#5 on the A string shifts the hand: the pitch eases
    // (raised cosine in log frequency, then smoothstepped to hide it) over the
    // portamento time.
    StringVoice voice;
    voice.prepare (internalRate, 2);
    voice.setIntonation (0.0);
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
        auto c = 0.5 - 0.5 * std::cos (std::numbers::pi * progress);
        c = c * c * (3.0 - 2.0 * c); // hidden: the finger moves late and fast, then lands
        const auto logF0 = from + (target - from) * c;
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
    settings.intonation = 0.0;
    settings.portamentoSeconds = 0.05;
    StringContext context;

    // From the open A, and from B4 up to D5: both in first position.
    for (const auto [from, to] : { std::pair { 69, 74 }, std::pair { 71, 74 }, std::pair { 74, 71 } })
    {
        StringVoice voice;
        voice.prepare (internalRate, 2);
        voice.setIntonation (0.0);
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
