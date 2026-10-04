#pragma once

#include "../core/Curves.h"

#include <juce_data_structures/juce_data_structures.h>

#include <array>
#include <atomic>
#include <vector>

namespace octavio2
{
// The drawn curves (2.3, Curves tab): one lane per o2::Dim up to o2::laneCount, breakpoints in
// beats on the host's timeline. Saved with the project (not in presets). The message thread
// edits; every edit is published whole to the audio thread through an o2::CurveExchange, so the
// audio thread never waits, allocates or sees a half-edited lane.
class CurveModel
{
public:
    struct Point
    {
        double beat;
        float v; // 0..1 (as the lane's CC / 127); < 0 a break
        bool operator== (const Point& o) const { return beat == o.beat && v == o.v; }
    };
    using Lane = std::vector<Point>;

    CurveModel() { publish(); }

    // message thread (or the host's state thread)
    Lane getLane (int k) const;
    void setLane (int k, Lane); // sorted by beat here
    void clearLane (int k) { setLane (k, {}); }
    void clearAll();
    bool hasCurve (int k) const;
    int version() const { return changes.load (std::memory_order_acquire); }

    static const juce::Identifier tag;
    juce::ValueTree toTree() const;
    void fromTree (const juce::ValueTree&); // missing: no curves (projects before 2.3)

    // audio thread: the newest published set, or nullptr when every lane is empty
    const o2::CurveSet* forAudio()
    {
        const auto& s = exchange.read();
        return any.load (std::memory_order_acquire) ? &s : nullptr;
    }

private:
    void publish();
    mutable juce::CriticalSection lock; // never taken by the audio thread
    std::array<Lane, o2::laneCount> lanes;
    o2::CurveExchange exchange;
    std::atomic<bool> any { false };
    std::atomic<int> changes { 0 };
};
} // namespace octavio2
