#include "CurvesView.h"

namespace octavio2::ui
{
namespace
{
constexpr float top = 104;
constexpr double span = 16.0; // seconds shown
constexpr float x0 = 210, x1 = designWidth - 24;

struct Lane
{
    const char* name;
    const char* source;
    const char* note;
    juce::Colour colour;
    float (*value) (const Telemetry::Point&); // 0..1, up = more
    const char* low;
    const char* high;
};

const Lane lanes[] = {
    { "Dynamics",
      "velocity + CC11",
      "from velocity, shaped by the player",
      colours::amber,
      [] (const Telemetry::Point& p) { return p.dynamics; },
      "pp",
      "ff" },
    { "Vibrato width",
      "CC24 scales it",
      "blooms on long notes, little on fast ones",
      colours::amber,
      [] (const Telemetry::Point& p) { return std::min (1.0f, p.vibWidth / 60.0f); },
      "0 ct",
      "60 ct" },
    { "Contact point",
      "auto",
      "follows dynamics: nearer the bridge when louder",
      colours::amber,
      [] (const Telemetry::Point& p) { return juce::jlimit (0.0f, 1.0f, 1.0f - (p.contact - 0.02f) / 0.2f); },
      "tasto",
      "pont." },
    { "Vibrato rate",
      "auto",
      "faster when louder and higher",
      colours::amber,
      [] (const Telemetry::Point& p) { return juce::jlimit (0.0f, 1.0f, (p.vibRate - 4.0f) / 4.0f); },
      "4 Hz",
      "8 Hz" },
};
} // namespace

CurvesView::CurvesView (Processor& p)
    : processor (p),
      guess (juce::String::fromUTF8 ("↻ Guess curves"), "a later milestone"),
      drag (*this)
{
    guess.setBounds (designWidth - 380, juce::roundToInt (112 - top), 150, 32);
    drag.setBounds (designWidth - 220, juce::roundToInt (112 - top), 196, 32);
    addAndMakeVisible (guess);
    addAndMakeVisible (drag);
    startTimerHz (30);
}

void CurvesView::timerCallback()
{
    if (isShowing())
        repaint();
}

void CurvesView::update()
{
    track.read (processor.getEngine());
    // copy the new curve points: every sounding one, and the first silent one after (a gap)
    const auto& T = processor.getTelemetry();
    const auto count = T.historyCount.load (std::memory_order_acquire);
    if (count - historyRead > Telemetry::historySize - 64)
        historyRead = count - (Telemetry::historySize - 64);
    for (; historyRead < count; ++historyRead)
    {
        const auto pt = T.history[(size_t) (historyRead % Telemetry::historySize)];
        if (! points.empty() && pt.t < points.back().t - 1.0f)
            points.clear(); // the engine was reset
        if (pt.sounding || (! points.empty() && points.back().sounding))
            points.push_back (pt);
    }
    if (points.size() > 8000) // keep the last 45 s or so of playing
        points.erase (points.begin(), points.begin() + (std::ptrdiff_t) (points.size() - 4500));
    // follow the playing; once it stops, hold the last region still
    const double now = processor.getEngine().seconds();
    bool sounding = false;
    double lastOff = -1e9;
    for (const auto& n : track.notes)
        if (! n.planned)
        {
            sounding = sounding || n.off < 0;
            lastOff = std::max (lastOff, n.off);
        }
    windowEnd = sounding ? now : std::min (now, std::max (windowEnd, lastOff + 1.0));
}

void CurvesView::paint (juce::Graphics& g)
{
    update();
    drawText (g,
              "The player's curves for the last 16 s. Drag them into your DAW as MIDI (CC1, 26, 19, 74): a lane "
              "played back takes over.",
              24,
              132 - top,
              Fonts::sans (12.5f),
              colours::muted);
    const double t0 = windowEnd - span;
    auto xOf = [t0] (double t) { return x0 + (x1 - x0) * (float) ((t - t0) / span); };

    // notes strip
    const float ny = 160 - top;
    g.setColour (colours::panel);
    g.fillRoundedRectangle (24, ny, designWidth - 48, 70, 8);
    g.setColour (colours::line);
    g.drawRoundedRectangle (24.5f, ny + 0.5f, designWidth - 49, 69, 8, 1);
    drawLabel (g, "Notes", 40, ny + 24, colours::muted, 10);
    int lo = 127, hi = 0;
    for (const auto& n : track.notes)
        if (! n.planned && (n.off < 0 || n.off > t0))
        {
            lo = std::min (lo, n.pitch);
            hi = std::max (hi, n.pitch);
        }
    if (lo <= hi)
        drawText (g,
                  noteName (lo) + juce::String::fromUTF8 (" – ") + noteName (hi),
                  40,
                  ny + 42,
                  Fonts::sans (11),
                  colours::dim);
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (juce::Rectangle<float> (x0, ny, x1 - x0, 70).toNearestInt());
        const float range = (float) std::max (12, hi - lo);
        for (const auto& n : track.notes)
        {
            if (n.planned || (n.off >= 0 && n.off < t0))
                continue;
            const float xa = xOf (n.on), xb = xOf (n.off < 0 ? windowEnd : n.off);
            const float y = ny + 54 - (float) (n.pitch - lo) / range * 42;
            g.setColour (colours::gold);
            g.fillRoundedRectangle (xa + 1, y, std::max (2.0f, xb - xa - 2), 7, 2);
        }
        // seconds
        for (int s = (int) std::ceil (t0); s <= (int) windowEnd; ++s)
            if (s % 2 == 0 && s >= 0)
                drawText (g, juce::String (s) + " s", xOf (s) + 3, ny + 66, Fonts::mono (9), colours::dim);
    }

