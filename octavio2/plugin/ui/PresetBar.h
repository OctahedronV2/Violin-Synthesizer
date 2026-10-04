#pragma once

#include "../PluginProcessor.h"
#include "Controls.h"

namespace octavio2::ui
{
// The header's preset box (M6): previous and next arrows, the preset's name with a dot when a
// setting differs from it, and a menu of the factory and user presets with Save, Delete and the
// folder. Message thread only (Presets).
class PresetBar final : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit PresetBar (Processor&);
    ~PresetBar() override;
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    void showMenu();
    void askSaveName();

private:
    void timerCallback() override;
    juce::Rectangle<float> prevBox() const { return { 0, 0, 34, 34 }; }
    juce::Rectangle<float> nameBox() const { return { 38, 0, (float) getWidth() - 76, 34 }; }
    juce::Rectangle<float> nextBox() const { return { (float) getWidth() - 34, 0, 34, 34 }; }

    Processor& processor;
    juce::String shownName;
    bool shownModified = false;
    int hover = -1;
    std::unique_ptr<juce::AlertWindow> saveWindow;
};
} // namespace octavio2::ui
