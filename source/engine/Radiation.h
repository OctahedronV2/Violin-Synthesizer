#pragma once

#include "engine/Filters.h"
#include "engine/Realtime.h"

#include <array>
#include <vector>

namespace violinsynth::engine
{
// What the violin radiates beyond its measured bridge admittance
// (docs/REFERENCE_SOUND.md), around Body::process():
//
//   - air: the measured bodies stop at 10 kHz, but the bridge still passes the
//     string's own top octave. It is taken from the bridge force before the
//     body and added back after it, at the level of the Iowa recordings.
//   - body peaks: real violins radiate with 10-11 dB of harmonic-to-harmonic
//     spread; the bridge admittance alone gives 4.5 dB. 36 peaks and dips from
//     250 Hz to 7 kHz, whose depth is the instrument's (InstrumentSpec::bodyPeaksDb).
//   - microphone peaks: 40 narrower ones from 800 Hz to 9 kHz, the fine
//     structure of the sound heard in one direction. Under vibrato each
//     harmonic then rises and falls on its own, as recorded violins do.
class Radiation
{
public:
    static constexpr float airGain = 0.004f;
    static constexpr double micPeaksDb = 10.0, micQ = 40.0, bodyQ = 20.0;
    // Silent input for this long lets the filters rest (their tails are far
    // below it by then), so an idle instrument costs nothing here.
    static constexpr float silenceThreshold = 1.0e-10f;
    static constexpr double dormantSeconds = 1.0;

    // Message thread; allocates.
    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    // Audio thread. Recomputes the body peaks when the depth changes.
    void setBodyPeaks (double depthDb) VIOLINSYNTH_NONBLOCKING;

    // Before the body: keeps the bridge force's air band.
    void processPreBody (const float* samples, int numSamples) VIOLINSYNTH_NONBLOCKING;
    // After the body: adds the air back, then the peaks and dips.
    void processPostBody (float* samples, int numSamples) VIOLINSYNTH_NONBLOCKING;

private:
    double fs = 48000.0;
    double bodyPeaksDb = -1.0;
    std::array<Biquad, 3> airFilters;
    std::array<Biquad, 36> bodyPeaks;
    std::array<Biquad, 40> micPeaks;
    std::array<Biquad, 3> hgEq, hgDark; // HG: less nasal body
    std::vector<float> air;
    int quietRun = 0, dormantAfter = 1; // samples of silent input
    bool dormant = false, airSilent = true;
};
} // namespace violinsynth::engine