    // lanes
    const float lh = 104; // four lanes between the notes and the keyboard
    for (int i = 0; i < 4; ++i)
    {
        const auto& lane = lanes[i];
        const float y = 242 - top + i * (lh + 8);
        g.setColour (colours::panel);
        g.fillRoundedRectangle (24, y, designWidth - 48, lh, 8);
        g.setColour (colours::line);
        g.drawRoundedRectangle (24.5f, y + 0.5f, designWidth - 49, lh - 1, 8, 1);
        drawText (g, lane.name, 40, y + 26, Fonts::sans (14, true), colours::text);
        drawText (g, lane.source, 40, y + 44, Fonts::mono (11), colours::muted);
        drawBadge (g, { 176, y + 22 }, Mode::autoMode);
        drawText (g, "Auto", 40, y + 66, Fonts::sans (11), colours::amber);
        const juce::Rectangle<float> plot (x0, y + 10, x1 - x0, lh - 20);
        g.setColour (colours::well);
        g.fillRoundedRectangle (plot, 5);
        drawText (g, lane.high, x0 + 6, y + 24, Fonts::mono (9), colours::dim);
        drawText (g, lane.low, x0 + 6, y + lh - 16, Fonts::mono (9), colours::dim);
        drawText (g, lane.note, x1 - 10, y + 26, Fonts::sans (10.5f), colours::dim, juce::Justification::right);

        // one point per 10 ms, the gaps (no note) left out
        juce::Path p;
        bool pen = false;
        for (const auto& pt : points)
        {
            if (pt.t < t0 || pt.t > windowEnd)
            {
                pen = false;
                continue;
            }
            const float px = xOf (pt.t), py = y + lh - 14 - lane.value (pt) * (lh - 30);
            if (! pt.sounding)
            {
                pen = false;
                continue;
            }
            if (pen)
                p.lineTo (px, py);
            else
                p.startNewSubPath (px, py);
            pen = true;
        }
        g.setColour (lane.colour);
        g.strokePath (p, juce::PathStrokeType (2));
    }
}

