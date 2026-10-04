#pragma once

#include "../PluginProcessor.h"
#include "Controls.h"
#include "NoteTrack.h"

namespace octavio2::ui
{
// Play tab (mockups main.svg and studio.svg): what the player is doing on the fingerboard, the
// note now, then the six main controls and articulations in Live mode, or the look-ahead plan
// in Studio mode.
class PlayView final : public juce::Component, private juce::Timer
{
public:
    explicit PlayView (Processor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void paintFingerboard (juce::Graphics&, juce::Rectangle<float>);
    void paintNow (juce::Graphics&, juce::Rectangle<float>);
    void paintLookAhead (juce::Graphics&, juce::Rectangle<float>);
    void paintPlan (juce::Graphics&, juce::Rectangle<float>);
    bool studio() const;
    void updateBadges();

    Processor& processor;
    Knob dynamics, expression, vibrato, portamento, stringPreference, room;
    Choices articulation;
    juce::Rectangle<float> dragArea;

    NoteTrack track;
};
} // namespace octavio2::ui
