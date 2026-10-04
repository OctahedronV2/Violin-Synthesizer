#pragma once

#include "../PluginProcessor.h"
#include "Controls.h"

#include <functional>

namespace octavio2::ui
{
// 2.3: a clickable A / G / M badge. It shows who is in charge of a dimension now (the player,
// the player around your curve or controller, or exactly you) and a click opens a menu to choose.
// forDim: one of the player's dimensions (o2::Dim), bound to its mode parameter; an incoming
// controller shows Guided while the parameter says Auto, and choosing Auto gives it back.
// The generic form shows `shown()` and calls `choose` with the picked mode (knob-guided
// dimensions such as String preference: Auto resets the knob).
class ModeBadge final : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    ModeBadge (Processor&, int dim);
    ModeBadge (std::function<Mode()> shown,
               std::function<void (Mode)> choose,
               juce::String name,
               juce::String autoText,
               juce::String guidedText);

    static const char* dimName (int dim); // "Dynamics", "Vibrato width", ...
    // what a dimension's badge shows: its mode parameter, or Guided while an incoming controller
    // holds an Auto dimension
    static Mode displayed (Processor&, int dim);
    static Mode modeOf (int m) { return m == 2 ? Mode::manual : m == 1 ? Mode::guided : Mode::autoMode; }

    // centred on c (in the parent's coordinates), 22 units across
    void placeAt (juce::Point<float> c);
    Mode shownMode() const { return shown(); }
    // what the menu does with a pick (tests call it directly)
    void choose (Mode m);
    void showMenu();

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void updateTooltip();

    Processor* processor = nullptr;
    int dim = -1;
    std::function<Mode()> shown;
    std::function<void (Mode)> chooser;
    juce::String name, autoText, guidedText;
    Mode last = Mode::autoMode;
};
} // namespace octavio2::ui
