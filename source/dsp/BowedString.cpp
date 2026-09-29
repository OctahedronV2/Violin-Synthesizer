#include "dsp/BowedString.h"

#include <cstdlib>

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
    {
        // Violin bridge admittance (s/kg): A0, CBR, B1-, B1+, a few higher
        // plate modes, and the bridge hill (typical values from the literature).
        struct M { double f, q, y; };
        constexpr M table[bodyModes] { { 275, 15, 0.006 }, { 405, 25, 0.01 }, { 470, 35, 0.02 }, { 545, 35, 0.03 },
                                       { 660, 30, 0.015 }, { 820, 30, 0.012 }, { 1050, 30, 0.01 }, { 1350, 25, 0.012 },
                                       { 2500, 2.5, 0.04 }, { 3300, 4.0, 0.025 } };
        const char* env = std::getenv ("HG");
        bodyFeedback = env != nullptr && (std::atoi (env) & 512) != 0;
        const char* c = std::getenv ("COUPLE");
        const auto couple = c != nullptr ? std::atof (c) : 1.0;
        for (int k = 0; k < bodyModes; ++k)
        {
            const auto w = 2.0 * 3.141592653589793 * table[k].f / internalSampleRate;
            const auto alpha = std::sin (w) / (2.0 * table[k].q);
            const auto a0 = 1.0 + alpha;
            modes[static_cast<std::size_t> (k)].b0 = couple * table[k].y * alpha / a0;
            modes[static_cast<std::size_t> (k)].a1 = -2.0 * std::cos (w) / a0;
            modes[static_cast<std::size_t> (k)].a2 = (1.0 - alpha) / a0;
        }
    }

    fs = internalSampleRate;
    const auto maxDelay = fs / lowestF0 + 8.0;
    bridgeLine.prepare (maxDelay);
    nutLine.prepare (maxDelay);
    torsionBridgeLine.prepare (maxDelay);
    torsionNutLine.prepare (maxDelay);
    midAB.prepare (maxDelay);
    midBA.prepare (maxDelay);
    tauTable.assign (tauTableSize, -1.0);
    setLowestF0 (lowestF0);
    reset();
}

void BowedString::setLowestF0 (double f0)
{
    tauLogLowest = std::log2 (f0);
    std::fill (tauTable.begin(), tauTable.end(), -1.0);
    lastF0 = -1.0;
}

void BowedString::setPickup (const std::array<double, maxCoils>& coilMetres,
                             int count,
                             double openLength,
                             double openF0)
{
    numCoils = std::clamp (count, 0, maxCoils);
    for (int c = 0; c < numCoils; ++c)
        coils[static_cast<std::size_t> (c)] = coilMetres[static_cast<std::size_t> (c)] / openLength;
    pickupScale = 1.0 / openF0;
    lastPickup = 0.0;
}

