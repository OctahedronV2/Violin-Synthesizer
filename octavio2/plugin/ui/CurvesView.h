#pragma once

#include "../PluginProcessor.h"
#include "Controls.h"
#include "NoteTrack.h"

namespace octavio2::ui
{
// Curves tab (mockup curves.svg): the curves the player chose for the last 16 s it played, under
// the notes. Drawing your own curves (Guided and Manual lanes) arrives with the player milestone.
class CurvesView final : public juce::Component, private juce::Timer
{
public:
    explicit CurvesView (Processor&);
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    void update();

    Processor& processor;
    NoteTrack track;
    PreviewButton guess, drag;
    double windowEnd = 0;
};
} // namespace octavio2::ui
