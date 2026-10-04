#pragma once

#include "../PluginProcessor.h"
#include "Controls.h"

namespace octavio2::ui
{
// Articulation tab: how the player plays each note. Articulation (arco, pizzicato, harmonics,
// tremolo, sautille, portato, col legno; also keyswitches, C1-G#1 by default), the contact point
// (A1-B1), the tremolo's speed, bow style (auto or one stroke for every note), phrasing amount,
// fingering planned over the Studio look-ahead, and whether drawn CC lanes take over (M4, M5, M7).
class ArticulationView final : public juce::Component, private juce::Timer
{
public:
    explicit ArticulationView (Processor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override; // 2.3: keyswitch and UACC latches change what is lit
    void showKeyswitches (int start); // 2.3: the keys named on each choice follow Keyswitch Start
    Choices articulation, bowStyle, fingerPlan, drawnCurves, contact, tremoloSync;
    Knob phrasing, tremoloSpeed;
    std::unique_ptr<juce::ParameterAttachment> keyswitchStart;
    juce::String keyswitchRange;
    std::array<int, 3> shown {};
};
} // namespace octavio2::ui
