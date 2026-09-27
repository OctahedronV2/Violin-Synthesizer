#include "dsp/BowedString.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace violinsynth::dsp
{
namespace
{
// Coefficients are recomputed when f0 moves by more than this fraction
// (0.17 cents). Recomputing every sample during vibrato would cost 40 atan2
// calls per sample for no audible benefit; for a steady pitch the output is
// identical to the reference implementation.
constexpr double f0Tolerance = 1.0e-4;
} // namespace

void BowedString::prepare (double internalSampleRate, double lowestF0)
{
    fs = internalSampleRate;
    const auto maxDelay = fs / lowestF0 + 8.0;
    bridgeLine.prepare (maxDelay);
    nutLine.prepare (maxDelay);
    reset();
}

void BowedString::reset()
{
    bridgeLine.reset();
    nutLine.reset();
    loopFilter.reset();
    lastF0 = -1.0;
    sticking = false;
    lastVelocity = 0.0;
}

void BowedString::setParams (const StringParams& newParams)
{
    params = newParams;
    lastF0 = -1.0; // force a coefficient update
}

void BowedString::updateCoefficients (double f0)
{
    const auto coeffs = lossCoefficients (f0, fs, params.loss);
    loopFilter.setCoefficients (coeffs);
    tau = params.tuning == Tuning::harmonic ? harmonicPhaseDelay (coeffs.a, f0, fs)
                                            : onePolePhaseDelay (coeffs.a, 2.0 * std::numbers::pi * f0 / fs);
    lastF0 = f0;
}

double BowedString::minBeta (double f0) const
{
    const auto coeffs = lossCoefficients (f0, fs, params.loss);
    const auto t = harmonicPhaseDelay (coeffs.a, f0, fs);
    return (FractionalDelay::minDelay + t) / (fs / f0);
}

double BowedString::process (double f0, double beta, double vBow, double force, double excitation)
{
    if (lastF0 < 0.0 || std::abs (f0 - lastF0) > f0Tolerance * lastF0)
        updateCoefficients (f0);

    const auto period = fs / f0;
    const auto maxDelay = bridgeLine.maxSupportedDelay();
    const auto dBridge = std::clamp (beta * period - tau, FractionalDelay::minDelay, maxDelay);
    const auto dNut = std::clamp ((1.0 - beta) * period, FractionalDelay::minDelay, maxDelay);

    const auto yBridge = bridgeLine.read (dBridge);
    const auto yNut = nutLine.read (dNut);

    const auto reflected = loopFilter.process (yBridge);
    const auto fromBridge = -reflected;
    const auto fromNut = -yNut;
    const auto vH = fromBridge + fromNut;

    const auto result = solveJunction (vBow, vH, force, params.friction, sticking);
    sticking = result.sticking;
    const auto injected = (result.velocity - vH) + excitation;

    bridgeLine.write (fromNut + injected);
    nutLine.write (fromBridge + injected);

    lastVelocity = result.velocity + excitation;
    return params.friction.impedance * (yBridge + reflected);
}
} // namespace violinsynth::dsp
