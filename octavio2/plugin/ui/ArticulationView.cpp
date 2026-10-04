#include "ArticulationView.h"

namespace octavio2::ui
{
namespace
{
constexpr float top = 104;
const char* const articulationNotes[] = { "bowed",
                                          "plucked",
                                          "snaps on the board",
                                          "plucked by a finger",
                                          "light finger, glassy",
                                          "quick strokes in one note",
                                          "bouncing, mid-bow",
                                          "pulses in one bow",
                                          "the stick strikes" };
const char* const contactNotes[] = { "as dynamics place it", "at the bridge, glassy", "over the fingerboard" };
juce::String keyName (int note)
{
    return juce::MidiMessage::getMidiNoteName (note, true, true, middleCOctave());
}
} // namespace

ArticulationView::ArticulationView (Processor& p)
    : articulation (&p.getParameters(),
                    params::id::articulation.getParamID(),
                    { { "Arco", "bowed (C1)" },
                      { "Pizzicato", "plucked (C#1)" },
                      { juce::String::fromUTF8 ("Bartók pizz"), "snaps on the board (D1)" },
                      { "Left-hand pizz", "plucked by a finger (D#1)" },
                      { "Harmonics", "light finger, glassy (E1)" },
                      { "Tremolo", "quick strokes in one note (F1)" },
                      { juce::String::fromUTF8 ("Sautillé"), "bouncing, mid-bow (F#1)" },
                      { "Portato", "pulses in one bow (G1)" },
                      { "Col legno battuto", "the stick strikes (G#1)" } }),
      bowStyle (&p.getParameters(),
                params::id::bowStyle.getParamID(),
                { { "Auto", "the player decides" },
                  { "Legato", "joined notes slurred" },
                  { juce::String::fromUTF8 ("Détaché"), "a new bow every note" },
                  { "Staccato", "short, stopped (Live: 120 ms)" },
                  { juce::String::fromUTF8 ("Martelé"), "bitten, stopped (Live: 150 ms)" },
                  { "Spiccato", "off the string (Live: 90 ms)" } }),
      fingerPlan (&p.getParameters(), params::id::fingerPlan.getParamID(), { { "Off" }, { "Plan ahead" } }),
      drawnCurves (&p.getParameters(), params::id::drawnCurves.getParamID(), { { "Ignore" }, { "Take over" } }),
      contact (&p.getParameters(),
               params::id::contactStyle.getParamID(),
               { { "Ordinario", "as dynamics place it (A1)" },
                 { "Sul ponticello", "at the bridge, glassy (A#1)" },
                 { "Sul tasto", "over the fingerboard (B1)" } }),
      tremoloSync (&p.getParameters(),
                   params::id::tremoloSync.getParamID(),
                   { { "Free" }, { "16ths" }, { "16th triplets" }, { "32nds" } }),
      phrasing (&p.getParameters(),
                params::id::phrasing.getParamID(),
                "Phrasing",
                Knob::Style::small,
                [] (float v) { return v < 0.5f ? juce::String ("off") : juce::String (juce::roundToInt (v)) + " %"; }),
      tremoloSpeed (&p.getParameters(),
                    params::id::tremoloSpeed.getParamID(),
                    "Speed",
                    Knob::Style::small,
                    [] (float v) { return juce::String (v, 1) + " /s"; })
{
    articulation.setTooltip ("How the notes are played. The keyswitches (C1 to G#1 by default; MIDI tab) and UACC "
                             "(CC32) choose the same; moving this control wins over the last keyswitch.");
    bowStyle.setTooltip (juce::String::fromUTF8 (
        "Auto reads each note's length, gap and overlap. The others play every bowed note one way: Détaché a new, "
        "articulated stroke per note; Staccato, Martelé and Spiccato short strokes (half the note in Studio mode, "
        "a fixed length in Live mode, where the length is unknown)."));
    contact.setTooltip ("Where the bow meets the string, for every bowed articulation (tremolo sul ponticello "
                        "too). The last three keyswitches (A1, A#1 and B1 by default) choose the same.");
    keyswitchStart = std::make_unique<juce::ParameterAttachment> (
        *p.getParameters().getParameter (params::id::keyswitchStart.getParamID()),
        [this] (float v) { showKeyswitches (juce::roundToInt (v)); });
    keyswitchStart->sendInitialUpdate();
    // 2.3: what plays now (keyswitches and UACC included); a click drops the latch
    const auto& T = p.getTelemetry();
    articulation.setActiveSource (Choices::showLatch (T.articulationLatch, T.articulationParam));
    bowStyle.setActiveSource (Choices::showLatch (T.styleLatch, T.styleParam));
    contact.setActiveSource (Choices::showLatch (T.contactLatch, T.contactParam));
    articulation.setOnClick ([&p] (int) { p.clearKeyswitchLatches (1); });
    bowStyle.setOnClick ([&p] (int) { p.clearKeyswitchLatches (2); });
    contact.setOnClick ([&p] (int) { p.clearKeyswitchLatches (4); });
    startTimerHz (10);
    tremoloSync.setTooltip ("Free plays the Speed below. The others follow the host's tempo (Free when the host "
                            "sends none).");
    tremoloSpeed.setTooltip ("Bow strokes per second of a free (unmeasured) tremolo.");
    fingerPlan.setTooltip ("Studio mode: choose strings and positions for the whole look-ahead, with shifts that "
                           "land on the beat. Off: decided note by note, as in Live mode.");
    drawnCurves.setTooltip ("Drawn lanes (CC1 dynamics, CC26 vibrato width, CC19 vibrato rate, CC74 contact) take "
                            "over from the player. CC121 hands everything back.");
    phrasing.setTooltip ("How much the player shapes phrases: arches, swells on long notes and stress, most "
                         "when every velocity is the same.");
    for (auto* c : { &articulation, &bowStyle, &fingerPlan, &drawnCurves, &contact, &tremoloSync })
        addAndMakeVisible (c);
    addAndMakeVisible (phrasing);
    addAndMakeVisible (tremoloSpeed);
}

void ArticulationView::timerCallback()
{
    const std::array<int, 3> now { articulation.active(), bowStyle.active(), contact.active() };
    if (now != shown)
    {
        shown = now;
        repaint();
    }
}

void ArticulationView::showKeyswitches (int start)
{
    for (int i = 0; i < 9; ++i)
        articulation.setNote (i, juce::String (articulationNotes[i]) + " (" + keyName (start + i) + ")");
    for (int i = 0; i < 3; ++i)
        contact.setNote (i, juce::String (contactNotes[i]) + " (" + keyName (start + 9 + i) + ")");
    keyswitchRange = keyName (start) + "-" + keyName (start + 8);
    repaint();
}

void ArticulationView::resized()
{
    const int y = juce::roundToInt (116 - top);
    articulation.setLayout (1, 32, 6);
    articulation.setBounds (40, y + 60, 400, 9 * 41);
    bowStyle.setLayout (1, 32, 6);
    bowStyle.setBounds (488, y + 60, 320, 6 * 41);
    contact.setLayout (1, 32, 6);
    contact.setBounds (840, y + 60, 320, 3 * 41);
    tremoloSync.setLayout (2, 30, 6);
    tremoloSync.setBounds (840, y + 214, 210, 2 * 41);
    tremoloSpeed.setBounds (1066, y + 196, 100, 100);
    phrasing.setBounds (488, y + 360, 100, 100);
    fingerPlan.setLayout (0, 30, 4, 120);
    fingerPlan.setBounds (640, y + 380, 250, 30);
    drawnCurves.setLayout (0, 30, 4, 120);
    drawnCurves.setBounds (912, y + 380, 250, 30);
}

void ArticulationView::paint (juce::Graphics& g)
{
    const float y = 116 - top;
    drawPanel (g, { 24, y, 432, 576 }, "Articulation", "keyswitches " + keyswitchRange);
    drawPanel (g, { 472, y, designWidth - 496, 576 }, "Bow");
    drawLabel (g, "Bow style", 488, y + 50);
    drawLabel (g, "Contact point", 840, y + 50);
    drawLabel (g, "Tremolo", 840, y + 204);
    drawLabel (g, "Player", 488, y + 344);
    drawLabel (g, "Fingering (Studio)", 640, y + 370);
    drawLabel (g, "Drawn curves", 912, y + 370);
    drawText (g,
              "Keyswitches work whatever the Octave; start and behaviour on the MIDI tab.",
              40,
              y + 482,
              Fonts::sans (11.5f),
              colours::dim);
    drawText (g,
              "Harmonics: natural where the note has a node, else artificial.",
              40,
              y + 500,
              Fonts::sans (11.5f),
              colours::dim);
    drawText (g,
              juce::String::fromUTF8 ("Tremolo, sautillé and portato are bowed by the same physics:"),
              40,
              y + 518,
              Fonts::sans (11.5f),
              colours::dim);
    drawText (g,
              juce::String::fromUTF8 ("only the bow's motion changes. Sautillé wants quick notes."),
              40,
              y + 534,
              Fonts::sans (11.5f),
              colours::dim);
    drawText (g,
              "DAW articulation maps: docs/articulation-maps in the source.",
              40,
              y + 552,
              Fonts::sans (11.5f),
              colours::dim);
}
} // namespace octavio2::ui
