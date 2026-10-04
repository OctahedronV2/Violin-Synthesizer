#include "CurvesView.h"

namespace octavio2::ui
{
namespace
{
constexpr float top = 104;
constexpr double span = 16.0; // seconds shown without a host timeline
constexpr float x0 = 210, x1 = designWidth - 24;
constexpr float laneTop = 242 - top, laneH = 104, laneGap = 8;

// the lanes, by o2::Dim; shown in the mockup's order (dynamics, vibrato width, contact, rate)
struct Lane
{
    const char* name;
    const char* source;
    const char* note;
    float (*value) (const Telemetry::Point&); // 0..1, up = more (as played)
    float (*base) (const Telemetry::Point&); // before the player's micro-shaping
    const char* low;
    const char* high;
};

float vibLane (float cents)
{
    return juce::jlimit (0.0f, 1.0f, cents / 63.5f);
}
float contactLane (float beta)
{
    return juce::jlimit (0.0f, 1.0f, 1.0f - (beta - 0.02f) / 0.2f);
}
float rateLane (float hz)
{
    return juce::jlimit (0.0f, 1.0f, (hz - 4.0f) / 4.0f);
}

const Lane lanes[o2::laneCount] = {
    { "Dynamics",
      "CC1",
      "from velocity, shaped by the player",
      [] (const Telemetry::Point& p) { return p.dynamics; },
      [] (const Telemetry::Point& p) { return p.dynBase; },
      "pp",
      "ff" },
    { "Vibrato width",
      "CC26",
      "blooms on long notes, little on fast ones",
      [] (const Telemetry::Point& p) { return vibLane (p.vibWidth); },
      [] (const Telemetry::Point& p) { return vibLane (p.vibBase); },
      "0 ct",
      "64 ct" },
    { "Vibrato rate",
      "CC19",
      "faster when louder and higher",
      [] (const Telemetry::Point& p) { return rateLane (p.vibRate); },
      [] (const Telemetry::Point& p) { return rateLane (p.rateBase); },
      "4 Hz",
      "8 Hz" },
    { "Contact point",
      "CC74",
      "follows dynamics: nearer the bridge when louder",
      [] (const Telemetry::Point& p) { return contactLane (p.contact); },
      [] (const Telemetry::Point& p) { return contactLane (p.contactBase); },
      "tasto",
      "pont." },
};
constexpr int rowOfDim[o2::laneCount] = { 0, 1, 3, 2 }; // dynamics, width, rate, contact -> row
constexpr int dimOfRow[o2::laneCount] = { 0, 1, 3, 2 };

float laneY (int k)
{
    return laneTop + (float) rowOfDim[k] * (laneH + laneGap);
}

juce::Colour modeColour (Mode m)
{
    return m == Mode::autoMode ? colours::amber : m == Mode::guided ? colours::steel : colours::coral;
}

// a lane's value as the user reads it
juce::String valueText (int k, float v)
{
    switch (k)
    {
        case o2::dimDynamics:
            return dynamicName (v);
        case o2::dimVibWidth:
            return juce::String (juce::roundToInt (o2::laneToUnit (k, v))) + " ct";
        case o2::dimVibRate:
            return juce::String (o2::laneToUnit (k, v), 1) + " Hz";
        default:
            return juce::String (o2::laneToUnit (k, v), 3);
    }
}

// fewer points for the same curve (Ramer-Douglas-Peucker, eps in lane units)
CurveModel::Lane simplify (const CurveModel::Lane& in, float eps)
{
    if (in.size() < 3)
        return in;
    std::vector<bool> keep (in.size(), false);
    keep.front() = keep.back() = true;
    std::vector<std::pair<size_t, size_t>> todo { { 0, in.size() - 1 } };
    while (! todo.empty())
    {
        const auto [a, b] = todo.back();
        todo.pop_back();
        if (b <= a + 1)
            continue;
        float worst = 0;
        size_t at = a;
        const double span = in[b].beat - in[a].beat;
        for (size_t i = a + 1; i < b; ++i)
        {
            const double u = span > 0 ? (in[i].beat - in[a].beat) / span : 0.0;
            const float line = in[a].v + (in[b].v - in[a].v) * (float) u;
            const float err = std::abs (in[i].v - line);
            if (err > worst)
            {
                worst = err;
                at = i;
            }
        }
        // keep a point at least every 2 beats too, so a long flat stretch stays editable
        if (worst > eps || in[b].beat - in[a].beat > 2.0)
        {
            keep[at == a ? (a + b) / 2 : at] = true;
            const size_t m = at == a ? (a + b) / 2 : at;
            todo.push_back ({ a, m });
            todo.push_back ({ m, b });
        }
    }
    CurveModel::Lane out;
    for (size_t i = 0; i < in.size(); ++i)
        if (keep[i])
            out.push_back (in[i]);
    return out;
}
} // namespace

// ================================================================ buttons
CurvesView::Button::Button (juce::String t, std::function<void()> f)
    : text (std::move (t)),
      onClick (std::move (f))
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void CurvesView::Button::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    const bool lit = on && enabled; // a disabled tool is never shown chosen
    g.setColour (lit ? colours::amber.withAlpha (0.85f) : colours::panel2);
    g.fillRoundedRectangle (r, 7);
    g.setColour (isMouseOver() && enabled ? colours::muted : colours::line);
    g.drawRoundedRectangle (r, 7, 1);
    drawText (g,
              text,
              r.getCentreX(),
              r.getCentreY() + 4.5f,
              Fonts::sans (12.5f, lit),
              lit           ? colours::bg
                  : enabled ? colours::text
                            : colours::dim,
              juce::Justification::horizontallyCentred);
}

void CurvesView::Button::mouseUp (const juce::MouseEvent& e)
{
    if (enabled && getLocalBounds().contains (e.getPosition()) && onClick)
        onClick();
}

