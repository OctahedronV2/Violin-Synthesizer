#pragma once

#include <array>

namespace violinsynth::engine
{
// Playing techniques (docs/PHASE5.md). The order is saved in host projects
// as the Articulation parameter and fixes the keyswitch notes: never reorder,
// only append.
enum class Articulation
{
    legato, // overlapping notes slur, separate notes get a new stroke
    detache, // every note gets its own stroke
    staccato, // martelé: bitten onset, short stroke, bow stops on the string
    spiccato, // bouncing bow: a short force pulse, then the string rings
    tremolo, // rapid bow reversals
    pizzicato, // plucked
    harmonics, // natural (flageolet) harmonics
    sulPonticello, // bow near the bridge
    sulTasto, // bow over the fingerboard
    conSordino, // mute on the bridge
};

inline constexpr int numArticulations = 10;

inline constexpr std::array<const char*, numArticulations> articulationNames {
    "Legato",    "Detache",   "Staccato",       "Spiccato",  "Tremolo",
    "Pizzicato", "Harmonics", "Sul ponticello", "Sul tasto", "Con sordino",
};

// Keyswitches: MIDI notes 24 (C1, shown as C2 in FL Studio) upwards, one per
// articulation in the order above. They are below the violin's range and
// never sound.
inline constexpr int firstKeyswitch = 24;

inline constexpr bool isKeyswitch (int note)
{
    return note >= firstKeyswitch && note < firstKeyswitch + numArticulations;
}

// Whether overlapping notes slur (no new stroke) in this articulation.
inline constexpr bool slurs (Articulation a)
{
    switch (a)
    {
        case Articulation::legato:
        case Articulation::harmonics:
        case Articulation::sulPonticello:
        case Articulation::sulTasto:
        case Articulation::conSordino:
            return true;
        case Articulation::detache:
        case Articulation::staccato:
        case Articulation::spiccato:
        case Articulation::tremolo:
        case Articulation::pizzicato:
            break;
    }
    return false;
}
} // namespace violinsynth::engine
