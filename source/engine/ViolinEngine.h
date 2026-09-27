#pragma once

#include "engine/Body.h"
#include "engine/OutputChain.h"
#include "engine/ViolinVoice.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <memory>

namespace violinsynth::engine
{
struct EngineSettings
{
    VoiceSettings voice;
    OutputSettings output;
    int body = 0;
    Body::Quality bodyQuality = Body::Quality::convolution;
};

// The complete instrument: MIDI in, stereo audio out.
//
//   voice (bowed string at >= 176.4 kHz) -> decimate -> DC block, sordino
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

    // Message thread: applies a body change that needs an impulse-response load.
    void updateConvolutionBody();

    // Audio thread. Renders into all channels of `buffer` (1 or 2), consuming `midi`.
    void process (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi);

    int getOversamplingFactor() const { return 1 << oversamplingOrder; }
    double getInternalSampleRate() const { return hostRate * getOversamplingFactor(); }
    int getLatencySamples() const;

    ViolinVoice& getVoice() { return voice; }

    static int oversamplingOrderFor (double hostSampleRate);

private:
    void renderString (int start, int numSamples);
    void handleMidi (const juce::MidiMessage& message);

    double hostRate = 48000.0;
    int maxBlock = 512;
    int oversamplingOrder = 2;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;

    ViolinVoice voice;
    Body body;
    OutputChain output;
    EngineSettings settings;
    std::atomic<int> requestedBody { 0 };

    juce::AudioBuffer<float> mono;
};
} // namespace violinsynth::engine