// ================================================================ the view
CurvesView::CurvesView (Processor& p)
    : processor (p),
      guess (juce::String::fromUTF8 ("↻ Guess curves"), [this] { guessCurves(); }),
      drag (*this),
      toolDraw ("Draw", [this] { setTool (Tool::draw); }),
      toolLine ("Line", [this] { setTool (Tool::line); }),
      toolErase ("Erase", [this] { setTool (Tool::erase); })
{
    guess.setBounds (designWidth - 380, juce::roundToInt (112 - top), 150, 32);
    drag.setBounds (designWidth - 220, juce::roundToInt (112 - top), 196, 32);
    guess.setTooltip ("Fill the lanes with the player's own curves from the last playback, to edit from there. The "
                      "lanes become Guided: played back, they give the same performance.");
    toolDraw.setTooltip ("Draw: drag to draw freehand, click to add a point, drag a point to move it, double-click "
                         "a point to remove it. Shift-drag draws a straight line.");
    toolLine.setTooltip ("Line: drag from where the line starts to where it ends.");
    toolErase.setTooltip ("Erase: drag across the part of the curve to remove; the player decides there again.");
    for (auto* c : std::initializer_list<juce::Component*> { &guess, &drag, &toolDraw, &toolLine, &toolErase })
        addAndMakeVisible (c);
    for (int k = 0; k < o2::laneCount; ++k)
    {
        badges.push_back (std::make_unique<ModeBadge> (p, k));
        addAndMakeVisible (*badges.back());
    }
    setTool (Tool::draw);
    setWantsKeyboardFocus (true);
    startTimerHz (30);
}

void CurvesView::resized()
{
    int x = designWidth - 380 - 12 - 3 * 60 - 2 * 4;
    for (auto* b : { &toolDraw, &toolLine, &toolErase })
    {
        b->setBounds (x, juce::roundToInt (112 - top), 60, 32);
        x += 64;
    }
    for (int k = 0; k < o2::laneCount; ++k)
        badges[(size_t) k]->placeAt ({ 176, laneY (k) + 22 });
}

void CurvesView::setTool (Tool t)
{
    tool = t;
    toolDraw.on = t == Tool::draw;
    toolLine.on = t == Tool::line;
    toolErase.on = t == Tool::erase;
    for (auto* b : { &toolDraw, &toolLine, &toolErase })
        b->repaint();
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
}

void CurvesView::timerCallback()
{
    if (isShowing())
        repaint();
}

bool CurvesView::onTimeline() const
{
    return processor.getTelemetry().hasTimeline.load (std::memory_order_relaxed);
}

// ---------------------------------------------------------------- what the player did
void CurvesView::update()
{
    track.read (processor.getEngine());
    const auto& T = processor.getTelemetry();
    const auto count = T.historyCount.load (std::memory_order_acquire);
    if (count - historyRead > Telemetry::historySize - 64)
        historyRead = count - (Telemetry::historySize - 64);
    for (; historyRead < count; ++historyRead)
    {
        const auto pt = T.history[(size_t) (historyRead % Telemetry::historySize)];
        // the seconds view: every sounding point, and the first silent one after (a gap)
        if (! points.empty() && pt.t < points.back().t - 1.0f)
            points.clear(); // the engine was reset
        if (pt.sounding || (! points.empty() && points.back().sounding))
            points.push_back (pt);
        // 2.3 the timeline view, by beat
        if (pt.beat < -1e8)
            continue;
        const float bpm = T.bpm.load();
        anchorBpm = bpm > 20.0f && bpm < 400.0f ? (double) bpm : 120.0;
        if (! anchors.empty() && pt.t < anchors.back().t)
            anchors.clear(); // the engine was reset
        anchors.push_back ({ pt.t, pt.beat, anchorBpm });
        if (anchors.size() > 8192)
            anchors.erase (anchors.begin(), anchors.begin() + 4096);
        if (pt.beat < lastBeat - 0.5) // the transport jumped back (a loop, or played again): a new pass
        {
            const auto from = (size_t) std::max (0, (int) std::floor (pt.beat * binsPerBeat));
            for (size_t i = from; i < bins.size(); ++i)
                bins[i].valid = false;
            beatNotes.erase (std::remove_if (beatNotes.begin(),
                                             beatNotes.end(),
                                             [&] (const BeatNote& n) { return n.on >= pt.beat - 0.01; }),
                             beatNotes.end());
        }
        const int b = (int) std::floor (pt.beat * binsPerBeat);
        if (b >= 0 && b < maxBins)
        {
            if ((size_t) b >= bins.size())
                bins.resize ((size_t) b + 1024);
            // the bins since the last point take its values (up to a quarter beat)
            const int prev = (int) std::floor (lastBeat * binsPerBeat);
            const int from = prev >= 0 && b - prev > 0 && b - prev <= binsPerBeat / 4 ? prev + 1 : b;
            for (int i = from; i <= b; ++i)
            {
                auto& bin = bins[(size_t) i];
                for (int k = 0; k < o2::laneCount; ++k)
                {
                    bin.now[k] = lanes[k].value (pt);
                    bin.base[k] = lanes[k].base (pt);
                }
                bin.sounding = pt.sounding;
                bin.valid = true;
            }
        }
        lastBeat = pt.beat;
    }
    if (points.size() > 8000) // keep the last 45 s or so of playing
        points.erase (points.begin(), points.begin() + (std::ptrdiff_t) (points.size() - 4500));

    // the notes, by beat: each mapped from the engine's time through the anchor just before it
    const auto& engine = processor.getEngine();
    const auto written = engine.logCount();
    if (written - logRead > o2::Engine::logSize - 64)
        logRead = written - (o2::Engine::logSize - 64);
    for (; logRead < written; ++logRead)
    {
        const auto e = engine.logEntry (logRead);
        if (e.kind == o2::Engine::NoteLog::planned)
            continue;
        auto it = std::upper_bound (anchors.begin(),
                                    anchors.end(),
                                    e.t,
                                    [] (double t, const Anchor& a) { return t < a.t; });
        if (it == anchors.end() && T.playing.load() && e.t > engine.seconds() - 0.25)
            break; // its point on the timeline is still to come: read it again next time
        if (it != anchors.begin())
            --it;
        if (it == anchors.end() || std::abs (e.t - it->t) > 0.1)
            continue; // played off the timeline (the transport stopped)
        const double beat = it->beat + (e.t - it->t) * it->bpm / 60.0;
        if (e.kind == o2::Engine::NoteLog::off)
        {
            for (auto it = beatNotes.rbegin(); it != beatNotes.rend(); ++it)
                if (it->pitch == e.pitch && it->off < it->on)
                {
                    it->off = beat;
                    break;
                }
            continue;
        }
        if (e.kind == o2::Engine::NoteLog::slur) // a slur ends the note before it
            for (auto& n : beatNotes)
                if (n.off < n.on)
                    n.off = beat;
        beatNotes.push_back ({ beat, -1e9, e.pitch });
    }
    if (beatNotes.size() > 6000)
        beatNotes.erase (beatNotes.begin(), beatNotes.begin() + 1000);

    // the seconds view follows the playing; once it stops, holds the last region still
    const double now = engine.seconds();
    bool sounding = false;
    double lastOff = -1e9;
    for (const auto& n : track.notes)
        if (! n.planned)
        {
            sounding = sounding || n.off < 0;
            lastOff = std::max (lastOff, n.off);
        }
    windowEnd = sounding ? now : std::min (now, std::max (windowEnd, lastOff + 1.0));
    followPlayhead();
}

