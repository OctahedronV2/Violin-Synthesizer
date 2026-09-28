#pragma once

#include "engine/Body.h"
#include "engine/OutputChain.h"
#include "engine/SympatheticStrings.h"
#include "engine/Violin.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <memory>

namespace violinsynth::engine
{
struct EngineSettings
{
    PerformanceSettings performance;
    OutputSettings output;
    int body = 0;
    Body::Quality bodyQuality = Body::Quality::convolution;
};

// The complete instrument: MIDI in, stereo audio out.
//
//   violin (four bowed strings at >= 176.4 kHz) -> decimate -> + open-string resonance
//   -> DC block, sordino
//   -> body -> stereo width, room, gain, limiter
//
// The string runs at the host rate times a power of two chosen so the
// internal rate is at least 176.4 kHz; below that, stick/slip timing snaps
// to the sample grid and detunes the top octave (docs/PHASE1_FINDINGS.md).
class ViolinEngine
{
public:
    static constexpr double minInternalRate = 176400.0;

    // Message thread; allocates.
    void prepare (double hostSampleRate, int maxBlockSize);
    void reset();

    // Audio thread, once per block before process().
    void setSettings (const EngineSettings& s);

    // Nothing to do: every body is prepared in prepare(), so body changes
    // need nothing from the message thread. The processor's timer still
    // calls this; the timer can go once no caller needs it.
    void updateConvolutionBody() { }

    // Audio thread. Renders into all channels of `buffer` (1 or 2), consuming `midi`.
    void process (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi);

    int getOversamplingFactor() const { return 1 << oversamplingOrder; }
    double getInternalSampleRate() const { return hostRate * getOversamplingFactor(); }
    int getLatencySamples() const;

    Violin& getViolin() { return violin; }
    // For tests: which stages are skipping silence.
    const Body& getBody() const { return body; }
    const SympatheticStrings& getSympathetic() const { return sympathetic; }
    const OutputChain& getOutputChain() const { return output; }

    static int oversamplingOrderFor (double hostSampleRate);

private:
    void renderString (int start, int numSamples);

    double hostRate = 48000.0;
    int maxBlock = 512;
    int oversamplingOrder = 2;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    juce::dsp::AudioBlock<float> internalBlock; // the oversampler's internal-rate buffer

    Violin violin;
    SympatheticStrings sympathetic;
    Body body;
    OutputChain output;
    EngineSettings settings;
    float sordino = 0.0f; // smoothed: the Sordino setting or the con sordino articulation

    juce::AudioBuffer<float> mono;
};
} // namespace violinsynth::engine
