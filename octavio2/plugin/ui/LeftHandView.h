#pragma once

#include "../PluginProcessor.h"
#include "Controls.h"
#include "ModeBadge.h"
#include "TraceTrack.h"

namespace octavio2::ui
{
// Left hand tab: the strings, hand positions, shifts and vibrato of the last 6 s, the left hand
// now (string, position, finger, shift, vibrato), and the left-hand controls (fingering plan,
// string preference, portamento, vibrato width, rate and delay). Intonation is on the Tone tab.
class LeftHandView final : public juce::Component, private juce::Timer
{
public:
    explicit LeftHandView (Processor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void paintHistory (juce::Graphics&, juce::Rectangle<float>);
    void paintNow (juce::Graphics&, juce::Rectangle<float>);

    Processor& processor;
    Choices fingerPlan;
    Knob stringPreference, portamento, width, rate, delay;
    TraceTrack trace;
    // 2.3: who sets the vibrato's width and rate (click to choose)
    ModeBadge widthBadge { processor, o2::dimVibWidth }, rateBadge { processor, o2::dimVibRate };
};
} // namespace octavio2::ui
