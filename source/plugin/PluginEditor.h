#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <memory>
#include <vector>

namespace violinsynth
{
class ViolinSynthProcessor;

class ViolinSynthEditor final : public juce::AudioProcessorEditor, private juce::Timer
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

    struct Toggle
    {
        juce::ToggleButton button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
    };

    struct Section
    {
        juce::String title;
        int row = 0;
        std::vector<Knob*> knobs;
        std::vector<Choice*> choices;
        std::vector<Toggle*> toggles;
        std::vector<juce::Component*> extras; // placed below the choices and toggles
        juce::Rectangle<int> bounds;

        int units() const { return static_cast<int> (knobs.size()) + (choices.empty() && toggles.empty() ? 0 : 2); }
    };

    Knob& addKnob (const juce::ParameterID& id, const juce::String& text);
    Choice& addChoice (const juce::ParameterID& id, const juce::String& text);
    Toggle& addToggle (const juce::ParameterID& id, const juce::String& text);
    void timerCallback() override;

    ViolinSynthProcessor& processor;
    juce::LookAndFeel_V4 lookAndFeel;
    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::unique_ptr<Choice>> choices;
    std::vector<std::unique_ptr<Toggle>> toggles;
    std::vector<Section> sections;
    juce::Label articulationStatus, keyswitchHint;
    int shownArticulation = -1;
    juce::Label credits;
    juce::MidiKeyboardComponent keyboard;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ViolinSynthEditor)
};
} // namespace violinsynth
