#include "PluginEditor.h"

#include "ui/ArticulationView.h"
#include "ui/BowView.h"
#include "ui/CurvesView.h"
#include "ui/LeftHandView.h"
#include "ui/MidiView.h"
#include "ui/PlayView.h"
#include "ui/ToneView.h"

#include <cstring>

namespace octavio2
{
using namespace ui;

namespace
{
constexpr float viewTop = 104;
const char* tabNames[] = { "Play", "Curves", "Bow", "Left hand", "Articulation", "Tone", "MIDI" };
constexpr int tabCount = 7;

void stopClicksTakingFocus (juce::Component& c)
{
    c.setMouseClickGrabsKeyboardFocus (false);
    for (auto* child : c.getChildren())
        stopClicksTakingFocus (*child);
}

// a dropdown or button of a later feature, drawn as in the mockups
void previewBox (juce::Graphics& g,
                 juce::Rectangle<float> r,
                 const juce::String& text,
                 bool arrow,
                 juce::Justification j = juce::Justification::left)
{
    g.setColour (colours::panel2);
    g.fillRoundedRectangle (r, 7);
    g.setColour (colours::line);
    g.drawRoundedRectangle (r.reduced (0.5f), 7, 1);
    const auto font = Fonts::sans (13);
    if (j == juce::Justification::left)
        drawText (g, text, r.getX() + 12, r.getCentreY() + 5, font, colours::muted);
    else
        drawText (g,
                  text,
                  r.getCentreX(),
                  r.getCentreY() + 5,
                  font,
                  colours::muted,
                  juce::Justification::horizontallyCentred);
    if (arrow)
    {
        juce::Path p;
        const float cx = r.getRight() - 20, cy = r.getCentreY() - 3;
        p.startNewSubPath (cx, cy);
        p.lineTo (cx + 5, cy + 6);
        p.lineTo (cx + 10, cy);
        g.setColour (colours::dim);
        g.strokePath (p, juce::PathStrokeType (1.5f));
    }
}
} // namespace

Editor::Editor (Processor& p)
    : AudioProcessorEditor (p),
      processor (p),
      mode (&p.getParameters(), params::id::mode.getParamID(), { { "Live" }, { "Studio" } }),
      keyboard (p),
      presets (p)
{
    setLookAndFeel (&lookAndFeel);
    mode.setLayout (0, 30, 4, 70);
    mode.setTooltip ("Live plays at once. Studio looks 1.2 s ahead so the player knows each note's length; the DAW "
                     "compensates the delay.");
    instrument.setTooltip ("Viola, cello and bass arrive after the violin (the strings section).");
    // M7: the player's style, offsets on the automation (vibrato, slides, bow, swells, intonation)
    for (int i = 0; i < params::playerStyleNames().size(); ++i)
        player.addItem ("Player: " + params::playerStyleNames()[i], i + 1);
    playerAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        p.getParameters(),
        params::id::playerStyle.getParamID(),
        player);
    player.setTooltip ("How the virtual violinist plays: vibrato, slides, bow strokes, swells and expressive "
                       "intonation. Modern soloist is the default player; the others shift its habits and still "
                       "follow your controls.");
    for (auto* c : std::initializer_list<juce::Component*> { &mode, &keyboard, &instrument, &player, &presets })
        canvas.addAndMakeVisible (c);
    mode.setBounds (916, 16, 144, 30);
    instrument.setBounds (214, 14, 170, 34);
    player.setBounds (394, 14, 180, 34);
    presets.setBounds (592, 14, 306, 34);
    keyboard.setBounds (24, 712, designWidth - 48, 58);
    canvas.views.push_back (std::make_unique<PlayView> (p));
    canvas.views.push_back (std::make_unique<CurvesView> (p));
    canvas.views.push_back (std::make_unique<BowView> (p));
    canvas.views.push_back (std::make_unique<LeftHandView> (p));
    canvas.views.push_back (std::make_unique<ArticulationView> (p));
    canvas.views.push_back (std::make_unique<ToneView> (p));
    canvas.views.push_back (std::make_unique<MidiView> (p));
    for (auto& v : canvas.views)
    {
        canvas.addChildComponent (*v);
        v->setBounds (0, (int) viewTop, designWidth, 596);
    }
    canvas.showTab (juce::jlimit (0, tabCount - 1, p.editorTab));
    addAndMakeVisible (canvas);

    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) designWidth / designHeight);
    setResizeLimits (designWidth / 2, designHeight / 2, designWidth * 2, designHeight * 2);
    setSize (designWidth, designHeight);
    // clicks must not take the computer keyboard from the host (FL Studio, Ableton); runs last
    stopClicksTakingFocus (*this);
    startTimerHz (4);
}

Editor::~Editor()
{
    setLookAndFeel (nullptr);
}

