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
      hiss (s.getRawParameterValue (id::hiss.getParamID()))
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
    e.hiss = hissScale[juce::jlimit (0, 2, juce::roundToInt (hiss->load()))];
    return e;
}

int Reader::octaveShift() const
{
    return juce::jlimit (-2, 2, juce::roundToInt (octave->load()) - octaveChoiceOffset);
}
} // namespace octavio2::params
