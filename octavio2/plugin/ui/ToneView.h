#pragma once

#include "../PluginProcessor.h"
#include "Controls.h"

namespace octavio2::ui
{
// Tone tab (mockup tone.svg): the instrument's parts, bridge and resonance, microphones and room.
// Brilliance, the room, reverb and volume work now; the other parts arrive with the milestones
// that build them (docs: octavio-2/PLAN.md section 8) and show as previews until then.
class ToneView final : public juce::Component, private juce::Timer
{
public:
    explicit ToneView (Processor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void paintScope (juce::Graphics&, juce::Rectangle<float>);
    void paintMics (juce::Graphics&, float x, float y);

    Processor& processor;
    Choices body, strings, rosin, bow, mute, quality, rooms;
    Knob bridge, sympathetic, wolf, hold, brilliance, imperfection;
    Knob reverb, volume, width, movement;
    std::vector<float> scope;
};
} // namespace octavio2::ui
