#pragma once

#include "dsp/LoopFilter.h"

#include <array>
#include <cmath>

namespace violinsynth::engine
{
// The four violin strings. Impedances come from typical tensions and a
// 328 mm vibrating length (research/violin_model/strings.py). The force
// window is the range of bow force, as a fraction of Schelleng's F_max, that
// the Bow Pressure setting spans. It starts where Helmholtz motion begins on
// the open string at beta = 0.1 (docs/PHASE1_FINDINGS.md, section 2.6) and
// ends where the tone is still clean after the attack: above that the model
// scratches long before a real string would, most on the heavy G string and
// the thin E string (docs/BOW_NOISE.md).
//
// With clean bowing (docs/CLEAN_BOWING.md) the player eases off whenever the
// string scratches, so the top of the G window goes back to where Helmholtz
// motion exists: below about 0.5 F_max the G string tends to lock into a
// hollow double slip instead. playerWindowHigh is the top at Imperfection 0,
// forceWindowHigh at 100%.
struct StringSpec
{
    const char* name;
    int openMidiNote;
    double impedance; // kg/s
    double forceWindowLow;
    double forceWindowHigh;
    double playerWindowHigh;
};

inline constexpr std::array<StringSpec, 4> strings { {
    { "G", 55, 0.350, 0.24, 0.44, 0.74 },
    { "D", 62, 0.234, 0.13, 0.55, 0.55 },
    { "A", 69, 0.197, 0.09, 0.87, 0.87 },
    { "E", 76, 0.180, 0.11, 0.36, 0.36 },
} };

// The six strings of an electric guitar in standard tuning (a light 10-46
// set on a 628 mm scale, as on a Les Paul). Impedance is tension over wave
// speed, Z = T / (2 L f0), from the maker's published tensions
// (docs/BOWED_GUITAR.md). Steel strings lose little energy, and below these
// windows they keep slipping several times a period: a steady but thin,
// whistling tone instead of the sawtooth. The heavy strings need the most
// weight. Each window starts where the string settles into Helmholtz motion
// and ends before it scratches (ViolinSynthTests "[.guitarstrings]").
inline constexpr std::array<StringSpec, 6> guitarStrings { {
    { "E", 40, 0.73, 0.50, 0.80, 0.65 },
    { "A", 45, 0.61, 0.45, 0.80, 0.65 },
    { "D", 50, 0.43, 0.45, 0.80, 0.65 },
    { "G", 55, 0.29, 0.35, 0.75, 0.60 },
    { "B", 59, 0.214, 0.30, 0.70, 0.55 },
    { "e", 64, 0.169, 0.20, 0.65, 0.50 },
} };

// The same tuning on a steel-string acoustic (a light 12-53 phosphor bronze
// set on a 650 mm scale, as on a Yamaha dreadnought). The strings are heavier
// than the electric's, so their impedance is higher.
inline constexpr std::array<StringSpec, 6> acousticGuitarStrings { {
    { "E", 40, 0.95, 0.56, 0.85, 0.66 },
    { "A", 45, 0.91, 0.55, 0.85, 0.70 },
    { "D", 50, 0.73, 0.58, 0.90, 0.75 },
    { "G", 55, 0.55, 0.55, 0.85, 0.70 },
    { "B", 59, 0.32, 0.45, 0.80, 0.60 },
    { "e", 64, 0.24, 0.38, 0.75, 0.52 },
} };

inline constexpr int maxStrings = 6;

enum class Instrument
{
    violin,
    bowedGuitar, // electric
    bowedAcousticGuitar,
    baroqueViolin, // gut strings, as heard in historically informed playing (v1.1)
};

inline constexpr int numInstruments = 4;
// Every one is bowed, so the names leave that out.
inline constexpr std::array<const char*, numInstruments> instrumentNames { "Violin",
                                                                           "Electric guitar",
                                                                           "Acoustic guitar",
                                                                           "Baroque violin" };

// Everything about an instrument that the strings and the player need
// (the start of the InstrumentSpec planned for Octastra, docs/OCTASTRA_DESIGN.md).
struct InstrumentSpec
{
    const StringSpec* strings;
    int numStrings;
    int lowestNote, highestNote; // playable range; notes outside it are silent
    double scaleLength; // m, open string
    double pluckDistance; // m from the bridge, where the finger plucks
    double pluckRing; // pizzicato decay times, relative to the violin's
    dsp::LossSpec loss; // bowed string losses
    double bowPositionScale; // the Bow Position setting times this is where the bow plays
    bool fretted; // notes step from fret to fret; vibrato only bends up
    bool pickup; // heard through a magnetic pickup rather than a body
    bool flatBridge; // the bow also catches the strings beside the played ones
    // How much of the bow speed is kept while the bow lifts off at the end of
    // a smooth stroke, 0..1. Kept speed keeps the string's amplitude, so the
    // note rings on after the bow leaves (docs/REFERENCE_SOUND.md). 0 slows
    // the bow to a stop on the string.
    double releaseRing;
    // Sympathetic resonance of the open strings, relative to the Resonance setting.
    double sympatheticScale;
    // Depth of the radiation peaks and dips added after the body, dB (0: none).
    double bodyPeaksDb;
    // The player listens for the pitch the twisting string really sounds,
    // slightly sharp of the note. The violin's player was tuned listening at
    // the note itself, and keeps doing so.
    bool playerHearsTwist;

