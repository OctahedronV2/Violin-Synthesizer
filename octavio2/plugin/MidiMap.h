#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <vector>

namespace octavio2
{
// The MIDI mapping (M6): which incoming controller drives what. A fixed table of up to
// maxEntries rows, each "CC n -> target, over lo..hi of the target's range, maybe inverted".
// Several rows may share a CC (one controller, several targets).
//
// Targets are the player's own controllers (dynamics, vibrato, contact ... as Player::controller
// numbers them), the slur pedal, or any host parameter. The map presets (Octavio 2, Legacy, None)
// only fill the table; the table is what plays and what is saved with the project.
//
// Threads: the message thread edits (setEntries, Learn); the audio thread reads the table through
// atomics (forEach), so it never waits, allocates or sees a half-written row as a valid one.
class MidiMap
{
public:
    static constexpr int maxEntries = 32;

    // target codes: 0..127 the player's controller of that number, slurPedal, or
    // parameterBase + the host parameter's index (AudioProcessor::getParameters)
    static constexpr int slurPedal = 1000;
    static constexpr int parameterBase = 2000;

    struct Entry
    {
        int cc = -1; // 0..127
        int target = 1;
        float lo = 0.0f, hi = 1.0f; // of the target's range (0..1)
        bool invert = false;
        bool operator== (const Entry& o) const
        {
            return cc == o.cc && target == o.target && lo == o.lo && hi == o.hi && invert == o.invert;
        }
    };

    enum class Preset
    {
        octavio, // CC1 = dynamics (the library standard, as Octastra)
        legacy, // CC1 = vibrato, CC11 = expression (as many sample libraries and Octavio 1)
        none
    };
    static juce::StringArray presetNames();
    static std::vector<Entry> presetEntries (Preset);

    // The player's controllers a row can drive, in menu order.
    struct PlayerTarget
    {
        int code;
        const char* name;
        const char* range; // what 0..127 does
        bool drawn; // a drawn curve: takes over the player's own (Auto -> Guided)
    };
    static const std::vector<PlayerTarget>& playerTargets();

    explicit MidiMap (juce::AudioProcessor&);

    // message thread
    std::vector<Entry> getEntries() const;
    void setEntries (std::vector<Entry>);
    void loadPreset (Preset p) { setEntries (presetEntries (p)); }
    // the preset the table equals, or -1 (edited)
    int matchingPreset() const;
    juce::String targetName (int target) const;
    juce::String targetRange (int target) const;
    static juce::String sourceName (int cc); // "Mod wheel", "Sustain", ...

    // the state saved with the project (getStateInformation)
    static const juce::Identifier tag;
    juce::ValueTree toTree() const;
    // a missing or empty tree gives the Octavio 2 map (projects saved before M6)
    void fromTree (const juce::ValueTree&);

    // Learn: the last controller that arrived (any thread may read; the audio thread writes)
    void noteIncoming (int cc, int value)
    {
        lastCc.store (cc | (value << 8), std::memory_order_relaxed);
        incoming.fetch_add (1, std::memory_order_release);
    }
    int incomingCount() const { return incoming.load (std::memory_order_acquire); }
    int lastIncomingCc() const { return lastCc.load (std::memory_order_relaxed) & 0xff; }
    int lastIncomingValue() const { return lastCc.load (std::memory_order_relaxed) >> 8; }

    // audio thread: fn (target, value 0..1 after range and invert) for every row of this CC
    template <typename Fn>
    bool forEach (int cc, int value127, Fn&& fn) const
    {
        bool any = false;
        const int n = count.load (std::memory_order_acquire);
        for (int i = 0; i < n; ++i)
        {
            const auto& s = slots[(size_t) i];
            if (s.cc.load (std::memory_order_acquire) != cc)
                continue;
            float x = (float) value127 / 127.0f;
            if (s.invert.load (std::memory_order_relaxed))
                x = 1.0f - x;
            const float lo = s.lo.load (std::memory_order_relaxed), hi = s.hi.load (std::memory_order_relaxed);
            fn (s.target.load (std::memory_order_relaxed), lo + (hi - lo) * x);
            any = true;
        }
        return any;
    }

private:
    struct Slot
    {
        std::atomic<int> cc { -1 }, target { 0 };
        std::atomic<float> lo { 0.0f }, hi { 1.0f };
        std::atomic<bool> invert { false };
    };
    juce::AudioProcessor& processor;
    std::array<Slot, maxEntries> slots;
    std::atomic<int> count { 0 };
    std::vector<Entry> entries; // the message thread's copy
    mutable juce::CriticalSection entriesLock; // getStateInformation may run on another thread (never the audio one)
    std::atomic<int> lastCc { -1 }, incoming { 0 };
};
} // namespace octavio2
