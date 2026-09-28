#pragma once

// Helpers for tests that play MIDI into the whole engine.

#include "engine/ViolinEngine.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>
#include <numbers>
#include <vector>

namespace violinsynth::test
{
inline constexpr double fs = 48000.0;
inline constexpr int block = 256;
inline constexpr int G = 0, D = 1, A = 2, E = 3;

inline engine::EngineSettings plainSettings()
{
    engine::EngineSettings s;
    s.performance.voice.vibratoDepthCents = 0.0;
    s.performance.voice.humanise = 0.0;
    s.performance.voice.resonance = 0.0;
    s.output.room = 0.0f;
    s.output.width = 0.0f;
    s.performance.velocityTop = 1.0; // the model tests are calibrated on linear velocity
    s.bodyQuality = engine::Body::Quality::modal;
    return s;
}

struct Event
{
    double time;
    juce::MidiMessage message;
};

// Renders the mono (left) output; `probe` is called after each block.
template <typename Probe>
std::vector<double> run (engine::ViolinEngine& e, double seconds, std::vector<Event> events, Probe&& probe)
{
    std::vector<double> out;
    juce::AudioBuffer<float> buffer (2, block);
    const auto total = static_cast<int> (seconds * fs);
    for (int start = 0; start < total; start += block)
    {
        juce::MidiBuffer midi;
        for (const auto& ev : events)
        {
            const auto pos = static_cast<int> (ev.time * fs);
            if (pos >= start && pos < start + block)
                midi.addEvent (ev.message, pos - start);
        }
        e.process (buffer, midi);
        for (int i = 0; i < block; ++i)
            out.push_back (buffer.getSample (0, i));
        probe (static_cast<double> (start + block) / fs);
    }
    return out;
}

inline std::vector<double> run (engine::ViolinEngine& e, double seconds, std::vector<Event> events)
{
    return run (e, seconds, std::move (events), [] (double) {});
}

// Amplitude of the component at `freq` (Goertzel over a Hann window).
inline double toneLevel (const std::vector<double>& x, double from, double to, double freq)
{
    const auto a = static_cast<std::size_t> (from * fs);
    const auto b = static_cast<std::size_t> (to * fs);
    const auto n = static_cast<double> (b - a);
    const auto w = 2.0 * std::numbers::pi * freq / fs;
    double re = 0.0, im = 0.0;
    for (auto i = a; i < b; ++i)
    {
        const auto k = static_cast<double> (i - a);
        const auto hann = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * k / n);
        re += x[i] * hann * std::cos (w * k);
        im -= x[i] * hann * std::sin (w * k);
    }
    return 2.0 * std::sqrt (re * re + im * im) / (0.5 * n);
}

inline double rms (const std::vector<double>& x, double from, double to)
{
    double sum = 0.0;
    for (auto i = static_cast<std::size_t> (from * fs); i < static_cast<std::size_t> (to * fs); ++i)
        sum += x[i] * x[i];
    return std::sqrt (sum / ((to - from) * fs));
}

inline juce::MidiMessage on (int note, int channel = 1, float velocity = 0.8f)
{
    return juce::MidiMessage::noteOn (channel, note, velocity);
}
inline juce::MidiMessage off (int note, int channel = 1)
{
    return juce::MidiMessage::noteOff (channel, note);
}
} // namespace violinsynth::test
