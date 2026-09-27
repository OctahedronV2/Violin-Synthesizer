#include "plugin/Parameters.h"

namespace violinsynth::params
{
juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterFloat> (id::outputGain,
                                                             "Output Gain",
                                                             juce::NormalisableRange<float> { -60.0f, 12.0f, 0.1f },
                                                             0.0f,
                                                             juce::AudioParameterFloatAttributes {}.withLabel ("dB")));

    return layout;
}
} // namespace violinsynth::params
