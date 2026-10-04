#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace octavio2::ui::instruments
{
// One click sets the parts of a modern or a baroque violin (M7): strings, rosin and bow, and the
// Tuning row's A4, intonation and player style. Shared by the header's Instrument menu and the
// Tone tab's one-click row (2.3), so both do exactly the same.
struct Part
{
    const char* id;
    float modern, baroque;
};
// the instrument's parts (they decide which instrument is shown), then the Tuning row's
inline constexpr Part parts[] = { { "strings", 0, 1 }, { "rosin", 1, 3 }, { "bow", 0, 1 } };
inline constexpr Part tuning[] = { { "a4", 440, 415 }, { "intonation", 0, 3 }, { "playerStyle", 0, 3 } };
inline constexpr int count = 2;
inline const char* const names[count] = { "Modern violin", "Baroque violin" };

// the instrument whose parts are on now, or -1 (parts changed one by one: a custom violin)
inline int active (juce::AudioProcessorValueTreeState& state)
{
    for (int k = 0; k < count; ++k)
    {
        bool all = true;
        for (const auto& part : parts)
            if (auto* v = state.getRawParameterValue (part.id))
                all = all && juce::roundToInt (v->load()) == juce::roundToInt (k == 0 ? part.modern : part.baroque);
        if (all)
            return k;
    }
    return -1;
}

// sets every part (each one an undoable host gesture)
inline void choose (juce::AudioProcessorValueTreeState& state, int k)
{
    auto set = [&state, k] (const Part& part)
    {
        if (auto* p = state.getParameter (part.id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (k == 0 ? part.modern : part.baroque));
            p->endChangeGesture();
        }
    };
    for (const auto& part : parts)
        set (part);
    for (const auto& part : tuning)
        set (part);
}
} // namespace octavio2::ui::instruments
