#pragma once

#include <array>
#include <cmath>

namespace violinsynth::engine
{
// The four violin strings. Impedances come from typical tensions and a
// 328 mm vibrating length (research/violin_model/strings.py). The force
// window is the range of bow force, as a fraction of Schelleng's F_max, that
// gave Helmholtz motion on the open string at beta = 0.1
// (docs/PHASE1_FINDINGS.md, section 2.6).
struct StringSpec
{
    const char* name;
    int openMidiNote;
    double impedance; // kg/s
    double forceWindowLow;
    double forceWindowHigh;
};

inline constexpr std::array<StringSpec, 4> strings { {
    { "G", 55, 0.350, 0.24, 0.74 },
    { "D", 62, 0.234, 0.13, 0.87 },
    { "A", 69, 0.197, 0.09, 0.87 },
    { "E", 76, 0.180, 0.11, 0.87 },
} };

// Highest string whose open pitch is at or below the note (the usual choice).
inline const StringSpec& stringForNote (double midiNote)
{
    for (auto it = strings.rbegin(); it != strings.rend(); ++it)
        if (static_cast<double> (it->openMidiNote) <= midiNote + 1.0e-9)
            return *it;

    return strings.front();
}

inline double midiToHz (double note)
{
    return 440.0 * std::pow (2.0, (note - 69.0) / 12.0);
}
} // namespace violinsynth::engine
