#include "engine/StringAllocator.h"

#include <algorithm>

namespace violinsynth::engine
{
namespace
{
constexpr int naturalLegatoInterval = 7; // stay on the string for steps up to a fifth
constexpr int naturalPositionRange = 12; // ... within the first octave of the string
} // namespace

void StringAllocator::reset()
{
    heldCount = 0;
    lastNoteOnTime = -1.0e9;
}

bool StringAllocator::canPlay (int string, int note)
{
    const auto open = strings[static_cast<std::size_t> (string)].openMidiNote;
    return note >= open && note <= open + maxReachSemitones;
}

int StringAllocator::usualString (int note)
{
    for (int s = numStrings - 1; s >= 0; --s)
        if (canPlay (s, note))
            return s;
    return 0; // below the G string: play on G (pitch is clamped by the voice)
}

int StringAllocator::findHeld (int note, int channel) const
{
    for (int i = 0; i < heldCount; ++i)
        if (held[static_cast<std::size_t> (i)].note == note && held[static_cast<std::size_t> (i)].channel == channel)
            return i;
    return -1;
}

void StringAllocator::removeHeld (int index)
{
    for (int i = index; i + 1 < heldCount; ++i)
        held[static_cast<std::size_t> (i)] = held[static_cast<std::size_t> (i + 1)];
    --heldCount;
}

int StringAllocator::melodyIndex() const
{
    int best = -1;
    for (int i = 0; i < heldCount; ++i)
    {
        const auto& h = held[static_cast<std::size_t> (i)];
        if (h.string >= 0 && (best < 0 || h.time >= held[static_cast<std::size_t> (best)].time))
            best = i;
    }
    return best;
}

int StringAllocator::heldOnString (int string) const
{
    for (int i = 0; i < heldCount; ++i)
        if (held[static_cast<std::size_t> (i)].string == string)
            return i;
    return -1;
}

int StringAllocator::noteOnString (int string) const
{
    const auto i = heldOnString (string);
    return i >= 0 ? held[static_cast<std::size_t> (i)].note : -1;
}

int StringAllocator::channelOnString (int string) const
{
    const auto i = heldOnString (string);
    return i >= 0 ? held[static_cast<std::size_t> (i)].channel : 1;
}

int StringAllocator::soundingCount() const
{
    int n = 0;
    for (int i = 0; i < heldCount; ++i)
        n += held[static_cast<std::size_t> (i)].string >= 0 ? 1 : 0;
    return n;
}

StringActions StringAllocator::noteOn (int note, int channel, float velocity, double timeSeconds)
{
    StringActions actions;

    // Re-triggering a held note: treat it as a new note.
    if (const auto existing = findHeld (note, channel); existing >= 0)
    {
        if (const auto s = held[static_cast<std::size_t> (existing)].string; s >= 0)
            actions.add ({ StringAction::Type::release, s });
        removeHeld (existing);
    }

    // Keep room for the new note: forget the oldest held note if full.
    if (heldCount == maxHeld)
    {
        if (const auto s = held[0].string; s >= 0)
            actions.add ({ StringAction::Type::release, s });
        removeHeld (0);
    }

    const auto melody = melodyIndex();
    const auto withinChord = timeSeconds - lastNoteOnTime <= chordWindow;
    lastNoteOnTime = timeSeconds;

    const auto newIndex = heldCount++;
    held[static_cast<std::size_t> (newIndex)] = { note, channel, velocity, timeSeconds, -1 };

    const bool slur = melody >= 0 && (mode == PlayMode::monoLegato || (mode == PlayMode::automatic && ! withinChord));

    if (slur)
    {
        legatoTo (newIndex, melody, actions);
        if (! slursEnabled)
            actions.items[static_cast<std::size_t> (actions.count - 1)].type = StringAction::Type::start; // re-stroke
    }
    else
    {
        startChordNote (newIndex, timeSeconds, actions);
    }

    return actions;
}

void StringAllocator::legatoTo (int newIndex, int fromIndex, StringActions& actions)
{
    auto& target = held[static_cast<std::size_t> (newIndex)];
    auto& from = held[static_cast<std::size_t> (fromIndex)];
    const auto fromString = from.string;
    const auto open = strings[static_cast<std::size_t> (fromString)].openMidiNote;

    // Stay on the current string for natural steps, otherwise use the usual string.
    auto string = usualString (target.note);
    if (canPlay (fromString, target.note) && std::abs (target.note - from.note) <= naturalLegatoInterval
        && target.note - open <= naturalPositionRange)
        string = fromString;

    // In monoLegato only one string sounds; in automatic mode other chord
    // notes keep sounding unless the line needs their string.
    if (mode == PlayMode::monoLegato)
    {
        for (int i = 0; i < heldCount; ++i)
        {
            auto& h = held[static_cast<std::size_t> (i)];
            if (h.string >= 0 && h.string != fromString && i != newIndex)
            {
                actions.add ({ StringAction::Type::release, h.string });
                h.string = -1;
            }
        }
    }
    else if (string != fromString)
    {
        if (const auto occupant = heldOnString (string); occupant >= 0 && occupant != fromIndex)
        {
            // The usual string is busy with another chord note; slur on the current string if possible.
            if (canPlay (fromString, target.note))
                string = fromString;
            else
            {
                actions.add ({ StringAction::Type::release, string });
                held[static_cast<std::size_t> (occupant)].string = -1;
            }
        }
    }

    if (string != fromString)
        actions.add ({ StringAction::Type::release, fromString });

    from.string = -1;
    target.string = string;
    actions.add ({ StringAction::Type::legato, string, target.note, target.channel, target.velocity });
}

void StringAllocator::startChordNote (int newIndex, double time, StringActions& actions)
{
    // Notes forming the current chord: the new note plus sounding notes that
    // started within the chord window. They may be re-spread over the strings.
    std::array<int, maxHeld> chord {};
    int chordSize = 0;
    for (int i = 0; i < heldCount; ++i)
    {
        const auto& h = held[static_cast<std::size_t> (i)];
        if (i == newIndex || (h.string >= 0 && time - h.time <= chordWindow))
            chord[static_cast<std::size_t> (chordSize++)] = i;
    }

    // Highest note first, each on the highest free string that can play it.
    std::sort (chord.begin(),
               chord.begin() + chordSize,
               [this] (int a, int b)
               { return held[static_cast<std::size_t> (a)].note > held[static_cast<std::size_t> (b)].note; });

    std::array<bool, numStrings> taken {};
    for (int i = 0; i < heldCount; ++i)
    {
        const auto& h = held[static_cast<std::size_t> (i)];
        const bool inChord = std::find (chord.begin(), chord.begin() + chordSize, i) != chord.begin() + chordSize;
        if (h.string >= 0 && ! inChord)
            taken[static_cast<std::size_t> (h.string)] = true; // older notes keep their strings for now
    }

    std::array<int, maxHeld> assigned {};
    for (int k = 0; k < chordSize; ++k)
    {
        const auto& h = held[static_cast<std::size_t> (chord[static_cast<std::size_t> (k)])];
        int choice = -1;
        for (int s = numStrings - 1; s >= 0 && choice < 0; --s)
            if (! taken[static_cast<std::size_t> (s)] && canPlay (s, h.note))
                choice = s;
        assigned[static_cast<std::size_t> (k)] = choice;
        if (choice >= 0)
            taken[static_cast<std::size_t> (choice)] = true;
    }

    // If the new note found no free string, it takes its usual string from
    // whatever is there (the newest note wins).
    for (int k = 0; k < chordSize; ++k)
    {
        if (chord[static_cast<std::size_t> (k)] != newIndex || assigned[static_cast<std::size_t> (k)] >= 0)
            continue;

        const auto string = usualString (held[static_cast<std::size_t> (newIndex)].note);
        for (int j = 0; j < chordSize; ++j)
            if (assigned[static_cast<std::size_t> (j)] == string)
                assigned[static_cast<std::size_t> (j)] = -1;
        if (const auto occupant = heldOnString (string); occupant >= 0)
        {
            actions.add ({ StringAction::Type::release, string });
            held[static_cast<std::size_t> (occupant)].string = -1;
        }
        assigned[static_cast<std::size_t> (k)] = string;
    }

    // Apply: release strings whose chord note moved or lost its string, then start.
    for (int k = 0; k < chordSize; ++k)
    {
        auto& h = held[static_cast<std::size_t> (chord[static_cast<std::size_t> (k)])];
        const auto newString = assigned[static_cast<std::size_t> (k)];
        if (h.string >= 0 && h.string != newString)
        {
            actions.add ({ StringAction::Type::release, h.string });
            h.string = -1;
        }
    }
    for (int k = 0; k < chordSize; ++k)
    {
        auto& h = held[static_cast<std::size_t> (chord[static_cast<std::size_t> (k)])];
        const auto newString = assigned[static_cast<std::size_t> (k)];
        if (newString >= 0 && h.string != newString)
        {
            h.string = newString;
            actions.add ({ StringAction::Type::start, newString, h.note, h.channel, h.velocity });
        }
    }
}

StringActions StringAllocator::noteOff (int note, int channel)
{
    StringActions actions;
    const auto index = findHeld (note, channel);
    if (index < 0)
        return actions;

    const auto string = held[static_cast<std::size_t> (index)].string;
    const bool wasMelody = index == melodyIndex();

    if (string < 0)
    {
        removeHeld (index);
        return actions;
    }

    // In a legato line, releasing the newest note returns to the most recent
    // held note that is not sounding (as in a trill).
    int fallback = -1;
    if (mode != PlayMode::poly && slursEnabled && wasMelody)
        for (int i = 0; i < heldCount; ++i)
            if (i != index && held[static_cast<std::size_t> (i)].string < 0
                && (fallback < 0
                    || held[static_cast<std::size_t> (i)].time > held[static_cast<std::size_t> (fallback)].time))
                fallback = i;

    if (fallback >= 0)
    {
        legatoTo (fallback, index, actions);
        removeHeld (index);
        return actions;
    }

    actions.add ({ StringAction::Type::release, string });
    removeHeld (index);
    return actions;
}

StringActions StringAllocator::allNotesOff()
{
    StringActions actions;
    for (int i = 0; i < heldCount; ++i)
        if (const auto s = held[static_cast<std::size_t> (i)].string; s >= 0)
            actions.add ({ StringAction::Type::release, s });
    heldCount = 0;
    return actions;
}
} // namespace violinsynth::engine
