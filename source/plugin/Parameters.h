#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace violinsynth::params
{
// Parameter IDs are saved in host projects and presets. Never rename or reuse
// one; add a new ID instead.
namespace id
{
inline const juce::ParameterID outputGain { "outputGain", 1 };
} // namespace id

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
} // namespace violinsynth::params
