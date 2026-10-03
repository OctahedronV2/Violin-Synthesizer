#include "PlayView.h"

namespace octavio2::ui
{
namespace
{
constexpr float top = 104; // the views start below the header (mockup y 104)
constexpr double openPitch[4] = { 55, 62, 69, 76 };
const char* stringNames = "GDAE";

juce::String ordinal (int n)
{
    static const char* s[] = { "th", "st", "nd", "rd" };
    const int k = (n % 100 >= 11 && n % 100 <= 13) || n % 10 > 3 ? 0 : n % 10;
    return juce::String (n) + s[k];
}

// the position a hand sits in, from where the first finger is (semitones above the open string)
int positionOf (float handPos)
{
    static const int firstFinger[] = { 0, 2, 4, 5, 7, 9, 10, 12, 14, 15, 17, 19 }; // half, 1st .. 11th
    int best = 1;
    for (int p = 1; p < 12; ++p)
        if (std::abs (firstFinger[p] - handPos) < std::abs (firstFinger[best] - handPos))
            best = p;
    return best;
}

juce::String contactName (float beta)
{
    return beta < 0.06f ? "sul ponticello side" : beta > 0.16f ? "sul tasto side" : "normal";
}

void line (juce::Graphics& g, float x0, float y0, float x1, float y1, juce::Colour c, float w = 1)
{
    g.setColour (c);
    g.drawLine (x0, y0, x1, y1, w);
}
} // namespace

PlayView::PlayView (Processor& p)
    : processor (p),
      dynamics (&p.getParameters(),
                params::id::dynamics.getParamID(),
                "Dynamics",
                Knob::Style::card,
                [] (float v)
                {
                    return std::abs (v) < 0.5f ? juce::String ("as played")
                                               : (v > 0 ? "+" : "") + juce::String (juce::roundToInt (v)) + " %";
                }),
      expression (nullptr, {}, "Expression", Knob::Style::card),
      vibrato (&p.getParameters(),
               params::id::vibrato.getParamID(),
               "Vibrato",
               Knob::Style::card,
               [] (float v) { return juce::String (juce::roundToInt (v * 100)) + " %"; }),
      portamento (nullptr, {}, "Portamento", Knob::Style::card),
      stringPreference (nullptr, {}, "String preference", Knob::Style::card),
      room (&p.getParameters(),
            params::id::room.getParamID(),
            "Room",
            Knob::Style::card,
            [] (float v)
            {
                static const char* shortNames[]
                    = { "Close mics",   "Arvedi near",  "Arvedi far",  "Brahmssaal", "Church",
                        "Maida Vale 4", "Maida Vale 5", "WDR control", "WDR studio" };
                return juce::String (shortNames[juce::jlimit (0, 8, juce::roundToInt (v))]);
            }),
      articulation (nullptr,
                    {},
                    { { "Auto" },
                      { "Legato" },
                      { juce::String::fromUTF8 ("Détaché") },
                      { juce::String::fromUTF8 ("Martelé") },
                      { "Staccato" },
                      { "Spiccato" },
                      { juce::String::fromUTF8 ("Sautillé") },
                      { "Tremolo" },
                      { "Pizz" },
                      { "Harmonics" },
                      { "Pont." },
                      { "Tasto" },
                      { "Sordino" } })
{
    dynamics.setCaption ("Level, from velocity");
    dynamics.setBadge (Mode::autoMode);
    expression.setCaption ("How much the player shapes");
    expression.setPreview (0.55f, "55 %", "the player milestone (M4)");
    vibrato.setCaption ("Width scale on context vibrato");
    vibrato.setBadge (Mode::autoMode);
    portamento.setCaption ("Slides between positions");
    portamento.setPreview (0.22f, "Rare", "the player milestone (M4)");
    stringPreference.setCaption (juce::String::fromUTF8 ("Bright ←  → dark"));
    stringPreference.setPreview (0.5f, "Balanced", "the player milestone (M4)");
    room.setCaption ("Room and distance");
    for (auto* k : { &dynamics, &expression, &vibrato, &portamento, &stringPreference, &room })
        addAndMakeVisible (k);
    articulation.setLayout (0, 30, 4);
    articulation.setTooltip ("The player picks the articulation from how you play (Auto). Choosing one arrives with "
                             "the player milestone (M4).");
    addAndMakeVisible (articulation);
    startTimerHz (30);
}

bool PlayView::studio() const
{
    return processor.getParameters().getRawParameterValue (params::id::mode.getParamID())->load() >= 0.5f;
}

void PlayView::resized()
{
    const bool s = studio();
    const float cw = (designWidth - 48 - 5 * 12) / 6.0f;
    int i = 0;
    for (auto* k : { &dynamics, &expression, &vibrato, &portamento, &stringPreference, &room })
    {
        k->setBounds (juce::roundToInt (24 + i++ * (cw + 12)),
                      juce::roundToInt (450 - top),
                      juce::roundToInt (cw),
                      180);
        k->setVisible (! s);
    }
    articulation.setBounds (130, juce::roundToInt (646 - top), 1050, 30);
    articulation.setVisible (! s);
}

void PlayView::timerCallback()
{
    track.read (processor.getEngine());
    const bool s = studio();
    if (s == dynamics.isVisible())
        resized();
    repaint();
}

void PlayView::paint (juce::Graphics& g)
{
    track.read (processor.getEngine());
    paintFingerboard (g, { 24, 116 - top, 760, 318 });
    if (studio())
    {
        paintLookAhead (g, { 800, 116 - top, 376, 318 });
        paintPlan (g, { 24, 450 - top, designWidth - 48.0f, 238 });
    }
    else
    {
        paintNow (g, { 800, 116 - top, 376, 318 });
        drawText (g,
                  "ARTICULATION",
                  24,
                  646 - top + 20,
                  Fonts::sans (10.5f, true).withExtraKerningFactor (0.12f),
                  colours::amber);
    }
}

void PlayView::paintFingerboard (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto& T = processor.getTelemetry();
    const float x = r.getX(), y = r.getY(), w = r.getWidth(), h = r.getHeight();
    drawPanel (g, r, "What the player is doing", "live view");
    const float nut = x + 70, bridge = x + w - 150, L = bridge - nut;
    const float boardTop = y + 64, spN = 22, spB = 30, cy = boardTop + 70;
    const int active = T.note.load() >= 0 ? T.string.load() : -1;
    // fingerboard
    const float fbEnd = nut + L * 0.72f;
    const float yn0 = cy - 1.9f * spN, yn1 = cy + 1.9f * spN;
    const float yb0 = cy - 1.9f * (spN + (spB - spN) * 0.72f), yb1 = cy + 1.9f * (spN + (spB - spN) * 0.72f);
    juce::Path board;
    board.startNewSubPath (nut, yn0);
    board.lineTo (fbEnd, yb0);
    board.quadraticTo (fbEnd + 16, cy, fbEnd, yb1);
    board.lineTo (nut, yn1);
    board.closeSubPath();
    g.setColour (juce::Colour (0xff0f0c0a));
    g.fillPath (board);
    g.setColour (colours::line);
    g.strokePath (board, juce::PathStrokeType (1));
    g.setColour (juce::Colour (0xffd8cbb5));
    g.fillRoundedRectangle (nut - 8, yn0 - 6, 8, yn1 - yn0 + 12, 2);
    drawText (g, "nut", nut - 4, yn1 + 18, Fonts::sans (10), colours::dim, juce::Justification::horizontallyCentred);
    juce::Path br;
    br.startNewSubPath (bridge, cy - 2.1f * spB);
    br.quadraticTo (bridge + 6, cy, bridge, cy + 2.1f * spB);
    g.setColour (juce::Colour (0xffd8cbb5));
    g.strokePath (br, juce::PathStrokeType (5, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    drawText (g, "bridge", bridge + 14, cy + 4, Fonts::sans (10), colours::dim);
    auto along = [&] (double semis) { return nut + L * (float) (1.0 - std::pow (2.0, -semis / 12.0)); };
    for (auto [name, semis] : { std::pair { "I", 2 }, { "III", 5 }, { "V", 9 }, { "VII", 12 } })
    {
        const float px = along (semis);
        line (g, px, yn0 - 14, px, yn0 - 8, colours::dim);
        drawText (g, name, px, yn0 - 18, Fonts::serif (12), colours::dim, juce::Justification::horizontallyCentred);
    }
    // the hand: from a stretched-back first finger to the fourth
    const float hp = T.handPos.load();
    const float hx0 = along (std::max (0.0f, hp - 1)), hx1 = along (hp + 5);
    g.setColour (colours::steel.withAlpha (0.12f));
    g.fillRoundedRectangle (hx0, yn0 - 4, hx1 - hx0, yn1 - yn0 + 8, 6);
    const int position = positionOf (hp);
    drawText (g,
              "hand: " + (position == 0 ? juce::String ("half") : ordinal (position)) + " position",
              (hx0 + hx1) / 2,
              yn1 + 26,
              Fonts::sans (11),
              colours::steel,
              juce::Justification::horizontallyCentred);
    // strings, E at the top
    const float gauges[] = { 2.4f, 1.9f, 1.5f, 1.1f };
    auto stringY = [&] (int s, float atX)
    {
        const float k = (float) (3 - s) - 1.5f;
        return cy + k * (spN + (spB - spN) * (atX - nut) / L);
    };
    for (int s = 3; s >= 0; --s)
    {
        const bool on = s == active;
        const float force = T.stringForce[(size_t) s].load();
        const auto col = on ? colours::gold : juce::Colour (0xffb9ab98).withAlpha (force > 0.01f ? 0.8f : 0.55f);
        line (g, nut, stringY (s, nut), bridge, stringY (s, bridge), col, gauges[s] + (on ? 0.6f : 0));
        drawText (g,
                  juce::String::charToString (stringNames[s]),
                  nut - 40,
                  stringY (s, nut) + 4,
                  Fonts::serif (14),
                  on ? colours::gold : colours::muted,
                  juce::Justification::horizontallyCentred);
    }
    // the finger on the sounding string
    int finger = 0;
    if (active >= 0)
    {
        const double semis = T.pitch.load() - openPitch[active];
        if (semis > 0.3)
        {
            finger = juce::jlimit (1, 4, juce::roundToInt ((semis - hp) / 1.75) + 1);
            const float fx = along (semis), fy = stringY (active, fx);
            if (T.vibWidth.load() > 3)
            {
                juce::Path wave;
                wave.startNewSubPath (fx - 14, fy - 14);
                for (int k = 0; k < 4; ++k)
                    wave.quadraticTo (fx - 14 + k * 7 + 3.5f, fy - 19, fx - 14 + (k + 1) * 7, fy - 14);
                g.setColour (colours::gold.withAlpha (0.8f));
                g.strokePath (wave, juce::PathStrokeType (1.4f));
            }
            g.setColour (colours::gold);
            g.fillEllipse (fx - 8, fy - 8, 16, 16);
            g.setColour (colours::bg);
            g.drawEllipse (fx - 8, fy - 8, 16, 16, 2);
            drawText (g,
                      juce::String (finger),
                      fx,
                      fy + 4,
                      Fonts::sans (10, true),
                      colours::bg,
                      juce::Justification::horizontallyCentred);
        }
    }
    // the bow, across the strings at its contact point
    const float beta = T.contact.load();
    const float bx = bridge - L * beta;
    const float btop = boardTop - 30, bbot = y + h - 64;
    g.setColour (juce::Colour (0xff5a3a24));
    g.fillRoundedRectangle (bx - 4, btop, 8, bbot - btop, 3);
    g.setColour (juce::Colour (0xffefe6d4));
    g.fillRoundedRectangle (bx + 5, btop + 6, 3, bbot - btop - 30, 1);
    g.setColour (juce::Colour (0xff2c1c12));
    g.fillRoundedRectangle (bx - 9, bbot - 26, 18, 26, 3);
    g.setColour (juce::Colour (0xff7b5a3e));
    g.drawRoundedRectangle (bx - 9, bbot - 26, 18, 26, 3, 1);
    drawText (g, "frog", bx + 18, bbot - 8, Fonts::sans (10), colours::dim);
    drawText (g, "tip", bx + 18, btop + 12, Fonts::sans (10), colours::dim);
    // where on the hair the string is (the bow moves, the strings stay)
    const float hair = T.hair.load();
    const float hy = (bbot - 26) - (bbot - 26 - btop - 6) * hair;
    g.setColour (colours::gold);
    g.fillEllipse (bx + 2, hy - 3, 9, 6);
    const bool down = T.bowDir.load() > 0;
    const bool moving = T.speed.load() > 0.005f;
    if (moving)
    {
        const float ay0 = down ? cy + 70 : cy + 20, ay1 = down ? cy + 20 : cy + 70, d = down ? -1.0f : 1.0f;
        line (g, bx - 22, ay0, bx - 22, ay1, colours::amber, 2.5f);
        juce::Path head;
        head.startNewSubPath (bx - 28, ay1 - 6 * d);
        head.lineTo (bx - 22, ay1);
        head.lineTo (bx - 16, ay1 - 6 * d);
        g.setColour (colours::amber);
        g.strokePath (head, juce::PathStrokeType (2.5f));
        drawText (g,
                  juce::String::fromUTF8 (down ? "down ⊓" : "up V"),
                  bx - 30,
                  cy + 90,
                  Fonts::sans (11),
                  colours::amber,
                  juce::Justification::right);
    }
    const float yy = cy + 2.1f * spB + 18;
    g.setColour (colours::muted);
    const float dash[] = { 3, 3 };
    g.drawDashedLine ({ bx, yy, bridge, yy }, dash, 2);
    drawText (g, "contact " + juce::String (beta, 2), bx + 10, yy + 16, Fonts::mono (10), colours::muted);
    // readout row
    const float ry = y + h - 22;
    const bool sounding = active >= 0;
    struct Item
    {
        juce::String label, value;
    };
    const Item items[] = {
        { "String", sounding ? juce::String::charToString (stringNames[active]) : juce::String ("-") },
        { "Position",
          sounding ? (finger == 0 ? juce::String ("open string")
                                  : ordinal (std::max (1, position)) + juce::String::fromUTF8 (" · finger ")
                              + juce::String (finger))
                   : juce::String ("-") },
        { "Bow",
          juce::String::fromUTF8 (down ? "down ⊓" : "up V") + juce::String::fromUTF8 (" · ")
              + juce::String (juce::roundToInt (hair * 100)) + "% of hair" },
        { "Contact", juce::String (beta, 2) + juce::String::fromUTF8 (" · ") + contactName (beta) },
    };
    float rx = x + 18;
    for (const auto& it : items)
    {
        drawLabel (g, it.label, rx, ry);
        drawBadge (g, { rx + it.label.length() * 7.6f + 14, ry - 4 }, Mode::autoMode);
        drawText (g, it.value, rx, ry + 17, Fonts::sans (12.5f), colours::text);
        rx += 158;
    }
}

void PlayView::paintNow (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto& T = processor.getTelemetry();
    const float x = r.getX(), y = r.getY(), w = r.getWidth();
    const int note = T.note.load();
    drawPanel (g, r, "Now", "velocity " + juce::String (T.velocity.load()));
    if (note >= 0)
    {
        const float pitch = T.pitch.load();
        drawText (g, noteName (note), x + 16, y + 82, Fonts::serif (52), colours::text);
        drawText (g,
                  juce::String (440.0 * std::pow (2.0, (note - 69) / 12.0), 1) + " Hz",
                  x + 92 + 20,
                  y + 62,
                  Fonts::mono (12),
                  colours::muted);
        const int cents = juce::roundToInt ((pitch - (float) note) * 100.0f);
        drawText (g,
                  (cents >= 0 ? "+" : "") + juce::String (cents) + " ct now",
                  x + 112,
                  y + 80,
                  Fonts::mono (12),
                  colours::muted);
    }
    else
        drawText (g, "-", x + 16, y + 82, Fonts::serif (52), colours::dim);
    // dynamic meter, pp..ff
    drawLabel (g, "Dynamic", x + 230, y + 50);
    const float d = T.dynamics.load();
    const int level = note >= 0 ? juce::jlimit (0, 5, (int) (d * 6)) : -1;
    static const char* dyn[] = { "pp", "p", "mp", "mf", "f", "ff" };
    for (int i = 0; i < 6; ++i)
    {
        const bool on = i <= level;
        const juce::Rectangle<float> cell (x + 230 + i * 22, y + 58, 19, 14);
        if (on)
        {
            g.setColour (colours::amber.withAlpha (0.45f + i * 0.11f));
            g.fillRoundedRectangle (cell, 3);
        }
        else
        {
            g.setColour (colours::panel2);
            g.fillRoundedRectangle (cell, 3);
            g.setColour (colours::line);
            g.drawRoundedRectangle (cell, 3, 1);
        }
        drawText (g,
                  dyn[i],
                  x + 239.5f + i * 22,
                  y + 88,
                  Fonts::serif (12),
                  i == level ? colours::text : colours::dim,
                  juce::Justification::horizontallyCentred);
    }
    // rows
    const float vw = T.vibWidth.load(), vr = T.vibRate.load(), slips = T.slips.load();
    juce::String art = "-", vib = "-", bow = "-", helm = "-";
    juce::Colour helmColour = colours::dim;
    if (note >= 0)
    {
        art = T.slur.load() ? juce::String::fromUTF8 ("Legato · slurred note ") + juce::String (T.slurNotes.load() + 1)
                            : juce::String::fromUTF8 ("Détaché · new bow");
        vib = vw < 1
            ? juce::String ("none yet")
            : juce::String (vr, 1) + juce::String::fromUTF8 (" Hz · ") + juce::String (juce::roundToInt (vw)) + " ct";
        bow = juce::String (T.speed.load(), 2) + juce::String::fromUTF8 (" m/s · ") + juce::String (T.force.load(), 2)
            + " N";
        const bool clean = slips > 0.8f && slips < 1.25f;
        helm = clean ? "clean" : slips >= 1.25f ? "slipping " + juce::String (slips, 1) + "x per period" : "catching";
        helmColour = clean ? colours::good : colours::coral;
    }
    struct Row
    {
        const char* label;
        juce::String value;
        bool badge;
    };
    const Row rows[] = { { "Articulation", art, true },
                         { "Vibrato", vib, true },
                         { "Bow speed / force", bow, true },
                         { "Helmholtz", helm, false } };
    float ry = y + 118;
    for (const auto& row : rows)
    {
        line (g, x + 16, ry - 16, x + w - 16, ry - 16, colours::line);
        drawText (g, row.label, x + 16, ry + 2, Fonts::sans (12), colours::muted);
        drawText (g, row.value, x + w - 16, ry + 2, Fonts::sans (12.5f), colours::text, juce::Justification::right);
        const float bx = x + 16 + juce::GlyphArrangement::getStringWidth (Fonts::sans (12), row.label) + 14;
        if (row.badge)
            drawBadge (g, { bx, ry - 2 }, Mode::autoMode);
        else
        {
            g.setColour (helmColour);
            g.fillEllipse (bx - 5, ry - 7, 10, 10);
        }
        ry += 34;
    }
    // the dynamics the player chose over the last 8 s
    const float cy0 = y + 254;
    drawLabel (g, juce::String::fromUTF8 ("Auto curves · last 8 s"), x + 16, cy0);
    const juce::Rectangle<float> plot (x + 16, cy0 + 8, w - 120, 42);
    g.setColour (colours::well);
    g.fillRoundedRectangle (plot, 5);
    g.setColour (colours::line);
    g.drawRoundedRectangle (plot, 5, 1);
    const auto count = T.historyCount.load (std::memory_order_acquire);
    const double now = processor.getEngine().seconds();
    juce::Path curve;
    bool started = false;
    for (uint64_t k = count > 800 ? count - 800 : 0; k < count; ++k)
    {
        const auto& pt = T.history[(size_t) (k % Telemetry::historySize)];
        const float u = (float) ((pt.t - (now - 8.0)) / 8.0);
        if (u < 0)
            continue;
        const float px = plot.getX() + 4 + u * (plot.getWidth() - 8),
                    py = plot.getBottom() - 4 - pt.dynamics * (plot.getHeight() - 8);
        if (! started)
            curve.startNewSubPath (px, py);
        else
            curve.lineTo (px, py);
        started = true;
    }
    g.setColour (colours::amber);
    g.strokePath (curve, juce::PathStrokeType (1.8f));
    dragArea = { x + w - 96, cy0 + 8, 80, 42 };
    g.setColour (colours::panel2);
    g.fillRoundedRectangle (dragArea, 6);
    g.setColour (colours::dim);
    g.drawRoundedRectangle (dragArea, 6, 1);
    drawText (g,
              juce::String::fromUTF8 ("⠿ Drag"),
              dragArea.getCentreX(),
              cy0 + 26,
              Fonts::sans (12, true),
              colours::dim,
              juce::Justification::horizontallyCentred);
    drawText (g,
              "as MIDI",
              dragArea.getCentreX(),
              cy0 + 41,
              Fonts::sans (11),
              colours::dim,
              juce::Justification::horizontallyCentred);
}

void PlayView::paintLookAhead (juce::Graphics& g, juce::Rectangle<float> r)
{
    const float x = r.getX(), y = r.getY();
    drawPanel (g, r, "Look-ahead", juce::String (juce::roundToInt (o2::Engine::studioLookAhead * 1000)) + " ms");
    drawText (g, "The player can see the next notes, so it can:", x + 16, y + 56, Fonts::sans (12.5f), colours::muted);
    struct Row
    {
        const char *a, *b;
    };
    const Row rows[] = { { "Plan the bow", "spreads each stroke over the note" },
                         { "Vibrate short notes at once", "and to their end" },
                         { "End vibrato", "relaxes before the note ends" },
                         { "Start shifts early", "with the player milestone (M4)" },
                         { "Time the attack", "with the player milestone (M4)" } };
    float ry = y + 92;
    for (int i = 0; i < 5; ++i)
    {
        const bool now = i < 3;
        g.setColour (now ? colours::amber : colours::dim);
        g.fillEllipse (x + 18.5f, ry - 7.5f, 7, 7);
        drawText (g, rows[i].a, x + 34, ry, Fonts::sans (13), now ? colours::text : colours::muted);
        drawText (g, rows[i].b, x + 34, ry + 17, Fonts::sans (11.5f), colours::dim);
        ry += 42;
    }
}

void PlayView::paintPlan (juce::Graphics& g, juce::Rectangle<float> r)
{
    drawPanel (g,
               r,
               juce::String::fromUTF8 ("Plan · Studio mode only"),
               "the DAW compensates the " + juce::String (juce::roundToInt (o2::Engine::studioLookAhead * 1000))
                   + " ms");
    const float gx0 = 140, gx1 = designWidth - 60, laneTop = r.getY() + 50;
    const char* lanes = "EADG";
    for (int i = 0; i < 4; ++i)
    {
        const float ly = laneTop + i * 34;
        drawText (g,
                  juce::String::charToString (lanes[i]),
                  110,
                  ly + 21,
                  Fonts::serif (15),
                  colours::muted,
                  juce::Justification::horizontallyCentred);
        line (g, gx0, ly + 17, gx1, ly + 17, colours::line);
    }
    // time: 4 s of past, the look-ahead, and a little beyond, one grid line every 0.5 s
    const double now = processor.getEngine().seconds();
    const double span = 7.0, past = span * 5.0 / 16.0;
    const double t0 = now - past;
    auto xAt = [&] (double t) { return gx0 + (float) ((t - t0) / span) * (gx1 - gx0); };
    for (double t = std::ceil (t0 * 2) / 2; t < t0 + span; t += 0.5)
    {
        const bool strong = std::abs (std::fmod (t, 2.0)) < 1e-6;
        line (g,
              xAt (t),
              laneTop - 6,
              xAt (t),
              laneTop + 4 * 34,
              strong ? juce::Colour (0xff4d3f35) : colours::line,
              strong ? 1.4f : 0.6f);
    }
    const float nowX = xAt (now), winX = xAt (now + o2::Engine::studioLookAhead);
    g.setColour (colours::steel.withAlpha (0.08f));
    g.fillRoundedRectangle (nowX, laneTop - 10, winX - nowX, 4 * 34 + 14, 4);
    line (g, nowX, laneTop - 14, nowX, laneTop + 4 * 34 + 4, colours::gold, 2);
    drawText (g, "now", nowX, laneTop - 18, Fonts::sans (10), colours::gold, juce::Justification::horizontallyCentred);
    drawText (g,
              "look-ahead window",
              (nowX + winX) / 2,
              laneTop - 18,
              Fonts::sans (10),
              colours::steel,
              juce::Justification::horizontallyCentred);
    drawLabel (g, "Bow", 60, laneTop + 4 * 34 + 22);
    g.saveState();
    g.reduceClipRegion (juce::Rectangle<float> (gx0, laneTop - 30, gx1 - gx0, 4 * 34 + 70).toNearestInt());
    for (const auto& n : track.notes)
    {
        // planned notes go where a player would most likely take them: the lowest string that
        // plays them within first position, else the E string
        int s = n.string;
        if (s < 0)
        {
            s = 3;
            for (int k = 0; k < 4; ++k)
                if (n.pitch >= openPitch[k] && n.pitch - openPitch[k] <= 7)
                {
                    s = k;
                    break;
                }
        }
        const double end = n.off >= 0 ? n.off : (n.planned ? n.on + 0.25 : now);
        const float nx = xAt (n.on), nw = std::max (6.0f, xAt (end) - nx);
        const float ny = laneTop + (3 - s) * 34 + 5;
        const bool done = n.off >= 0 && n.off <= now;
        const auto col = n.planned ? juce::Colour (0xffb9ab98) : done ? colours::muted.withAlpha (0.5f) : colours::gold;
        g.setColour (col);
        g.fillRoundedRectangle (nx + 1, ny, nw - 2, 24, 4);
        drawText (g, noteName (n.pitch), nx + 6, ny + 16, Fonts::sans (11, true), colours::bg);
        if (! n.planned && ! n.slur)
            drawText (g,
                      juce::String::fromUTF8 (n.dir > 0 ? "⊓" : "V"),
                      nx + 4,
                      laneTop + 4 * 34 + 22,
                      Fonts::serif (15),
                      done ? colours::dim : colours::amber);
    }
    g.restoreState();
    drawText (
        g,
        "planned notes are drawn on the string a player would most likely use; the bow marks show the strokes played",
        gx0,
        r.getBottom() - 14,
        Fonts::sans (10.5f),
        colours::dim);
}
} // namespace octavio2::ui