void CurvesView::followPlayhead()
{
    const auto& T = processor.getTelemetry();
    const bool playing = T.playing.load (std::memory_order_relaxed);
    if (playing && ! wasPlaying)
        follow = true; // the transport started: follow it again
    wasPlaying = playing;
    if (playing && follow && gesture == Gesture::none)
    {
        const double beat = T.beat.load (std::memory_order_relaxed);
        if (beat < viewStart + 0.1 * viewSpan || beat > viewStart + 0.85 * viewSpan)
            viewStart = std::max (0.0, beat - 0.1 * viewSpan); // page on, as DAWs do
    }
}

void CurvesView::setViewRange (double startBeat, double spanBeats)
{
    viewStart = std::max (0.0, startBeat);
    viewSpan = juce::jlimit (1.0, 512.0, spanBeats);
    follow = false;
    repaint();
}

// ---------------------------------------------------------------- coordinates
float CurvesView::xOfBeat (double b) const
{
    return x0 + (x1 - x0) * (float) ((b - viewStart) / viewSpan);
}

double CurvesView::beatOfX (float x) const
{
    return viewStart + (double) ((x - x0) / (x1 - x0)) * viewSpan;
}

juce::Rectangle<float> CurvesView::plotArea (int k) const
{
    return { x0, laneY (k) + 10, x1 - x0, laneH - 20 };
}

float CurvesView::yOfValue (int k, float v) const
{
    return laneY (k) + laneH - 14 - v * (laneH - 30);
}

float CurvesView::valueOfY (int k, float y) const
{
    return juce::jlimit (0.0f, 1.0f, (laneY (k) + laneH - 14 - y) / (laneH - 30));
}

int CurvesView::laneAt (juce::Point<float> p) const
{
    for (int k = 0; k < o2::laneCount; ++k)
        if (plotArea (k).expanded (0, 4).contains (p))
            return k;
    return -1;
}

// ---------------------------------------------------------------- painting
void CurvesView::paint (juce::Graphics& g)
{
    update();
    const bool timeline = onTimeline();
    drawText (g,
              juce::String::fromUTF8 ("Draw on a lane to guide the player · click a badge to change who's in charge"),
              24,
              124 - top,
              Fonts::sans (12.5f),
              colours::muted);
    drawText (g,
              timeline ? juce::String::fromUTF8 ("Right-click a lane for more · wheel scrolls, Ctrl+wheel zooms · "
                                                 "saved with your project")
                       : juce::String ("No DAW timeline here (standalone): the last 16 s. Drawing needs a host's "
                                       "transport."),
              24,
              141 - top,
              Fonts::sans (11),
              colours::dim);
    for (auto* b : { &toolDraw, &toolLine, &toolErase, &guess })
        if (b->enabled != timeline)
        {
            b->enabled = timeline;
            b->repaint();
        }
    if (timeline)
        paintTimeline (g);
    else
        paintSeconds (g);
}

void CurvesView::paintSeconds (juce::Graphics& g)
{
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
        for (int s = (int) std::ceil (t0); s <= (int) windowEnd; ++s)
            if (s % 2 == 0 && s >= 0)
                drawText (g, juce::String (s) + " s", xOf (s) + 3, ny + 66, Fonts::mono (9), colours::dim);
    }

    for (int k = 0; k < o2::laneCount; ++k)
    {
        const auto& lane = lanes[k];
        const float y = laneY (k), lh = laneH;
        g.setColour (colours::panel);
        g.fillRoundedRectangle (24, y, designWidth - 48, lh, 8);
        g.setColour (colours::line);
        g.drawRoundedRectangle (24.5f, y + 0.5f, designWidth - 49, lh - 1, 8, 1);
        drawText (g, lane.name, 40, y + 26, Fonts::sans (14, true), colours::text);
        drawText (g, lane.source, 40, y + 44, Fonts::mono (11), colours::muted);
        const auto m = ModeBadge::displayed (processor, k);
        drawText (g,
                  m == Mode::autoMode     ? "Auto"
                      : m == Mode::guided ? "Guided"
                                          : "Manual",
                  40,
                  y + 66,
                  Fonts::sans (11),
                  modeColour (m));
        const auto plot = plotArea (k);
        g.setColour (colours::well);
        g.fillRoundedRectangle (plot, 5);
        drawText (g, lane.high, x0 + 6, y + 24, Fonts::mono (9), colours::dim);
        drawText (g, lane.low, x0 + 6, y + lh - 16, Fonts::mono (9), colours::dim);
        drawText (g, lane.note, x1 - 10, y + 26, Fonts::sans (10.5f), colours::dim, juce::Justification::right);
        juce::Path p;
        bool pen = false;
        for (const auto& pt : points)
        {
            if (pt.t < t0 || pt.t > windowEnd || ! pt.sounding)
            {
                pen = false;
                continue;
            }
            const float px = xOf (pt.t), py = yOfValue (k, lane.value (pt));
            if (pen)
                p.lineTo (px, py);
            else
                p.startNewSubPath (px, py);
            pen = true;
        }
        g.setColour (colours::amber);
        g.strokePath (p, juce::PathStrokeType (2));
    }
}

