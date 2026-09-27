#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

namespace violinsynth
{
class ViolinSynthProcessor;

class ViolinSynthEditor final : public juce::AudioProcessorEditor
{
public:
    explicit ViolinSynthEditor (ViolinSynthProcessor&);
    ~ViolinSynthEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Label gainLabel;
    juce::Slider gainSlider;
    juce::AudioProcessorValueTreeState::SliderAttachment gainAttachment;
    juce::MidiKeyboardComponent keyboard;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ViolinSynthEditor)
};
} // namespace violinsynth
