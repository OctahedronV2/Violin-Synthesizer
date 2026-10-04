#pragma once

#include "PluginProcessor.h"
#include "plugin/HostKeyboardFocus.h"
#include "ui/Controls.h"
#include "ui/Keyboard.h"
#include "ui/PresetBar.h"

namespace octavio2
{
// The Octavio 2 interface, from the mockups (octavio-2/mockups): a header with the instrument,
// player, presets, Live/Studio and the tabs; the tab's view; the keyboard. Drawn at 1200 x 780
// and scaled to the window, which keeps that shape.
class Editor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit Editor (Processor&);
    ~Editor() override;

    void resized() override;
    void parentHierarchyChanged() override { hostFocus.editorParentChanged(); }

private:
    void timerCallback() override;

    // everything, in design units
    class Canvas final : public juce::Component
    {
    public:
        explicit Canvas (Editor&);
        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void showTab (int);
        juce::Rectangle<float> tabBounds (int) const;

        Editor& editor;
        std::vector<std::unique_ptr<juce::Component>> views;
        int tab = 0;
    };

    Processor& processor;
    ui::LookAndFeel lookAndFeel;
    ui::Choices mode;
    ui::Keyboard keyboard;
    struct HeaderPreview final : juce::Component, juce::SettableTooltipClient
    {
    } instrument;
    ui::PresetBar presets; // M6
    juce::ComboBox player; // M7: Player Style
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> playerAttachment;
    Canvas canvas { *this };
    juce::TooltipWindow tooltips { this, 500 };
    violinsynth::HostKeyboardFocus hostFocus { *this };
    juce::String cpuText, latencyText;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Editor)
};
} // namespace octavio2
