#pragma once

#include <cstddef>
#include <vector>

struct PFFFT_Setup;

namespace violinsynth::dsp
{
// Float buffer aligned for PFFFT's SIMD transforms.
class AlignedFloats
{
public:
    AlignedFloats() = default;
    explicit AlignedFloats (std::size_t size) { resize (size); }
    ~AlignedFloats();
    AlignedFloats (AlignedFloats&& other) noexcept;
    AlignedFloats& operator= (AlignedFloats&& other) noexcept;
    AlignedFloats (const AlignedFloats&) = delete;
    AlignedFloats& operator= (const AlignedFloats&) = delete;

    void resize (std::size_t newSize); // allocates and zeroes
    void clear(); // zeroes
    float* data() { return ptr; }
    const float* data() const { return ptr; }
    std::size_t size() const { return count; }

private:
    float* ptr = nullptr;
    std::size_t count = 0;
};

// Zero-latency uniformly partitioned convolution (overlap-add in the
// frequency domain) on PFFFT, with several impulse responses held at once.
//
// The input is cut into blocks of B samples and each block's spectrum is kept
// in a frequency-domain delay line. An impulse response of length L becomes
// P = ceil(L / B) partitions. For every host call, the partial current block
// is transformed and multiplied with the first partition; the older blocks'
// products with the later partitions are summed once per block. So the output
// has no latency whatever the host block size, and small host blocks cost one
// extra FFT pair each rather than more partitions.
//
// Switching to another impulse response crossfades over `crossfadeSamples`,
// starting on the next block boundary; both filters share the input history,
// so the new one starts with its full tail.
//
// After `input` has been silent (|x| <= silenceThreshold) for longer than the
// impulse response, the convolver clears its state and stops computing until
// sound arrives again.
class PartitionedConvolution
{
public:
    static constexpr float silenceThreshold = 1.0e-10f; // about -200 dBFS

    PartitionedConvolution() = default;
    ~PartitionedConvolution();
    PartitionedConvolution (const PartitionedConvolution&) = delete;
    PartitionedConvolution& operator= (const PartitionedConvolution&) = delete;

    // Message thread; allocates. `impulseResponses` are all used at this rate.
    void prepare (int blockSize,
                  const std::vector<std::vector<float>>& impulseResponses,
                  int initialFilter,
                  int crossfadeSamples);
    void reset();

    // Audio thread. A change takes effect with a crossfade.
    void selectFilter (int index);
    int selectedFilter() const { return pendingFilter; }

    // Audio thread: convolves `samples` in place.
    void process (float* samples, int numSamples);

    int blockSize() const { return B; }
    int numPartitions() const { return P; }
    bool isDormant() const { return dormant; }

private:
    struct Slot
    {
        int filter = 0;
        AlignedFloats pre; // older blocks' products, summed once per block
        AlignedFloats overlap; // second half of the last full block's output
        AlignedFloats out; // time-domain result of the current partial block
    };

    const float* partition (int filter, int k) const;
    float* segment (int blocksAgo);
    void startBlock();
    void beginCrossfade (int filter);
    void convolveCurrent (Slot& slot);
    void clearState();

    PFFFT_Setup* setup = nullptr;
    int B = 0, N = 0, P = 0, numFilters = 0;
    AlignedFloats filters; // numFilters * P spectra of N floats, scaled by 1/N
    AlignedFloats segments; // (P + 1) input spectra, a ring buffer
    AlignedFloats input; // current block, zero-padded to N
    AlignedFloats work, scratch;
    int current = 0; // ring index of the current block
    int inputPos = 0;

    Slot slots[2];
    int active = 0;
    int pendingFilter = 0;
    int fadeLength = 1, fadePos = -1; // fadePos >= 0 while crossfading into the other slot

    bool dormant = true;
    long long silentRun = 0, dormantAfter = 0;
};
} // namespace violinsynth::dsp
