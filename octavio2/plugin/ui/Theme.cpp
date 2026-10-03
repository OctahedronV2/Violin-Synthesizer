#include "Theme.h"

#include "Octavio2BinaryData.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace octavio2::ui
{
namespace
{
juce::Typeface::Ptr load (const void* data, int size)
{
    return juce::Typeface::createSystemTypefaceFor (data, static_cast<size_t> (size));
}

juce::Typeface::Ptr serifFace()
{
    static auto f
        = load (Octavio2BinaryData::InstrumentSerifRegular_ttf, Octavio2BinaryData::InstrumentSerifRegular_ttfSize);
    return f;
}
juce::Typeface::Ptr sansFace()
{
    static auto f = load (Octavio2BinaryData::IBMPlexSansRegular_ttf, Octavio2BinaryData::IBMPlexSansRegular_ttfSize);
    return f;
}
juce::Typeface::Ptr sansBoldFace()
{
    static auto f = load (Octavio2BinaryData::IBMPlexSansSemiBold_ttf, Octavio2BinaryData::IBMPlexSansSemiBold_ttfSize);
    return f;
}
juce::Typeface::Ptr monoFace()
{
    static auto f = load (Octavio2BinaryData::IBMPlexMonoRegular_ttf, Octavio2BinaryData::IBMPlexMonoRegular_ttfSize);
    return f;
}

// SVG font sizes are em sizes; JUCE heights include ascent and descent
juce::Font sized (juce::Typeface::Ptr face, float emSize)
{
    juce::Font f { juce::FontOptions (face) };
    return f.withPointHeight (emSize);
}
} // namespace

juce::Font Fonts::serif (float h)
{
    return sized (serifFace(), h);
}
juce::Font Fonts::sans (float h, bool semibold)
{
    return sized (semibold ? sansBoldFace() : sansFace(), h);
}
juce::Font Fonts::mono (float h)
{
    return sized (monoFace(), h);
}

void drawText (juce::Graphics& g,
               const juce::String& s,
               float x,
               float baseline,
               const juce::Font& font,
               juce::Colour c,
               juce::Justification j)
{
    g.setColour (c);
    g.setFont (font);
    const float w = juce::GlyphArrangement::getStringWidth (font, s);
    float left = x;
    if (j.testFlags (juce::Justification::horizontallyCentred))
        left = x - w / 2;
    else if (j.testFlags (juce::Justification::right))
        left = x - w;
    g.drawSingleLineText (s, juce::roundToInt (left), juce::roundToInt (baseline));
}

void drawLabel (juce::Graphics& g, const juce::String& s, float x, float baseline, juce::Colour c, float size)
{
    auto f = Fonts::sans (size).withExtraKerningFactor (0.1f);
    drawText (g, s.toUpperCase(), x, baseline, f, c);
}

void drawBadge (juce::Graphics& g, juce::Point<float> c, Mode m)
{
    const auto col = m == Mode::autoMode ? colours::amber : m == Mode::guided ? colours::steel : colours::coral;
    const juce::String letter = m == Mode::autoMode ? "A" : m == Mode::guided ? "G" : "M";
    g.setColour (colours::bg);
    g.fillEllipse (c.x - 9, c.y - 9, 18, 18);
    g.setColour (col);
    g.drawEllipse (c.x - 9, c.y - 9, 18, 18, 1.5f);
    drawText (g, letter, c.x, c.y + 4, Fonts::sans (10, true), col, juce::Justification::horizontallyCentred);
}

void drawPanel (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title, const juce::String& right)
{
    g.setColour (colours::panel);
    g.fillRoundedRectangle (r, 10);
    g.setColour (colours::line);
    g.drawRoundedRectangle (r.reduced (0.5f), 10, 1);
    if (title.isNotEmpty())
        drawText (g,
                  title.toUpperCase(),
                  r.getX() + 16,
                  r.getY() + 24,
                  Fonts::sans (11, true).withExtraKerningFactor (0.12f),
                  colours::amber);
    if (right.isNotEmpty())
        drawText (g,
                  right,
                  r.getRight() - 16,
                  r.getY() + 24,
                  Fonts::sans (11),
                  colours::muted,
                  juce::Justification::right);
}

void drawKnob (juce::Graphics& g, juce::Point<float> c, float r, float v, juce::Colour col, float ghost)
{
    const float a0 = juce::degreesToRadians (-135.0f), a1 = juce::degreesToRadians (135.0f);
    auto arc = [&] (float from, float to, juce::Colour colour)
    {
        juce::Path p;
        p.addCentredArc (c.x, c.y, r, r, 0, from, to, true);
        g.setColour (colour);
        g.strokePath (p, juce::PathStrokeType (6, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };
    arc (a0, a1, colours::line);
    if (ghost >= 0)
    {
        const float ga = a0 + (a1 - a0) * ghost;
        g.setColour (colours::amber.withAlpha (0.8f));
        g.fillEllipse (c.x + r * std::sin (ga) - 3, c.y - r * std::cos (ga) - 3, 6, 6);
    }
    const float a = a0 + (a1 - a0) * juce::jlimit (0.0f, 1.0f, v);
    if (v > 0.002f)
        arc (a0, a, col);
    const float ri = r - 10;
    g.setColour (colours::panel2);
    g.fillEllipse (c.x - ri, c.y - ri, 2 * ri, 2 * ri);
    g.setColour (colours::line);
    g.drawEllipse (c.x - ri, c.y - ri, 2 * ri, 2 * ri, 1);
    g.setColour (colours::text);
    g.drawLine (c.x, c.y, c.x + (r - 14) * std::sin (a), c.y - (r - 14) * std::cos (a), 2.5f);
}

int middleCOctave()
{
    static const int octave = []
    {
        const juce::PluginHostType host;
        if (host.isFruityLoops())
            return 5;
        if (host.isAbletonLive() || host.isCubase() || host.isNuendo() || host.isLogic() || host.isGarageBand()
            || host.isBitwigStudio() || host.isStudioOne())
            return 3;
        return 4;
    }();
    return octave;
}

juce::String noteName (int midi)
{
    static const char* names[] = { "C", "C♯", "D", "D♯", "E", "F", "F♯", "G", "G♯", "A", "A♯", "B" };
    if (midi < 0)
        return "-";
    return juce::String::fromUTF8 (names[midi % 12]) + juce::String (midi / 12 + middleCOctave() - 5);
}

juce::String dynamicName (float d)
{
    static const char* names[] = { "pp", "p", "mp", "mf", "f", "ff" };
    return names[juce::jlimit (0, 5, static_cast<int> (d * 6.0f))];
}
} // namespace octavio2::ui
