#include "LeftHandView.h"
#include "PlayerText.h"

namespace octavio2::ui
{
namespace
{
constexpr double span = 6.0; // s of history shown
constexpr float topSemis = 24; // the position lane's range above the open string

std::vector<Choices::Item> planItems()
{
    return { { "As it comes" }, { "Planned (Studio)" } };
}
} // namespace

LeftHandView::LeftHandView (Processor& p)
    : processor (p),
      fingerPlan (&p.getParameters(), params::id::fingerPlan.getParamID(), planItems()),
      stringPreference (&p.getParameters(),
                        params::id::stringPreference.getParamID(),
                        "String pref.",
                        Knob::Style::small,
                        stringPreferenceText),
      portamento (&p.getParameters(),
                  params::id::portamento.getParamID(),
                  "Portamento",
                  Knob::Style::small,
                  portamentoText),
      width (&p.getParameters(),
             params::id::vibrato.getParamID(),
             "Width",
             Knob::Style::small,
             [] (float v) { return juce::String (juce::roundToInt (v * 100)) + " %"; }),
      rate (&p.getParameters(),
            params::id::vibratoRate.getParamID(),
            "Rate",
            Knob::Style::small,
            [] (float v) {
                return std::abs (v) < 0.05f ? juce::String ("as played")
                                            : (v > 0 ? "+" : "") + juce::String (v, 1) + " Hz";
            }),
      delay (&p.getParameters(),
             params::id::vibratoDelay.getParamID(),
             "Delay",
             Knob::Style::small,
             [] (float v) { return juce::String (juce::roundToInt (v)) + " %"; })
{
    fingerPlan.setLayout (2, 30, 6);
    fingerPlan.setTooltip ("As it comes: each note takes the easiest string from where the hand is. Planned: in "
                           "Studio mode the player looks ahead and plans strings and shifts for the coming notes.");
    stringPreference.setTooltip (
        "String preference. Bright: low positions and higher strings. Dark: high positions on the lower strings.");
    portamento.setTooltip ("How slowly the finger slides when the hand shifts position. 0: clean shifts.");
    width.setTooltip ("Vibrato width, a scale on the player's context vibrato (100 %: as played).");
    rate.setTooltip ("Added to every note's vibrato rate (the player varies it with dynamics and register).");
    delay.setTooltip ("How long a note waits before its vibrato starts, and how long it takes to bloom.");
    addAndMakeVisible (fingerPlan);
    for (auto* k : { &stringPreference, &portamento, &width, &rate, &delay })
        addAndMakeVisible (k);
    addAndMakeVisible (widthBadge); // 2.3
    addAndMakeVisible (rateBadge);
    startTimerHz (30);
}

void LeftHandView::resized()
{
    fingerPlan.setBounds (856, 214 + 56, 304, 30);
    stringPreference.setBounds (856 + 30, 214 + 100, 100, 96);
    portamento.setBounds (856 + 174, 214 + 100, 100, 96);
    int i = 0;
    for (auto* k : { &width, &rate, &delay })
        k->setBounds (858 + 102 * i++, 422 + 38, 100, 96);
    widthBadge.placeAt (width.getPosition().toFloat() + width.badgeCentre()); // 2.3
    rateBadge.placeAt (rate.getPosition().toFloat() + rate.badgeCentre());
}

void LeftHandView::timerCallback()
{
    if (! isShowing())
        return;
    trace.read (processor.getEngine());
    repaint();
}

void LeftHandView::paint (juce::Graphics& g)
{
    trace.read (processor.getEngine());
    paintHistory (g, { 24, 12, 800, 576 });
    paintNow (g, { 840, 12, 336, 190 });
    drawPanel (g, { 840, 214, 336, 196 }, "Fingering", "strings and shifts");
    drawLabel (g, "String choice", 856, 214 + 48);
    drawPanel (g, { 840, 422, 336, 166 }, "Vibrato", "A / G / M: click a badge to guide it");
    drawText (g, "Intonation and tuning are on the Tone tab.", 856, 422 + 150, Fonts::sans (11.5f), colours::dim);
}

void LeftHandView::paintHistory (juce::Graphics& g, juce::Rectangle<float> r)
{
    const double t1 = trace.windowEnd, t0 = t1 - span;
    const float x0 = r.getX() + 110, x1 = r.getRight() - 18;
    auto xAt = [&] (double t) { return x0 + (float) ((t - t0) / span) * (x1 - x0); };
    int shifts = 0;
    {
        bool was = false;
        for (const auto& p : trace.points)
            if (p.t >= t0 && p.t <= t1)
            {
                if (p.sliding && ! was)
                    ++shifts;
                was = p.sliding;
            }
    }
    drawPanel (g,
               r,
               juce::String::fromUTF8 ("The left hand · last 6 s"),
               juce::String (shifts) + (shifts == 1 ? " shift" : " shifts"));

    // strings: E at the top, a bar per note with its finger
    const float rowH = 26, sy = r.getY() + 40;
    const juce::Rectangle<float> strings (x0, sy, x1 - x0, 4 * rowH);
    g.setColour (colours::well);
    g.fillRoundedRectangle (strings, 5);
    g.setColour (colours::line);
    g.drawRoundedRectangle (strings, 5, 1);
    drawLabel (g, "String", r.getX() + 18, sy + 14);
    drawText (g, "and finger", r.getX() + 18, sy + 29, Fonts::sans (10.5f), colours::dim);
    for (int s = 0; s < 4; ++s)
    {
        const float ly = sy + (3 - s) * rowH;
        drawText (g,
                  juce::String::charToString (stringLetters[s]),
                  x0 - 10,
                  ly + 18,
                  Fonts::serif (14),
                  colours::muted,
                  juce::Justification::right);
        if (s < 3)
        {
            g.setColour (colours::line.withAlpha (0.6f));
            g.drawLine (x0 + 4, ly + rowH, x1 - 4, ly + rowH, 0.6f);
        }
    }
    g.saveState();
    g.reduceClipRegion (strings.toNearestInt());
    {
        // runs of the same string and note while sounding
        size_t i = 0;
        const auto& P = trace.points;
        while (i < P.size())
        {
            if (! P[i].sounding || P[i].t > t1)
            {
                ++i;
                continue;
            }
            size_t j = i;
            while (j + 1 < P.size() && P[j + 1].sounding && P[j + 1].string == P[i].string
                   && P[j + 1].target == P[i].target)
                ++j;
            if (P[j].t >= t0)
            {
                const int s = juce::jlimit (0, 3, P[i].string);
                const float bx = xAt (P[i].t), bw = std::max (3.0f, xAt (P[j].t + 0.005) - bx);
                const float by = sy + (3 - s) * rowH + 4;
                const double semis = P[i].target - openPitches[s];
                const int finger = fingerOf (semis, P[j].handPos);
                g.setColour (colours::gold.withAlpha (0.85f));
                g.fillRoundedRectangle (bx + 1, by, bw - 2, rowH - 8, 4);
                const auto text = noteName (juce::roundToInt (P[i].target)) + " " + juce::String (finger);
                if (juce::GlyphArrangement::getStringWidth (Fonts::sans (10, true), text) < bw - 8)
                    drawText (g, text, bx + 5, by + 13, Fonts::sans (10, true), colours::bg);
            }
            i = j + 1;
        }
    }
    g.restoreState();

    // positions: the hand's reach (first finger stretched back to the fourth) and the finger
    const juce::Rectangle<float> pos (x0, r.getY() + 166, x1 - x0, 190);
    drawLane (g, pos, "Position", "the hand's reach,", {}, {});
    drawText (g, "the finger, shifts", pos.getX() - 92, pos.getY() + 43, Fonts::sans (10.5f), colours::dim);
    auto yOf = [&] (float semis)
    { return pos.getBottom() - 4 - juce::jlimit (0.0f, topSemis, semis) / topSemis * (pos.getHeight() - 8); };
    const float dash[] = { 2, 4 };
    for (auto [name, semis] : { std::pair { "1st", 2 }, { "3rd", 5 }, { "5th", 9 }, { "7th", 12 }, { "9th", 15 } })
    {
        const float y = yOf ((float) semis);
        g.setColour (colours::line);
        g.drawDashedLine ({ pos.getX() + 2, y, pos.getRight() - 2, y }, dash, 2);
        drawText (g, name, pos.getX() - 8, y + 4, Fonts::mono (9.5f), colours::dim, juce::Justification::right);
    }
    g.saveState();
    g.reduceClipRegion (pos.reduced (1).toNearestInt());
    {
        // the reach, as a band
        juce::Path band, finger, slide;
        bool bandOpen = false;
        std::vector<juce::Point<float>> lo;
        auto closeBand = [&]
        {
            for (auto it = lo.rbegin(); it != lo.rend(); ++it)
                band.lineTo (*it);
            if (bandOpen)
                band.closeSubPath();
            lo.clear();
            bandOpen = false;
        };
        bool fingerOpen = false, slideOpen = false;
        for (const auto& p : trace.points)
        {
            if (p.t < t0 - 0.05 || p.t > t1 + 0.05)
                continue;
            const float x = xAt (p.t);
            if (! p.sounding)
            {
                closeBand();
                fingerOpen = slideOpen = false;
                continue;
            }
            const float hi = yOf (p.handPos + 5), loY = yOf (std::max (0.0f, p.handPos - 1));
            if (! bandOpen)
            {
                band.startNewSubPath (x, hi);
                bandOpen = true;
            }
            else
                band.lineTo (x, hi);
            lo.push_back ({ x, loY });
            const int s = juce::jlimit (0, 3, p.string);
            const float fy = yOf ((float) (p.pitch - openPitches[s]));
            if (! fingerOpen)
                finger.startNewSubPath (x, fy);
            else
                finger.lineTo (x, fy);
            fingerOpen = true;
            if (p.sliding)
            {
                if (! slideOpen)
                    slide.startNewSubPath (x, fy);
                else
                    slide.lineTo (x, fy);
            }
            slideOpen = p.sliding;
        }
        closeBand();
        g.setColour (colours::steel.withAlpha (0.13f));
        g.fillPath (band);
        g.setColour (colours::gold);
        g.strokePath (finger, juce::PathStrokeType (1.6f));
        g.setColour (colours::steel);
        g.strokePath (slide, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.restoreState();

    // vibrato: the finger's swing around the note, in cents
    const juce::Rectangle<float> vib (x0, r.getY() + 376, x1 - x0, 104);
    drawLane (g, vib, "Vibrato", "cents", "+40", "-40");
    g.setColour (colours::line);
    g.drawDashedLine ({ vib.getX() + 2, vib.getCentreY(), vib.getRight() - 2, vib.getCentreY() }, dash, 2);
    g.saveState();
    g.reduceClipRegion (vib.reduced (1).toNearestInt());
    {
        juce::Path path;
        bool open = false;
        for (const auto& p : trace.points)
        {
            if (p.t < t0 - 0.05 || p.t > t1 + 0.05)
                continue;
            if (! p.sounding || p.sliding)
            {
                open = false;
                continue;
            }
            const float cents = juce::jlimit (-40.0f, 40.0f, (p.pitch - p.target) * 100.0f);
            const float x = xAt (p.t), y = vib.getCentreY() - cents / 40.0f * (vib.getHeight() / 2 - 4);
            if (! open)
                path.startNewSubPath (x, y);
            else
                path.lineTo (x, y);
            open = true;
        }
        g.setColour (colours::amber);
        g.strokePath (path, juce::PathStrokeType (1.3f));
    }
    g.restoreState();
    drawTimeAxis (g, x0, x1, vib.getBottom() + 18, span);

    // legend
    const float ly = r.getBottom() - 20;
    float lx = x0;
    auto key = [&] (juce::Colour c, const juce::String& text, float h)
    {
        g.setColour (c);
        g.fillRoundedRectangle (lx, ly - 4 - h / 2, 14, h, 1);
        drawText (g, text, lx + 20, ly, Fonts::sans (11), colours::muted);
        lx += 20 + juce::GlyphArrangement::getStringWidth (Fonts::sans (11), text) + 22;
    };
    key (colours::steel.withAlpha (0.35f), "hand's reach (1st to 4th finger)", 9);
    key (colours::gold, "finger", 2);
    key (colours::steel, "shift (slide)", 3);
    key (colours::amber, "vibrato", 2);
}

void LeftHandView::paintNow (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto& T = processor.getTelemetry();
    const float x = r.getX(), y = r.getY(), w = r.getWidth();
    const int note = T.note.load();
    const bool sounding = note >= 0;
    const int s = juce::jlimit (0, 3, T.string.load());
    const float hp = T.handPos.load();
    const double semis = T.target.load() - openPitches[s];
    const int finger = fingerOf (semis, hp);
    drawPanel (g, r, "The left hand now", sounding ? noteName (note) : juce::String ("not playing"));
    drawText (g,
              sounding ? juce::String::charToString (stringLetters[s]) : juce::String ("-"),
              x + 18,
              y + 84,
              Fonts::serif (50),
              sounding ? colours::gold : colours::dim);
    drawText (g, "string", x + 22, y + 102, Fonts::sans (10.5f), colours::dim);
    drawText (g, sounding ? positionName (hp) : juce::String ("-"), x + 82, y + 62, Fonts::serif (20), colours::text);
    drawText (g,
              ! sounding        ? juce::String()
                  : finger == 0 ? juce::String ("open string")
                                : "finger " + juce::String (finger),
              x + 82,
              y + 84,
              Fonts::sans (12.5f),
              colours::muted);
    const double now = processor.getEngine().seconds();
    // the last shift in the trace: where the hand was before the slide, and where it went
    juce::String shift = "none lately";
    const auto& P = trace.points;
    for (size_t i = P.size(); i-- > 1;)
        if (P[i].sliding && ! P[i - 1].sliding)
        {
            shift = ordinal (positionOf (P[i - 1].handPos)) + juce::String::fromUTF8 (" → ")
                + ordinal (positionOf (P[i].handPos)) + ", " + juce::String (std::max (0.0, now - P[i].t), 1)
                + " s ago";
            break;
        }
    if (T.sliding.load() && sounding)
        shift = "shifting to " + ordinal (positionOf (hp));
    const float vw = T.vibWidth.load(), vr = T.vibRate.load();
    const juce::String vib = ! sounding ? juce::String ("-")
        : finger == 0                   ? juce::String ("none (open string)")
        : vw < 1
        ? juce::String ("waiting to bloom")
        : juce::String (vr, 1) + juce::String::fromUTF8 (" Hz · ") + juce::String (juce::roundToInt (vw)) + " ct";
    struct Row
    {
        const char* label;
        juce::String value;
    };
    const Row rows[] = { { "Last shift", shift }, { "Vibrato", vib } };
    float ry = y + 140;
    for (const auto& row : rows)
    {
        g.setColour (colours::line);
        g.drawLine (x + 16, ry - 15, x + w - 16, ry - 15);
        drawText (g, row.label, x + 16, ry + 2, Fonts::sans (12), colours::muted);
        drawText (g, row.value, x + w - 16, ry + 2, Fonts::sans (12.5f), colours::text, juce::Justification::right);
        ry += 24;
    }
}
} // namespace octavio2::ui
