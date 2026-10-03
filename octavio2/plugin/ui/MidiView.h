#pragma once

#include "../PluginProcessor.h"
#include "Controls.h"

namespace octavio2::ui
{
// MIDI tab (mockup midi.svg): the controllers the player listens to, the velocity response, the
// octave, keyswitches and MPE. Learn, other maps, keyswitches and MPE arrive later (previews).
class MidiView final : public juce::Component, private juce::Timer
{
public:
    explicit MidiView (Processor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void paintVelocity (juce::Graphics&, juce::Rectangle<float>);

    Processor& processor;
    Knob curve, dynamics;
    Choices octave, behaviour, mpe;
    PreviewButton learn;
    int shownVelocity = -1;
};
} // namespace octavio2::ui
