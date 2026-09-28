#pragma once

#include "dsp/BowedString.h"
#include "engine/StringData.h"

#include <array>

namespace violinsynth::engine
{
// Sympathetic resonance of the undamped open strings.
//
// An open string with no bow on it is a linear resonator, so it runs at the
// host rate (no friction, no oversampling needed): the same waveguide as the
// bowed string with zero bow force, driven through the bridge by the force of
// the fingered strings. The coupling is one-way: the resonators never drive
// the fingered strings or each other, so energy cannot circulate and grow
// between strings tuned in fifths.
class SympatheticStrings
{
public:
    static constexpr int numStrings = static_cast<int> (strings.size());
    static constexpr double maxCoupling = 0.2; // m/s of drive per N of bridge force (host rate)

    void prepare (double sampleRate);
    void reset();

    // Adds the open strings' response to `bridgeForce` in place. `isOpen[s]`
    // says which strings are currently undamped; amount is 0..1.
    //
    // Once the drive and the strings' ringing have both been silent for two
    // round trips of the lowest string, the resonators are cleared and skipped
    // until the drive returns.
    void process (float* bridgeForce, int numSamples, const std::array<bool, 4>& isOpen, double amount);

    bool isDormant() const { return dormant; } // for tests

private:
    static constexpr float silenceThreshold = 1.0e-10f; // about -200 dBFS

    std::array<dsp::BowedString, 4> resonators;
    std::array<bool, 4> wasOpen {};
    std::array<double, 4> f0 {}, beta {}; // fixed per string, set in prepare()
    double fs = 48000.0;
    bool dormant = true;
    int quietRun = 0, dormantAfter = 1; // samples of silent drive and response
};
} // namespace violinsynth::engine
