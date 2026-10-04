#pragma once

#include "../PluginProcessor.h"
#include "Controls.h"

namespace octavio2::ui
{
// Articulation tab: how the player plays each note. Articulation (arco, pizzicato, harmonics,
// tremolo, sautille, portato, col legno; also keyswitches C1-G#1), the contact point (A1-B1), the
// tremolo's speed, bow style (auto or one stroke for every note), phrasing amount, fingering
// planned over the Studio look-ahead, and whether drawn CC lanes take over (M4, M5, M7).
class ArticulationView final : public juce::Component
{
public:
    explicit ArticulationView (Processor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    Choices articulation, bowStyle, fingerPlan, drawnCurves, contact, tremoloSync;
    Knob phrasing, tremoloSpeed;
};
} // namespace octavio2::ui
