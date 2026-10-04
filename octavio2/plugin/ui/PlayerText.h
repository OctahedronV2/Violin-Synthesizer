#pragma once

#include <juce_core/juce_core.h>

#include <cmath>

namespace octavio2::ui
{
// Words for what the player does, shared by the Play, Bow and Left hand tabs.
inline constexpr double openPitches[4] = { 55, 62, 69, 76 };
inline const char* const stringLetters = "GDAE";

inline juce::String ordinal (int n)
{
    static const char* s[] = { "th", "st", "nd", "rd" };
    const int k = (n % 100 >= 11 && n % 100 <= 13) || n % 10 > 3 ? 0 : n % 10;
    return juce::String (n) + s[k];
}

// the position a hand sits in, from where the first finger is (semitones above the open string)
inline int positionOf (float handPos)
{
    static const int firstFinger[] = { 0, 2, 4, 5, 7, 9, 10, 12, 14, 15, 17, 19 }; // half, 1st .. 11th
    int best = 1;
    for (int p = 1; p < 12; ++p)
        if (std::abs (firstFinger[p] - handPos) < std::abs (firstFinger[best] - handPos))
            best = p;
    return best;
}

inline juce::String positionName (float handPos)
{
    return ordinal (positionOf (handPos)) + " position";
}

// the finger (1..4) that stops semis above the open string with the hand at handPos; 0 = open
inline int fingerOf (double semis, float handPos)
{
    if (semis < 0.3)
        return 0;
    return juce::jlimit (1, 4, juce::roundToInt ((semis - handPos) / 1.75) + 1);
}

// the Portamento parameter (0..300 % of the player's slide time)
inline juce::String portamentoText (float v)
{
    return v < 5   ? juce::String ("Clean shifts")
        : v < 75   ? juce::String ("Quick slides")
        : v <= 135 ? juce::String ("Natural")
        : v <= 220 ? juce::String ("Slow slides")
                   : juce::String ("Very slow");
}

// the String Preference parameter (-100 bright .. +100 dark)
inline juce::String stringPreferenceText (float v)
{
    if (std::abs (v) < 5)
        return "Balanced";
    return juce::String (v < 0 ? "Bright " : "Dark ") + juce::String (juce::roundToInt (std::abs (v))) + " %";
}
} // namespace octavio2::ui
