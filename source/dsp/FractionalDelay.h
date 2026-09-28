#pragma once

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <vector>

namespace violinsynth::dsp
{
// Circular delay line read with 3rd-order Lagrange interpolation.
// Mirrors _lagrange_read in research/violin_model/waveguide.py: a value
// written at step n is returned by read(delay) at step n + delay.
// Delays must be at least minDelay samples.
class FractionalDelay
{
public:
    static constexpr double minDelay = 2.0;

    // Allocates for delays up to maxDelay samples. Not real-time safe.
    void prepare (double maxDelay)
    {
        std::size_t size = 1;
        while (static_cast<double> (size) < maxDelay + 8.0)
            size <<= 1;

        buffer.assign (size, 0.0);
        mask = size - 1;
        writePos = 0;
    }

    void reset()
    {
        std::fill (buffer.begin(), buffer.end(), 0.0);
        writePos = 0;
    }

    double read (double delay) const
    {
        assert (delay >= minDelay && delay + 4.0 < static_cast<double> (buffer.size()));

        const auto n0 = static_cast<std::size_t> (std::floor (delay)) - 1;
        const auto x = delay - static_cast<double> (n0); // in [1, 2)
        const auto h0 = -(x - 1.0) * (x - 2.0) * (x - 3.0) / 6.0;
        const auto h1 = x * (x - 2.0) * (x - 3.0) / 2.0;
        const auto h2 = -x * (x - 1.0) * (x - 3.0) / 2.0;
        const auto h3 = x * (x - 1.0) * (x - 2.0) / 6.0;

        // Age k lives at writePos - k.
        const auto s0 = buffer[(writePos - n0) & mask];
        const auto s1 = buffer[(writePos - n0 - 1) & mask];
        const auto s2 = buffer[(writePos - n0 - 2) & mask];
        const auto s3 = buffer[(writePos - n0 - 3) & mask];
        return h0 * s0 + h1 * s1 + h2 * s2 + h3 * s3;
    }

    // Linear interpolation: cheaper, for waves whose exact delay matters little.
    double readLinear (double delay) const
    {
        assert (delay >= 1.0 && delay + 2.0 < static_cast<double> (buffer.size()));

        const auto n = static_cast<std::size_t> (delay);
        const auto frac = delay - static_cast<double> (n);
        const auto s0 = buffer[(writePos - n) & mask];
        const auto s1 = buffer[(writePos - n - 1) & mask];
        return s0 + frac * (s1 - s0);
    }

    // Writes the next sample; call once per step, after the reads of that step.
    void write (double value)
    {
        buffer[writePos] = value;
        writePos = (writePos + 1) & mask;
    }

    double maxSupportedDelay() const { return static_cast<double> (buffer.size()) - 5.0; }

private:
    std::vector<double> buffer;
    std::size_t mask = 0;
    std::size_t writePos = 0;
};
} // namespace violinsynth::dsp