void CurvesView::paintTimeline (juce::Graphics& g)
{
    const auto& T = processor.getTelemetry();
    const double b0 = viewStart, b1 = viewStart + viewSpan;
    const double bar = std::max (0.25, (double) T.barLength.load());
    const bool playing = T.playing.load();
    const double playBeat = T.beat.load();

    // the grid: bars, and beats when there is room
    auto grid = [&] (float yTop, float yBottom, bool labels)
    {
        const double beatPx = (x1 - x0) / viewSpan;
        const double step = beatPx >= 12 ? 1.0 : bar;
        for (double b = std::ceil (b0 / step) * step; b <= b1; b += step)
        {
            const bool isBar = std::abs (b / bar - std::round (b / bar)) < 1e-6;
            g.setColour (colours::line.withAlpha (isBar ? 0.9f : 0.35f));
            g.drawVerticalLine (juce::roundToInt (xOfBeat (b)), yTop, yBottom);
            if (labels && isBar)
                drawText (g,
                          juce::String (juce::roundToInt (b / bar) + 1),
                          xOfBeat (b) + 3,
                          yBottom - 4,
                          Fonts::mono (9),
                          colours::dim);
        }
    };

    // notes strip
    const float ny = 160 - top;
    g.setColour (colours::panel);
    g.fillRoundedRectangle (24, ny, designWidth - 48, 70, 8);
    g.setColour (colours::line);
    g.drawRoundedRectangle (24.5f, ny + 0.5f, designWidth - 49, 69, 8, 1);
    drawLabel (g, "Notes", 40, ny + 24, colours::muted, 10);
    drawText (g,
              "bars " + juce::String ((int) std::floor (b0 / bar) + 1) + juce::String::fromUTF8 ("–")
                  + juce::String ((int) std::ceil (b1 / bar)),
              40,
              ny + 42,
              Fonts::sans (11),
              colours::dim);
    drawText (g,
              playing ? juce::String ("following the DAW") : juce::String ("DAW stopped: drag here to scroll"),
              40,
              ny + 58,
              Fonts::sans (9.5f),
              colours::dim);
    int lo = 127, hi = 0;
    for (const auto& n : beatNotes)
        if (n.on < b1 && (n.off < n.on || n.off > b0))
        {
            lo = std::min (lo, n.pitch);
            hi = std::max (hi, n.pitch);
        }
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (juce::Rectangle<float> (x0, ny, x1 - x0, 70).toNearestInt());
        grid (ny + 4, ny + 70, true);
        const float range = (float) std::max (12, hi - lo);
        for (const auto& n : beatNotes)
        {
            const double off = n.off < n.on ? (playing ? playBeat : n.on + 0.25) : n.off;
            if (n.on > b1 || off < b0)
                continue;
            const float xa = xOfBeat (n.on), xb = xOfBeat (off);
            const float y = ny + 52 - (float) (n.pitch - lo) / range * 42;
            g.setColour (colours::gold);
            g.fillRoundedRectangle (xa + 1, y, std::max (2.0f, xb - xa - 2), 7, 2);
        }
    }

    const auto& model = processor.getCurves();
    for (int k = 0; k < o2::laneCount; ++k)
    {
        const auto& lane = lanes[k];
        const float y = laneY (k), lh = laneH;
        const auto m = ModeBadge::displayed (processor, k);
        const auto drawn = model.getLane (k);
        g.setColour (colours::panel);
        g.fillRoundedRectangle (24, y, designWidth - 48, lh, 8);
        g.setColour (k == editLane && gesture != Gesture::none ? colours::muted : colours::line);
        g.drawRoundedRectangle (24.5f, y + 0.5f, designWidth - 49, lh - 1, 8, 1);
        drawText (g, lane.name, 40, y + 26, Fonts::sans (14, true), colours::text);
        drawText (g, lane.source, 40, y + 44, Fonts::mono (11), colours::muted);
        drawText (g,
                  m == Mode::autoMode     ? "Auto"
                      : m == Mode::guided ? "Guided"
                                          : "Manual",
                  40,
                  y + 66,
                  Fonts::sans (11),
                  modeColour (m));
        const bool drawnOff = ! drawn.empty() && processor.getDimMode (k) == o2::modeAuto;
        drawText (g,
                  drawn.empty()  ? juce::String ("nothing drawn")
                      : drawnOff ? juce::String ("curve off (Auto)")
                                 : juce::String ((int) drawn.size()) + " points",
                  40,
                  y + 84,
                  Fonts::sans (10.5f),
                  colours::dim);

        const auto plot = plotArea (k);
        g.setColour (colours::well);
        g.fillRoundedRectangle (plot, 5);
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (plot.toNearestInt());
        grid (plot.getY(), plot.getBottom(), false);
        drawText (g, lane.high, x0 + 6, y + 24, Fonts::mono (9), colours::dim);
        drawText (g, lane.low, x0 + 6, y + lh - 16, Fonts::mono (9), colours::dim);
        const juce::String note = drawn.empty() ? juce::String (lane.note)
            : m == Mode::guided ? juce::String::fromUTF8 ("your curve guides (solid) · what the player played (dashed)")
            : m == Mode::manual
            ? juce::String::fromUTF8 ("your curve, exactly (solid) · what the player played (dashed)")
            : juce::String ("Auto: your curve is ignored; click the badge to use it");
        drawText (g,
                  note,
                  x1 - 10,
                  y + 26,
                  Fonts::sans (10.5f),
                  drawn.empty() ? colours::dim : modeColour (m),
                  juce::Justification::right);

        // what the player played (amber), from the recorded bins in view
        juce::Path played;
        bool pen = false;
        const int i0 = std::max (0, (int) std::floor (b0 * binsPerBeat)),
                  i1 = std::min ((int) bins.size() - 1, (int) std::ceil (b1 * binsPerBeat));
        for (int i = i0; i <= i1; ++i)
        {
            const auto& bin = bins[(size_t) i];
            if (! bin.valid || ! bin.sounding)
            {
                pen = false;
                continue;
            }
            const float px = xOfBeat ((double) i / binsPerBeat), py = yOfValue (k, bin.now[k]);
            if (pen)
                played.lineTo (px, py);
            else
                played.startNewSubPath (px, py);
            pen = true;
        }
        g.setColour (colours::amber);
        if (drawn.empty())
            g.strokePath (played, juce::PathStrokeType (2));
        else
        {
            juce::Path dashed;
            const float dash[] = { 5, 4 };
            juce::PathStrokeType (1.4f).createDashedStroke (dashed, played, dash, 2);
            g.fillPath (dashed);
        }

        // your curve: a line through the points, broken at the breaks
        auto lane2 = drawn;
        if (k == editLane && gesture == Gesture::line)
        {
            // the line being drawn, shown in place
            const double a = std::min (beatOfX (gestureFrom.x), beatOfX (gestureTo.x)),
                         b = std::max (beatOfX (gestureFrom.x), beatOfX (gestureTo.x));
            const float va = valueOfY (k, gestureFrom.x <= gestureTo.x ? gestureFrom.y : gestureTo.y),
                        vb = valueOfY (k, gestureFrom.x <= gestureTo.x ? gestureTo.y : gestureFrom.y);
            lane2.erase (std::remove_if (lane2.begin(),
                                         lane2.end(),
                                         [&] (const CurveModel::Point& p) { return p.beat >= a && p.beat <= b; }),
                         lane2.end());
            lane2.push_back ({ a, va });
            lane2.push_back ({ b, vb });
            std::stable_sort (lane2.begin(),
                              lane2.end(),
                              [] (const auto& p, const auto& q) { return p.beat < q.beat; });
        }
        const juce::Colour curveColour = drawnOff ? colours::muted.withAlpha (0.6f) : modeColour (m);
        juce::Path curve;
        pen = false;
        int visible = 0;
        for (size_t i = 0; i < lane2.size(); ++i)
        {
            const auto& p = lane2[i];
            if (p.v < 0)
            {
                pen = false;
                continue;
            }
            const float px = xOfBeat (p.beat), py = yOfValue (k, p.v);
            if (pen)
                curve.lineTo (px, py);
            else
                curve.startNewSubPath (px, py);
            pen = true;
            if (p.beat >= b0 && p.beat <= b1)
                ++visible;
        }
        g.setColour (curveColour);
        g.strokePath (curve, juce::PathStrokeType (2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        if (visible > 0 && visible < (int) ((x1 - x0) / 8))
            for (size_t i = 0; i < lane2.size(); ++i)
            {
                const auto& p = lane2[i];
                if (p.v < 0 || p.beat < b0 || p.beat > b1)
                    continue;
                const bool active = k == editLane && (int) i == editPoint && gesture == Gesture::point;
                const float r = active ? 4.5f : 3.0f;
                g.setColour (colours::well);
                g.fillEllipse (xOfBeat (p.beat) - r - 1, yOfValue (k, p.v) - r - 1, 2 * r + 2, 2 * r + 2);
                g.setColour (curveColour);
                g.fillEllipse (xOfBeat (p.beat) - r, yOfValue (k, p.v) - r, 2 * r, 2 * r);
            }
        // the part being erased
        if (k == editLane && gesture == Gesture::erase)
        {
            const float a = std::min (gestureFrom.x, gestureTo.x), b = std::max (gestureFrom.x, gestureTo.x);
            g.setColour (colours::coral.withAlpha (0.18f));
            g.fillRect (a, plot.getY(), std::max (2.0f, b - a), plot.getHeight());
        }
        // the playhead
        if (playing && playBeat >= b0 && playBeat <= b1)
        {
            g.setColour (colours::text.withAlpha (0.5f));
            g.drawVerticalLine (juce::roundToInt (xOfBeat (playBeat)), plot.getY(), plot.getBottom());
        }
        // the value under the mouse
        if (laneAt (hover) == k && plot.contains (hover))
        {
            const float v = valueOfY (k, hover.y);
            g.setColour (colours::text.withAlpha (0.25f));
            g.drawHorizontalLine (juce::roundToInt (hover.y), plot.getX(), plot.getRight());
            drawText (g,
                      valueText (k, v) + "  " + juce::String::fromUTF8 ("· bar ")
                          + juce::String ((int) std::floor (beatOfX (hover.x) / bar) + 1),
                      hover.x + 8,
                      hover.y - 6,
                      Fonts::mono (10),
                      colours::text);
        }
    }
    // the playhead across the notes too
    if (playing && playBeat >= b0 && playBeat <= b1)
    {
        g.setColour (colours::text.withAlpha (0.5f));
        g.drawVerticalLine (juce::roundToInt (xOfBeat (playBeat)), ny + 4, ny + 66);
    }
}

// ---------------------------------------------------------------- editing
void CurvesView::pushUndo()
{
    std::array<CurveModel::Lane, o2::laneCount> s;
    for (int k = 0; k < o2::laneCount; ++k)
        s[(size_t) k] = processor.getCurves().getLane (k);
    undoStack.push_back (std::move (s));
    if (undoStack.size() > 40)
        undoStack.erase (undoStack.begin());
}

bool CurvesView::undo()
{
    if (undoStack.empty())
        return false;
    auto s = std::move (undoStack.back());
    undoStack.pop_back();
    for (int k = 0; k < o2::laneCount; ++k)
        processor.getCurves().setLane (k, std::move (s[(size_t) k]));
    repaint();
    return true;
}

void CurvesView::edited (int k)
{
    // drawing a lane hands its dimension to your curve (Guided; Manual stays Manual); a lane left
    // empty gives a Guided dimension back to the player
    const bool has = processor.getCurves().hasCurve (k);
    const int m = processor.getDimMode (k);
    if (has && m == o2::modeAuto)
        processor.setDimMode (k, o2::modeGuided);
    else if (! has && m == o2::modeGuided)
        processor.setDimMode (k, o2::modeAuto);
}

void CurvesView::mouseMove (const juce::MouseEvent& e)
{
    hover = e.position;
}

void CurvesView::mouseExit (const juce::MouseEvent&)
{
    hover = { -1, -1 };
}

void CurvesView::mouseDown (const juce::MouseEvent& e)
{
    gesture = Gesture::none;
    changedInGesture = false;
    const int k = laneAt (e.position);
    const float ny = 160 - top;
    if (e.mods.isPopupMenu())
    {
        if (k >= 0)
            laneMenu (k);
        return;
    }
    if (! onTimeline())
        return;
    if (k < 0)
    {
        if (e.position.y >= ny && e.position.y <= ny + 70 && e.position.x >= x0) // the notes strip pans
        {
            gesture = Gesture::pan;
            panFrom = viewStart;
            gestureFrom = e.position;
        }
        return;
    }
    editLane = k;
    editBase = processor.getCurves().getLane (k);
    gestureFrom = gestureTo = e.position;
    pushUndo();
    if (tool == Tool::erase)
    {
        gesture = Gesture::erase;
        return;
    }
    if (tool == Tool::line || e.mods.isShiftDown())
    {
        gesture = Gesture::line;
        return;
    }
    // on a point: move it
    for (size_t i = 0; i < editBase.size(); ++i)
    {
        const auto& p = editBase[i];
        if (p.v >= 0 && std::abs (xOfBeat (p.beat) - e.position.x) <= 6
            && std::abs (yOfValue (k, p.v) - e.position.y) <= 6)
        {
            gesture = Gesture::point;
            editPoint = (int) i;
            return;
        }
    }
    // freehand
    gesture = Gesture::stroke;
    stroke.clear();
    const double b = std::max (0.0, beatOfX (e.position.x));
    stroke.push_back ({ b, valueOfY (k, e.position.y) });
    strokeLo = strokeHi = lastStrokeBeat = b;
    auto lane = editBase;
    lane.erase (std::remove_if (lane.begin(),
                                lane.end(),
                                [b] (const CurveModel::Point& p) { return std::abs (p.beat - b) < 1e-9; }),
                lane.end());
    lane.push_back ({ b, stroke.back().second });
    processor.getCurves().setLane (k, std::move (lane));
    changedInGesture = true;
}

void CurvesView::mouseDrag (const juce::MouseEvent& e)
{
    hover = e.position;
    gestureTo = e.position;
    const int k = editLane;
    switch (gesture)
    {
        case Gesture::pan:
            viewStart = std::max (0.0, panFrom - (double) ((e.position.x - gestureFrom.x) / (x1 - x0)) * viewSpan);
            follow = false;
            break;
        case Gesture::point:
        {
            auto lane = editBase;
            const size_t i = (size_t) editPoint;
            const double lo = i > 0 ? lane[i - 1].beat + 1e-4 : 0.0;
            const double hi = i + 1 < lane.size() ? lane[i + 1].beat - 1e-4 : 1e9;
            lane[i].beat = juce::jlimit (lo, std::max (lo, hi), beatOfX (e.position.x));
            lane[i].v = valueOfY (k, e.position.y);
            processor.getCurves().setLane (k, std::move (lane));
            changedInGesture = true;
            break;
        }
        case Gesture::stroke:
        {
            const double b = std::max (0.0, beatOfX (e.position.x));
            const float v = valueOfY (k, e.position.y);
            // moving back over the stroke redraws that part
            const double a = std::min (lastStrokeBeat, b), c = std::max (lastStrokeBeat, b);
            stroke.erase (std::remove_if (stroke.begin(),
                                          stroke.end(),
                                          [&] (const auto& s)
                                          { return s.first > a && s.first < c && s.first != lastStrokeBeat; }),
                          stroke.end());
            if (std::abs (b - lastStrokeBeat) > 1e-9)
            {
                stroke.erase (std::remove_if (stroke.begin(),
                                              stroke.end(),
                                              [b] (const auto& s) { return std::abs (s.first - b) < 1e-9; }),
                              stroke.end());
                stroke.push_back ({ b, v });
            }
            lastStrokeBeat = b;
            strokeLo = std::min (strokeLo, b);
            strokeHi = std::max (strokeHi, b);
            auto lane = editBase;
            lane.erase (std::remove_if (lane.begin(),
                                        lane.end(),
                                        [this] (const CurveModel::Point& p)
                                        { return p.beat >= strokeLo - 1e-9 && p.beat <= strokeHi + 1e-9; }),
                        lane.end());
            for (const auto& s : stroke)
                lane.push_back ({ s.first, s.second });
            processor.getCurves().setLane (k, std::move (lane));
            changedInGesture = true;
            break;
        }
        default:
            break;
    }
    repaint();
}

void CurvesView::mouseUp (const juce::MouseEvent& e)
{
    const int k = editLane;
    if (gesture == Gesture::line && k >= 0)
    {
        const auto a = gestureFrom.x <= e.position.x ? gestureFrom : e.position;
        const auto b = gestureFrom.x <= e.position.x ? e.position : gestureFrom;
        const double ba = std::max (0.0, beatOfX (a.x)), bb = std::max (0.0, beatOfX (b.x));
        auto lane = editBase;
        lane.erase (std::remove_if (lane.begin(),
                                    lane.end(),
                                    [&] (const CurveModel::Point& p) { return p.beat >= ba && p.beat <= bb; }),
                    lane.end());
        lane.push_back ({ ba, valueOfY (k, a.y) });
        if (bb - ba > 1e-6)
            lane.push_back ({ bb, valueOfY (k, b.y) });
        processor.getCurves().setLane (k, std::move (lane));
        changedInGesture = true;
    }
    else if (gesture == Gesture::erase && k >= 0)
    {
        double ba = beatOfX (std::min (gestureFrom.x, e.position.x)),
               bb = beatOfX (std::max (gestureFrom.x, e.position.x));
        if (bb - ba < viewSpan / 400) // a click erases the points near it
        {
            ba -= viewSpan / 200;
            bb += viewSpan / 200;
        }
        auto lane = editBase;
        const bool before
            = std::any_of (lane.begin(), lane.end(), [&] (const auto& p) { return p.beat < ba && p.v >= 0; });
        const bool after
            = std::any_of (lane.begin(), lane.end(), [&] (const auto& p) { return p.beat > bb && p.v >= 0; });
        lane.erase (std::remove_if (lane.begin(),
                                    lane.end(),
                                    [&] (const CurveModel::Point& p) { return p.beat >= ba && p.beat <= bb; }),
                    lane.end());
        if (before && after) // a gap: the player decides there
            lane.push_back ({ 0.5 * (ba + bb), -1.0f });
        changedInGesture = lane.size() != editBase.size() || before != after;
        processor.getCurves().setLane (k, std::move (lane));
    }
    if (gesture != Gesture::none && gesture != Gesture::pan && k >= 0)
    {
        if (changedInGesture)
            edited (k);
        else if (! undoStack.empty())
            undoStack.pop_back();
    }
    gesture = Gesture::none;
    editPoint = -1;
    repaint();
}

void CurvesView::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int k = laneAt (e.position);
    if (k < 0 || ! onTimeline())
        return;
    auto lane = processor.getCurves().getLane (k);
    for (size_t i = 0; i < lane.size(); ++i)
        if (lane[i].v >= 0 && std::abs (xOfBeat (lane[i].beat) - e.position.x) <= 6
            && std::abs (yOfValue (k, lane[i].v) - e.position.y) <= 8)
        {
            pushUndo();
            lane.erase (lane.begin() + (std::ptrdiff_t) i);
            processor.getCurves().setLane (k, std::move (lane));
            edited (k);
            return;
        }
}

