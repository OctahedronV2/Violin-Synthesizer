#pragma once

#include "../core/Engine.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <cstdint>

namespace octavio2
{
// M7: MPE input (MIDI Polyphonic Expression, lower zone: channel 1 is the master channel, notes
// arrive on channels 2-16, one note per channel). With the MPE parameter on, each note's own
// channel carries its expression to that note only:
//   pitch bend    -> the note's pitch (range: the MPE Bend Range parameter, 48 semitones default)
//   pressure      -> the bow's dynamics while it is the newest note (channel or poly aftertouch)
//   CC74 (timbre) -> the contact point: up moves the bow towards the bridge (brighter), 64 = the
//                    player's own
// Notes themselves still go through the ordinary note path, so each note gets its own string
// decision, and the master channel (and everything when MPE is off) is ordinary MIDI.
// Audio thread only; no allocation.
class Mpe
{
public:
    using SentNotes = std::array<std::array<std::int8_t, 128>, 16>;

    // Returns true when the message was MPE expression handled here, false when the caller plays
    // it as ordinary MIDI (a note-on also gets its channel's expression sent ahead of it here).
    bool handle (const juce::MidiMessage& m,
                 int64_t when,
                 o2::Engine& engine,
                 int octaveShift,
                 float bendRange,
                 const SentNotes& sent)
    {
        const int ch = juce::jlimit (1, 16, m.getChannel()) - 1;
        if (ch == 0) // the master channel: ordinary MIDI
            return false;
        auto& c = channels[static_cast<size_t> (ch)];
        const auto& notes = sent[static_cast<size_t> (ch)];
        const double cents = c.bend * bendRange * 100.0;
        if (m.isNoteOn())
        {
            const int note = m.getNoteNumber();
            const int pitch = note + 12 * octaveShift;
            if ((note >= 24 && note <= 28) || pitch < 55 || pitch > 104)
                return false; // keyswitches and notes outside the violin: as ordinary MIDI
            // the channel's state before the note-on sets how the note starts
            engine.noteBend (when, pitch, cents);
            engine.notePressure (when, pitch, c.pressure);
            engine.noteTimbre (when, pitch, c.timbre);
            return false;
        }
        if (m.isNoteOff())
        {
            c.pressure = -1.0; // a new note starts from its velocity until pressure arrives
            return false;
        }
        if (m.isPitchWheel())
        {
            c.bend = (m.getPitchWheelValue() - 8192) / 8192.0;
            forNotes (notes, [&] (int p) { engine.noteBend (when, p, c.bend * bendRange * 100.0); });
            return true;
        }
        if (m.isChannelPressure())
        {
            c.pressure = m.getChannelPressureValue() / 127.0;
            forNotes (notes, [&] (int p) { engine.notePressure (when, p, c.pressure); });
            return true;
        }
        if (m.isAftertouch())
        {
            const auto p = notes[static_cast<size_t> (m.getNoteNumber() & 127)];
            if (p >= 0)
                engine.notePressure (when, p, m.getAfterTouchValue() / 127.0);
            return true;
        }
        if (m.isController() && m.getControllerNumber() == 74)
        {
            c.timbre = m.getControllerValue();
            forNotes (notes, [&] (int p) { engine.noteTimbre (when, p, c.timbre); });
            return true;
        }
        return false;
    }

    void reset() { channels = {}; }

private:
    struct Channel
    {
        double bend = 0.0; // -1..1 of the bend range
        double pressure = -1.0; // 0..1, -1: none since the last note-off
        double timbre = 64.0; // CC74
    };
    template <typename Fn>
    static void forNotes (const std::array<std::int8_t, 128>& notes, Fn&& fn)
    {
        for (auto p : notes)
            if (p >= 0)
                fn (p);
    }
    std::array<Channel, 16> channels {};
};
} // namespace octavio2
