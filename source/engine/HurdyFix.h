#pragma once

#include <cstdlib>

// Scratch switches for the "less hurdy-gurdy" experiments (env HG bitmask).
namespace violinsynth::engine::hg
{
inline int mask()
{
    static const int m = [] {
        const char* s = std::getenv ("HG");
        return s != nullptr ? std::atoi (s) : 0;
    }();
    return m;
}
inline bool on (int bit) { return (mask() & bit) != 0; }
inline double param (const char* name, double fallback)
{
    const char* s = std::getenv (name);
    return s != nullptr ? std::atof (s) : fallback;
}
constexpr int vibrato = 1, bowArm = 2, transitions = 4, cleanEnds = 8, body = 16, living = 32, proBowing = 64, somber = 128, longing = 256;
constexpr int playerDynamics = 1024, wideBow = 2048, softFinger = 4096, rollingFinger = 8192;
} // namespace violinsynth::engine::hg
