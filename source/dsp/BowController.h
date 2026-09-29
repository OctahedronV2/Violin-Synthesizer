#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace violinsynth::dsp
{
// The player's ear and bow arm for one bowed string (docs/CLEAN_BOWING.md).
//
// A professional keeps the string in clean Helmholtz motion by feel: one
// stick and one slip per period. This listens to the string and adjusts the
// bow weight the same way:
//
//   scratch     The bridge force does not repeat from one period to the next:
//               the string is crushed, or still settling. Ease off.
//   multi-slip  The tone is steady, but the string lets go of the bow more than
//               once per period (a hollow "surface" tone): the weight is below
//               what the string needs. Lean in.
//   clean       Drift back to the weight the player was asked for.
//
// Leaning in while the string is still chattering after the attack makes it
// worse in this model, so the weight only rises once the tone is steady.
//
// The weight is a factor on the bow force; the string model itself is
// unchanged. It is kept from note to note on the same string, as a player
// remembers how a string responds.
class BowController
{
public:
    // Scratch above this share of the signal (-25 dB) counts as not clean.
    static constexpr double scratchThreshold = 0.00316;
    // Slips per period above this count as multiple slipping.
    static constexpr double multiSlipThreshold = 1.2;
    // A steady double slip: this many slips per period, with scratch below -20 dB.
    static constexpr double doubleSlipLow = 1.5, doubleSlipHigh = 2.6, doubleSlipScratch = 0.01;
    // How fast the weight moves, in natural-log units per second (x2.7 in 100 ms).
    static constexpr double correctionRate = 10.0;
    static constexpr double returnSeconds = 0.5;
    static constexpr double lightest = 0.5, heaviest = 2.0;
    static constexpr double logLightest = -0.69314718055994531, logHeaviest = 0.69314718055994531; // ln 0.5, ln 2
    // The detectors average over this many periods.
    static constexpr double listenPeriods = 3.0;
    static constexpr std::size_t listenStep = 2;

    // Allocates the listening buffer for notes down to lowestF0. Not real-time safe.
    void prepare (double sampleRate, double lowestF0)
    {
        std::size_t size = 1;
        while (static_cast<double> (size) < sampleRate / lowestF0 + 4.0)
            size *= 2;
        history.assign (size, 0.0);
        mask = size - 1;
        reset();
    }

    void reset()
    {
        std::fill (history.begin(), history.end(), 0.0);
        position = 0;
        xx = xy = yy = slips = newSlips = 0.0;
        logWeight = 0.0;
        currentWeight = 1.0;
    }

    // The string's period in samples; at control rate is enough.
    void setPeriod (double samples)
    {
        periodSamples = samples;
        rate = static_cast<double> (listenStep) / (listenPeriods * samples);
        // One period back, read between two stored samples.
        delayWhole = static_cast<std::size_t> (samples);
        delayFrac = samples - static_cast<double> (delayWhole);
    }

    // Every sample while the bow is on the string: the bridge force, and
    // whether the string just let go of the bow. The averages are updated on
    // every listenStep-th sample only, which halves the cost; at the internal
    // rate (at least 176.4 kHz) that loses nothing audible.
    void listen (double bridgeForce, bool slipStarted)
    {
        history[position & mask] = bridgeForce;
        newSlips += slipStarted ? 1.0 : 0.0;
        if ((position++ % listenStep) != 0)
            return;

        const auto back = position - 1 - delayWhole; // wraps harmlessly through the mask
        const auto delayed = (1.0 - delayFrac) * history[back & mask] + delayFrac * history[(back - 1) & mask];
        xx += rate * (bridgeForce * bridgeForce - xx);
        xy += rate * (bridgeForce * delayed - xy);
        yy += rate * (delayed * delayed - yy);
        slips += rate * (newSlips * periodSamples / static_cast<double> (listenStep) - slips);
        newSlips = 0.0;
    }

    // The share of the signal that does not repeat after one period, with a
    // gain fitted so a swelling or fading note does not count (as the scratch
    // meter in tests/BowNoiseTests.cpp).
    double scratch() const
    {
        if (xx <= 0.0)
            return 0.0;
        const auto g = yy > 0.0 ? std::clamp (xy / yy, 0.5, 2.0) : 1.0;
        return std::max (xx - 2.0 * g * xy + g * g * yy, 0.0) / xx;
    }

    double slipsPerPeriod() const { return slips; }

    // While the bow plays the string: moves the weight over `seconds`.
    // skill 1 applies the whole correction, 0 none (the unassisted model).
    void adjust (double seconds, double skill, bool leanIntoDoubleSlip = false)
    {
        const auto noise = scratch();
        // Two or so slips per period with little scratch: a steady double slip
        // (a hollow tone, often with the octave above the note). Easing off
        // would lock it in, so the weight only drifts back to where it was.
        const bool doubleSlip = slips > doubleSlipLow && slips < doubleSlipHigh && noise < doubleSlipScratch;

        if (doubleSlip && leanIntoDoubleSlip)
            logWeight += correctionRate * seconds; // HG: a player presses to bring the fundamental in
        else if (noise > scratchThreshold && ! doubleSlip)
            logWeight -= correctionRate * seconds;
        else if (slips > multiSlipThreshold && noise <= scratchThreshold)
            logWeight += correctionRate * seconds;
        else
            logWeight *= std::max (0.0, 1.0 - seconds / returnSeconds);

        logWeight = std::clamp (logWeight, logLightest, logHeaviest);
        currentWeight = std::exp (std::clamp (skill, 0.0, 1.0) * logWeight);
    }

    // Factor on the bow force.
    double weight() const { return currentWeight; }

private:
    std::vector<double> history;
    std::size_t mask = 0, position = 0;
    double periodSamples = 100.0, rate = 0.0, delayFrac = 0.0, newSlips = 0.0;
    std::size_t delayWhole = 100;
    double xx = 0.0, xy = 0.0, yy = 0.0, slips = 0.0;
    double logWeight = 0.0, currentWeight = 1.0;
};
} // namespace violinsynth::dsp
