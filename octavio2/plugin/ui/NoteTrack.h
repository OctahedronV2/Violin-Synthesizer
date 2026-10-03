#pragma once

#include "../../core/Engine.h"

#include <cstdint>
#include <vector>

namespace octavio2::ui
{
// The notes the player played and plans, read from the engine's note log on the message thread
// (the Play tab's plan, the Curves tab's notes strip). Keeps the last 30 s.
struct NoteTrack
{
    struct Note
    {
        double on, off; // engine seconds; off < 0 while it sounds
        int pitch, string;
        double dir;
        bool slur, planned;
    };
    void read (const o2::Engine&);
    std::vector<Note> notes;
    uint64_t logRead = 0;
};
} // namespace octavio2::ui
