#include "engine/SympatheticStrings.h"

#include <algorithm>
#include <cstdlib>

namespace violinsynth::engine
{
namespace
{
constexpr double drivePoint = 0.08; // where the bridge motion enters (fraction from the bridge)
} // namespace

void SympatheticStrings::prepare (double sampleRate)
{
    fs = sampleRate;
    for (std::size_t s = 0; s < resonators.size(); ++s)
    {
        resonators[s].prepare (fs, midiToHz (strings[s].openMidiNote));
        auto params = resonators[s].getParams();
        params.friction.impedance = strings[s].impedance;
        params.tuning = dsp::Tuning::fundamental; // free vibration: each partial at its own pitch
        resonators[s].setParams (params);

        f0[s] = midiToHz (strings[s].openMidiNote);
        beta[s] = std::max (drivePoint, 1.2 * resonators[s].minBeta (f0[s]));
    }

    const auto lowestF0 = *std::min_element (f0.begin(), f0.end());
    dormantAfter = std::max (1, static_cast<int> (2.0 * fs / lowestF0));
    reset();
}

void SympatheticStrings::reset()
{
    for (auto& r : resonators)
        r.reset();
    wasOpen.fill (false);
    dormant = true;
    quietRun = dormantAfter;
}

void SympatheticStrings::process (float* bridgeForce, int numSamples, const std::array<bool, 4>& isOpen, double amount)
{
    static const double scratchScale = std::getenv ("SYMPC") ? std::atof (std::getenv ("SYMPC")) : 1.0; // SCRATCH
    const auto coupling = std::clamp (amount, 0.0, 1.0) * maxCoupling * scratchScale;

    std::array<bool, 4> active {};
    bool any = false;

    for (std::size_t s = 0; s < resonators.size(); ++s)
    {
        // A string that has just been fingered stops resonating as an open string.
        if (! isOpen[s] && wasOpen[s])
            resonators[s].reset();
        wasOpen[s] = isOpen[s];

        active[s] = isOpen[s] && coupling > 0.0;
        any = any || active[s];
    }

    if (! any)
        return;

    int start = 0;
    if (dormant)
    {
        // The resonators are at rest: nothing to add until the drive returns.
        while (start < numSamples && std::abs (bridgeForce[start]) <= silenceThreshold)
            ++start;
        if (start == numSamples)
            return;
        dormant = false;
        quietRun = 0;
    }

    for (int i = start; i < numSamples; ++i)
    {
        // Every resonator is driven by the fingered strings only.
        const auto in = bridgeForce[i];
        const auto drive = coupling * static_cast<double> (in);
        double response = 0.0;
        for (std::size_t s = 0; s < resonators.size(); ++s)
            if (active[s])
                response += resonators[s].process (f0[s], beta[s], 0.0, 0.0, drive);
        bridgeForce[i] += static_cast<float> (response);

        const auto quiet
            = std::abs (in) <= silenceThreshold && std::abs (response) <= static_cast<double> (silenceThreshold);
        quietRun = quiet ? quietRun + 1 : 0;
    }

    if (quietRun >= dormantAfter)
    {
        for (auto& r : resonators)
            r.reset();
        dormant = true;
    }
}
} // namespace violinsynth::engine
