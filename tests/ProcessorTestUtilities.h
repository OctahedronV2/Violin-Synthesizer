#pragma once

// Helpers for tests that drive the plugin through its processor, the way a host does.

#include "plugin/Parameters.h"
#include "plugin/PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <chrono>
#include <thread>
#include <vector>

namespace violinsynth::test
{
struct TimedMidi
{
    double time; // seconds from the start of the render
    juce::MidiMessage message;
};

inline void setParameter (ViolinSynthProcessor& p, const juce::ParameterID& id, float plainValue)
{
    auto* parameter = p.getParameters().getParameter (id.getParamID());
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (plainValue));
}

// No vibrato, humanising, bow noise, resonance, room or width: a steady, repeatable tone.
// The modal body renders the same every time; the convolution body loads its
// impulse response on a background thread.
inline void useSteadySettings (ViolinSynthProcessor& p, bool modalBody = true)
{
    setParameter (p, params::id::vibratoDepth, 0.0f);
    setParameter (p, params::id::humanise, 0.0f);
    setParameter (p, params::id::bowNoise, 0.0f);
    setParameter (p, params::id::resonance, 0.0f);
    setParameter (p, params::id::room, 0.0f);
    setParameter (p, params::id::width, 0.0f);
    setParameter (p, params::id::bodyQuality, modalBody ? 1.0f : 0.0f);
}

// Renders `seconds` of audio in blocks of `blockSize` and returns the left channel.
inline std::vector<float> renderProcessor (ViolinSynthProcessor& p,
                                           double sampleRate,
                                           int blockSize,
                                           double seconds,
                                           const std::vector<TimedMidi>& events = {})
{
    std::vector<float> out;
    const auto total = static_cast<int> (seconds * sampleRate);
    out.reserve (static_cast<std::size_t> (total));
    juce::AudioBuffer<float> buffer (p.getTotalNumOutputChannels(), blockSize);

    for (int start = 0; start < total; start += blockSize)
    {
        const auto length = std::min (blockSize, total - start);
        buffer.setSize (buffer.getNumChannels(), length, false, false, true);

        juce::MidiBuffer midi;
        for (const auto& e : events)
        {
            const auto position = static_cast<int> (e.time * sampleRate);
            if (position >= start && position < start + length)
                midi.addEvent (e.message, position - start);
        }

        p.processBlock (buffer, midi);
        for (int i = 0; i < length; ++i)
            out.push_back (buffer.getSample (0, i));
    }
    return out;
}

// Gives the convolution body time to load its impulse response on JUCE's
// background thread and crossfade to it.
inline void waitForBody (ViolinSynthProcessor& p, double sampleRate, int blockSize)
{
    std::this_thread::sleep_for (std::chrono::milliseconds (300));
    renderProcessor (p, sampleRate, blockSize, 0.5);
}
} // namespace violinsynth::test