void BowedString::reset()
{
    bridgeLine.reset();
    midAB.reset();
    midBA.reset();
    fingerState = 0.0;
    stickingB = false;
    for (auto& m : modes)
        m.x1 = m.x2 = m.y1 = m.y2 = 0.0;
    lastForce = 0.0;
    nutLine.reset();
    torsionBridgeLine.reset();
    torsionNutLine.reset();
    loopFilter.reset();
    lastF0 = -1.0;
    sticking = false;
    lastVelocity = 0.0;
    fingerVelocity = fingerHold = 0.0;
    lastPickup = 0.0;
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
    if (widthFraction > 0.0 && numCoils == 0)
        return process2 (f0, beta, vBow, force, excitation);

    const auto period = fs / f0;
    const auto maxDelay = bridgeLine.maxSupportedDelay();
    const auto dBridge = std::clamp (beta * period - tau, FractionalDelay::minDelay, maxDelay);
    const auto fingerDelay = fingerDelayFor (f0);
    const auto dNut = std::clamp ((1.0 - beta) * period - fingerDelay, FractionalDelay::minDelay, maxDelay);

    const auto yBridge = bridgeLine.read (dBridge);
    auto yNut = nutLine.read (dNut);
    if (fingerPole > 0.0 || fingerGain < 1.0)
    {
        fingerState = (1.0 - fingerPole) * yNut + fingerPole * fingerState;
        yNut = fingerGain * fingerState;
    }

    const auto reflected = loopFilter.process (yBridge);
    double bridgeVelocity = 0.0;
    if (bodyFeedback)
    {
        for (auto& m : modes)
        {
            const auto y = m.b0 * (lastForce - m.x2) - m.a1 * m.y1 - m.a2 * m.y2;
            m.x2 = m.x1;
            m.x1 = lastForce;
            m.y2 = m.y1;
            m.y1 = y;
            bridgeVelocity += y;
        }
    }
    const auto fromBridge = -reflected + 2.0 * bridgeVelocity;
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

    // The pickup: each coil hears the wave on its way from the bow to the end
    // of the string on its side and the wave reflected back from that end
    // (inverted), so its velocity is their difference. The loss filter's
    // small extra delay and damping are left out of the reflected wave.
    if (numCoils > 0)
    {
        double sum = 0.0;
        const auto half = 0.5 * period;
        const auto clampDelay = [maxDelay] (double d) { return std::clamp (d, 1.0, maxDelay); };
        for (int c = 0; c < numCoils; ++c)
        {
            const auto p = std::min (coils[static_cast<std::size_t> (c)] * f0 * pickupScale, 0.99);
            if (p < beta)
                sum += bridgeLine.readLinear (clampDelay ((beta - p) * half))
                    - bridgeLine.readLinear (clampDelay ((beta + p) * half - tau));
            else
                sum += nutLine.readLinear (clampDelay ((p - beta) * half))
                    - nutLine.readLinear (clampDelay ((2.0 - beta - p) * half));
        }
        lastPickup = sum / numCoils;
    }

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
    lastForce = params.friction.impedance * (yBridge - fromBridge);
    return lastForce;
}
double BowedString::process2 (double f0, double beta, double vBow, double force, double excitation)
{
    // A bow with width: contact A nearer the bridge, B nearer the nut, joined
    // by a short stretch of string. Each grips and slips on its own, so the
    // hairs across the ribbon can slip at different moments (differential
    // slipping), which rounds the Helmholtz corner and roughens it slightly.
    const auto period = fs / f0;
    const auto maxDelay = bridgeLine.maxSupportedDelay();
    const auto m = std::max (FractionalDelay::minDelay, 0.5 * widthFraction * period); // one way, A to B
    const auto betaA = std::max (beta - 0.5 * widthFraction, 0.5 * beta);
    const auto dBridge = std::clamp (betaA * period - tau, FractionalDelay::minDelay, maxDelay);
    const auto fingerDelay = fingerDelayFor (f0);
    const auto dNut = std::clamp (period - betaA * period - 2.0 * m - fingerDelay, FractionalDelay::minDelay, maxDelay);

    const auto yBridge = bridgeLine.read (dBridge);
    auto yNut = nutLine.read (dNut);
    if (fingerPole > 0.0 || fingerGain < 1.0)
    {
        fingerState = (1.0 - fingerPole) * yNut + fingerPole * fingerState;
        yNut = fingerGain * fingerState;
    }
    const auto reflected = loopFilter.process (yBridge);
    double bridgeVelocity = 0.0;
    if (bodyFeedback)
    {
        for (auto& md : modes)
        {
            const auto y = md.b0 * (lastForce - md.x2) - md.a1 * md.y1 - md.a2 * md.y2;
            md.x2 = md.x1;
            md.x1 = lastForce;
            md.y2 = md.y1;
            md.y1 = y;
            bridgeVelocity += y;
        }
    }
    const auto fromBridge = -reflected + 2.0 * bridgeVelocity;
    const auto fromNut = -yNut;
    const auto aMid = midBA.read (m); // arriving at A from B
    const auto bMid = midAB.read (m); // arriving at B from A

    // Hair near the bridge takes a little more of the weight.
    const auto shareA = 0.55;
    const auto& torsion = params.torsion;
    const bool twists = torsion.speedRatio > 0.0 && force > 0.0;
    double tFromBridge = 0.0, tFromNut = 0.0;
    auto frictionA = params.friction;
    if (twists)
    {
        const auto torsionPeriod = period / torsion.speedRatio;
        tFromBridge = torsionReflection
            * torsionBridgeLine.readLinear (std::clamp (beta * torsionPeriod, FractionalDelay::minDelay, maxDelay));
        tFromNut = torsionReflection
            * torsionNutLine.readLinear (std::clamp ((1.0 - beta) * torsionPeriod, FractionalDelay::minDelay, maxDelay));
        frictionA.impedance = contactImpedance;
    }
    const auto vtH = tFromBridge + tFromNut;

    // Contact A
    const auto vHA = fromBridge + aMid;
    auto ra = solveJunction (vBow, vHA + vtH, force * shareA, frictionA, sticking);
    if (fingerHold > 0.0)
        ra.velocity += fingerHold * (fingerVelocity - ra.velocity);
    sinceSlip += 1.0;
    slipOnset = sticking && ! ra.sticking;
    if (slipOnset)
    {
        lastSlipInterval = sinceSlip;
        sinceSlip = 0.0;
    }
    sticking = ra.sticking;
    const auto changeA = ra.velocity - vHA - vtH;
    const auto share = twists ? transverseShare : 1.0;
    const auto injA = changeA * share + excitation;
    if (twists)
    {
        const auto twist = changeA - changeA * share;
        torsionBridgeLine.write (tFromNut + twist);
        torsionNutLine.write (tFromBridge + twist);
    }

    // Contact B (no twist of its own: the torsion is modelled at A)
    const auto vHB = bMid + fromNut;
    const auto rb = solveJunction (vBow, vHB, force * (1.0 - shareA), params.friction, stickingB);
    stickingB = rb.sticking;
    const auto injB = rb.velocity - vHB;

    bridgeLine.write (aMid + injA);
    midAB.write (fromBridge + injA);
    nutLine.write (bMid + injB);
    midBA.write (fromNut + injB);

    lastVelocity = ra.velocity;
    lastForce = params.friction.impedance * (yBridge - fromBridge);
    return lastForce;
}

double BowedString::fingerDelayFor (double f0)
{
    if (fingerPole <= 0.0)
        return 0.0;
    if (fingerPole != cachedFingerPole || f0 != cachedFingerF0)
    {
        cachedFingerPole = fingerPole;
        cachedFingerF0 = f0;
        cachedFingerDelay = harmonicPhaseDelay (fingerPole, f0, fs);
    }
    return cachedFingerDelay;
}
} // namespace violinsynth::dsp
