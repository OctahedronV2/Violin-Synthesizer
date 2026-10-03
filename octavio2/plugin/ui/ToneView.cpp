#include "ToneView.h"

namespace octavio2::ui
{
namespace
{
constexpr float top = 104;

juce::String db (float v)
{
    return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB";
}

std::vector<Choices::Item> previewItems (std::initializer_list<const char*> names)
{
    std::vector<Choices::Item> items;
    for (auto* n : names)
        items.push_back ({ juce::String::fromUTF8 (n) });
    return items;
}
} // namespace

ToneView::ToneView (Processor& p)
    : processor (p),
      body (nullptr,
            {},
            { { "Measured violin", "full band, directional" },
              { "Stoppani", "warm, even" },
              { "Klimke", "bright, focused" },
              { "Levaggi", "dark, round" },
              { "TU Berlin", "balanced" } }),
      strings (nullptr, {}, previewItems ({ "Synthetic", "Gut", "Steel" })),
      rosin (nullptr, {}, previewItems ({ "Light", "Standard", "Dark", "Baroque" })),
      bow (nullptr, {}, previewItems ({ "Modern", "Baroque" })),
      mute (nullptr, {}, previewItems ({ "Off", "Sordino", "Practice" })),
      quality (nullptr, {}, previewItems ({ "High (96 kHz strings)", "Eco" })),
      rooms (&p.getParameters(),
             params::id::room.getParamID(),
             previewItems ({ "Close mics",
                             "Arvedi near",
                             "Arvedi far",
                             "Brahmssaal",
                             "Church",
                             "Maida Vale 4",
                             "Maida Vale 5",
                             "WDR control",
                             "WDR studio" })),
      bridge (nullptr, {}, "Bridge", Knob::Style::small),
      sympathetic (nullptr, {}, "Sympathetic", Knob::Style::small),
      wolf (nullptr, {}, "Wolf", Knob::Style::small),
      hold (nullptr, {}, "Hold", Knob::Style::small),
      brilliance (&p.getParameters(), params::id::brightness.getParamID(), "Brilliance", Knob::Style::small, db),
      imperfection (nullptr, {}, "Imperfection", Knob::Style::small),
      reverb (&p.getParameters(), params::id::reverb.getParamID(), "Reverb", Knob::Style::small, db),
      volume (&p.getParameters(), params::id::volume.getParamID(), "Volume", Knob::Style::small, db),
      width (nullptr, {}, "Width", Knob::Style::small),
      movement (nullptr, {}, "Movement", Knob::Style::small)
{
    body.setTooltip ("More measured bodies arrive with the radiation milestone (M1).");
    strings.setTooltip ("String types arrive with the styles milestone (M7).");
    rosin.setPreviewActive (1);
    rosin.setTooltip ("Rosin types arrive with the second bow-physics milestone (M2).");
    bow.setTooltip ("The baroque bow arrives with the styles milestone (M7).");
    mute.setTooltip ("Mutes arrive with the radiation milestone (M1).");
    quality.setTooltip ("Eco arrives with the optimisation milestone (M8).");
    rooms.setTooltip ("Measured rooms (impulse responses, credits in the About box). Close mics: the violin alone.");
    bridge.setPreview (0.5f, "2.9 kHz", "the bridge milestone (M3)");
    sympathetic.setPreview (0.6f, "60 %", "the bridge milestone (M3)");
    wolf.setPreview (0.3f, "30 %", "the bridge milestone (M3)");
    hold.setPreview (0.45f, "chin", "the bridge milestone (M3)");
    imperfection.setPreview (0.1f, "10 %", "the player milestone (M4)");
    width.setPreview (0.6f, "60 %", "the radiation milestone (M1)");
    movement.setPreview (0.3f, "sway", "the radiation milestone (M1)");
    brilliance.setTooltip ("A high shelf at 1.5 kHz on the bridge force (the strings' sparkle).");
    reverb.setTooltip ("The room's level against the direct sound.");
    for (auto* c : { &body, &strings, &rosin, &bow, &mute, &quality, &rooms })
        addAndMakeVisible (c);
    for (auto* k :
         { &bridge, &sympathetic, &wolf, &hold, &brilliance, &imperfection, &reverb, &volume, &width, &movement })
        addAndMakeVisible (k);
    startTimerHz (30);
}

void ToneView::resized()
{
    const int y = juce::roundToInt (116 - top);
    // instrument
    body.setLayout (1, 30, 6);
    body.setBounds (40, y + 60, 328, 5 * 39);
    strings.setLayout (0, 30, 4, 104);
    strings.setBounds (40, y + 290, 328, 30);
    rosin.setLayout (0, 30, 4, 77);
    rosin.setBounds (40, y + 350, 328, 30);
    bow.setLayout (0, 30, 4, 160);
    bow.setBounds (40, y + 410, 328, 30);
    // bridge and resonance
    int i = 0;
    for (auto* k : { &bridge, &sympathetic, &wolf, &hold, &brilliance, &imperfection })
    {
        const int cx = 396 + 70 + (i % 3) * 110, cy = y + 100 + (i / 3) * 120;
        k->setBounds (cx - 50, cy - 44, 100, 100);
        ++i;
    }
    mute.setLayout (0, 30, 4, 104);
    mute.setBounds (412, y + 318, 328, 30);
    quality.setLayout (0, 30, 4, 160);
    quality.setBounds (412, y + 380, 328, 30);
    // room
    rooms.setLayout (3, 32, 8);
    rooms.setBounds (784, y + 314, 376, 3 * 44);
    i = 0;
    for (auto* k : { &reverb, &volume, &width, &movement })
    {
        const int cx = 768 + 62 + i * 95, cy = y + 504;
        k->setBounds (cx - 47, cy - 44, 94, 100);
        ++i;
    }
}

void ToneView::timerCallback()
{
    if (isShowing())
        repaint();
}

void ToneView::paint (juce::Graphics& g)
{
    const float y = 116 - top;
    // instrument
    drawPanel (g, { 24, y, 360, 576 }, "Instrument");
    drawLabel (g, "Body (measured violins)", 40, y + 50);
    drawLabel (g, "Strings", 40, y + 282);
    drawLabel (g, "Rosin", 40, y + 342);
    drawLabel (g, "Bow", 40, y + 402);
    drawLabel (g, "Tuning", 40, y + 462);
    for (int k = 0; k < 2; ++k)
    {
        const juce::Rectangle<float> r (40 + k * 168.0f, y + 470, 160, 34);
        g.setColour (colours::panel2);
        g.fillRoundedRectangle (r, 7);
        g.setColour (colours::line);
        g.drawRoundedRectangle (r.reduced (0.5f), 7, 1);
        drawText (g,
                  k == 0 ? "A4 = 440 Hz" : "Expressive",
                  r.getX() + 12,
                  r.getCentreY() + 5,
                  Fonts::sans (13),
                  colours::muted);
    }
    drawText (g, "Tuning systems arrive with M7.", 40, y + 526, Fonts::sans (11.5f), colours::dim);
    drawText (g, "Every option swaps a physical part.", 40, y + 548, Fonts::sans (11.5f), colours::dim);
    drawText (g, "There is no EQ anywhere in Octavio 2.", 40, y + 564, Fonts::sans (11.5f), colours::dim);

    // bridge and resonance
    drawPanel (g, { 396, y, 360, 576 }, "Bridge and resonance");
    drawLabel (g, "Mute", 412, y + 310);
    drawLabel (g, "Quality", 412, y + 372);
    drawText (g, "Eco arrives with the optimisation milestone.", 412, y + 428, Fonts::sans (11.5f), colours::dim);
    paintScope (g, { 412, y + 470, 328, 90 });

    // microphones and room
    drawPanel (g, { 768, y, 408, 576 }, "Microphones and room");
    paintMics (g, 768, y);
    drawLabel (g, "Room", 784, y + 306);
}

void ToneView::paintScope (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto& T = processor.getTelemetry();
    const int note = T.note.load();
    const int s = T.string.load();
    drawLabel (g, juce::String ("String motion (") + "GDAE"[juce::jlimit (0, 3, s)] + ")", r.getX(), r.getY() - 10);
    g.setColour (colours::well);
    g.fillRoundedRectangle (r, 6);
    const float slips = T.slips.load();
    const bool clean = slips < 1.5f;
    if (note < 0)
    {
        drawText (g,
                  "play a note",
                  r.getRight() - 8,
                  r.getY() + 18,
                  Fonts::sans (10.5f),
                  colours::dim,
                  juce::Justification::right);
        return;
    }
    drawText (g,
              clean ? juce::String ("clean Helmholtz") : juce::String (juce::roundToInt (slips)) + " slips per period",
              r.getRight() - 8,
              r.getY() + 18,
              Fonts::sans (10.5f),
              clean ? colours::good : colours::coral,
              juce::Justification::right);
    // three periods of the bridge force, from a rising zero crossing
    const auto& engine = processor.getEngine();
    const double hz = 440.0 * std::pow (2.0, (note - 69) / 12.0);
    const int period = juce::jlimit (8, 1200, (int) std::lround (o2::Engine::rate / hz));
    const int length = 3 * period;
    const int64_t end = engine.scopeCount();
    if (end < length * 2 + 16)
        return;
    int64_t start = end - length - 1;
    for (int64_t k = end - length - 1; k > end - length - 1 - period - 2; --k)
        if (engine.scopeSample (k - 1) < 0 && engine.scopeSample (k) >= 0)
        {
            start = k;
            break;
        }
    float peak = 1e-6f;
    scope.resize ((size_t) length);
    for (int k = 0; k < length; ++k)
    {
        scope[(size_t) k] = engine.scopeSample (start + k);
        peak = std::max (peak, std::abs (scope[(size_t) k]));
    }
    juce::Path p;
    for (int k = 0; k < length; ++k)
    {
        const float px = r.getX() + 8 + (r.getWidth() - 16) * k / (float) (length - 1);
        const float py = r.getCentreY() + 6 - scope[(size_t) k] / peak * (r.getHeight() / 2 - 14);
        k == 0 ? p.startNewSubPath (px, py) : p.lineTo (px, py);
    }
    g.setColour (clean ? colours::good : colours::coral);
    g.strokePath (p, juce::PathStrokeType (1.8f));
}

void ToneView::paintMics (juce::Graphics& g, float x3, float y)
{
    const float cx = x3 + 204, cy = y + 186;
    g.setColour (juce::Colour (0xff5b3820));
    g.fillEllipse (cx - 44, cy - 74, 88, 148);
    g.setColour (juce::Colour (0xff8a5a35));
    g.drawEllipse (cx - 44, cy - 74, 88, 148, 1);
    juce::Path ff;
    ff.startNewSubPath (cx - 30, cy - 6);
    ff.quadraticTo (cx - 42, cy + 4, cx - 30, cy + 14);
    ff.startNewSubPath (cx + 30, cy - 6);
    ff.quadraticTo (cx + 42, cy + 4, cx + 30, cy + 14);
    g.setColour (juce::Colour (0xff2a1a0f));
    g.strokePath (ff, juce::PathStrokeType (3));
    g.setColour (juce::Colour (0xff1a1310));
    g.fillRoundedRectangle (cx - 7, cy - 130, 14, 60, 3);
    g.setColour (juce::Colour (0xff3a2516));
    g.fillEllipse (cx - 8, cy - 144, 16, 16);
    drawText (g,
              "violin seen from above (more microphones with M1)",
              cx,
              cy + 92,
              Fonts::sans (10),
              colours::dim,
              juce::Justification::horizontallyCentred);
    struct Mic
    {
        const char* name;
        float x, y;
        bool on;
    };
    const Mic mics[] = { { "Front", cx - 150, cy + 30, true },
                         { "Above", cx + 76, cy - 60, false },
                         { "Player's ear", cx - 110, cy - 100, false },
                         { "Side", cx + 150, cy + 20, false } };
    for (const auto& m : mics)
    {
        if (m.on)
        {
            juce::Path beam;
            beam.addTriangle (m.x, m.y, cx - 40, cy - 10, cx - 40, cy + 40);
            g.setColour (colours::amber.withAlpha (0.12f));
            g.fillPath (beam);
        }
        g.setColour (m.on ? colours::amber : colours::panel2);
        g.fillEllipse (m.x - 9, m.y - 9, 18, 18);
        g.setColour (m.on ? colours::amber : colours::dim);
        g.drawEllipse (m.x - 9, m.y - 9, 18, 18, 1.5f);
        drawText (g,
                  m.name,
                  m.x,
                  m.y + 26,
                  Fonts::sans (11, m.on),
                  m.on ? colours::text : colours::dim,
                  juce::Justification::horizontallyCentred);
    }
}
} // namespace octavio2::ui
