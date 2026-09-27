#pragma once

#include "engine/StringData.h"

#include <array>

namespace violinsynth::engine
{
enum class PlayMode
{
    automatic, // chords within the chord window become double stops, otherwise legato lines
    monoLegato, // one line: overlapping notes always slur
    poly, // every note gets its own stroke on its own string (up to four)
};

// What a string should do in response to a note event.
struct StringAction
{
    enum class Type
    {
        start, // new bow stroke on this string
        legato, // take the note without a new stroke (glide, or a string crossing mid-bow)
        release, // lift the bow from this string
    };

    Type type;
    int string;
    int note = -1;
    int channel = 1;
    float velocity = 0.0f;
};

// Fixed-capacity action list (no allocation on the audio thread).
struct StringActions
{
    std::array<StringAction, 12> items {};
    int count = 0;

    void add (StringAction a)
    {
        if (count < static_cast<int> (items.size()))
            items[static_cast<std::size_t> (count++)] = a;
    }
    const StringAction* begin() const { return items.data(); }
    const StringAction* end() const { return items.data() + count; }
};

// Assigns MIDI notes to the four violin strings.
//
// A string plays one note at a time and only notes at or above its open
// pitch. Chords (notes starting within the chord window) are spread over
// different strings as double or triple stops, the highest note on the
// highest string that can play it. In a legato line the new note stays on
// the current string when that is a natural fingering (within a fifth and
// the first octave of the string), otherwise it crosses to the usual string
// without restarting the bow. Notes are identified by (note, channel) so MPE
// notes on different channels are independent.
class StringAllocator
{
public:
    static constexpr int numStrings = static_cast<int> (strings.size());
    static constexpr int maxHeld = 16;
    static constexpr int maxReachSemitones = 36; // highest note above the open string

    void reset();
    void setMode (PlayMode m) { mode = m; }
    void setChordWindow (double seconds) { chordWindow = seconds; }
    // With slurs off (détaché, staccato, pizzicato ...), a note that would
    // slur gets a new stroke instead, and releasing a note never returns to
    // an older held one.
    void setSlurs (bool enabled) { slursEnabled = enabled; }

    StringActions noteOn (int note, int channel, float velocity, double timeSeconds);
    StringActions noteOff (int note, int channel);
    StringActions allNotesOff();

    // Note sounding on a string, or -1.
    int noteOnString (int string) const;
    int channelOnString (int string) const;
    int soundingCount() const;

    static bool canPlay (int string, int note);
    static int usualString (int note); // highest string that can play the note

private:
    struct Held
    {
        int note = -1;
        int channel = 1;
        float velocity = 0.0f;
        double time = 0.0;
        int string = -1; // -1 if held but not sounding
    };

    int findHeld (int note, int channel) const;
    void removeHeld (int index);
    int melodyIndex() const; // most recently started sounding note
    int heldOnString (int string) const;

    void startChordNote (int newIndex, double time, StringActions& actions);
    void legatoTo (int newIndex, int fromIndex, StringActions& actions);

    PlayMode mode = PlayMode::automatic;
    bool slursEnabled = true;
    double chordWindow = 0.04;
    double lastNoteOnTime = -1.0e9;
    std::array<Held, maxHeld> held {};
    int heldCount = 0;
};
} // namespace violinsynth::engine
