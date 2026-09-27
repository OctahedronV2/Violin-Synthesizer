#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <memory>
#include <vector>

namespace violinsynth
{
class ViolinSynthProcessor;

class ViolinSynthEditor final : public juce::AudioProcessorEditor
{
public:
    explicit ViolinSynthEditor (ViolinSynthProcessor&);
    ~ViolinSynthEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Label label;
        juce::Slider slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    struct Choice
    {
        juce::Label label;
        juce::ComboBox box;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
    };

    struct Section
    {
        juce::String title;
        std::vector<Knob*> knobs;
        std::vector<Choice*> choices;
        juce::Rectangle<int> bounds;
    };

    Knob& addKnob (const juce::ParameterID& id, const juce::String& text);
    Choice& addChoice (const juce::ParameterID& id, const juce::String& text);

    ViolinSynthProcessor& processor;
    juce::LookAndFeel_V4 lookAndFeel;
    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::unique_ptr<Choice>> choices;
    std::vector<Section> sections;
    juce::Label credits;
    juce::MidiKeyboardComponent keyboard;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ViolinSynthEditor)
};
} // namespace violinsynth
