#pragma once

#include "../core/Engine.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace octavio2::params
{
// Parameter IDs are saved in host projects. Never rename or reuse one; add a new ID instead.
namespace id
{
inline const juce::ParameterID mode { "mode", 1 };
inline const juce::ParameterID octave { "octave", 1 };
inline const juce::ParameterID velocityCurve { "velocityCurve", 1 };
inline const juce::ParameterID vibrato { "vibrato", 1 };
inline const juce::ParameterID brightness { "brightness", 1 };
inline const juce::ParameterID hiss { "hiss", 1 };
inline const juce::ParameterID sympathetic { "sympathetic", 1 };
inline const juce::ParameterID wolf { "wolf", 1 };
inline const juce::ParameterID hold { "hold", 1 };
inline const juce::ParameterID articulation { "articulation", 1 };
inline const juce::ParameterID bowStyle { "bowStyle", 1 };
inline const juce::ParameterID phrasing { "phrasing", 1 };
inline const juce::ParameterID fingerPlan { "fingerPlan", 1 };
inline const juce::ParameterID drawnCurves { "drawnCurves", 1 };
inline const juce::ParameterID room { "room", 1 };
inline const juce::ParameterID reverb { "reverb", 1 };
inline const juce::ParameterID volume { "volume", 1 };
inline const juce::ParameterID dynamics { "dynamics", 1 };
// radiation and room (M1)
inline const juce::ParameterID violin { "violin", 1 };
inline const juce::ParameterID mic { "mic", 1 };
inline const juce::ParameterID width { "width", 1 };
inline const juce::ParameterID movement { "movement", 1 };
inline const juce::ParameterID distance { "distance", 1 };
inline const juce::ParameterID bridge { "bridge", 1 };
inline const juce::ParameterID mute { "mute", 1 };
// M7 player: style, intonation, MPE
inline const juce::ParameterID playerStyle { "playerStyle", 1 };
inline const juce::ParameterID intonation { "intonation", 1 };
inline const juce::ParameterID tuningKey { "tuningKey", 1 };
inline const juce::ParameterID a4 { "a4", 1 };
inline const juce::ParameterID mpe { "mpe", 1 };
inline const juce::ParameterID mpeBendRange { "mpeBendRange", 1 };
} // namespace id

// The Room choices: "None" (the two microphones only), then the halls in the order the engine
// loads them (Processor::loadRadiationData).
const juce::StringArray& roomNames();

// The Violin and Mic position choices, in the order the engine loads them
const juce::StringArray& violinNames();
const juce::StringArray& micNames();
const juce::StringArray& articulationNames();
const juce::StringArray& bowStyleNames();
// M7: Player Style, Intonation and Key choices, in the engine's order (o2::PlayerStyle, o2::Intonation)
const juce::StringArray& playerStyleNames();
const juce::StringArray& intonationNames();
const juce::StringArray& keyNames();

// Choices of the Octave parameter; index 2 plays notes where they are.
inline constexpr int octaveChoiceOffset = 2;
// +1, as in Octavio 1: typing keyboards (FL Studio, Ableton) start an octave below the violin.
inline constexpr int defaultOctaveShift = 1;

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

// Reads the parameters into engine settings (audio thread safe).
class Reader
{
public:
    explicit Reader (juce::AudioProcessorValueTreeState&);
    o2::EngineSettings read() const;
    int octaveShift() const;
    bool studio() const { return mode->load() >= 0.5f; }
    // M7 MPE: on/off and the per-note bend range in semitones
    bool mpe() const { return mpeOn->load() >= 0.5f; }
    float mpeBendRange() const { return mpeBend->load(); }
    int intonation() const { return juce::roundToInt (intonationSystem->load()); }

private:
    std::atomic<float>*mode, *octave, *velocityCurve, *vibrato, *brightness, *room, *reverb, *volume, *dynamics;
    std::atomic<float>*violin, *mic, *width, *movement, *distance, *bridge, *mute, *hiss, *sympathetic, *wolf, *hold,
        *articulation, *bowStyle, *phrasing, *fingerPlan, *drawnCurves;
    std::atomic<float>*playerStyle, *intonationSystem, *tuningKey, *a4, *mpeOn, *mpeBend; // M7 player
};
} // namespace octavio2::params
