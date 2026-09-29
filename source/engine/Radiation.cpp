#include "engine/Radiation.h"

#include "engine/HurdyFix.h"

#include <algorithm>
#include <cmath>

namespace violinsynth::engine
{
namespace
{
bool isSilent (float x)
{
    return std::abs (x) <= Radiation::silenceThreshold;
}

// A fixed pseudo-random set, so every instance and every run sounds the same.
struct Draw
{
    unsigned state;
    double next()
    {
        state = state * 1664525u + 1013904223u;
        return static_cast<double> (state >> 8) / 16777216.0;
    }
};

// Peaks and dips spread evenly in log frequency with a little jitter,
// alternating in sign, each 60% to 140% of `depthDb`.
template <std::size_t N>
void setPeaks (std::array<Biquad, N>& filters,
               double fs,
               unsigned seed,
               double lowHz,
               double highHz,
               double depthDb,
               double q)
{
    Draw draw { seed };
    for (std::size_t k = 0; k < N; ++k)
    {
        const auto f
            = lowHz * std::pow (highHz / lowHz, (static_cast<double> (k) + draw.next()) / static_cast<double> (N));
        filters[k].setPeak (fs, f, (k % 2 != 0 ? depthDb : -depthDb) * (0.6 + 0.8 * draw.next()), q);
    }
}
} // namespace

void Radiation::prepare (double sampleRate, int maxBlockSize)
{
    fs = sampleRate;
    dormantAfter = std::max (1, static_cast<int> (dormantSeconds * fs));
    air.assign (static_cast<std::size_t> (std::max (1, maxBlockSize)), 0.0f);
    airFilters[0].setHighPass (fs, 11000.0, 0.54);
    airFilters[1].setHighPass (fs, 11000.0, 1.31);
    airFilters[2].setHighShelf (fs, 14000.0, -6.0);
    setPeaks (micPeaks, fs, 4242u, 800.0, 9000.0, micPeaksDb, micQ);
    hgEq[0].setPeak (fs, hg::param ("NASAL_HZ", 1000.0), hg::param ("NASAL_DB", -7.0), 0.8);
    hgEq[1].setPeak (fs, hg::param ("WARM_HZ", 420.0), hg::param ("WARM_DB", 4.0), 0.9);
    hgEq[2].setPeak (fs, hg::param ("BRILL_HZ", 3000.0), hg::param ("BRILL_DB", 2.0), 1.2);
    bodyPeaksDb = -1.0;
    setBodyPeaks (0.0);
    reset();
}

void Radiation::reset()
{
    for (auto& f : airFilters)
        f.reset();
    for (auto& f : bodyPeaks)
        f.reset();
    for (auto& f : micPeaks)
        f.reset();
    for (auto& f : hgEq)
        f.reset();
    quietRun = dormantAfter;
    dormant = true;
}

void Radiation::setBodyPeaks (double depthDb)
{
    if (depthDb == bodyPeaksDb)
        return;
    bodyPeaksDb = depthDb;
    setPeaks (bodyPeaks, fs, 777u, 250.0, 7000.0, depthDb, bodyQ);
}

void Radiation::processPreBody (const float* samples, int numSamples)
{
    airSilent = std::all_of (samples, samples + numSamples, isSilent);
    if (dormant && airSilent)
    {
        std::fill_n (air.begin(), numSamples, 0.0f);
        return;
    }
    for (int i = 0; i < numSamples; ++i)
        air[static_cast<std::size_t> (i)]
            = airFilters[2].process (airFilters[1].process (airFilters[0].process (samples[i])));
}

void Radiation::processPostBody (float* samples, int numSamples)
{
    const auto inputSilent = airSilent && std::all_of (samples, samples + numSamples, isSilent);
    if (dormant && inputSilent)
        return; // at rest: silence in, silence out
    dormant = false;

    const bool peaks = bodyPeaksDb != 0.0;
    for (int i = 0; i < numSamples; ++i)
    {
        auto x = samples[i] + airGain * air[static_cast<std::size_t> (i)];
        if (peaks)
            for (auto& f : bodyPeaks)
                x = f.process (x);
        for (auto& f : micPeaks)
            x = f.process (x);
        if (hg::on (hg::body))
            for (auto& f : hgEq)
                x = f.process (x);
        samples[i] = x;
    }

    quietRun = inputSilent ? quietRun + numSamples : 0;
    if (quietRun >= dormantAfter)
        reset();
}
} // namespace violinsynth::engine
