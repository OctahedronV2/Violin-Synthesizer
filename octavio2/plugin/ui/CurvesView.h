#pragma once

#include "../PluginProcessor.h"
#include "Controls.h"
#include "ModeBadge.h"
#include "NoteTrack.h"

#include <memory>
#include <vector>

namespace octavio2::ui
{
// Curves tab (mockup curves.svg; 2.3 makes it editable). Four lanes (dynamics, vibrato width,
// contact point, vibrato rate) on the host's timeline in bars and beats, under the notes the
// player played there. In amber: what the player played (its auto curve). Drawn on top: your
// curve, which guides the player (Guided, steel) or sets the dimension exactly (Manual, coral).
// The badge on each lane chooses who is in charge. Tools: Draw (freehand; click adds a point,
// drag a point to move it, double-click removes it), Line, Erase; right-click for more.
// "Guess curves" fills the lanes with the player's own curves from the last playback, ready to
// edit. "Drag curves as MIDI" exports the shown region (the notes plus CC1, CC26, CC19, CC74).
//
// Without a host timeline (the standalone app, or before the transport has played) the tab
// shows the last 16 s of playing in seconds, as 2.2 did, and drawing waits for a DAW transport.
class CurvesView final : public juce::Component, private juce::Timer
{
public:
    explicit CurvesView (Processor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

    // writes the shown region as a MIDI file; returns the file, or an empty File if there is
    // nothing to export or it can't be written
    juce::File writeMidi (const juce::File&);

    // ---- 2.3, also for the tests and snapshots
    enum class Tool
    {
        draw,
        line,
        erase
    };
    void setTool (Tool);
    Tool getTool() const { return tool; }
    void update(); // reads what the player did since the last call
    bool onTimeline() const; // beats (a host timeline) rather than the 16 s seconds view
    // fills lane k (all lanes: -1) with the player's own curve from the last playback; false
    // when nothing was recorded on the timeline
    bool guessCurves (int k = -1);
    void setViewRange (double startBeat, double spanBeats);
    double viewStartBeat() const { return viewStart; }
    double viewSpanBeats() const { return viewSpan; }
    bool undo();
    // the plot of lane k, in this component's coordinates
    juce::Rectangle<float> plotArea (int k) const;

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
    // a button drawn as in the mockups (Guess curves, the tools)
    class Button final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        Button (juce::String text, std::function<void()> onClick);
        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent&) override;
        bool on = false, enabled = true;

    private:
        juce::String text;
        std::function<void()> onClick;
    };

    void timerCallback() override;
    void paintSeconds (juce::Graphics&);
    void paintTimeline (juce::Graphics&);
    juce::File writeMidiSeconds (const juce::File&);
    juce::File writeMidiBeats (const juce::File&);

    // the timeline: beats <-> x, values <-> y
    float xOfBeat (double b) const;
    double beatOfX (float x) const;
    float yOfValue (int k, float v) const;
    float valueOfY (int k, float y) const;
    int laneAt (juce::Point<float>) const; // -1: none
    void followPlayhead();
    void laneMenu (int k);
    void pushUndo();
    void edited (int k); // after an edit of lane k: a lane drawn on goes from Auto to Guided

    Processor& processor;
    NoteTrack track;
    Button guess;
    DragButton drag;
    Button toolDraw, toolLine, toolErase;
    std::vector<std::unique_ptr<ModeBadge>> badges;
    // the seconds view (no host timeline): the curves kept here, as 2.2
    double windowEnd = 0;
    std::vector<Telemetry::Point> points;
    uint64_t historyRead = 0;

    // ---- the timeline view (2.3): what the player played, by beat (binsPerBeat a beat)
    static constexpr int binsPerBeat = 32, maxBins = binsPerBeat * 8192;
    struct Bin
    {
        float now[o2::laneCount]; // the lanes' values as played (0..1)
        float base[o2::laneCount]; // before the player's micro-shaping (Guess curves)
        bool sounding = false, valid = false;
    };
    std::vector<Bin> bins;
    struct BeatNote
    {
        double on, off; // beats; off < on: still sounding
        int pitch;
    };
    std::vector<BeatNote> beatNotes;
    uint64_t logRead = 0;
    double lastBeat = -1e9; // the last recorded beat (a jump back starts a new pass)
    struct Anchor // engine time -> beat, from the telemetry's points on the timeline
    {
        double t, beat, bpm;
    };
    std::vector<Anchor> anchors;
    double anchorBpm = 120;
    bool wasPlaying = false, follow = true;
    double viewStart = 0, viewSpan = 16;

    // editing
    Tool tool = Tool::draw;
    int editLane = -1, editPoint = -1;
    enum class Gesture
    {
        none,
        stroke,
        point,
        line,
        erase,
        pan
    } gesture
        = Gesture::none;
    CurveModel::Lane editBase; // the lane when the gesture started
    std::vector<std::pair<double, float>> stroke; // freehand samples, by beat
    double strokeLo = 0, strokeHi = 0, lastStrokeBeat = 0;
    juce::Point<float> gestureFrom, gestureTo;
    double panFrom = 0;
    bool changedInGesture = false;
    juce::Point<float> hover { -1, -1 };
    std::vector<std::array<CurveModel::Lane, o2::laneCount>> undoStack;
};
} // namespace octavio2::ui
