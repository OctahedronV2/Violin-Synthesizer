#pragma once

#include <algorithm>
#include <cmath>
#include <span>
#include <vector>

namespace violinsynth::test
{
inline double cents (double f, double ref)
{
    return 1200.0 * std::log2 (f / ref);
}

// Fundamental by normalised autocorrelation near the expected period, refined
// with parabolic interpolation (as analysis.estimate_f0 in the research code).
// Needs at least ~50 samples per period for sub-cent accuracy.
inline double estimateF0 (std::span<const double> x, double fs, double fExpected, double search = 0.1)
{
    double mean = 0.0;
    for (auto v : x)
        mean += v;
    mean /= static_cast<double> (x.size());

    std::vector<double> y (x.size());
    for (std::size_t i = 0; i < x.size(); ++i)
        y[i] = x[i] - mean;

    const auto period = fs / fExpected;
    const auto lo = std::max (2, static_cast<int> (std::floor (period * (1.0 - search))));
    const auto hi = static_cast<int> (std::ceil (period * (1.0 + search))) + 1;
    const auto n = static_cast<int> (y.size()) - (hi + 2);

    auto correlation = [&] (int lag)
    {
        double num = 0.0, e0 = 0.0, e1 = 0.0;
        for (int i = 0; i < n; ++i)
        {
            num += y[static_cast<std::size_t> (i)] * y[static_cast<std::size_t> (i + lag)];
            e0 += y[static_cast<std::size_t> (i)] * y[static_cast<std::size_t> (i)];
            e1 += y[static_cast<std::size_t> (i + lag)] * y[static_cast<std::size_t> (i + lag)];
        }
        return num / std::sqrt (e0 * e1 + 1e-30);
    };

    std::vector<double> r;
    for (int lag = lo - 1; lag <= hi + 1; ++lag)
        r.push_back (correlation (lag));

    std::size_t best = 1;
    for (std::size_t k = 1; k + 1 < r.size(); ++k)
        if (r[k] > r[best])
            best = k;

    const auto denom = r[best - 1] - 2.0 * r[best] + r[best + 1];
    const auto offset = denom != 0.0 ? 0.5 * (r[best - 1] - r[best + 1]) / denom : 0.0;
    return fs / (static_cast<double> (lo - 1) + static_cast<double> (best) + offset);
}

inline double midiToHz (double note)
{
    return 440.0 * std::pow (2.0, (note - 69.0) / 12.0);
}
} // namespace violinsynth::test
