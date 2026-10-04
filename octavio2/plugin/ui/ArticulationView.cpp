#include "ArticulationView.h"

namespace octavio2::ui
{
namespace
{
constexpr float top = 104;
}

ArticulationView::ArticulationView (Processor& p)
    : articulation (&p.getParameters(),
                    params::id::articulation.getParamID(),
                    { { "Arco", "bowed (C1)" },
                      { "Pizzicato", "plucked (C#1)" },
                      { juce::String::fromUTF8 ("Bartók pizz"), "snaps on the board (D1)" },
                      { "Left-hand pizz", "plucked by a finger (D#1)" },
                      { "Harmonics", "light finger, glassy (E1)" } }),
      bowStyle (&p.getParameters(),
                params::id::bowStyle.getParamID(),
                { { "Auto", "the player decides" },
                  { "Legato", "joined notes slurred" },
                  { juce::String::fromUTF8 ("Détaché"), "a new bow every note" },
                  { "Staccato", "short, stopped" },
                  { juce::String::fromUTF8 ("Martelé"), "bitten, stopped" },
                  { "Spiccato", "off the string" } }),
      fingerPlan (&p.getParameters(), params::id::fingerPlan.getParamID(), { { "Off" }, { "Plan ahead" } }),
      drawnCurves (&p.getParameters(), params::id::drawnCurves.getParamID(), { { "Ignore" }, { "Take over" } }),
      phrasing (&p.getParameters(),
                params::id::phrasing.getParamID(),
                "Phrasing",
                Knob::Style::small,
                [] (float v) { return v < 0.5f ? juce::String ("off") : juce::String (juce::roundToInt (v)) + " %"; })
{
    articulation.setTooltip ("How the notes are played. The keyswitches C1 to E1 choose the same; moving this "
                             "control wins over the last keyswitch.");
    bowStyle.setTooltip ("Auto reads each note's length, gap and overlap. The others play every bowed note one way.");
    fingerPlan.setTooltip ("Studio mode: choose strings and positions for the whole look-ahead, with shifts that "
                           "land on the beat. Off: decided note by note, as in Live mode.");
    drawnCurves.setTooltip ("Drawn lanes (CC1 dynamics, CC26 vibrato width, CC19 vibrato rate, CC74 contact) take "
                            "over from the player. CC121 hands everything back.");
    phrasing.setTooltip ("How much the player shapes phrases: arches, swells on long notes and stress, most "
                         "when every velocity is the same.");
    for (auto* c : { &articulation, &bowStyle, &fingerPlan, &drawnCurves })
        addAndMakeVisible (c);
    addAndMakeVisible (phrasing);
}

void ArticulationView::resized()
{
    const int y = juce::roundToInt (116 - top);
    articulation.setLayout (1, 34, 6);
    articulation.setBounds (40, y + 60, 400, 5 * 40);
    bowStyle.setLayout (1, 34, 6);
    bowStyle.setBounds (488, y + 60, 400, 6 * 40);
    phrasing.setBounds (40, y + 360, 100, 100);
    fingerPlan.setLayout (0, 30, 4, 120);
    fingerPlan.setBounds (190, y + 380, 250, 30);
    drawnCurves.setLayout (0, 30, 4, 120);
    drawnCurves.setBounds (488, y + 380, 250, 30);
}

void ArticulationView::paint (juce::Graphics& g)
{
    const float y = 116 - top;
    drawPanel (g, { 24, y, 432, 576 }, "Articulation", "keyswitches C1-E1");
    drawPanel (g, { 472, y, designWidth - 496, 576 }, "Bow style");
    drawLabel (g, "Player", 40, y + 344);
    drawLabel (g, "Fingering (Studio)", 190, y + 370);
    drawLabel (g, "Drawn curves", 488, y + 370);
    drawText (g, "Keyswitches work whatever the Octave setting.", 40, y + 500, Fonts::sans (11.5f), colours::dim);
    drawText (g,
              "Harmonics: natural where the note has a node, else artificial.",
              40,
              y + 518,
              Fonts::sans (11.5f),
              colours::dim);
}
} // namespace octavio2::ui
