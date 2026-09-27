#include "engine/SympatheticStrings.h"

#include <algorithm>

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
    }
    reset();
}

void SympatheticStrings::reset()
{
    for (auto& r : resonators)
        r.reset();
    wasOpen.fill (false);
}

void SympatheticStrings::process (float* bridgeForce, int numSamples, const std::array<bool, 4>& isOpen, double amount)
{
    const auto coupling = std::clamp (amount, 0.0, 1.0) * maxCoupling;

    std::array<bool, 4> active {};
    std::array<double, 4> f0 {}, beta {};
    bool any = false;

    for (std::size_t s = 0; s < resonators.size(); ++s)
    {
        // A string that has just been fingered stops resonating as an open string.
        if (! isOpen[s] && wasOpen[s])
            resonators[s].reset();
        wasOpen[s] = isOpen[s];

        active[s] = isOpen[s] && coupling > 0.0;
        f0[s] = midiToHz (strings[s].openMidiNote);
        beta[s] = std::max (drivePoint, 1.2 * resonators[s].minBeta (f0[s]));
        any = any || active[s];
    }

    if (! any)
        return;

    for (int i = 0; i < numSamples; ++i)
    {
        // Every resonator is driven by the fingered strings only.
        const auto drive = coupling * static_cast<double> (bridgeForce[i]);
        double response = 0.0;
        for (std::size_t s = 0; s < resonators.size(); ++s)
            if (active[s])
                response += resonators[s].process (f0[s], beta[s], 0.0, 0.0, drive);
        bridgeForce[i] += static_cast<float> (response);
    }
}
} // namespace violinsynth::engine
