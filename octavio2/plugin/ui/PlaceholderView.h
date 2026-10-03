#pragma once

#include "Theme.h"

namespace octavio2::ui
{
// A tab whose controls arrive with a later milestone (Bow, Left hand, Articulation): says what
// it will hold and when.
class PlaceholderView final : public juce::Component
{
public:
    PlaceholderView (juce::String title, juce::String what, juce::String when);
    void paint (juce::Graphics&) override;

private:
    juce::String title, what, when;
};
} // namespace octavio2::ui
