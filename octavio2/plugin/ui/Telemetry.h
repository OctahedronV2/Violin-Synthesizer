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
    std::array<std::atomic<float>, 4> stringForce {};

    // the auto curves: one point every 10 ms of engine time
    struct Point
    {
        float t = 0, dynamics = 0, vibWidth = 0, contact = 0, vibRate = 0;
        bool sounding = false;
    };
    static constexpr uint64_t historySize = 4096; // 41 s
    std::array<Point, historySize> history {};
    std::atomic<uint64_t> historyCount { 0 };
};
} // namespace octavio2
