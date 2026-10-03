#pragma once

#include "PluginProcessor.h"
#include "plugin/HostKeyboardFocus.h"
#include "plugin/LookAndFeel.h"

namespace octavio2
{
// First Octavio 2 editor: every parameter, in Octavio 1's dark wood and amber look, plus the
// on-screen keyboard. The full interface from the mockups (octavio-2/mockups) replaces it.
class Editor final : public juce::AudioProcessorEditor
{
public:
    explicit Editor (Processor&);
    ~Editor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void parentHierarchyChanged() override { hostFocus.editorParentChanged(); }

private:
    // right-click (Ctrl-click on macOS) opens the host's menu for the parameter: automation
    // clips, MIDI learn and so on
    struct MenuSlider final : juce::Slider
    {
        MenuSlider()
            : juce::Slider (RotaryHorizontalVerticalDrag, TextBoxBelow)
        {
        }
        std::function<void()> onMenu;
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu() && onMenu)
                onMenu();
            else
                juce::Slider::mouseDown (e);
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (! e.mods.isPopupMenu())
                juce::Slider::mouseDrag (e);
        }
        void mouseUp (const juce::MouseEvent& e) override
        {
            if (! e.mods.isPopupMenu())
                juce::Slider::mouseUp (e);
        }
    };
    struct Knob
    {
        MenuSlider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    struct Choice
    {
        juce::ComboBox box;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
    };

    void addKnob (Knob&, const juce::ParameterID&, const juce::String& name);
    void addChoice (Choice&, const juce::ParameterID&, const juce::String& name);
    void showParameterMenu (juce::Component&, const juce::ParameterID&);

    Processor& processor;
    violinsynth::ViolinLookAndFeel lookAndFeel;
    Knob velocityCurve, vibrato, brightness, reverb, volume;
    Choice mode, octave, room;
    juce::MidiKeyboardComponent keyboard;
    juce::Label credits;
    violinsynth::HostKeyboardFocus hostFocus { *this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Editor)
};
} // namespace octavio2
