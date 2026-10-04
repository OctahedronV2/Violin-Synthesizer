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
// M6 views (Play, Bow and Left hand tabs)
inline const juce::ParameterID portamento { "portamento", 1 };
inline const juce::ParameterID stringPreference { "stringPreference", 1 };
inline const juce::ParameterID vibratoRate { "vibratoRate", 1 };
inline const juce::ParameterID vibratoDelay { "vibratoDelay", 1 };
inline const juce::ParameterID bowChange { "bowChange", 1 };
inline const juce::ParameterID strokeShaping { "strokeShaping", 1 };
inline const juce::ParameterID bite { "bite", 1 };
inline const juce::ParameterID contact { "contact", 1 };
// MIDI (M6): semitones of the pitch wheel's full throw
inline const juce::ParameterID bendRange { "bendRange", 1 };
// M7 player: style, intonation, MPE
inline const juce::ParameterID playerStyle { "playerStyle", 1 };
inline const juce::ParameterID intonation { "intonation", 1 };
inline const juce::ParameterID tuningKey { "tuningKey", 1 };
inline const juce::ParameterID a4 { "a4", 1 };
inline const juce::ParameterID mpe { "mpe", 1 };
inline const juce::ParameterID mpeBendRange { "mpeBendRange", 1 };
// M7 instrument: string set, rosin, bow, contact point, tremolo
inline const juce::ParameterID strings { "strings", 1 };
inline const juce::ParameterID rosin { "rosin", 1 };
inline const juce::ParameterID bow { "bow", 1 };
inline const juce::ParameterID contactStyle { "contactStyle", 1 };
inline const juce::ParameterID tremoloSpeed { "tremoloSpeed", 1 };
inline const juce::ParameterID tremoloSync { "tremoloSync", 1 };
// 2.3 curves: who is in charge of each dimension (Auto / Guided / Manual), in o2::Dim order
inline const juce::ParameterID modeDynamics { "modeDynamics", 1 };
inline const juce::ParameterID modeVibrato { "modeVibrato", 1 };
inline const juce::ParameterID modeVibratoRate { "modeVibratoRate", 1 };
inline const juce::ParameterID modeContact { "modeContact", 1 };
inline const juce::ParameterID modePressure { "modePressure", 1 };
// 2.3 controls: keyswitch behaviour and start key, the Take (seed), Imperfection, velocity
inline const juce::ParameterID keyswitchMode { "keyswitchMode", 1 };
inline const juce::ParameterID keyswitchStart { "keyswitchStart", 1 };
inline const juce::ParameterID seed { "seed", 1 };
inline const juce::ParameterID imperfection { "imperfection", 1 };
inline const juce::ParameterID velocitySensitivity { "velocitySensitivity", 1 };
inline const juce::ParameterID attackWeight { "attackWeight", 1 };
} // namespace id

// 2.3: the mode parameter of each o2::Dim, and the choices (o2::DimMode order)
const juce::ParameterID& dimModeId (int dim);
const juce::StringArray& dimModeNames();

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
// M7 instrument
const juce::StringArray& stringsNames();
const juce::StringArray& rosinNames();
const juce::StringArray& bowNames();
const juce::StringArray& contactNames();
const juce::StringArray& tremoloSyncNames();
const juce::StringArray& keyswitchModeNames(); // 2.3: Latching, Momentary, Off

// Keyswitches are twelve keys, whatever the Octave, from Keyswitch Start (default C1 = MIDI 24):
// the first nine pick the Articulation (in its order), the last three the contact point
// (ordinario, sul ponticello, sul tasto). Keyswitch Behaviour: Latching (until the next one),
// Momentary (only while the key is held, then back to what played before), Off (the keys are
// ordinary notes: silent below the violin).
inline constexpr int keyswitchFirst = 24, keyswitchCount = 12;
inline constexpr int keyswitchStartMax = 116; // the block's last key is 127
enum KeyswitchMode
{
    keysLatching,
    keysMomentary,
    keysOff
};
// UACC: CC32's value picks the articulation, bow style and contact point (o2::uaccMap)
inline constexpr int uaccController = 32;
// the Take (Seed parameter): 1 = the 2.2 performance
inline constexpr int seedMax = 9999;

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
    double bendRange() const { return bendRangeValue->load(); } // M6: semitones
    // M7 MPE: on/off and the per-note bend range in semitones
    bool mpe() const { return mpeOn->load() >= 0.5f; }
    float mpeBendRange() const { return mpeBend->load(); }
    int intonation() const { return juce::roundToInt (intonationSystem->load()); }
    // 2.3: KeyswitchMode and the first keyswitch's MIDI note
    int keyswitchMode() const { return juce::jlimit (0, 2, juce::roundToInt (ksMode->load())); }
    int keyswitchStart() const { return juce::jlimit (0, keyswitchStartMax, juce::roundToInt (ksStart->load())); }

private:
    std::atomic<float>*mode, *octave, *velocityCurve, *vibrato, *brightness, *room, *reverb, *volume, *dynamics;
    std::atomic<float>*violin, *mic, *width, *movement, *distance, *bridge, *mute, *hiss, *sympathetic, *wolf, *hold,
        *articulation, *bowStyle, *phrasing, *fingerPlan, *drawnCurves;
    // M6 views
    std::atomic<float>*portamento, *stringPreference, *vibratoRate, *vibratoDelay, *bowChange, *strokeShaping, *bite,
        *contact;
    std::atomic<float>* bendRangeValue; // M6
    std::atomic<float>*playerStyle, *intonationSystem, *tuningKey, *a4, *mpeOn, *mpeBend; // M7 player
    std::atomic<float>*strings, *rosin, *bow, *contactStyle, *tremoloSpeed, *tremoloSync; // M7 instrument
    std::array<std::atomic<float>*, o2::dimCount> dimModes {}; // 2.3
    std::atomic<float>*ksMode, *ksStart, *seed, *imperfection, *velSens, *attackWeight; // 2.3
};
} // namespace octavio2::params
