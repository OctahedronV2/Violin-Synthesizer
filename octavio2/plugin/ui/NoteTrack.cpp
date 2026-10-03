#include "NoteTrack.h"

#include <algorithm>

namespace octavio2::ui
{
void NoteTrack::read (const o2::Engine& engine)
{
    const auto written = engine.logCount();
    if (written - logRead > o2::Engine::logSize - 64)
        logRead = written - (o2::Engine::logSize - 64);
    for (; logRead < written; ++logRead)
    {
        const auto e = engine.logEntry (logRead);
        if (e.kind == o2::Engine::NoteLog::planned)
            notes.push_back ({ e.t, -1, e.pitch, -1, 0, false, true });
        else if (e.kind == o2::Engine::NoteLog::off)
        {
            for (auto it = notes.rbegin(); it != notes.rend(); ++it)
                if (it->pitch == e.pitch && ! it->planned && it->off < 0)
                {
                    it->off = e.t;
                    break;
                }
        }
        else
        {
            // the plan is played: replace the planned entry
            for (auto it = notes.begin(); it != notes.end(); ++it)
                if (it->planned && it->pitch == e.pitch && std::abs (it->on - e.t) < 0.05)
                {
                    notes.erase (it);
                    break;
                }
            // a slur ends the note before it
            if (e.kind == o2::Engine::NoteLog::slur)
                for (auto& n : notes)
                    if (! n.planned && n.off < 0)
                        n.off = e.t;
            notes.push_back ({ e.t, -1, e.pitch, e.string, e.dir, e.kind == o2::Engine::NoteLog::slur, false });
        }
    }
    const double now = engine.seconds();
    notes.erase (std::remove_if (notes.begin(),
                                 notes.end(),
                                 [now] (const Note& n) { return (n.off >= 0 && n.off < now - 10) || n.on < now - 30; }),
                 notes.end());
}
} // namespace octavio2::ui
