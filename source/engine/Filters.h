#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace violinsynth::engine
{
// Small real-time-safe filters: coefficient updates never allocate.

// Transposed direct form II biquad with RBJ cookbook designs.
class Biquad
{
public:
    void reset() { z1 = z2 = 0.0f; }

    float process (float x)
    {
        const auto y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    // RBJ constant-0-dB-peak band-pass (the body resonator used by the research code).
    void setBandPass (double fs, double freq, double q)
    {
        const auto w0 = 2.0 * std::numbers::pi * freq / fs;
        const auto alpha = std::sin (w0) / (2.0 * q);
        set (alpha, 0.0, -alpha, 1.0 + alpha, -2.0 * std::cos (w0), 1.0 - alpha);
    }

    void setHighShelf (double fs, double freq, double gainDb, double q = 0.707)
    {
        const auto a = std::pow (10.0, gainDb / 40.0);
        const auto w0 = 2.0 * std::numbers::pi * freq / fs;
        const auto cw = std::cos (w0);
        const auto alpha = std::sin (w0) / (2.0 * q);
        const auto sq = 2.0 * std::sqrt (a) * alpha;
        set (a * ((a + 1) + (a - 1) * cw + sq),
             -2 * a * ((a - 1) + (a + 1) * cw),
             a * ((a + 1) + (a - 1) * cw - sq),
             (a + 1) - (a - 1) * cw + sq,
             2 * ((a - 1) - (a + 1) * cw),
             (a + 1) - (a - 1) * cw - sq);
    }

    void setLowPass (double fs, double freq, double q = 0.707)
    {
        const auto w0 = 2.0 * std::numbers::pi * std::min (freq, 0.45 * fs) / fs;
        const auto cw = std::cos (w0);
        const auto alpha = std::sin (w0) / (2.0 * q);
        set ((1 - cw) / 2, 1 - cw, (1 - cw) / 2, 1 + alpha, -2 * cw, 1 - alpha);
    }

    void setHighPass (double fs, double freq, double q = 0.707)
    {
        const auto w0 = 2.0 * std::numbers::pi * freq / fs;
        const auto cw = std::cos (w0);
        const auto alpha = std::sin (w0) / (2.0 * q);
        set ((1 + cw) / 2, -(1 + cw), (1 + cw) / 2, 1 + alpha, -2 * cw, 1 - alpha);
    }

    void setPeak (double fs, double freq, double gainDb, double q)
    {
        const auto a = std::pow (10.0, gainDb / 40.0);
        const auto w0 = 2.0 * std::numbers::pi * std::min (freq, 0.45 * fs) / fs;
        const auto cw = std::cos (w0);
        const auto alpha = std::sin (w0) / (2.0 * q);
        set (1 + alpha * a, -2 * cw, 1 - alpha * a, 1 + alpha / a, -2 * cw, 1 - alpha / a);
    }

private:
    void set (double nb0, double nb1, double nb2, double na0, double na1, double na2)
    {
        b0 = static_cast<float> (nb0 / na0);
        b1 = static_cast<float> (nb1 / na0);
        b2 = static_cast<float> (nb2 / na0);
        a1 = static_cast<float> (na1 / na0);
        a2 = static_cast<float> (na2 / na0);
    }

    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;
};

// First-order DC blocker.
class DcBlocker
{
public:
    void prepare (double fs, double cutoffHz = 15.0)
    {
        r = static_cast<float> (std::exp (-2.0 * std::numbers::pi * cutoffHz / fs));
        reset();
    }

    void reset() { x1 = y1 = 0.0f; }

    float process (float x)
    {
        const auto y = x - x1 + r * y1;
        x1 = x;
        y1 = y;
        return y;
    }

private:
    float r = 0.999f, x1 = 0.0f, y1 = 0.0f;
};

// Schroeder all-pass on a delay line; used for stereo decorrelation.
class AllPass
{
public:
    void prepare (int delaySamples, float gain)
    {
        buffer.assign (static_cast<std::size_t> (std::max (1, delaySamples)), 0.0f);
        g = gain;
        pos = 0;
    }

    void reset() { std::fill (buffer.begin(), buffer.end(), 0.0f); }

    float process (float x)
    {
        const auto delayed = buffer[pos];
        const auto v = x + g * delayed;
        buffer[pos] = v;
        pos = (pos + 1) % buffer.size();
        return delayed - g * v;
    }

private:
    std::vector<float> buffer;
    std::size_t pos = 0;
    float g = 0.5f;
};
} // namespace violinsynth::engine
