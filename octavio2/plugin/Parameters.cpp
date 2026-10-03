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
      volume (s.getRawParameterValue (id::volume.getParamID()))
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
    return e;
}

int Reader::octaveShift() const
{
    return juce::jlimit (-2, 2, juce::roundToInt (octave->load()) - octaveChoiceOffset);
}
} // namespace octavio2::params