void CurvesView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (! onTimeline())
        return;
    const float delta = std::abs (w.deltaX) > std::abs (w.deltaY) ? w.deltaX : w.deltaY;
    if (e.mods.isCtrlDown() || e.mods.isCommandDown())
    {
        // zoom round the mouse
        const double at = beatOfX (e.position.x);
        const double spanNew = juce::jlimit (1.0, 512.0, viewSpan * std::pow (0.5, (double) delta * 2.0));
        viewStart = std::max (0.0, at - (at - viewStart) * spanNew / viewSpan);
        viewSpan = spanNew;
    }
    else
        viewStart = std::max (0.0, viewStart - (double) delta * viewSpan * 0.25);
    follow = false;
    repaint();
}

bool CurvesView::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0))
        return undo();
    return false;
}

void CurvesView::laneMenu (int k)
{
    const auto m = processor.getDimMode (k);
    const bool any = std::any_of (bins.begin(), bins.end(), [] (const Bin& b) { return b.valid && b.sounding; });
    juce::PopupMenu menu;
    menu.addSectionHeader (lanes[k].name);
    menu.addItem (1, "Fill from the player's last playback (Guess)", onTimeline() && any);
    menu.addItem (2, "Clear this lane", processor.getCurves().hasCurve (k));
    menu.addItem (3, "Clear all lanes");
    menu.addItem (4, "Undo", ! undoStack.empty());
    menu.addSectionHeader ("Who is in charge");
    menu.addItem (11, "Auto: the player decides (your curve is ignored)", true, m == o2::modeAuto);
    menu.addItem (12, "Guided: your curve, the player shapes around it", true, m == o2::modeGuided);
    menu.addItem (13, "Manual: exactly your curve", true, m == o2::modeManual);
    menu.addSectionHeader ("Tool");
    menu.addItem (21, "Draw", true, tool == Tool::draw);
    menu.addItem (22, "Line", true, tool == Tool::line);
    menu.addItem (23, "Erase", true, tool == Tool::erase);
    juce::Component::SafePointer<CurvesView> safe (this);
    menu.showMenuAsync (
        juce::PopupMenu::Options().withTargetScreenArea (
            juce::Rectangle<int> (juce::Desktop::getMousePosition(), juce::Desktop::getMousePosition()).expanded (1)),
        [safe, k] (int r)
        {
            if (safe == nullptr || r <= 0)
                return;
            auto& v = *safe;
            if (r == 1)
                v.guessCurves (k);
            else if (r == 2 || r == 3)
            {
                v.pushUndo();
                for (int j = 0; j < o2::laneCount; ++j)
                    if (r == 3 || j == k)
                    {
                        v.processor.getCurves().clearLane (j);
                        v.edited (j);
                    }
            }
            else if (r == 4)
                v.undo();
            else if (r >= 11 && r <= 13)
                v.processor.setDimMode (k, r - 11);
            else if (r >= 21 && r <= 23)
                v.setTool (r == 21 ? Tool::draw : r == 22 ? Tool::line : Tool::erase);
            v.repaint();
        });
}

