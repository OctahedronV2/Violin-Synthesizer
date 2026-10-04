#include "BowView.h"
#include "PlayerText.h"

namespace octavio2::ui
{
namespace
{
constexpr double span = 6.0; // s of history shown
constexpr float hairCm = 62.0f; // PlayerParams::bowLength

juce::String percent (float v)
{
    return juce::String (juce::roundToInt (v)) + " %";
}

juce::String signedPercent (float v)
{
    const int k = juce::roundToInt (v);
    return k == 0 ? juce::String ("as played") : (k > 0 ? "+" : "") + juce::String (k) + " %";
}

juce::Colour strokeColour (const o2::Engine::Trace& p)
{
    if (! p.sounding)
        return colours::dim.withAlpha (0.6f);
    return p.speed >= 0 ? colours::amber : colours::steel;
}

// A trace as a line, coloured by bow direction (amber down, steel up, dim off the string)
template <typename Fn>
void plot (juce::Graphics& g,
           const std::vector<o2::Engine::Trace>& points,
           juce::Rectangle<float> r,
           double t0,
           double t1,
           Fn&& value)
{
    g.saveState();
    g.reduceClipRegion (r.reduced (1).toNearestInt());
    juce::Path path;
    juce::Colour current;
    bool open = false;
    juce::Point<float> last;
    auto flush = [&]
    {
        if (open)
        {
            g.setColour (current);
            g.strokePath (path, juce::PathStrokeType (1.7f, juce::PathStrokeType::curved));
        }
        path.clear();
        open = false;
    };
    for (const auto& p : points)
    {
        if (p.t < t0 - 0.05 || p.t > t1 + 0.05)
            continue;
        const float x = r.getX() + (float) ((p.t - t0) / (t1 - t0)) * r.getWidth();
        const float y = r.getBottom() - 3 - juce::jlimit (0.0f, 1.0f, value (p)) * (r.getHeight() - 6);
        const auto c = strokeColour (p);
        if (open && c != current)
        {
            flush();
            path.startNewSubPath (last);
            open = true;
        }
        if (! open)
        {
            path.startNewSubPath (x, y);
            open = true;
        }
        else
            path.lineTo (x, y);
        current = c;
        last = { x, y };
    }
    flush();
    g.restoreState();
}

juce::String contactWords (float beta)
{
    return beta < 0.06f ? "near the bridge" : beta > 0.16f ? "towards the fingerboard" : "normal";
}
} // namespace

BowView::BowView (Processor& p)
    : processor (p),
      bowStyle (&p.getParameters(),
                params::id::bowStyle.getParamID(),
                { { "Auto" },
                  { "Legato" },
                  { juce::String::fromUTF8 ("Détaché") },
                  { "Staccato" },
                  { juce::String::fromUTF8 ("Martelé") },
                  { "Spiccato" } }),
      bowChange (&p.getParameters(), params::id::bowChange.getParamID(), "Bow change", Knob::Style::small, percent),
      strokeShaping (&p.getParameters(),
                     params::id::strokeShaping.getParamID(),
                     "Stroke shaping",
                     Knob::Style::small,
                     percent),
      bite (&p.getParameters(), params::id::bite.getParamID(), "Bite", Knob::Style::small, percent),
      contact (&p.getParameters(), params::id::contact.getParamID(), "Contact point", Knob::Style::small, signedPercent)
{
    bowStyle.setLayout (3, 30, 6);
    bowStyle.setTooltip ("Auto: the player picks the stroke from how you play (overlapping notes are slurred). The "
                         "others play every note that way.");
    bowChange.setTooltip ("How quickly the bow turns round at a bow change and gets up to speed. 100 %: as fitted.");
    strokeShaping.setTooltip ("How much each quick separate stroke eases off after it speaks, and how much a stroke "
                              "tapers into the next bow change. 0: even, organ-like strokes.");
    bite.setTooltip ("The extra bow force that makes each stroke speak. 0: soft starts.");
    contact.setTooltip ("Where the bow plays: minus is nearer the bridge (brighter, louder), plus nearer the "
                        "fingerboard (softer, rounder). The player still moves it with the dynamics.");
    addAndMakeVisible (bowStyle);
    for (auto* k : { &bowChange, &strokeShaping, &bite, &contact })
        addAndMakeVisible (k);
    addAndMakeVisible (contactBadge); // 2.3
    addAndMakeVisible (pressureBadge);
    startTimerHz (30);
}

void BowView::resized()
{
    const int px = 840, py = 236;
    bowStyle.setBounds (px + 16, py + 56, 304, 70);
    int i = 0;
    for (auto* k : { &bowChange, &strokeShaping, &bite, &contact })
        k->setBounds (px + 16 + 76 * i++, py + 170, 76, 96);
    // 2.3: right of the dial (the label above is as wide as the knob)
    contactBadge.placeAt (contact.getPosition().toFloat() + juce::Point<float> (70, 30));
    pressureBadge.placeAt ({ (float) px + 16 + 9, (float) py + 280 });
}

void BowView::timerCallback()
{
    if (! isShowing())
        return;
    trace.read (processor.getEngine());
    track.read (processor.getEngine());
    repaint();
}

void BowView::paint (juce::Graphics& g)
{
    trace.read (processor.getEngine());
    track.read (processor.getEngine());
    paintHistory (g, { 24, 12, 800, 576 });
    paintNow (g, { 840, 12, 336, 212 });
    const juce::Rectangle<float> r (840, 236, 336, 352);
    drawPanel (g, r, "Bow controls", "100 % = as fitted");
    drawLabel (g, "Bow style", r.getX() + 16, r.getY() + 48);
    drawLabel (g, "Strokes", r.getX() + 16, r.getY() + 160);
    g.setColour (colours::line);
    g.drawLine (r.getX() + 16, r.getY() + 140, r.getRight() - 16, r.getY() + 140);
    // 2.3: the pressure's mode (pressureBadge sits left of this)
    const auto pm = ModeBadge::displayed (processor, o2::dimPressure);
    drawText (g,
              pm == Mode::autoMode     ? juce::String ("Bow pressure: the player's (CC22 guides it)")
                  : pm == Mode::guided ? juce::String ("Bow pressure: guided by CC22")
                                       : juce::String ("Bow pressure: exactly CC22"),
              r.getX() + 16 + 24,
              r.getY() + 284,
              Fonts::sans (11.5f),
              colours::muted);
    const char* notes[] = { "Bow hiss is on the Tone tab; dynamics and the drawn",
                            "curves (Curves tab) still move the bow. Every control",
                            "here is a host parameter." };
    float ny = r.getY() + 304;
    for (auto* n : notes)
    {
        drawText (g, n, r.getX() + 16, ny, Fonts::sans (11.5f), colours::dim);
        ny += 17;
    }
}

void BowView::paintHistory (juce::Graphics& g, juce::Rectangle<float> r)
{
    const double t1 = trace.windowEnd, t0 = t1 - span;
    const float x0 = r.getX() + 110, x1 = r.getRight() - 18;
    auto xAt = [&] (double t) { return x0 + (float) ((t - t0) / span) * (x1 - x0); };
    // bow changes: the direction flips while the bow is on the string
    std::vector<std::pair<double, bool>> changes; // time, now down-bow
    int lastSign = 0;
    for (const auto& p : trace.points)
    {
        if (p.t < t0 || p.t > t1)
            continue;
        if (std::abs (p.speed) < 0.01f)
            continue;
        const int sign = p.speed > 0 ? 1 : -1;
        if (lastSign != 0 && sign != lastSign)
            changes.push_back ({ p.t, sign > 0 });
        lastSign = sign;
    }
    drawPanel (g,
               r,
               juce::String::fromUTF8 ("The bow · last 6 s"),
               juce::String ((int) changes.size()) + (changes.size() == 1 ? " bow change" : " bow changes"));

    // the notes, with the bow mark at each new stroke
    const juce::Rectangle<float> notes (x0, r.getY() + 38, x1 - x0, 24);
    drawLabel (g, "Notes", r.getX() + 18, notes.getY() + 16);
    g.saveState();
    g.reduceClipRegion (notes.withHeight (26).toNearestInt());
    for (const auto& n : track.notes)
    {
        if (n.planned)
            continue;
        const double end = n.off >= 0 ? n.off : t1;
        if (end < t0 || n.on > t1)
            continue;
        const float nx = xAt (n.on), nw = std::max (4.0f, xAt (end) - nx);
        g.setColour (colours::panel2);
        g.fillRoundedRectangle (nx + 1, notes.getY(), nw - 2, notes.getHeight(), 4);
        g.setColour (colours::line);
        g.drawRoundedRectangle (nx + 1, notes.getY(), nw - 2, notes.getHeight(), 4, 1);
        const bool mark = ! n.slur && nw > 12;
        if (mark)
            drawText (g,
                      juce::String::fromUTF8 (n.dir > 0 ? "⊓" : "V"),
                      nx + 5,
                      notes.getY() + 17,
                      Fonts::serif (13),
                      n.dir > 0 ? colours::amber : colours::steel);
        const auto name = noteName (n.pitch);
        const float tx = nx + (mark ? 17.0f : 5.0f);
        if (tx + juce::GlyphArrangement::getStringWidth (Fonts::sans (10, true), name) < nx + nw - 2)
            drawText (g, name, tx, notes.getY() + 16, Fonts::sans (10, true), colours::text);
    }
    g.restoreState();

    const float laneX = x0, laneW = x1 - x0;
    const juce::Rectangle<float> hair (laneX, r.getY() + 76, laneW, 128);
    const juce::Rectangle<float> speed (laneX, r.getY() + 220, laneW, 88);
    const juce::Rectangle<float> force (laneX, r.getY() + 324, laneW, 88);
    const juce::Rectangle<float> contact (laneX, r.getY() + 428, laneW, 88);
    drawLane (g, hair, "Hair", "where it plays", "tip", "frog");
    drawLane (g, speed, "Bow speed", "m/s", "0.8", "0");
    drawLane (g, force, "Force", "N", "3", "0");
    drawLane (g, contact, "Contact", "from the bridge", "bridge", "board");
    // the middle of the hair, and the normal contact band
    const float dash[] = { 2, 4 };
    g.setColour (colours::line);
    g.drawDashedLine ({ hair.getX(), hair.getCentreY(), hair.getRight(), hair.getCentreY() }, dash, 2);
    for (float b : { 0.06f, 0.16f })
    {
        const float y = contact.getY() + 3 + (b - 0.02f) / (0.25f - 0.02f) * (contact.getHeight() - 6);
        g.drawDashedLine ({ contact.getX(), y, contact.getRight(), y }, dash, 2);
    }
    // bow changes across the lanes
    for (const auto& [t, down] : changes)
    {
        const float x = xAt (t);
        const float d2[] = { 3, 3 };
        g.setColour (colours::muted.withAlpha (0.45f));
        g.drawDashedLine ({ x, hair.getY() + 2, x, contact.getBottom() - 2 }, d2, 2, 1);
    }
    plot (g, trace.points, hair, t0, t1, [] (const o2::Engine::Trace& p) { return p.hair; });
    plot (g, trace.points, speed, t0, t1, [] (const o2::Engine::Trace& p) { return std::abs (p.speed) / 0.8f; });
    plot (g, trace.points, force, t0, t1, [] (const o2::Engine::Trace& p) { return p.force / 3.0f; });
    plot (g,
          trace.points,
          contact,
          t0,
          t1,
          [] (const o2::Engine::Trace& p) { return 1.0f - (p.contact - 0.02f) / (0.25f - 0.02f); });
    drawTimeAxis (g, x0, x1, contact.getBottom() + 18, span);
    // legend
    const float ly = r.getBottom() - 20;
    float lx = x0;
    auto key = [&] (juce::Colour c, const juce::String& text, bool dashed)
    {
        if (dashed)
        {
            const float d3[] = { 3, 3 };
            g.setColour (colours::muted);
            g.drawDashedLine ({ lx + 7, ly - 10, lx + 7, ly + 2 }, d3, 2, 1);
        }
        else
        {
            g.setColour (c);
            g.fillRoundedRectangle (lx, ly - 5, 14, 3, 1);
        }
        drawText (g, text, lx + 20, ly, Fonts::sans (11), colours::muted);
        lx += 20 + juce::GlyphArrangement::getStringWidth (Fonts::sans (11), text) + 22;
    };
    key (colours::amber, juce::String::fromUTF8 ("down-bow ⊓"), false);
    key (colours::steel, "up-bow V", false);
    key (colours::dim, "off the string", false);
    key (colours::muted, "bow change", true);
}

void BowView::paintNow (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto& T = processor.getTelemetry();
    const float x = r.getX(), y = r.getY(), w = r.getWidth();
    const bool sounding = T.note.load() >= 0;
    const bool down = T.bowDir.load() > 0;
    const float hair = juce::jlimit (0.0f, 1.0f, T.hair.load());
    const float left = (down ? 1.0f - hair : hair) * hairCm;
    drawPanel (g, r, "The bow now", sounding ? (T.changing.load() ? "changing bow" : "on the string") : "lifted");
    // the bow, frog on the left: the string's place on the hair, and the hair left this way
    const float bx0 = x + 34, bx1 = x + w - 24, by = y + 64;
    g.setColour (juce::Colour (0xff5a3a24));
    g.fillRoundedRectangle (bx0, by - 9, bx1 - bx0, 5, 2);
    g.setColour (juce::Colour (0xffefe6d4).withAlpha (0.9f));
    g.fillRect (bx0 + 8, by + 1, bx1 - bx0 - 10, 2.0f);
    g.setColour (juce::Colour (0xff2c1c12));
    g.fillRoundedRectangle (bx0 - 14, by - 12, 22, 18, 3);
    g.setColour (juce::Colour (0xff7b5a3e));
    g.drawRoundedRectangle (bx0 - 14, by - 12, 22, 18, 3, 1);
    drawText (g, "frog", bx0 - 3, by + 22, Fonts::sans (10), colours::dim, juce::Justification::horizontallyCentred);
    drawText (g, "tip", bx1, by + 22, Fonts::sans (10), colours::dim, juce::Justification::right);
    const float hx = bx0 + 8 + hair * (bx1 - bx0 - 10);
    // hair left in the direction of travel
    const float ex = down ? bx1 : bx0 + 8;
    g.setColour ((down ? colours::amber : colours::steel).withAlpha (0.25f));
    g.fillRect (juce::Rectangle<float> (std::min (hx, ex), by + 6, std::abs (ex - hx), 5));
    g.setColour (sounding ? colours::gold : colours::muted);
    g.fillEllipse (hx - 5, by - 3, 10, 10);
    if (sounding && T.speed.load() > 0.005f)
    {
        const float d = down ? 1.0f : -1.0f;
        juce::Path arrow;
        arrow.startNewSubPath (hx, by - 20);
        arrow.lineTo (hx + 26 * d, by - 20);
        g.setColour (down ? colours::amber : colours::steel);
        g.strokePath (arrow, juce::PathStrokeType (2));
        juce::Path head;
        head.addTriangle (hx + 30 * d, by - 20, hx + 22 * d, by - 24, hx + 22 * d, by - 16);
        g.fillPath (head);
    }
    // rows
    const float beta = T.contact.load();
    struct Row
    {
        const char* label;
        juce::String value;
    };
    const Row rows[] = {
        { "Direction",
          juce::String::fromUTF8 (down ? "down-bow ⊓" : "up-bow V") + juce::String::fromUTF8 (" · ")
              + juce::String (juce::roundToInt (left)) + " cm of hair left" },
        { "Speed", sounding ? juce::String (T.speed.load(), 2) + " m/s" : juce::String ("-") },
        { "Force", sounding ? juce::String (T.force.load(), 2) + " N" : juce::String ("-") },
        { "Contact", juce::String (beta, 3) + juce::String::fromUTF8 (" · ") + contactWords (beta) },
    };
    float ry = y + 116;
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
