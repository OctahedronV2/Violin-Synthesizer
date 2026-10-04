#pragma once

#include <array>
#include <atomic>
#include <cstdint>

namespace octavio2
{
// What the player is doing, written by the audio thread once per block and read by the editor's
// displays (fingerboard, Now panel, curves). Plain atomics: each value is read on its own, so a
// display may mix two neighbouring blocks, which is invisible at 30 frames a second.
struct Telemetry
{
    std::atomic<int> note { -1 }; // sounding pitch (MIDI), -1 = none
    std::atomic<int> string { 2 }; // 0 G .. 3 E
    std::atomic<int> velocity { 0 }; // of the last note
    std::atomic<bool> slur { false }; // the note is slurred from the last one
    std::atomic<int> slurNotes { 0 };
    std::atomic<bool> releasing { true };
    std::atomic<float> pitch { 0 }; // finger pitch on the sounding string (with vibrato), semitones
    std::atomic<float> dynamics { 0 }; // 0..1 (pp..ff)
    std::atomic<float> handPos { 2 }; // semitones above the open string where the first finger sits
    std::atomic<float> bowDir { 1 }; // +1 down, -1 up
    std::atomic<float> hair { 0 }; // 0 frog .. 1 tip
    std::atomic<float> speed { 0 }; // m/s
    std::atomic<float> force { 0 }; // N, sounding string
    std::atomic<float> contact { 0.1f }; // bow-bridge distance / string length
    std::atomic<float> vibWidth { 0 }; // cents peak to peak, sounding string
    std::atomic<float> vibRate { 0 }; // Hz
    std::atomic<float> slips { 1 }; // slips per period, sounding string (1 = clean Helmholtz)
    std::atomic<float> cpu { 0 }; // share of real time
    std::atomic<float> bpm { 0 }; // host tempo, 0 = unknown (the Curves tab's MIDI export)
    std::array<std::atomic<float>, 4> stringForce {};
    // M6 views: the left hand's shift, the bow change, and who sets each dimension (0 the player,
    // 2 a drawn curve: the CC lane has taken it over; the values of ui::Mode)
    std::atomic<bool> sliding { false }; // the finger is sliding to target (a shift)
    std::atomic<float> slideFrom { 0 }; // MIDI pitch the slide left from
    std::atomic<float> target { 0 }; // MIDI pitch the finger is going to
    std::atomic<bool> changing { false }; // the bow is turning round
    std::atomic<int> dynMode { 0 }, vibMode { 0 }, rateMode { 0 }, contactMode { 0 };
    // 2.3: who is in charge now per o2::Dim (o2::DimMode: Auto, Guided, Manual), and whether a
    // controller lane holds it (an incoming CC made it Guided)
    std::array<std::atomic<int>, 5> dimMode {};
    std::array<std::atomic<bool>, 5> ccHolds {};
    // 2.3: the host's timeline: it has given a position at all, it plays, the beat heard now,
    // beats per bar
    std::atomic<bool> hasTimeline { false }, playing { false };
    std::atomic<double> beat { 0.0 };
    std::atomic<float> barLength { 4.0f }; // quarter notes per bar
    // 2.3: the stroke now (0 Auto's, 1 legato, 2 detache, 3 staccato, 4 martele, 5 spiccato), the
    // articulation that plays (Articulation order), and what keyswitches or UACC latched over the
    // articulation, bow style and contact parameters (-1: none) with the parameter values they
    // were compared with (a display shows a latch only while its parameter still has that value)
    std::atomic<int> stroke { 0 }, articulation { 0 };
    std::atomic<int> articulationLatch { -1 }, styleLatch { -1 }, contactLatch { -1 };
    std::atomic<int> articulationParam { 0 }, styleParam { 0 }, contactParam { 0 };

    // the auto curves: one point every 10 ms of engine time
    struct Point
    {
        float t = 0, dynamics = 0, vibWidth = 0, contact = 0, vibRate = 0;
        bool sounding = false;
        // 2.3: the beat heard (-1e9: the timeline was not playing) and the player's levels before
        // its micro-shaping, in the lanes' units (Player::baseVibWidth ...): "Guess curves"
        double beat = -1e9;
        float dynBase = 0, vibBase = 0, contactBase = 0, rateBase = 0;
    };
    static constexpr uint64_t historySize = 4096; // 41 s
    std::array<Point, historySize> history {};
    std::atomic<uint64_t> historyCount { 0 };
};
} // namespace octavio2
