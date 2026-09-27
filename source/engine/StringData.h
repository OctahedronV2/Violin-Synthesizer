#pragma once

#include <array>
#include <cmath>

namespace violinsynth::engine
{
// The four violin strings. Impedances come from typical tensions and a
// 328 mm vibrating length (research/violin_model/strings.py). The force
// window is the range of bow force, as a fraction of Schelleng's F_max, that
// the Bow Pressure setting spans. It starts where Helmholtz motion begins on
// the open string at beta = 0.1 (docs/PHASE1_FINDINGS.md, section 2.6) and
// ends where the tone is still clean after the attack: above that the model
// scratches long before a real string would, most on the heavy G string and
// the thin E string (docs/BOW_NOISE.md).
struct StringSpec
{
    const char* name;
    int openMidiNote;
    double impedance; // kg/s
    double forceWindowLow;
    double forceWindowHigh;
};

inline constexpr std::array<StringSpec, 4> strings { {
    { "G", 55, 0.350, 0.24, 0.44 },
    { "D", 62, 0.234, 0.13, 0.55 },
    { "A", 69, 0.197, 0.09, 0.87 },
    { "E", 76, 0.180, 0.11, 0.36 },
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
