#include "MidiView.h"

namespace octavio2::ui
{
namespace
{
constexpr float top = 104;

void box (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, bool live)
{
    g.setColour (colours::panel2);
    g.fillRoundedRectangle (r, 7);
    g.setColour (colours::line);
    g.drawRoundedRectangle (r.reduced (0.5f), 7, 1);
    drawText (g, text, r.getX() + 12, r.getCentreY() + 5, Fonts::sans (13), live ? colours::text : colours::muted);
    juce::Path p;
    const float cx = r.getRight() - 20, cy = r.getCentreY() - 3;
    p.startNewSubPath (cx, cy);
    p.lineTo (cx + 5, cy + 6);
    p.lineTo (cx + 10, cy);
    g.setColour (colours::dim);
    g.strokePath (p, juce::PathStrokeType (1.5f));
}
} // namespace

MidiView::MidiView (Processor& p)
    : processor (p),
      curve (&p.getParameters(),
             params::id::velocityCurve.getParamID(),
             "Velocity curve",
             Knob::Style::small,
             [] (float v)
             {
                 return v < 0.97f ? juce::String ("louder ") + juce::String (v, 2)
                     : v > 1.03f  ? juce::String ("softer ") + juce::String (v, 2)
                                  : juce::String ("even");
             }),
      dynamics (&p.getParameters(),
                params::id::dynamics.getParamID(),
                "Dynamics",
                Knob::Style::small,
                [] (float v)
                {
                    return std::abs (v) < 0.5f ? juce::String ("as played")
                                               : (v > 0 ? "+" : "") + juce::String (juce::roundToInt (v)) + " %";
                }),
      octave (&p.getParameters(), params::id::octave.getParamID(), { { "-2" }, { "-1" }, { "0" }, { "+1" }, { "+2" } }),
      behaviour (nullptr, {}, { { "Latching" }, { "Momentary" }, { "Off" } }),
      mpe (&p.getParameters(), params::id::mpe.getParamID(), { { "Off" }, { "On" } }), // M7
      learn (juce::String::fromUTF8 ("◉ Learn"), "the MIDI mapping milestone (M6)")
{
    curve.setTooltip ("Velocity to dynamics: below 1 soft playing gets louder sooner, above 1 later.");
    dynamics.setTooltip ("Added to every note's dynamics.");
    octave.setTooltip ("Moves every note by octaves. +1 suits typing keyboards (z = C4 in FL Studio).");
    behaviour.setTooltip ("Keyswitches arrive with the articulations (M4 and M7).");
    mpe.setTooltip ("MPE (lower zone, notes on channels 2-16): each note's pitch bend bends it (range: MPE Bend "
                    "Range, 48 semitones), pressure sets the dynamics, CC74 moves the bow towards the bridge.");
    for (auto* c : std::initializer_list<juce::Component*> { &curve, &dynamics, &octave, &behaviour, &mpe, &learn })
        addAndMakeVisible (c);
    startTimerHz (15);
}

void MidiView::resized()
{
    const int y = juce::roundToInt (116 - top);
    learn.setBounds (354, y + 56, 120, 34);
    curve.setBounds (680 + 432 - 50, y + 110 - 44, 100, 100);
    dynamics.setBounds (680 + 432 - 50, y + 240 - 44, 100, 100);
    const int yb = y + 360;
    octave.setLayout (0, 30, 4, 46);
    octave.setBounds (696, yb + 58, 250, 30);
    behaviour.setLayout (0, 30, 4, 96);
    behaviour.setBounds (696, yb + 126, 300, 30);
    mpe.setLayout (0, 30, 4, 60);
    mpe.setBounds (1016, yb + 126, 130, 30);
}

void MidiView::timerCallback()
{
    const int v = processor.getTelemetry().velocity.load();
    if (v != shownVelocity && isShowing())
    {
        shownVelocity = v;
        repaint();
    }
}

void MidiView::paint (juce::Graphics& g)
{
    const float x = 24, y = 116 - top;
    drawPanel (g, { x, y, 640, 576 }, "Controller map");
    drawText (g, "MAP", x + 16, y + 50, Fonts::sans (10).withExtraKerningFactor (0.06f), colours::muted);
    box (g, { x + 16, y + 56, 300, 34 }, "Octavio 2 (velocity = dynamics)", false);
    drawText (g, "OTHER MAPS", x + 462, y + 50, Fonts::sans (10).withExtraKerningFactor (0.06f), colours::muted);
    box (g, { x + 462, y + 56, 162, 34 }, "Octavio 1 legacy", false);

    struct Row
    {
        const char *cc, *source, *target, *range;
        bool player;
    };
    static const Row rows[] = {
        { "Vel", "Note on", "Dynamics", "pp – ff", true },
        { "CC1", "Mod wheel", "Dynamics (drawn)", "takes over", true },
        { "CC19", "", "Vibrato rate (drawn)", "takes over", true },
        { "CC26", "", "Vibrato width (drawn)", "takes over", true },
        { "CC74", "", "Contact point (drawn)", "takes over", true },
        { "CC11", "Expression", "Level trim", "100 = as played", false },
        { "CC21", "", "Intonation", "64 = none, 1 ct/step", false },
        { "CC22", "", "Bow pressure", "64 = as played", false },
        { "CC23", "", "Attack bite", "64 = none", false },
        { "CC24", "", "Vibrato width", "0 – 8×, 64 = as played", false },
        { "CC25", "", "Vibrato relax", "10 ms / step", false },
        { "CC121", "Reset", "Curves back to player", "", false },
    };
    const float hy = y + 120;
    const float cols[] = { 0, 70, 200, 350, 540 };
    const char* heads[] = { "CC", "Source", "Controls", "Range", "Player" };
    for (int j = 0; j < 5; ++j)
        drawLabel (g, heads[j], x + 16 + cols[j], hy);
    for (int i = 0; i < (int) std::size (rows); ++i)
    {
        const auto& r = rows[i];
        const float ry = hy + 10 + i * 31;
        g.setColour (i % 2 == 0 ? colours::panel2 : colours::panel);
        g.fillRoundedRectangle (x + 12, ry, 616, 28, 6);
        drawText (g, r.cc, x + 16, ry + 19, Fonts::mono (13), colours::gold);
        drawText (g, r.source, x + 86, ry + 19, Fonts::sans (12), colours::muted);
        drawText (g, r.target, x + 216, ry + 19, Fonts::sans (13), colours::text);
        drawText (g, juce::String::fromUTF8 (r.range), x + 366, ry + 19, Fonts::mono (11), colours::muted);
        if (r.player)
        {
            drawBadge (g, { x + 566, ry + 14 }, Mode::autoMode);
            drawText (g, "shapes it", x + 582, ry + 18, Fonts::sans (11), colours::muted);
        }
    }
    drawText (g,
              "Sustain, pitch bend, any CC to any parameter and Learn arrive with MIDI",
              x + 16,
              y + 530,
              Fonts::sans (11.5f),
              colours::dim);
    drawText (g,
              "mapping (M6). Your host's own MIDI learn works on every parameter now.",
              x + 16,
              y + 548,
              Fonts::sans (11.5f),
              colours::dim);

    paintVelocity (g, { 680, y, 496, 348 });

    const float yb = y + 360;
    drawPanel (g, { 680, yb, 496, 216 }, "Octave, keyswitches and MPE");
    drawLabel (g, "Octave", 696, yb + 50);
    drawText (g, "+1: z on a typing keyboard plays C4", 960, yb + 78, Fonts::sans (11), colours::dim);
    drawLabel (g, "Keyswitches", 696, yb + 118);
    drawLabel (g, "MPE", 1016, yb + 118);
    drawText (g,
              juce::String::fromUTF8 ("Keyswitches (C1–B1) and MPE (pressure → dynamics, slide → contact)"),
              696,
              yb + 182,
              Fonts::sans (11),
              colours::dim);
    drawText (g,
              juce::String::fromUTF8 ("arrive with the articulations; C1–B1 are silent until then."),
              696,
              yb + 199,
              Fonts::sans (11),
              colours::dim);
}

void MidiView::paintVelocity (juce::Graphics& g, juce::Rectangle<float> r)
{
    drawPanel (g, r, "Velocity", "full range");
    const float gx = r.getX() + 70, gy = r.getY() + 56, gw = 300, gh = 226;
    g.setColour (colours::well);
    g.fillRoundedRectangle (gx, gy, gw, gh, 4);
    static const char* dyn[] = { "pp", "p", "mp", "mf", "f", "ff" };
    for (int i = 0; i < 6; ++i)
    {
        const float yy = gy + gh - (i + 0.5f) * gh / 6;
        g.setColour (colours::line);
        g.drawLine (gx, yy, gx + gw, yy, 0.6f);
        drawText (g, dyn[i], gx - 12, yy + 5, Fonts::serif (14), colours::muted, juce::Justification::right);
    }
    for (int v : { 1, 32, 64, 100, 127 })
    {
        const float xx = gx + gw * v / 127.0f;
        g.setColour (colours::muted);
        g.drawLine (xx, gy + gh, xx, gy + gh + 5);
        drawText (g,
                  juce::String (v),
                  xx,
                  gy + gh + 18,
                  Fonts::mono (10),
                  colours::muted,
                  juce::Justification::horizontallyCentred);
    }
    drawText (g,
              "velocity",
              gx + gw / 2,
              gy + gh + 36,
              Fonts::sans (10),
              colours::dim,
              juce::Justification::horizontallyCentred);

    const auto& pp = processor.getEngine().getPlayer().pp;
    auto& state = processor.getParameters();
    const double c = state.getRawParameterValue (params::id::velocityCurve.getParamID())->load();
    const double bias = state.getRawParameterValue (params::id::dynamics.getParamID())->load() / 100.0;
    auto dynOf = [&pp] (double v, double curve, double b)
    {
        const double x = std::clamp ((v - pp.velLo) / (pp.velHi - pp.velLo), 0.0, 1.0);
        return std::clamp (std::pow (x, curve) + b, 0.0, 1.0);
    };
    auto pointOf = [&] (double v, double curve, double b)
    { return juce::Point<float> (gx + gw * (float) v / 127.0f, gy + gh - (float) dynOf (v, curve, b) * gh); };
    for (int pass = 0; pass < 2; ++pass)
    {
        juce::Path p;
        for (int v = 1; v <= 127; ++v)
        {
            const auto pt = pass == 0 ? pointOf (v, 1.0, 0.0) : pointOf (v, c, bias);
            v == 1 ? p.startNewSubPath (pt) : p.lineTo (pt);
        }
        if (pass == 0)
        {
            juce::Path dashed;
            const float dashes[] = { 4, 4 };
            juce::PathStrokeType (1.5f).createDashedStroke (dashed, p, dashes, 2);
            g.setColour (colours::muted);
            g.fillPath (dashed);
        }
        else
        {
            g.setColour (colours::amber);
            g.strokePath (p, juce::PathStrokeType (2.5f));
        }
    }
    for (int v : { 64, 100, 127 })
    {
        const auto pt = pointOf (v, c, bias);
        g.setColour (colours::gold);
        g.fillEllipse (pt.x - 4.5f, pt.y - 4.5f, 9, 9);
        drawText (g,
                  juce::String (v) + " = " + dynamicName ((float) dynOf (v, c, bias)),
                  pt.x - 8,
                  pt.y - 10,
                  Fonts::sans (10.5f),
                  colours::gold,
                  juce::Justification::right);
    }
    if (shownVelocity > 0)
    {
        const auto pt = pointOf (shownVelocity, c, bias);
        g.setColour (colours::steel);
        g.drawEllipse (pt.x - 7, pt.y - 7, 14, 14, 2);
        drawText (g,
                  "last note " + juce::String (shownVelocity),
                  gx + gw - 8,
                  gy + 18,
                  Fonts::sans (10.5f),
                  colours::steel,
                  juce::Justification::right);
    }
    drawText (g,
              "dashed = even curve",
              r.getX() + 432,
              r.getY() + 176,
              Fonts::sans (10.5f),
              colours::muted,
              juce::Justification::horizontallyCentred);
}
} // namespace octavio2::ui