void Editor::resized()
{
    canvas.setBounds (0, 0, designWidth, designHeight);
    canvas.setTransform (
        juce::AffineTransform::scale ((float) getWidth() / designWidth, (float) getHeight() / designHeight));
}

void Editor::timerCallback()
{
    const auto cpu = juce::String (juce::roundToInt (processor.getTelemetry().cpu.load() * 100)) + "%";
    const auto latency = juce::String (juce::roundToInt (processor.getLatencyMs())) + " ms";
    if (cpu != cpuText || latency != latencyText)
    {
        cpuText = cpu;
        latencyText = latency;
        canvas.repaint (0, 0, designWidth, (int) viewTop);
    }
}

//==============================================================================
Editor::Canvas::Canvas (Editor& e)
    : editor (e)
{
    setOpaque (true);
}

juce::Rectangle<float> Editor::Canvas::tabBounds (int i) const
{
    float x = 24;
    for (int k = 0; k < i; ++k)
        x += (float) std::strlen (tabNames[k]) * 7.6f + 28;
    return { x, 66, (float) std::strlen (tabNames[i]) * 7.6f + 28, 34 };
}

void Editor::Canvas::showTab (int t)
{
    tab = t;
    editor.processor.editorTab = t;
    for (size_t i = 0; i < views.size(); ++i)
        views[i]->setVisible ((int) i == t);
    repaint();
}

void Editor::Canvas::mouseUp (const juce::MouseEvent& e)
{
    for (int i = 0; i < tabCount; ++i)
        if (tabBounds (i).contains (e.position))
            showTab (i);
}

void Editor::Canvas::mouseMove (const juce::MouseEvent& e)
{
    for (int i = 0; i < tabCount; ++i)
        if (tabBounds (i).contains (e.position))
        {
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            return;
        }
    setMouseCursor (juce::MouseCursor::NormalCursor);
}

void Editor::Canvas::paint (juce::Graphics& g)
{
    g.fillAll (colours::bg);
    // logo
    const auto serif = Fonts::serif (34);
    drawText (g, "Octavio ", 24, 40, serif, colours::text);
    const float x2 = 24 + juce::GlyphArrangement::getStringWidth (serif, "Octavio ");
    drawText (g, "2", x2, 40, serif, colours::amber);
    drawText (g,
              JucePlugin_VersionString,
              x2 + juce::GlyphArrangement::getStringWidth (serif, "2") + 8,
              40,
              Fonts::mono (11),
              colours::dim);
    // instrument, player, presets (previews)
    previewBox (g, { 214, 14, 170, 34 }, "Modern violin", true);
    // presets: ui::PresetBar (M6); player: the Player Style box (M7)
    drawText (g, editor.latencyText, 1176, 36, Fonts::mono (11), colours::muted, juce::Justification::right);
    // tabs
    for (int i = 0; i < tabCount; ++i)
    {
        const auto r = tabBounds (i);
        const bool on = i == tab;
        drawText (g,
                  tabNames[i],
                  r.getCentreX(),
                  86,
                  Fonts::sans (13, on),
                  on ? colours::text : colours::muted,
                  juce::Justification::horizontallyCentred);
        if (on)
        {
            g.setColour (colours::amber);
            g.fillRoundedRectangle (r.getX() + 8, 95, r.getWidth() - 16, 2.5f, 1);
        }
    }
    g.setColour (colours::line);
    g.drawLine (24, 100, designWidth - 24, 100);
    drawText (g,
              juce::String::fromUTF8 ("CPU ") + editor.cpuText + juce::String::fromUTF8 ("   ·   engine 48 kHz"),
              designWidth - 24,
              86,
              Fonts::mono (11),
              colours::dim,
              juce::Justification::right);
    // keyboard frame and labels
    g.setColour (juce::Colour (0xff0d0a08));
    g.fillRoundedRectangle (16, 704, designWidth - 32, 68, 8);
    const float kw = (designWidth - 48) / 43.0f;
    auto name = [] (int note) { return juce::MidiMessage::getMidiNoteName (note, true, true, middleCOctave()); };
    drawText (g,
              "KEYSWITCHES  " + name (24) + juce::String::fromUTF8 ("–") + name (35),
              24 + 3.5f * kw,
              700,
              Fonts::sans (9).withExtraKerningFactor (0.1f),
              colours::steel.withAlpha (0.6f),
              juce::Justification::horizontallyCentred);
    drawText (g,
              "below " + name (55) + ": silent",
              24 + 11 * kw,
              700,
              Fonts::sans (9),
              colours::dim,
              juce::Justification::horizontallyCentred);
    drawText (g,
              "violin range " + name (55) + juce::String::fromUTF8 (" – ") + name (104),
              24 + 19.5f * kw,
              700,
              Fonts::sans (9),
              colours::muted);
}
} // namespace octavio2