bool CurvesView::guessCurves (int only)
{
    update();
    int first = -1, last = -1;
    for (int i = 0; i < (int) bins.size(); ++i)
        if (bins[(size_t) i].valid && bins[(size_t) i].sounding)
        {
            if (first < 0)
                first = i;
            last = i;
        }
    if (first < 0)
        return false;
    pushUndo();
    const double ba = (double) first / binsPerBeat, bb = (double) last / binsPerBeat;
    for (int k = 0; k < o2::laneCount; ++k)
    {
        if (only >= 0 && k != only)
            continue;
        // the player's level before its own shaping, across the rests too (held), thinned
        CurveModel::Lane fresh;
        for (int i = first; i <= last; ++i)
        {
            const auto& bin = bins[(size_t) i];
            if (bin.valid && bin.sounding)
                fresh.push_back ({ (double) i / binsPerBeat, bin.base[k] });
        }
        fresh = simplify (fresh, 0.006f);
        auto lane = processor.getCurves().getLane (k);
        lane.erase (std::remove_if (lane.begin(),
                                    lane.end(),
                                    [&] (const CurveModel::Point& p) { return p.beat >= ba && p.beat <= bb; }),
                    lane.end());
        lane.insert (lane.end(), fresh.begin(), fresh.end());
        processor.getCurves().setLane (k, std::move (lane));
        edited (k);
    }
    if (! follow || ! processor.getTelemetry().playing.load())
        viewStart = std::max (0.0, std::floor (ba) - 1.0);
    repaint();
    return true;
}

