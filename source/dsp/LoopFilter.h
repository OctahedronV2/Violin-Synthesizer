#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>

namespace violinsynth::dsp
{
// One-pole loss filter of the waveguide loop, H(z) = g (1 - a) / (1 - a z^-1),
// set from physical decay times. Mirrors loss_filter_coefficients,
// one_pole_phase_delay and harmonic_phase_delay in
// research/violin_model/waveguide.py.
struct LossSpec
{
    double t60 = 1.5; // s, decay at low frequencies
    double t60High = 0.25; // s, decay at fHigh
    double fHigh = 4000.0; // Hz
};

struct LossCoefficients
{
    double g = 1.0; // DC gain per round trip
    double a = 0.0; // pole
};

inline LossCoefficients lossCoefficients (double f0, double fs, const LossSpec& spec)
{
    const auto g = std::pow (10.0, -3.0 / (f0 * spec.t60));

    if (spec.t60High >= spec.t60)
        return { g, 0.0 };

    const auto r = std::pow (10.0, -3.0 / (f0 * spec.t60High)) / g;
    const auto c = std::cos (2.0 * std::numbers::pi * std::min (spec.fHigh, 0.45 * fs) / fs);
    // |(1 - a) / (1 - a e^{-jw})| = r  <=>  (r^2 - 1) a^2 + (2 - 2 r^2 c) a + (r^2 - 1) = 0
    const auto qa = r * r - 1.0;
    const auto qb = 2.0 - 2.0 * r * r * c;
    const auto disc = qb * qb - 4.0 * qa * qa;

    if (disc < 0.0)
        return { g, 0.0 };

    auto a = (-qb + std::sqrt (disc)) / (2.0 * qa);

    if (a < 0.0 || a >= 1.0)
        a = (-qb - std::sqrt (disc)) / (2.0 * qa);

    return { g, std::min (std::max (a, 0.0), 0.99) };
}

// Phase delay in samples of (1 - a) / (1 - a z^-1) at omega (rad/sample).
inline double onePolePhaseDelay (double a, double omega)
{
    if (omega <= 0.0)
        return a / (1.0 - a);

    return std::atan2 (a * std::sin (omega), 1.0 - a * std::cos (omega)) / omega;
}

// Phase delay averaged over the harmonics below 0.45 fs with 1/n weights.
// Bowed (sawtooth) motion takes its period from all harmonics together.
inline double harmonicPhaseDelay (double a, double f0, double fs, int maxHarmonics = 40)
{
    const auto nMax = std::min (maxHarmonics, std::max (1, static_cast<int> (0.45 * fs / f0)));
    double num = 0.0;
    double den = 0.0;

    for (int n = 1; n <= nMax; ++n)
    {
        const auto w = 1.0 / n;
        num += w * onePolePhaseDelay (a, 2.0 * std::numbers::pi * n * f0 / fs);
        den += w;
    }

    return num / den;
}

class LoopFilter
{
public:
    void reset() { state = 0.0; }

    void setCoefficients (LossCoefficients c) { coeffs = c; }

    double process (double x)
    {
        state = coeffs.a * state + (1.0 - coeffs.a) * coeffs.g * x;
        return state;
    }

    const LossCoefficients& coefficients() const { return coeffs; }

private:
    LossCoefficients coeffs;
    double state = 0.0;
};
} // namespace violinsynth::dsp
