#pragma once

#include "dsp/FractionalDelay.h"
#include "dsp/FrictionJunction.h"
#include "dsp/LoopFilter.h"

namespace violinsynth::dsp
{
enum class Tuning
{
    harmonic, // bowed (sawtooth) motion
    fundamental, // free vibration, e.g. pizzicato
};

struct StringParams
{
    FrictionParams friction;
    LossSpec loss;
    Tuning tuning = Tuning::harmonic;
};

// Digital-waveguide bowed string, run at the internal (oversampled) rate.
// Mirrors _simulate in research/violin_model/waveguide.py; see there and
// docs/PHASE1_FINDINGS.md for the model.
//
//   bridge <-- line (round trip beta*P) -- BOW -- line (round trip (1-beta)*P) --> nut
//   -H(z)                              junction                                    -1
class BowedString
{
public:
    // Allocates the delay lines for notes down to lowestF0. Not real-time safe.
    void prepare (double internalSampleRate, double lowestF0);
    void reset();

    void setParams (const StringParams& newParams);
    const StringParams& getParams() const { return params; }

    // One sample. f0 in Hz, beta = bow distance from the bridge as a fraction
    // of the string length, vBow in m/s, force in N, excitation = velocity
    // injected at the bow point (m/s). Returns the transverse bridge force (N).
    double process (double f0, double beta, double vBow, double force, double excitation = 0.0);

    double stringVelocity() const { return lastVelocity; }
    bool isSticking() const { return sticking; }

    // Smallest beta the delay lines support at f0 (at least 2 samples bow-to-bridge).
    double minBeta (double f0) const;

    double sampleRate() const { return fs; }

private:
    void updateCoefficients (double f0);

    StringParams params;
    double fs = 192000.0;
    FractionalDelay bridgeLine;
    FractionalDelay nutLine;
    LoopFilter loopFilter;

    double lastF0 = -1.0;
    double tau = 0.0;
    bool sticking = false;
    double lastVelocity = 0.0;
};
} // namespace violinsynth::dsp
