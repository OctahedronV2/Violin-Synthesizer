#pragma once

#include "../PluginProcessor.h"
#include "Controls.h"

namespace octavio2::ui
{
// Articulation tab: how the player plays each note. Articulation (arco, pizzicato, harmonics; also
// keyswitches C1-E1), bow style (auto or one stroke for every note), phrasing amount, fingering
// planned over the Studio look-ahead, and whether drawn CC lanes take over (M4, M5).
class ArticulationView final : public juce::Component
{
public:
    explicit ArticulationView (Processor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    Choices articulation, bowStyle, fingerPlan, drawnCurves;
    Knob phrasing;
};
} // namespace octavio2::ui
