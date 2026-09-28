#pragma once

#include "engine/Filters.h"
#include "engine/Violin.h"

namespace violinsynth::engine
{
// What the bowed guitar is heard through: its magnetic pickup's own
// resonance, a valve-style amplifier and a speaker cabinet
// (docs/BOWED_GUITAR.md). It takes the place of the violin's body.
//
//   string velocity under the coils (internal rate)
//   -> pickup resonance (coil inductance against the cable's capacitance)
//   -> preamp: gain set by Drive, asymmetric soft clipping
//   -> decimate (in ViolinEngine) -> cabinet: speaker resonance, voicing, roll-off
//
// The clipping runs at the internal rate, before the decimator, so its
// harmonics do not fold back into the audible band.
class GuitarAmp
{
public:
    // The level a moderately bowed note has at the preamp's input.
    static constexpr double nominalLevel = 0.25;

    void prepare (double internalSampleRate, double hostSampleRate);
    void reset();

    // Audio thread: drive 0..1 (0 is a clean amp), and which pickup is heard.
    void setSettings (double drive, Pickup pickup);

    void processPreamp (float* samples, int numSamples); // internal rate
    void processCabinet (float* samples, int numSamples); // host rate

    bool isDormant() const { return dormant; } // for tests

private:
    static constexpr float silenceThreshold = 1.0e-9f;

    double internalRate = 192000.0, hostRate = 48000.0;
    Biquad coil;
    float highPassCoeff = 0.0f, highPassState = 0.0f, lastInput = 0.0f;
    float preGain = 1.0f, makeup = 1.0f;
    double appliedDrive = -1.0;
    Pickup appliedPickup = Pickup::neck;
    bool pickupSet = false;

    Biquad cabinetHighPass, cabinetLow, cabinetMid, cabinetPresence, cabinetLowPass1, cabinetLowPass2;
    bool dormant = true;
    int quietRun = 0, dormantAfter = 1;
};
} // namespace violinsynth::engine
