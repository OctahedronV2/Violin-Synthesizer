#pragma once

#include "../core/Engine.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace octavio2::params
{
// Parameter IDs are saved in host projects. Never rename or reuse one; add a new ID instead.
namespace id
{
inline const juce::ParameterID mode { "mode", 1 };
inline const juce::ParameterID octave { "octave", 1 };
inline const juce::ParameterID velocityCurve { "velocityCurve", 1 };
inline const juce::ParameterID vibrato { "vibrato", 1 };
inline const juce::ParameterID brightness { "brightness", 1 };
inline const juce::ParameterID room { "room", 1 };
inline const juce::ParameterID reverb { "reverb", 1 };
inline const juce::ParameterID volume { "volume", 1 };
inline const juce::ParameterID dynamics { "dynamics", 1 };
} // namespace id

// The Room choices: "None" (the two microphones only), then the halls in the order the engine
// loads them (Processor::loadRadiationData).
const juce::StringArray& roomNames();

// Choices of the Octave parameter; index 2 plays notes where they are.
inline constexpr int octaveChoiceOffset = 2;
// +1, as in Octavio 1: typing keyboards (FL Studio, Ableton) start an octave below the violin.
inline constexpr int defaultOctaveShift = 1;

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

// Reads the parameters into engine settings (audio thread safe).
class Reader
{
public:
    explicit Reader (juce::AudioProcessorValueTreeState&);
    o2::EngineSettings read() const;
    int octaveShift() const;
    bool studio() const { return mode->load() >= 0.5f; }

private:
    std::atomic<float>*mode, *octave, *velocityCurve, *vibrato, *brightness, *room, *reverb, *volume, *dynamics;
};
} // namespace octavio2::params
