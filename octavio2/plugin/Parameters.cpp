#include "Parameters.h"

namespace octavio2::params
{
const juce::StringArray& roomNames()
{
    static const juce::StringArray names { "None (close mics)",
                                           "Concert hall, near (Arvedi, Cremona)",
                                           "Concert hall, far (Arvedi, Cremona)",
                                           "Chamber hall (Brahmssaal, Detmold)",
                                           "Church (Cremona)",
                                           "Studio (Maida Vale 4)",
                                           "Studio (Maida Vale 5)",
                                           "Control room (WDR)",
                                           "Small studio (WDR)" };
    return names;
}

const juce::StringArray& violinNames()
{
    static const juce::StringArray names { "Stoppani", "Klimke", "Levaggi", "Iowa" };
    return names;
}

const juce::StringArray& articulationNames()
{
    static const juce::StringArray names { "Arco", "Pizzicato", "Bartok pizz", "Left-hand pizz", "Harmonics" };
    return names;
}

const juce::StringArray& bowStyleNames()
{
    static const juce::StringArray names {
        "Auto", "Legato", juce::String::fromUTF8 ("Détaché"), "Staccato", juce::String::fromUTF8 ("Martelé"), "Spiccato"
    };
    return names;
}

const juce::StringArray& micNames()
{
    static const juce::StringArray names { "Front", "Above", "Player's ear", "Side" };
    return names;
}

namespace
{
std::unique_ptr<juce::AudioParameterFloat> floatParam (const juce::ParameterID& id,
                                                       const juce::String& name,
                                                       juce::NormalisableRange<float> range,
                                                       float def,
                                                       const juce::String& unit,
                                                       int decimals)
{
    return std::make_unique<juce::AudioParameterFloat> (
        id,
        name,
        range,
        def,
        juce::AudioParameterFloatAttributes().withLabel (unit).withStringFromValueFunction (
            [decimals, unit] (float v, int)
            { return juce::String (v, decimals) + (unit.isEmpty() ? "" : " " + unit); }));
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    // Live plays at once; Studio looks 1.2 s ahead (reported as latency, which the DAW
    // compensates) so the player knows how long each note is.
    layout.add (
        std::make_unique<juce::AudioParameterChoice> (id::mode, "Mode", juce::StringArray { "Live", "Studio" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (id::octave,
                                                              "Octave",
                                                              juce::StringArray { "-2", "-1", "0", "+1", "+2" },
                                                              octaveChoiceOffset + defaultOctaveShift));
    // dynamics = velocity ^ curve: below 1 soft playing gets louder sooner, above 1 later
    juce::NormalisableRange<float> curve { 0.4f, 2.5f };
    curve.setSkewForCentre (1.0f);
    layout.add (floatParam (id::velocityCurve, "Velocity Curve", curve, 1.0f, "", 2));
    layout.add (floatParam (id::vibrato, "Vibrato", { 0.0f, 2.0f }, 1.0f, "x", 2));
    // the high shelf on the bridge force (finish.py --bright); Jake picked +5 dB (2026-10-01)
    layout.add (floatParam (id::brightness, "Brightness", { -6.0f, 12.0f }, 5.0f, "dB", 1));
    layout.add (std::make_unique<juce::AudioParameterChoice> (id::room, "Room", roomNames(), 1));
    layout.add (floatParam (id::reverb, "Reverb", { -24.0f, 6.0f }, 0.0f, "dB", 1));
    layout.add (floatParam (id::volume, "Volume", { -36.0f, 12.0f }, 0.0f, "dB", 1));
    // added to every note's dynamics, from velocity (0 = as played)
    layout.add (floatParam (id::dynamics, "Dynamics", { -50.0f, 50.0f }, 0.0f, "%", 0));
    // radiation and room (M1, Jake approved the clips 2026-10-03)
    layout.add (std::make_unique<juce::AudioParameterChoice> (id::violin, "Violin", violinNames(), 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (id::mic, "Mic Position", micNames(), 0));
    layout.add (floatParam (id::width, "Stereo Width", { 0.0f, 200.0f }, 100.0f, "%", 0));
    // the player's sway: 0 is a violin on a stand
    layout.add (floatParam (id::movement, "Movement", { 0.0f, 100.0f }, 50.0f, "%", 0));
    juce::NormalisableRange<float> distance { 0.5f, 10.0f };
    distance.setSkewForCentre (2.0f);
    layout.add (floatParam (id::distance, "Distance", distance, 2.0f, "m", 1));
    // the bridge's rocking resonance: lower is darker
    layout.add (floatParam (id::bridge, "Bridge", { 2400.0f, 3600.0f }, 2900.0f, "Hz", 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (id::mute,
                                                              "Mute",
                                                              juce::StringArray { "Off", "Con sordino", "Practice" },
                                                              0));
    // the hair's hiss as it slides (M2, Jake 2026-10-04: natural by default, louder as options)
    layout.add (std::make_unique<juce::AudioParameterChoice> (id::hiss,
                                                              "Bow Hiss",
                                                              juce::StringArray { "Natural", "3x", "6x" },
                                                              0));
    // the real bridge (M3): the open strings' ring, a wolf at the main body mode, the chin and hand
    layout.add (floatParam (id::sympathetic, "Sympathetic", { 0.0f, 100.0f }, 50.0f, "%", 0));
    layout.add (floatParam (id::wolf, "Wolf", { 0.0f, 100.0f }, 0.0f, "%", 0));
    layout.add (floatParam (id::hold, "Hold", { 0.0f, 100.0f }, 50.0f, "%", 0));
    // the player (M4, M5): how notes are played. Keyswitches C1..E1 (MIDI 24-28) pick the
    // articulation too; changing this parameter wins over the last keyswitch.
    layout.add (
        std::make_unique<juce::AudioParameterChoice> (id::articulation, "Articulation", articulationNames(), 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (id::bowStyle, "Bow Style", bowStyleNames(), 0));
    layout.add (floatParam (id::phrasing, "Phrasing", { 0.0f, 200.0f }, 100.0f, "%", 0));
    layout.add (std::make_unique<juce::AudioParameterBool> (id::fingerPlan, "Plan Fingering", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (id::drawnCurves, "Drawn Curves", true));
    // M6 views: scales on the player's own settings (100 % / 0 = as fitted, the 2.1 sound)
    // shift slides: 0 % clean shifts .. 300 % slow, audible slides
    layout.add (floatParam (id::portamento, "Portamento", { 0.0f, 300.0f }, 100.0f, "%", 0));
    // string choice: -100 % bright (low positions, higher strings) .. +100 % dark (high
    // positions on lower strings)
    layout.add (floatParam (id::stringPreference, "String Preference", { -100.0f, 100.0f }, 0.0f, "%", 0));
    layout.add (floatParam (id::vibratoRate, "Vibrato Rate", { -1.5f, 1.5f }, 0.0f, "Hz", 2));
    // how long a note waits before its vibrato starts, and how long it takes to bloom
    layout.add (floatParam (id::vibratoDelay, "Vibrato Delay", { 25.0f, 300.0f }, 100.0f, "%", 0));
    // the bow's acceleration: how quickly it turns at a bow change and gets up to speed
    layout.add (floatParam (id::bowChange, "Bow Change", { 50.0f, 200.0f }, 100.0f, "%", 0));
    // how much each quick separate stroke eases off after it speaks, and the taper into a change
    layout.add (floatParam (id::strokeShaping, "Stroke Shaping", { 0.0f, 150.0f }, 100.0f, "%", 0));
    // the extra force that makes a stroke speak
    layout.add (floatParam (id::bite, "Bite", { 0.0f, 200.0f }, 100.0f, "%", 0));
    // the contact point: - nearer the bridge (brighter, louder), + nearer the fingerboard
    layout.add (floatParam (id::contact, "Contact Point", { -50.0f, 50.0f }, 0.0f, "%", 0));
    return layout;
}

Reader::Reader (juce::AudioProcessorValueTreeState& s)
    : mode (s.getRawParameterValue (id::mode.getParamID())),
      octave (s.getRawParameterValue (id::octave.getParamID())),
      velocityCurve (s.getRawParameterValue (id::velocityCurve.getParamID())),
      vibrato (s.getRawParameterValue (id::vibrato.getParamID())),
      brightness (s.getRawParameterValue (id::brightness.getParamID())),
      room (s.getRawParameterValue (id::room.getParamID())),
      reverb (s.getRawParameterValue (id::reverb.getParamID())),
      volume (s.getRawParameterValue (id::volume.getParamID())),
      dynamics (s.getRawParameterValue (id::dynamics.getParamID())),
      violin (s.getRawParameterValue (id::violin.getParamID())),
      mic (s.getRawParameterValue (id::mic.getParamID())),
      width (s.getRawParameterValue (id::width.getParamID())),
      movement (s.getRawParameterValue (id::movement.getParamID())),
      distance (s.getRawParameterValue (id::distance.getParamID())),
      bridge (s.getRawParameterValue (id::bridge.getParamID())),
      mute (s.getRawParameterValue (id::mute.getParamID())),
      hiss (s.getRawParameterValue (id::hiss.getParamID())),
      sympathetic (s.getRawParameterValue (id::sympathetic.getParamID())),
      wolf (s.getRawParameterValue (id::wolf.getParamID())),
      hold (s.getRawParameterValue (id::hold.getParamID())),
      articulation (s.getRawParameterValue (id::articulation.getParamID())),
      bowStyle (s.getRawParameterValue (id::bowStyle.getParamID())),
      phrasing (s.getRawParameterValue (id::phrasing.getParamID())),
      fingerPlan (s.getRawParameterValue (id::fingerPlan.getParamID())),
      drawnCurves (s.getRawParameterValue (id::drawnCurves.getParamID())),
      portamento (s.getRawParameterValue (id::portamento.getParamID())),
      stringPreference (s.getRawParameterValue (id::stringPreference.getParamID())),
      vibratoRate (s.getRawParameterValue (id::vibratoRate.getParamID())),
      vibratoDelay (s.getRawParameterValue (id::vibratoDelay.getParamID())),
      bowChange (s.getRawParameterValue (id::bowChange.getParamID())),
      strokeShaping (s.getRawParameterValue (id::strokeShaping.getParamID())),
      bite (s.getRawParameterValue (id::bite.getParamID())),
      contact (s.getRawParameterValue (id::contact.getParamID()))
{
}

o2::EngineSettings Reader::read() const
{
    o2::EngineSettings e;
    e.studio = studio();
    e.velocityCurve = velocityCurve->load();
    e.vibrato = vibrato->load();
    e.brightnessDb = brightness->load();
    e.hall = juce::roundToInt (room->load());
    e.reverbDb = reverb->load();
    e.volumeDb = volume->load();
    e.dynamics = dynamics->load() / 100.0;
    e.violin = juce::roundToInt (violin->load());
    e.mic = juce::roundToInt (mic->load());
    e.width = width->load() / 100.0;
    e.movement = movement->load() / 100.0;
    e.distance = distance->load();
    e.bridgeHz = bridge->load();
    e.mute = juce::roundToInt (mute->load());
    static constexpr double hissScale[] { 1.0, 3.0, 6.0 };
    e.sympathetic = sympathetic->load() / 100.0;
    e.wolf = wolf->load() / 100.0;
    e.hold = hold->load() / 100.0;
    e.articulation = juce::roundToInt (articulation->load());
    e.bowStyle = juce::roundToInt (bowStyle->load());
    e.phrasing = phrasing->load() / 100.0;
    e.fingerPlan = fingerPlan->load() > 0.5f;
    e.drawnCurves = drawnCurves->load() > 0.5f;
    e.hiss = hissScale[juce::jlimit (0, 2, juce::roundToInt (hiss->load()))];
    // M6 views
    e.portamento = portamento->load() / 100.0;
    e.stringPreference = stringPreference->load() / 100.0;
    e.vibratoRate = vibratoRate->load();
    e.vibratoDelay = vibratoDelay->load() / 100.0;
    e.bowChange = bowChange->load() / 100.0;
    e.strokeShaping = strokeShaping->load() / 100.0;
    e.bite = bite->load() / 100.0;
    e.contact = std::exp2 (contact->load() / 100.0);
    return e;
}

int Reader::octaveShift() const
{
    return juce::jlimit (-2, 2, juce::roundToInt (octave->load()) - octaveChoiceOffset);
}
} // namespace octavio2::params
