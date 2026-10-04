#pragma once

#include "../../core/Engine.h"
#include "Theme.h"

#include <algorithm>
#include <vector>

namespace octavio2::ui
{
// The engine's bow and left-hand trace (o2::Engine::Trace, every 5 ms), read on the message
// thread for the Bow and Left hand tabs. Keeps the last `keep` seconds of engine time.
struct TraceTrack
{
    void read (const o2::Engine& engine, double keep = 12.0)
    {
        const auto written = engine.traceCount();
        if (written - traceRead > o2::Engine::traceSize - 64)
            traceRead = written - (o2::Engine::traceSize - 64);
        for (; traceRead < written; ++traceRead)
        {
            const auto& e = engine.traceEntry (traceRead);
            points.push_back (e);
            if (e.sounding)
                lastSounding = e.t;
        }
        if (! points.empty())
        {
            const float cut = points.back().t - (float) keep;
            points.erase (
                points.begin(),
                std::find_if (points.begin(), points.end(), [cut] (const o2::Engine::Trace& p) { return p.t >= cut; }));
        }
        // the window ends now while playing, and stays on the last phrase after it stops
        const double now = engine.seconds();
        windowEnd = lastSounding > -1e8 ? std::min (now, (double) lastSounding + 1.5) : now;
    }

    std::vector<o2::Engine::Trace> points;
    uint64_t traceRead = 0;
    float lastSounding = -1e9f;
    double windowEnd = 0;
};

// A plot lane of the Bow and Left hand tabs: a dark well with a label to its left.
inline void drawLane (juce::Graphics& g,
                      juce::Rectangle<float> r,
                      const juce::String& label,
                      const juce::String& unit,
                      const juce::String& topText = {},
                      const juce::String& bottomText = {})
{
    g.setColour (colours::well);
    g.fillRoundedRectangle (r, 5);
    g.setColour (colours::line);
    g.drawRoundedRectangle (r, 5, 1);
    drawLabel (g, label, r.getX() - 92, r.getY() + 14);
    if (unit.isNotEmpty())
        drawText (g, unit, r.getX() - 92, r.getY() + 29, Fonts::sans (10.5f), colours::dim);
    // the scale, inside the well's left edge
    if (topText.isNotEmpty())
        drawText (g, topText, r.getX() + 6, r.getY() + 13, Fonts::mono (9.5f), colours::dim);
    if (bottomText.isNotEmpty())
        drawText (g, bottomText, r.getX() + 6, r.getBottom() - 5, Fonts::mono (9.5f), colours::dim);
}

// Seconds ticks under the lanes: "now" at the right edge, one tick a second.
inline void drawTimeAxis (juce::Graphics& g, float x0, float x1, float y, double span)
{
    for (int k = 0; k <= (int) span; ++k)
    {
        const float x = x1 - (float) (k / span) * (x1 - x0);
        g.setColour (colours::line);
        g.drawLine (x, y - 6, x, y - 2, 1);
        drawText (g,
                  k == 0 ? juce::String ("now") : "-" + juce::String (k) + " s",
                  x,
                  y + 10,
                  Fonts::mono (9.5f),
                  colours::dim,
                  juce::Justification::horizontallyCentred);
    }
}
} // namespace octavio2::ui