    const StringSpec& string (int s) const { return strings[s]; }
};

inline constexpr InstrumentSpec violinSpec {
    strings.data(),
    static_cast<int> (strings.size()),
    55,
    103,
    0.328,
    0.07,
    1.0,
    // A stopped string rings for about 3 s once the bow leaves it (1.5 s
    // before v1.1, which the notes' ends never let you hear).
    dsp::LossSpec { 3.0, 0.25, 4000.0 },
    1.0,
    false,
    false,
    false,
    0.6,
    0.1, // measured against real recordings: quieter than the model's full coupling
    9.0,
    false,
};

// A baroque violin: gut strings at lower tension. They ring on longer, the
// open strings answer more, and the bow comes off almost at full speed at the
// end of each stroke, as in historically informed playing. Fitted to Dmitry
// Sinkovsky's Biber (docs/REFERENCE_SOUND.md).
inline constexpr InstrumentSpec baroqueViolinSpec {
    strings.data(),
    static_cast<int> (strings.size()),
    55,
    103,
    0.328,
    0.07,
    1.0,
    dsp::LossSpec { 3.75, 0.25, 4000.0 },
    1.0,
    false,
    false,
    false,
    0.8,
    1.0,
    10.0,
    false,
};

// A solid-body electric guitar played with a violin bow, as Jimmy Page did.
// Its bridge is flat, so the bow cannot reach one string without touching its
// neighbours. Range: open low E to the 22nd fret of the top string. The bow
// plays further from the bridge than on the violin (0.16 of the string at the
// default Bow Position), between the pickups, where the steel strings speak
// most cleanly.
inline constexpr InstrumentSpec bowedGuitarSpec {
    guitarStrings.data(),
    static_cast<int> (guitarStrings.size()),
    40,
    86,
    0.628,
    0.12,
    3.0,
    dsp::LossSpec { 6.0, 1.2, 4000.0 },
    1.45,
    true,
    true,
    true,
    0.0,
    1.0,
    0.0,
    true,
};

// A steel-string acoustic played with a violin bow, as Ramin Djawadi bowed a
// Yamaha acoustic in his scores. The same flat bridge and frets as the
// electric, but heard through a wooden body: the strings drive the bridge,
// which drives the top, so the body takes more of their energy, the upper
// harmonics most (fitted to a recording, docs/BOWED_GUITAR.md). The top is in
// the bow's way near the bridge, so the bow plays nearer the soundhole: 0.19
// of the string at the default Bow Position.
inline constexpr InstrumentSpec bowedAcousticGuitarSpec {
    acousticGuitarStrings.data(),
    static_cast<int> (acousticGuitarStrings.size()),
    40,
    84,
    0.650,
    0.12,
    2.0,
    dsp::LossSpec { 3.0, 0.15, 2000.0 },
    1.7,
    true,
    false,
    true,
    0.0,
    1.0,
    0.0,
    true,
};

inline const InstrumentSpec& instrumentSpec (Instrument i)
{
    switch (i)
    {
        case Instrument::bowedGuitar:
            return bowedGuitarSpec;
        case Instrument::bowedAcousticGuitar:
            return bowedAcousticGuitarSpec;
        case Instrument::baroqueViolin:
            return baroqueViolinSpec;
        default:
            return violinSpec;
    }
}

// Whether the instrument is one of the guitars (six strings, fretted, drones).
inline bool isGuitar (Instrument i)
{
    return i == Instrument::bowedGuitar || i == Instrument::bowedAcousticGuitar;
}

// Lowest open string of any instrument: delay lines are sized for it.
inline constexpr int lowestOpenNote = 40;

// Highest string whose open pitch is at or below the note (the usual choice).
inline const StringSpec& stringForNote (double midiNote)
{
    for (auto it = strings.rbegin(); it != strings.rend(); ++it)
        if (static_cast<double> (it->openMidiNote) <= midiNote + 1.0e-9)
            return *it;

    return strings.front();
}

inline double midiToHz (double note)
{
    return 440.0 * std::pow (2.0, (note - 69.0) / 12.0);
}
} // namespace violinsynth::engine
