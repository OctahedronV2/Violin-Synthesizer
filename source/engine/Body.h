#pragma once

#include "engine/Filters.h"

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>

namespace violinsynth::engine
{
// Violin body: bridge force -> radiated sound.
//
// Four measured bodies (docs/BODY_MODELLING.md), each available as
//   - convolution with the measured impulse response (default, most detailed), or
//   - a fitted modal bank of RBJ band-pass resonators (light CPU option).
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

    // Message thread: allocates, and loads the impulse response of `bodyIndex`.
    void prepare (double sampleRate, int maxBlockSize, int bodyIndex);
    void reset();

    // Message thread: starts loading another impulse response (crossfaded in by JUCE).
    void loadConvolutionBody (int bodyIndex);
    int convolutionBody() const { return loadedConvolutionBody; }

    // Audio thread.
    void setModalBody (int bodyIndex);
    void setQuality (Quality q) { quality = q; }
    void process (float* samples, int numSamples);

    int getLatencySamples() const;

private:
    static constexpr int maxModes = 64;

    juce::dsp::Convolution convolution;
    double fs = 48000.0;
    int maxBlock = 512;
    int loadedConvolutionBody = -1;

    Quality quality = Quality::convolution;
    int modalBody = -1;
    std::array<Biquad, maxModes> resonators;
    std::array<float, maxModes> modeGains {};
    int numModes = 0;
    float directGain = 0.0f;
    float modalOutputGain = 1.0f;
    float convolutionTrim = 1.0f;
    float modalTrim = 1.0f;
};
} // namespace violinsynth::engine
