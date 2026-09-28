#pragma once

#include "engine/Body.h"
#include "engine/GuitarAmp.h"
#include "engine/OutputChain.h"
#include "engine/Realtime.h"
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
    double drive = 0.3; // bowed guitar: the amplifier's drive, 0..1
};

// The complete instrument: MIDI in, stereo audio out.
//
//   violin (four bowed strings at >= 176.4 kHz) -> decimate -> + open-string resonance
//   -> DC block, sordino
//   -> body -> stereo width, room, gain, limiter
//
// The bowed guitar (PerformanceSettings::instrument) is heard through a
// pickup and an amplifier instead of the body: its strings' pickup signal
// goes through the preamp at the internal rate, then decimate -> DC block,
// sordino -> cabinet -> width, room, gain, limiter. Its strings have no
// sympathetic resonance model; the bow's drones take that part.
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
    void process (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi) VIOLINSYNTH_NONBLOCKING;

    int getOversamplingFactor() const { return 1 << oversamplingOrder; }
    double getInternalSampleRate() const { return hostRate * getOversamplingFactor(); }
    int getLatencySamples() const;

    Violin& getViolin() { return violin; }
    // For tests: which stages are skipping silence.
    const Body& getBody() const { return body; }
    const GuitarAmp& getGuitarAmp() const { return amp; }
    const SympatheticStrings& getSympathetic() const { return sympathetic; }
    const OutputChain& getOutputChain() const { return output; }

    static int oversamplingOrderFor (double hostSampleRate);

private:
    void renderString (int start, int numSamples);
    // The electric guitar is heard through its amp, the others through a body.
    bool amplified() const { return settings.performance.instrument == Instrument::bowedGuitar; }
    bool acoustic() const { return settings.performance.instrument == Instrument::bowedAcousticGuitar; }

    double hostRate = 48000.0;
    int maxBlock = 512;
    int oversamplingOrder = 2;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    juce::dsp::AudioBlock<float> internalBlock; // the oversampler's internal-rate buffer

    Violin violin;
    SympatheticStrings sympathetic;
    Body body;
    GuitarAmp amp;
    OutputChain output;
    EngineSettings settings;
    float sordino = 0.0f; // smoothed: the Sordino setting or the con sordino articulation

    juce::AudioBuffer<float> mono;
};
} // namespace violinsynth::engine
