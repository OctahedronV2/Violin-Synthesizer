#include "engine/GuitarAmp.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace violinsynth::engine
{
namespace
{
// Pickup velocity (m/s) to preamp input: a moderately bowed note arrives at
// about nominalLevel.
constexpr double pickupGain = 0.25;
// The cabinet's output level: a moderately bowed note plays at about the
// violin's level (Body::targetRmsDb).
constexpr float outputTrim = 0.035f;
// Drive 1 adds this much preamp gain.
constexpr double maxDriveDb = 40.0;
// The clipping leans to one side, as a single valve stage does: even harmonics.
constexpr float bias = 0.25f;
// Before the clipping, the lows are thinned more as the drive rises, so the
// low strings stay tight instead of turning to mush.
constexpr double highPassClean = 20.0, highPassDriven = 180.0; // Hz

// Pickup resonance: frequency (Hz) and Q with a typical guitar cable. The
// two humbuckers in parallel load each other less, so they sound brighter.
struct Resonance
{
    double hz, q;
};
constexpr Resonance neckCoil { 3200.0, 2.2 }, bothCoils { 4300.0, 1.4 }, bridgeCoil { 2800.0, 2.6 };

float shape (float x)
{
    return std::tanh (x + bias) - std::tanh (bias);
}
} // namespace

void GuitarAmp::prepare (double internalSampleRate, double hostSampleRate)
{
    internalRate = internalSampleRate;
    hostRate = hostSampleRate;

    // A closed-back 4x12 cabinet: the speakers' low resonance, a scooped
    // low-mid, a presence peak and the cone's steep roll-off above 5 kHz.
    cabinetHighPass.setHighPass (hostRate, 75.0, 0.8);
    cabinetLow.setPeak (hostRate, 110.0, 3.0, 1.2);
    cabinetMid.setPeak (hostRate, 450.0, -3.0, 0.8);
    cabinetPresence.setPeak (hostRate, 2000.0, 3.5, 1.0);
    cabinetLowPass1.setLowPass (hostRate, 5000.0, 0.9);
    cabinetLowPass2.setLowPass (hostRate, 7500.0, 0.6);
    dormantAfter = std::max (1, static_cast<int> (0.5 * hostRate));

    appliedDrive = -1.0;
    pickupSet = false;
    setSettings (0.0, Pickup::neck);
    reset();
}

void GuitarAmp::reset()
{
    coil.reset();
    highPassState = lastInput = 0.0f;
    for (auto* f : { &cabinetHighPass, &cabinetLow, &cabinetMid, &cabinetPresence, &cabinetLowPass1, &cabinetLowPass2 })
        f->reset();
    dormant = true;
    quietRun = dormantAfter;
}

void GuitarAmp::setSettings (double drive, Pickup pickup)
{
    if (! pickupSet || pickup != appliedPickup)
    {
        const auto r = pickup == Pickup::neck ? neckCoil : pickup == Pickup::both ? bothCoils : bridgeCoil;
        coil.setLowPass (internalRate, r.hz, r.q);
        appliedPickup = pickup;
        pickupSet = true;
    }

    drive = std::clamp (drive, 0.0, 1.0);
    if (drive == appliedDrive)
        return;
    appliedDrive = drive;
    const auto gain = std::pow (10.0, maxDriveDb * drive / 20.0);
    preGain = static_cast<float> (gain * pickupGain);
    // Keep a moderately bowed note at about the same level whatever the drive.
    makeup = static_cast<float> (nominalLevel
                                 / std::abs (static_cast<double> (shape (static_cast<float> (gain * nominalLevel)))));
    const auto cutoff = highPassClean + (highPassDriven - highPassClean) * drive;
    highPassCoeff = static_cast<float> (std::exp (-2.0 * std::numbers::pi * cutoff / internalRate));
}

void GuitarAmp::processPreamp (float* samples, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        const auto x = coil.process (samples[i]);
        highPassState = highPassCoeff * (highPassState + x - lastInput);
        lastInput = x;
        samples[i] = makeup * shape (preGain * highPassState);
    }
}

void GuitarAmp::processCabinet (float* samples, int numSamples)
{
    bool silentInput = true;
    for (int i = 0; i < numSamples && silentInput; ++i)
        silentInput = std::abs (samples[i]) < silenceThreshold;
    if (silentInput && dormant)
        return; // already zero in, zero out
    if (silentInput)
    {
        quietRun += numSamples;
        if (quietRun > dormantAfter)
        {
            reset();
            std::fill (samples, samples + numSamples, 0.0f);
            return;
        }
    }
    else
    {
        quietRun = 0;
    }
    dormant = false;

    for (int i = 0; i < numSamples; ++i)
    {
        auto x = samples[i];
        for (auto* f :
             { &cabinetHighPass, &cabinetLow, &cabinetMid, &cabinetPresence, &cabinetLowPass1, &cabinetLowPass2 })
            x = f->process (x);
        samples[i] = outputTrim * x;
    }
}
} // namespace violinsynth::engine
