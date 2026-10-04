#pragma once

#include "../PluginProcessor.h"
#include "Controls.h"
#include "NoteTrack.h"

#include <vector>

namespace octavio2::ui
{
// Curves tab (mockup curves.svg): the curves the player chose for the last 16 s it played, under
// the notes. "Drag curves as MIDI" drags those curves out as a MIDI file (the notes plus CC1
// dynamics, CC26 vibrato width, CC19 vibrato rate, CC74 contact point); a click saves the file
// to Documents/Octavio 2. Played back into the plugin, each CC lane takes over its dimension
// (drawn curves win), so editing starts from the player's own curves.
class CurvesView final : public juce::Component, private juce::Timer
{
public:
    explicit CurvesView (Processor&);
    void paint (juce::Graphics&) override;

    // writes the shown region as a MIDI file; returns the file, or an empty File if there is
    // nothing to export or it can't be written
    juce::File writeMidi (const juce::File&);

private:
    class DragButton final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit DragButton (CurvesView&);
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        CurvesView& view;
        bool dragged = false;
        juce::String status;
    };

    void timerCallback() override;
    void update();

    Processor& processor;
    NoteTrack track;
    PreviewButton guess;
    DragButton drag;
    double windowEnd = 0;
    // the curves kept here (the telemetry ring only holds 41 s of engine time, idle included)
    std::vector<Telemetry::Point> points;
    uint64_t historyRead = 0;
};
} // namespace octavio2::ui