// ---------------------------------------------------------------- export as MIDI
juce::File CurvesView::writeMidi (const juce::File& file)
{
    update();
    return onTimeline() ? writeMidiBeats (file) : writeMidiSeconds (file);
}

namespace
{
juce::File writeSequence (juce::MidiMessageSequence& seq, const juce::File& file)
{
    seq.sort();
    seq.updateMatchedPairs();
    juce::MidiFile midi;
    midi.setTicksPerQuarterNote (960);
    midi.addTrack (seq);
    file.deleteFile();
    juce::FileOutputStream out (file);
    if (! out.openedOk() || ! midi.writeTo (out))
        return {};
    out.flush();
    return file;
}
} // namespace

// the shown region, from the bar of its first note: the notes, and per lane your curve (when it
// is in charge) or else the player's own (as Guess curves would draw it). Dropped at that bar,
// it plays back as it was played.
juce::File CurvesView::writeMidiBeats (const juce::File& file)
{
    const double b0 = viewStart, b1 = viewStart + viewSpan;
    std::vector<BeatNote> notes;
    for (const auto& n : beatNotes)
        if (n.on >= b0 && n.on < b1)
            notes.push_back (n);
    if (notes.empty())
        return {};
    std::sort (notes.begin(), notes.end(), [] (const auto& a, const auto& b) { return a.on < b.on; });
    const double bar = std::max (0.25, (double) processor.getTelemetry().barLength.load());
    const double start = std::floor (notes.front().on / bar) * bar;
    constexpr int ppq = 960;
    auto tick = [&] (double b) { return std::max (0.0, (b - start) * ppq); };
    juce::MidiMessageSequence seq;
    seq.addEvent (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60.0e6 / anchorBpm)), 0.0);
    seq.addEvent (juce::MidiMessage::textMetaEvent (1,
                                                    "Octavio 2 curves from bar "
                                                        + juce::String ((int) std::round (start / bar) + 1)),
                  0.0);
    for (size_t i = 0; i < notes.size(); ++i)
    {
        const auto& n = notes[i];
        double off = n.off < n.on ? std::min (b1, n.on + 1.0) : n.off;
        // a slurred note overlaps the one before it, so playing the file back slurs it again
        if (i + 1 < notes.size() && std::abs (off - notes[i + 1].on) < 0.01)
            off = notes[i + 1].on + 0.04;
        seq.addEvent (juce::MidiMessage::noteOn (1, n.pitch, (juce::uint8) 100), tick (n.on));
        seq.addEvent (juce::MidiMessage::noteOff (1, n.pitch), tick (std::max (off, n.on + 0.02)));
    }
    const auto& model = processor.getCurves();
    for (int k = 0; k < o2::laneCount; ++k)
    {
        const auto lane = model.getLane (k);
        const bool yours = ! lane.empty() && processor.getDimMode (k) != o2::modeAuto;
        auto flat = std::make_unique<o2::CurveLane>(); // the drawn lane, for its values between points
        int hint = -1;
        if (yours)
        {
            flat->n = std::min ((int) lane.size(), o2::CurveLane::maxPoints);
            for (int i = 0; i < flat->n; ++i)
            {
                flat->ppq[i] = lane[(size_t) i].beat;
                flat->v[i] = lane[(size_t) i].v;
            }
        }
        int last = -1;
        for (int i = std::max (0, (int) std::floor (start * binsPerBeat)); i <= (int) std::ceil (b1 * binsPerBeat); ++i)
        {
            const double b = (double) i / binsPerBeat;
            float v = -1;
            if (yours)
                v = flat->at (b, hint);
            if (v < 0 && i < (int) bins.size() && bins[(size_t) i].valid && bins[(size_t) i].sounding)
                v = bins[(size_t) i].base[k];
            if (v < 0)
                continue;
            const int cc = juce::jlimit (0, 127, juce::roundToInt (v * 127.0f));
            if (cc != last)
            {
                last = cc;
                seq.addEvent (juce::MidiMessage::controllerEvent (1, o2::laneCc[k], cc), tick (b));
            }
        }
    }
    return writeSequence (seq, file);
}

