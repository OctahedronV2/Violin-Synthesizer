#include "engine/StringAllocator.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace violinsynth::engine;

namespace
{
constexpr int G = 0, D = 1, A = 2, E = 3;

struct Summary
{
    std::vector<int> started, legato, released;
};

Summary summarise (const StringActions& actions)
{
    Summary s;
    for (const auto& a : actions)
    {
        if (a.type == StringAction::Type::start)
            s.started.push_back (a.string);
        else if (a.type == StringAction::Type::legato)
            s.legato.push_back (a.string);
        else
            s.released.push_back (a.string);
    }
    return s;
}
} // namespace

TEST_CASE ("Single notes go to the highest string that can play them", "[allocator]")
{
    CHECK (StringAllocator::usualString (55) == G);
    CHECK (StringAllocator::usualString (61) == G);
    CHECK (StringAllocator::usualString (62) == D);
    CHECK (StringAllocator::usualString (69) == A);
    CHECK (StringAllocator::usualString (76) == E);
    CHECK (StringAllocator::usualString (100) == E);
    CHECK (StringAllocator::usualString (40) == G); // below the violin's range
}

TEST_CASE ("A separate note starts a new stroke on its usual string", "[allocator]")
{
    StringAllocator alloc;
    const auto s = summarise (alloc.noteOn (69, 1, 0.8f, 0.0));
    CHECK (s.started == std::vector<int> { A });
    CHECK (alloc.noteOnString (A) == 69);

    CHECK (summarise (alloc.noteOff (69, 1)).released == std::vector<int> { A });
    CHECK (alloc.soundingCount() == 0);
}

TEST_CASE ("Notes starting together become a double stop", "[allocator]")
{
    StringAllocator alloc;
    alloc.noteOn (62, 1, 0.8f, 0.0); // D4
    alloc.noteOn (69, 1, 0.8f, 0.01); // A4, within the chord window
    CHECK (alloc.noteOnString (D) == 62);
    CHECK (alloc.noteOnString (A) == 69);
    CHECK (alloc.soundingCount() == 2);
}

TEST_CASE ("A chord is re-spread when the second note needs the first note's string", "[allocator]")
{
    StringAllocator alloc;
    alloc.noteOn (72, 1, 0.8f, 0.0); // C5 goes to A
    alloc.noteOn (74, 1, 0.8f, 0.01); // D5 arrives: highest note on A, C5 moves to D
    CHECK (alloc.noteOnString (A) == 74);
    CHECK (alloc.noteOnString (D) == 72);
}

TEST_CASE ("An impossible chord keeps the newest note", "[allocator]")
{
    StringAllocator alloc;
    alloc.noteOn (57, 1, 0.8f, 0.0); // A3: only the G string
    alloc.noteOn (55, 1, 0.8f, 0.01); // G3: also only the G string
    CHECK (alloc.noteOnString (G) == 55);
    CHECK (alloc.soundingCount() == 1);
}

TEST_CASE ("Four notes make a quadruple stop", "[allocator]")
{
    StringAllocator alloc;
    alloc.noteOn (55, 1, 0.8f, 0.0);
    alloc.noteOn (62, 1, 0.8f, 0.0);
    alloc.noteOn (71, 1, 0.8f, 0.0);
    alloc.noteOn (79, 1, 0.8f, 0.0);
    CHECK (alloc.noteOnString (G) == 55);
    CHECK (alloc.noteOnString (D) == 62);
    CHECK (alloc.noteOnString (A) == 71);
    CHECK (alloc.noteOnString (E) == 79);
}

TEST_CASE ("An overlapping note slurs on the same string for small steps", "[allocator]")
{
    StringAllocator alloc;
    alloc.noteOn (69, 1, 0.8f, 0.0);
    const auto s = summarise (alloc.noteOn (71, 1, 0.8f, 0.5));
    CHECK (s.legato == std::vector<int> { A });
    CHECK (s.started.empty());
    CHECK (s.released.empty());
    CHECK (alloc.noteOnString (A) == 71);
    CHECK (alloc.soundingCount() == 1);
}

TEST_CASE ("A large legato leap crosses strings without a new stroke", "[allocator]")
{
    StringAllocator alloc;
    alloc.noteOn (69, 1, 0.8f, 0.0); // A4 on A
    const auto s = summarise (alloc.noteOn (84, 1, 0.8f, 0.5)); // C6: E string
    CHECK (s.legato == std::vector<int> { E });
    CHECK (s.released == std::vector<int> { A });
    CHECK (s.started.empty());
}

TEST_CASE ("Releasing the newest note of a trill returns to the held note", "[allocator]")
{
    StringAllocator alloc;
    alloc.noteOn (69, 1, 0.8f, 0.0);
    alloc.noteOn (71, 1, 0.8f, 0.5);
    const auto s = summarise (alloc.noteOff (71, 1));
    CHECK (s.legato == std::vector<int> { A });
    CHECK (alloc.noteOnString (A) == 69);

    CHECK (summarise (alloc.noteOff (69, 1)).released == std::vector<int> { A });
}

TEST_CASE ("A melody over a held double stop keeps the chord sounding", "[allocator]")
{
    StringAllocator alloc;
    alloc.noteOn (62, 1, 0.8f, 0.0); // D4 on D
    alloc.noteOn (69, 1, 0.8f, 0.0); // A4 on A
    alloc.noteOn (71, 1, 0.8f, 0.5); // later B4 slurs from the melody note A4
    CHECK (alloc.noteOnString (D) == 62);
    CHECK (alloc.noteOnString (A) == 71);
}

TEST_CASE ("Mono legato mode never sounds more than one string", "[allocator]")
{
    StringAllocator alloc;
    alloc.setMode (PlayMode::monoLegato);
    alloc.noteOn (62, 1, 0.8f, 0.0);
    alloc.noteOn (69, 1, 0.8f, 0.0);
    CHECK (alloc.soundingCount() == 1);
    CHECK (alloc.noteOnString (D) == 69); // a fifth up: stays on the D string
}

TEST_CASE ("Poly mode starts every note on its own stroke", "[allocator]")
{
    StringAllocator alloc;
    alloc.setMode (PlayMode::poly);
    alloc.noteOn (69, 1, 0.8f, 0.0);
    const auto s = summarise (alloc.noteOn (71, 1, 0.8f, 0.5));
    CHECK (s.started == std::vector<int> { D });
    CHECK (s.legato.empty());
    CHECK (alloc.noteOnString (A) == 69);
    CHECK (alloc.noteOnString (D) == 71);
}

TEST_CASE ("MPE notes on different channels are independent", "[allocator]")
{
    StringAllocator alloc;
    alloc.noteOn (69, 2, 0.8f, 0.0);
    alloc.noteOn (69, 3, 0.8f, 0.0); // same pitch, another channel: unison on D
    CHECK (alloc.soundingCount() == 2);
    alloc.noteOff (69, 2);
    CHECK (alloc.soundingCount() == 1);
}

TEST_CASE ("All notes off releases every sounding string", "[allocator]")
{
    StringAllocator alloc;
    alloc.noteOn (55, 1, 0.8f, 0.0);
    alloc.noteOn (76, 1, 0.8f, 0.0);
    CHECK (summarise (alloc.allNotesOff()).released.size() == 2);
    CHECK (alloc.soundingCount() == 0);
}
