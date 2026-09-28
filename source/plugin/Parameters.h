#pragma once

#include "engine/ViolinEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace violinsynth::params
{
// Parameter IDs are saved in host projects and presets. Never rename or reuse
// one; add a new ID instead.
namespace id
{
inline const juce::ParameterID bowPosition { "bowPosition", 1 };
inline const juce::ParameterID bowPressure { "bowPressure", 1 };
inline const juce::ParameterID attack { "attack", 1 };
inline const juce::ParameterID release { "release", 1 };
inline const juce::ParameterID vibratoRate { "vibratoRate", 1 };
inline const juce::ParameterID vibratoDepth { "vibratoDepth", 1 };
inline const juce::ParameterID vibratoDelay { "vibratoDelay", 1 };
inline const juce::ParameterID portamento { "portamento", 1 };
inline const juce::ParameterID bendRange { "bendRange", 1 };
inline const juce::ParameterID body { "body", 1 };
inline const juce::ParameterID bodyQuality { "bodyQuality", 1 };
inline const juce::ParameterID sordino { "sordino", 1 };
inline const juce::ParameterID width { "width", 1 };
inline const juce::ParameterID room { "room", 1 };
inline const juce::ParameterID outputGain { "outputGain", 1 };
inline const juce::ParameterID playMode { "playMode", 1 };
inline const juce::ParameterID resonance { "resonance", 1 };
inline const juce::ParameterID humanise { "humanise", 1 };
inline const juce::ParameterID autoBowChange { "autoBowChange", 1 };
inline const juce::ParameterID mpe { "mpe", 1 };
inline const juce::ParameterID mpeBendRange { "mpeBendRange", 1 };
inline const juce::ParameterID articulation { "articulation", 1 };
inline const juce::ParameterID velocityRange { "velocityRange", 2 };
inline const juce::ParameterID octave { "octave", 2 };
inline const juce::ParameterID imperfection { "imperfection", 3 };
} // namespace id

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

// Choices of the Octave parameter; index 2 plays notes where they are.
inline constexpr int octaveChoiceOffset = 2;

// Reads the current parameter values into engine settings (audio thread safe).
class Reader
{
public:
    explicit Reader (juce::AudioProcessorValueTreeState& state);
    engine::EngineSettings read() const;

private:
    std::atomic<float>* bowPosition;
    std::atomic<float>* bowPressure;
    std::atomic<float>* attack;
    std::atomic<float>* release;
    std::atomic<float>* vibratoRate;
    std::atomic<float>* vibratoDepth;
    std::atomic<float>* vibratoDelay;
    std::atomic<float>* portamento;
    std::atomic<float>* bendRange;
    std::atomic<float>* body;
    std::atomic<float>* bodyQuality;
    std::atomic<float>* sordino;
    std::atomic<float>* width;
    std::atomic<float>* room;
    std::atomic<float>* outputGain;
    std::atomic<float>* playMode;
    std::atomic<float>* resonance;
    std::atomic<float>* humanise;
    std::atomic<float>* autoBowChange;
    std::atomic<float>* mpe;
    std::atomic<float>* mpeBendRange;
    std::atomic<float>* articulation;
    std::atomic<float>* velocityRange;
    std::atomic<float>* octave;
    std::atomic<float>* imperfection;
};
} // namespace violinsynth::params
