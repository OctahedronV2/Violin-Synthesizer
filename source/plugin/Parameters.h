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
} // namespace id

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

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
};
} // namespace violinsynth::params
