#pragma once

#include "dsp/PartitionedConvolution.h"
#include "engine/Filters.h"

#include <array>
#include <vector>

namespace violinsynth::engine
{
// Violin body: bridge force -> radiated sound.
//
// Four measured bodies (docs/BODY_MODELLING.md), each available as
//   - convolution with the measured impulse response (default, most detailed), or
//   - a fitted modal bank of RBJ band-pass resonators (light CPU option).
// The convolution is zero-latency and partitioned, on PFFFT (docs/PHASE7.md
// 7.3). Both forms stop computing once their input and ringing have died
// away, and start again on the next sample of sound.
// Sources: CNSM Dataset (Pauget Ballesteros 2026, CC BY 4.0) and the
// University of Iowa MIS violin; see research/data/SOURCES.md.
class Body
{
public:
    static constexpr int numBodies = 4;
    static constexpr std::array<const char*, numBodies> names { "Levaggi", "Klimke", "Stoppani", "Tambovsky (Iowa)" };
    static constexpr std::array<const char*, numBodies> credits {
        "CNSM Dataset, Pauget Ballesteros (2026), CC BY 4.0",
        "CNSM Dataset, Pauget Ballesteros (2026), CC BY 4.0",
        "CNSM Dataset, Pauget Ballesteros (2026), CC BY 4.0",
        "University of Iowa Musical Instrument Samples",
    };

    // Level calibration: an mf A4 (velocity 0.8) plays at about -21 dBFS RMS
    // on every body, leaving headroom for loud four-string chords. Measured
    // with ViolinSynthTests "[.diagnostics]" (single string, before Phase 4 resonance).
    static constexpr float targetRmsDb = -21.0f;
    static constexpr std::array<float, numBodies> measuredConvolutionRmsDb { -10.8f, -5.8f, -7.0f, -12.4f };
    static constexpr std::array<float, numBodies> measuredModalRmsDb { -10.5f, -6.4f, -7.9f, -11.4f };

    enum class Quality
    {
        convolution,
        modal,
    };

    static constexpr double crossfadeSeconds = 0.05; // between convolution bodies

    // Message thread: allocates, and prepares every body's impulse response
    // at `sampleRate`, so body changes never allocate.
    void prepare (double sampleRate, int bodyIndex);
    void reset();

    // Audio thread. A convolution body change crossfades.
    void setBody (int bodyIndex);
    void setQuality (Quality q);
    void process (float* samples, int numSamples);

    int getLatencySamples() const { return 0; }
    int convolutionBody() const { return convolution.selectedFilter(); }
    bool isDormant() const; // for tests: the active form is skipping silence

    // The impulse response of `bodyIndex`, resampled to `sampleRate` and
    // level-trimmed, as the convolution uses it.
    static std::vector<float> impulseResponse (int bodyIndex, double sampleRate);
    // Partition length: about 2.7 ms at any rate.
    static int convolutionBlockSize (double sampleRate);

private:
    static constexpr int maxModes = 64;

    void setModalBody (int bodyIndex);
    void processModal (float* samples, int numSamples);

    dsp::PartitionedConvolution convolution;
    double fs = 48000.0;

    Quality quality = Quality::convolution;
    int modalBody = -1;
    std::array<Biquad, maxModes> resonators;
    std::array<float, maxModes> modeGains {};
    int numModes = 0;
    float directGain = 0.0f;
    float modalOutputGain = 1.0f;
    float modalTrim = 1.0f;
    bool modalDormant = true;
    int modalQuietRun = 0, modalDormantAfter = 1; // samples of silent input and output
};
} // namespace violinsynth::engine
