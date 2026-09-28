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

// Phase delay table: 5 cents per entry, six octaves above the lowest note.
constexpr double tauStepsPerOctave = 240.0;
constexpr int tauTableSize = 6 * 240 + 2;
} // namespace

void BowedString::prepare (double internalSampleRate, double lowestF0)
{
    fs = internalSampleRate;
    const auto maxDelay = fs / lowestF0 + 8.0;
    bridgeLine.prepare (maxDelay);
    nutLine.prepare (maxDelay);
    torsionBridgeLine.prepare (maxDelay);
    torsionNutLine.prepare (maxDelay);
    tauLogLowest = std::log2 (lowestF0);
    tauTable.assign (tauTableSize, -1.0);
    reset();
}

void BowedString::reset()
{
    bridgeLine.reset();
    nutLine.reset();
    torsionBridgeLine.reset();
    torsionNutLine.reset();
    loopFilter.reset();
    lastF0 = -1.0;
    sticking = false;
    lastVelocity = 0.0;
    fingerVelocity = fingerHold = 0.0;
    slipOnset = false;
    sinceSlip = lastSlipInterval = 0.0;
}

void BowedString::setParams (const StringParams& newParams)
{
    params = newParams;
    // Each end reflects the twist inverted, losing this much: the round trip
    // decays as a mode of quality factor q.
    torsionReflection = -std::exp (-std::numbers::pi / (2.0 * params.torsion.q));
    // The bow sees the transverse and torsional impedances in series.
    const auto ratio = params.torsion.impedanceRatio;
    contactImpedance = params.friction.impedance * ratio / (1.0 + ratio);
    transverseShare = ratio / (1.0 + ratio);
    lastF0 = -1.0; // force a coefficient update
    std::fill (tauTable.begin(), tauTable.end(), -1.0);
}

void BowedString::updateCoefficients (double f0)
{
    const auto coeffs = lossCoefficients (f0, fs, params.loss);
    loopFilter.setCoefficients (coeffs);
    tau = params.tuning == Tuning::harmonic ? tabulatedPhaseDelay (f0, coeffs.a)
                                            : onePolePhaseDelay (coeffs.a, 2.0 * std::numbers::pi * f0 / fs);
    lastF0 = f0;
}

double BowedString::tabulatedPhaseDelay (double f0, double a)
{
    const auto x = (std::log2 (f0) - tauLogLowest) * tauStepsPerOctave;
    if (! (x >= 0.0 && x < tauTableSize - 1))
        return harmonicPhaseDelay (a, f0, fs);

    const auto i = static_cast<int> (x);
    for (auto k : { i, i + 1 })
    {
        auto& entry = tauTable[static_cast<std::size_t> (k)];
        if (entry < 0.0)
        {
            const auto f = std::exp2 (tauLogLowest + k / tauStepsPerOctave);
            entry = harmonicPhaseDelay (lossCoefficients (f, fs, params.loss).a, f, fs);
        }
    }
    const auto frac = x - i;
    const auto t0 = tauTable[static_cast<std::size_t> (i)];
    return t0 + frac * (tauTable[static_cast<std::size_t> (i) + 1] - t0);
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

    const auto& torsion = params.torsion;
    // Without the bow on the string the twist does not reach the bend.
    const bool twists = torsion.speedRatio > 0.0 && force > 0.0;
    double tFromBridge = 0.0, tFromNut = 0.0;
    auto friction = params.friction;
    if (twists)
    {
        const auto torsionPeriod = period / torsion.speedRatio;
        tFromBridge = torsionReflection
            * torsionBridgeLine.readLinear (std::clamp (beta * torsionPeriod, FractionalDelay::minDelay, maxDelay));
        tFromNut = torsionReflection
            * torsionNutLine.readLinear (
                std::clamp ((1.0 - beta) * torsionPeriod, FractionalDelay::minDelay, maxDelay));
        friction.impedance = contactImpedance;
    }
    const auto vtH = tFromBridge + tFromNut;

    auto result = solveJunction (vBow, vH + vtH, force, friction, sticking);
    if (fingerHold > 0.0)
        result.velocity += fingerHold * (fingerVelocity - result.velocity);
    sinceSlip += 1.0;
    slipOnset = sticking && ! result.sticking;
    if (slipOnset)
    {
        lastSlipInterval = sinceSlip;
        sinceSlip = 0.0;
    }
    sticking = result.sticking;
    // The contact velocity changes by (result - vH - vtH); the transverse part
    // takes the share its lower impedance gives it.
    const auto contactChange = result.velocity - vH - vtH;
    const auto share = twists ? transverseShare : 1.0;
    const auto injected = contactChange * share + excitation;
    if (twists)
    {
        const auto twist = contactChange - contactChange * share;
        torsionBridgeLine.write (tFromNut + twist);
        torsionNutLine.write (tFromBridge + twist);
    }

    bridgeLine.write (fromNut + injected);
    nutLine.write (fromBridge + injected);

    lastVelocity = twists ? vH + injected : result.velocity + excitation;
    return params.friction.impedance * (yBridge + reflected);
}
} // namespace violinsynth::dsp
