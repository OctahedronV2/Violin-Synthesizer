#pragma once

#include "plugin/EditorComponents.h"
#include "plugin/LookAndFeel.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <memory>
#include <vector>

namespace violinsynth
{
class ViolinSynthProcessor;

// The editor is laid out once at a base size and scaled as a whole, so it
// stays sharp and in proportion at any window size.
class ViolinSynthEditor final : public juce::AudioProcessorEditor
{
public:
    static constexpr int baseWidth = 1100;
    static constexpr int baseHeight = 700;

    explicit ViolinSynthEditor (ViolinSynthProcessor&);
    ~ViolinSynthEditor() override;

    void resized() override;

private:
    class Content;

    ViolinLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this, 600 };
    std::unique_ptr<Content> content;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ViolinSynthEditor)
};
} // namespace violinsynth
