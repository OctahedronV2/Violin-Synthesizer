#pragma once

#include "../PluginProcessor.h"
#include "Controls.h"
#include "NoteTrack.h"
#include "TraceTrack.h"

namespace octavio2::ui
{
// Bow tab: the bow's plan and physics over the last 6 s (where on the hair it plays, the bow
// changes, bow speed, force and contact point over each stroke), the bow now (direction, hair
// left), and the bow controls (bow style, bow change, stroke shaping, bite, contact point).
class BowView final : public juce::Component, private juce::Timer
{
public:
    explicit BowView (Processor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void paintHistory (juce::Graphics&, juce::Rectangle<float>);
    void paintNow (juce::Graphics&, juce::Rectangle<float>);

    Processor& processor;
    Choices bowStyle;
    Knob bowChange, strokeShaping, bite, contact;
    NoteTrack track;
    TraceTrack trace;
};
} // namespace octavio2::ui
