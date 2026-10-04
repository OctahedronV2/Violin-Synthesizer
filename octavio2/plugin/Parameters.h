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
// M7 instrument: string set, rosin, bow, contact point, tremolo
inline const juce::ParameterID strings { "strings", 1 };
inline const juce::ParameterID rosin { "rosin", 1 };
inline const juce::ParameterID bow { "bow", 1 };
inline const juce::ParameterID contact { "contact", 1 };
inline const juce::ParameterID tremoloSpeed { "tremoloSpeed", 1 };
inline const juce::ParameterID tremoloSync { "tremoloSync", 1 };
} // namespace id

// The Room choices: "None" (the two microphones only), then the halls in the order the engine
// loads them (Processor::loadRadiationData).
const juce::StringArray& roomNames();

// The Violin and Mic position choices, in the order the engine loads them
const juce::StringArray& violinNames();
const juce::StringArray& micNames();
const juce::StringArray& articulationNames();
const juce::StringArray& bowStyleNames();
// M7 instrument
const juce::StringArray& stringsNames();
const juce::StringArray& rosinNames();
const juce::StringArray& bowNames();
const juce::StringArray& contactNames();
const juce::StringArray& tremoloSyncNames();

// Keyswitches are fixed keys, whatever the Octave: C1 (MIDI 24) .. B1 (35). 24-32 pick the
// Articulation (in its order), 33-35 the contact point (ordinario, sul ponticello, sul tasto).
inline constexpr int keyswitchFirst = 24, keyswitchLast = 35;

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

private:
    std::atomic<float>*mode, *octave, *velocityCurve, *vibrato, *brightness, *room, *reverb, *volume, *dynamics;
    std::atomic<float>*violin, *mic, *width, *movement, *distance, *bridge, *mute, *hiss, *sympathetic, *wolf, *hold,
        *articulation, *bowStyle, *phrasing, *fingerPlan, *drawnCurves;
    std::atomic<float>*strings, *rosin, *bow, *contact, *tremoloSpeed, *tremoloSync; // M7 instrument
};
} // namespace octavio2::params
