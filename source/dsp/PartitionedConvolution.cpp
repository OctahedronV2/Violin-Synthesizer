#include "dsp/PartitionedConvolution.h"

#include <pffft.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace violinsynth::dsp
{
AlignedFloats::~AlignedFloats()
{
    pffft_aligned_free (ptr);
}

AlignedFloats::AlignedFloats (AlignedFloats&& other) noexcept
    : ptr (std::exchange (other.ptr, nullptr)),
      count (std::exchange (other.count, 0))
{
}

AlignedFloats& AlignedFloats::operator= (AlignedFloats&& other) noexcept
{
    if (this != &other)
    {
        pffft_aligned_free (ptr);
        ptr = std::exchange (other.ptr, nullptr);
        count = std::exchange (other.count, 0);
    }
    return *this;
}

void AlignedFloats::resize (std::size_t newSize)
{
    if (newSize != count)
    {
        pffft_aligned_free (ptr);
        ptr = newSize > 0 ? static_cast<float*> (pffft_aligned_malloc (newSize * sizeof (float))) : nullptr;
        count = ptr != nullptr ? newSize : 0;
    }
    clear();
}

void AlignedFloats::clear()
{
    if (ptr != nullptr)
        std::memset (ptr, 0, count * sizeof (float));
}

PartitionedConvolution::~PartitionedConvolution()
{
    if (setup != nullptr)
        pffft_destroy_setup (setup);
}

void PartitionedConvolution::prepare (int blockSize,
                                      const std::vector<std::vector<float>>& impulseResponses,
                                      int initialFilter,
                                      int crossfadeSamples)
{
    if (setup != nullptr && B != blockSize)
    {
        pffft_destroy_setup (setup);
        setup = nullptr;
    }

    B = blockSize;
    N = 2 * B;
    if (setup == nullptr)
        setup = pffft_new_setup (N, PFFFT_REAL);

    numFilters = std::max (1, static_cast<int> (impulseResponses.size()));
    std::size_t longest = 1;
    for (const auto& ir : impulseResponses)
        longest = std::max (longest, ir.size());
    P = static_cast<int> ((longest + static_cast<std::size_t> (B) - 1) / static_cast<std::size_t> (B));

    const auto n = static_cast<std::size_t> (N);
    filters.resize (static_cast<std::size_t> (numFilters * P) * n);
    segments.resize (static_cast<std::size_t> (P + 1) * n);
    input.resize (n);
    work.resize (n);
    scratch.resize (n);
    for (auto& slot : slots)
    {
        slot.pre.resize (n);
        slot.overlap.resize (static_cast<std::size_t> (B));
        slot.out.resize (n);
    }

    // Partition spectra, with PFFFT's 1/N inverse scaling folded in.
    const auto scale = 1.0f / static_cast<float> (N);
    for (std::size_t f = 0; f < impulseResponses.size(); ++f)
    {
        const auto& ir = impulseResponses[f];
        for (int k = 0; k < P; ++k)
        {
            scratch.clear();
            const auto first = static_cast<std::size_t> (k * B);
            for (std::size_t i = first; i < std::min (ir.size(), first + static_cast<std::size_t> (B)); ++i)
                scratch.data()[i - first] = scale * ir[i];
            auto* spectrum = filters.data() + (f * static_cast<std::size_t> (P) + static_cast<std::size_t> (k)) * n;
            pffft_transform (setup, scratch.data(), spectrum, work.data(), PFFFT_FORWARD);
        }
    }

    fadeLength = std::max (1, crossfadeSamples);
    dormantAfter = static_cast<long long> (P + 1) * B;
    pendingFilter = std::clamp (initialFilter, 0, numFilters - 1);
    reset();
}

void PartitionedConvolution::reset()
{
    clearState();
    active = 0;
    fadePos = -1;
    for (auto& slot : slots)
        slot.filter = pendingFilter;
    dormant = true;
    silentRun = dormantAfter;
}

void PartitionedConvolution::clearState()
{
    segments.clear();
    input.clear();
    for (auto& slot : slots)
    {
        slot.pre.clear();
        slot.overlap.clear();
        slot.out.clear();
    }
    current = 0;
    inputPos = 0;
}

void PartitionedConvolution::selectFilter (int index)
{
    pendingFilter = std::clamp (index, 0, numFilters - 1);
}

const float* PartitionedConvolution::partition (int filter, int k) const
{
    return filters.data() + static_cast<std::size_t> (filter * P + k) * static_cast<std::size_t> (N);
}

float* PartitionedConvolution::segment (int blocksAgo)
{
    return segments.data() + static_cast<std::size_t> ((current + blocksAgo) % (P + 1)) * static_cast<std::size_t> (N);
}

void PartitionedConvolution::beginCrossfade (int filter)
{
    // The incoming filter needs the tail of the previous block's output:
    // the full product of the previous block and its history with this filter.
    auto& slot = slots[1 - active];
    slot.filter = filter;
    scratch.clear();
    for (int k = 0; k < P; ++k)
        pffft_zconvolve_accumulate (setup, segment (k + 1), partition (filter, k), scratch.data(), 1.0f);
    pffft_transform (setup, scratch.data(), slot.out.data(), work.data(), PFFFT_BACKWARD);
    std::memcpy (slot.overlap.data(), slot.out.data() + B, static_cast<std::size_t> (B) * sizeof (float));
    fadePos = 0;
}

void PartitionedConvolution::startBlock()
{
    if (fadePos < 0 && pendingFilter != slots[active].filter)
        beginCrossfade (pendingFilter);

    for (int s = 0; s < 2; ++s)
    {
        if (s != active && fadePos < 0)
            continue;
        auto& slot = slots[s];
        slot.pre.clear();
        for (int k = 1; k < P; ++k)
            pffft_zconvolve_accumulate (setup, segment (k), partition (slot.filter, k), slot.pre.data(), 1.0f);
    }
}

void PartitionedConvolution::convolveCurrent (Slot& slot)
{
    std::memcpy (scratch.data(), slot.pre.data(), static_cast<std::size_t> (N) * sizeof (float));
    pffft_zconvolve_accumulate (setup, segment (0), partition (slot.filter, 0), scratch.data(), 1.0f);
    pffft_transform (setup, scratch.data(), slot.out.data(), work.data(), PFFFT_BACKWARD);
}

void PartitionedConvolution::process (float* samples, int numSamples)
{
    if (setup == nullptr || numSamples <= 0)
        return;

    int lastSound = -1;
    for (int i = numSamples; --i >= 0;)
    {
        if (std::abs (samples[i]) > silenceThreshold)
        {
            lastSound = i;
            break;
        }
    }
    silentRun = lastSound < 0 ? silentRun + numSamples : numSamples - 1 - lastSound;

    if (dormant)
    {
        // All state is zero, so a filter change needs no crossfade.
        slots[active].filter = pendingFilter;
        if (lastSound < 0)
        {
            std::fill (samples, samples + numSamples, 0.0f);
            return;
        }
        dormant = false;
    }

    for (int done = 0; done < numSamples;)
    {
        if (inputPos == 0)
            startBlock();

        const auto n = std::min (numSamples - done, B - inputPos);
        float* x = samples + done;
        std::memcpy (input.data() + inputPos, x, static_cast<std::size_t> (n) * sizeof (float));
        pffft_transform (setup, input.data(), segment (0), work.data(), PFFFT_FORWARD);

        auto& a = slots[active];
        convolveCurrent (a);
        const float* aOut = a.out.data() + inputPos;
        const float* aOverlap = a.overlap.data() + inputPos;

        if (fadePos < 0)
        {
            for (int i = 0; i < n; ++i)
                x[i] = aOut[i] + aOverlap[i];
        }
        else
        {
            auto& b = slots[1 - active];
            convolveCurrent (b);
            const float* bOut = b.out.data() + inputPos;
            const float* bOverlap = b.overlap.data() + inputPos;
            const auto step = 1.0f / static_cast<float> (fadeLength);
            for (int i = 0; i < n; ++i)
            {
                const auto g = std::min (1.0f, static_cast<float> (fadePos + i + 1) * step);
                x[i] = (1.0f - g) * (aOut[i] + aOverlap[i]) + g * (bOut[i] + bOverlap[i]);
            }
            fadePos += n;
        }

        inputPos += n;
        done += n;

        if (inputPos == B)
        {
            for (int s = 0; s < 2; ++s)
                if (s == active || fadePos >= 0)
                    std::memcpy (slots[s].overlap.data(),
                                 slots[s].out.data() + B,
                                 static_cast<std::size_t> (B) * sizeof (float));

            std::memset (input.data(), 0, static_cast<std::size_t> (B) * sizeof (float));
            current = (current + P) % (P + 1); // the oldest spectrum becomes the new block's
            inputPos = 0;
        }

        if (fadePos >= fadeLength)
        {
            active = 1 - active;
            fadePos = -1;
        }
    }

    // Everything the last sound can reach has been played: stop until the next one.
    if (silentRun >= dormantAfter && fadePos < 0)
    {
        clearState();
        dormant = true;
    }
}
} // namespace violinsynth::dsp