// without a host timeline: the last 16 s, as 2.2 (the player's levels before its shaping)
juce::File CurvesView::writeMidiSeconds (const juce::File& file)
{
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
        if (i + 1 < notes.size() && notes[i + 1].slur && std::abs (off - notes[i + 1].on) < 0.005)
            off = notes[i + 1].on + 0.02;
        seq.addEvent (juce::MidiMessage::noteOn (1, n.pitch, (juce::uint8) 100), tick (n.on));
        seq.addEvent (juce::MidiMessage::noteOff (1, n.pitch), tick (std::max (off, n.on + 0.01)));
    }
    int last[o2::laneCount] = { -1, -1, -1, -1 };
    for (const auto& pt : points)
    {
        if (! pt.sounding || pt.t < start - 0.05 || pt.t > t1)
            continue;
        for (int k = 0; k < o2::laneCount; ++k)
        {
            const int v = juce::jlimit (0, 127, juce::roundToInt (lanes[k].base (pt) * 127.0f));
            if (v != last[k])
            {
                last[k] = v;
                seq.addEvent (juce::MidiMessage::controllerEvent (1, o2::laneCc[k], v), tick (pt.t));
            }
        }
    }
    return writeSequence (seq, file);
}

CurvesView::DragButton::DragButton (CurvesView& v)
    : view (v)
{
    setTooltip ("Drag the shown region into your DAW as a MIDI file: the notes, CC1 dynamics, CC26 vibrato width, "
                "CC19 vibrato rate, CC74 contact point (your curves where you drew, else the player's). It starts at "
                "the bar of its first note. Click to save it in Documents/Octavio 2.");
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