// ---------------------------------------------------------------- export as MIDI
juce::File CurvesView::writeMidi (const juce::File& file)
{
    update();
    const double t1 = windowEnd, t0 = t1 - span;
    std::vector<NoteTrack::Note> notes;
    for (const auto& n : track.notes)
        if (! n.planned && n.on >= t0 && n.on < t1)
            notes.push_back (n);
    if (notes.empty())
        return {};
    std::sort (notes.begin(), notes.end(), [] (const auto& a, const auto& b) { return a.on < b.on; });
    const double start = notes.front().on;
    const float bpmNow = processor.getTelemetry().bpm.load();
    const double bpm = bpmNow > 20.0f && bpmNow < 400.0f ? (double) bpmNow : 120.0;
    constexpr int ppq = 960;
    auto tick = [&] (double t) { return std::max (0.0, (t - start) * bpm / 60.0 * ppq); };

    juce::MidiMessageSequence seq;
    seq.addEvent (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60.0e6 / bpm)), 0.0);
    for (size_t i = 0; i < notes.size(); ++i)
    {
        const auto& n = notes[i];
        double off = n.off < 0 ? t1 : n.off;
        // a slurred note overlaps the one before it, so playing the file back slurs it again
        if (i + 1 < notes.size() && notes[i + 1].slur && std::abs (off - notes[i + 1].on) < 0.005)
            off = notes[i + 1].on + 0.02;
        seq.addEvent (juce::MidiMessage::noteOn (1, n.pitch, (juce::uint8) 100), tick (n.on));
        seq.addEvent (juce::MidiMessage::noteOff (1, n.pitch), tick (std::max (off, n.on + 0.01)));
    }
    // one controller event per lane whenever its value moves a step
    int last[4] = { -1, -1, -1, -1 };
    const int ccs[4] = { 1, 26, 19, 74 };
    for (const auto& pt : points)
    {
        if (! pt.sounding || pt.t < start - 0.05 || pt.t > t1)
            continue;
        const int v[4] = { juce::jlimit (0, 127, juce::roundToInt (pt.dynamics * 127.0f)),
                           juce::jlimit (0, 127, juce::roundToInt (pt.vibWidth * 2.0f)),
                           juce::jlimit (0, 127, juce::roundToInt ((pt.vibRate - 4.0f) / 4.0f * 127.0f)),
                           juce::jlimit (0, 127, juce::roundToInt ((1.0f - (pt.contact - 0.02f) / 0.2f) * 127.0f)) };
        for (int k = 0; k < 4; ++k)
            if (v[k] != last[k])
            {
                last[k] = v[k];
                seq.addEvent (juce::MidiMessage::controllerEvent (1, ccs[k], v[k]), tick (pt.t));
            }
    }
    seq.sort();
    seq.updateMatchedPairs();
    juce::MidiFile midi;
    midi.setTicksPerQuarterNote (ppq);
    midi.addTrack (seq);
    file.deleteFile();
    juce::FileOutputStream out (file);
    if (! out.openedOk() || ! midi.writeTo (out))
        return {};
    out.flush();
    return file;
}

CurvesView::DragButton::DragButton (CurvesView& v)
    : view (v)
{
    setTooltip ("Drag the shown curves into your DAW as a MIDI file: the notes, CC1 dynamics, CC26 vibrato width, "
                "CC19 vibrato rate, CC74 contact point. Click to save it in Documents/Octavio 2.");
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

void CurvesView::DragButton::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (colours::amber.withAlpha (isMouseOverOrDragging() ? 0.55f : 0.4f));
    g.fillRoundedRectangle (r, 7);
    g.setColour (colours::line);
    g.drawRoundedRectangle (r, 7, 1);
    drawText (g,
              juce::String::fromUTF8 ("⠿ Drag curves as MIDI"),
              r.getCentreX(),
              r.getCentreY() + 4.5f,
              Fonts::sans (12.5f, true),
              colours::text,
              juce::Justification::horizontallyCentred);
}

void CurvesView::DragButton::mouseDown (const juce::MouseEvent&)
{
    dragged = false;
}

void CurvesView::DragButton::mouseDrag (const juce::MouseEvent& e)
{
    if (dragged || e.getDistanceFromDragStart() < 6)
        return;
    dragged = true;
    const auto f = view.writeMidi (
        juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Octavio 2 curves.mid"));
    if (f.existsAsFile())
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
}

void CurvesView::DragButton::mouseUp (const juce::MouseEvent& e)
{
    if (dragged || ! getLocalBounds().contains (e.getPosition()))
        return;
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Octavio 2");
    dir.createDirectory();
    const auto f = view.writeMidi (dir.getNonexistentChildFile ("Octavio 2 curves", ".mid"));
    status
        = f.existsAsFile() ? "Saved " + f.getFullPathName() : juce::String ("Play something first: nothing to export.");
    setTooltip (status);
    repaint();
}
} // namespace octavio2::ui
