#pragma once

#include "engine/StringAllocator.h"
#include "engine/StringVoice.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>

namespace violinsynth::engine
{
struct PerformanceSettings
{
    VoiceSettings voice;
    PlayMode playMode = PlayMode::automatic;
    bool mpe = false;
    double mpeBendRangeSemitones = 48.0;
    Articulation articulation = Articulation::legato; // the Articulation parameter
};

// The four strings, the bow and the player: MIDI in, bridge force out, at
// the internal (oversampled) rate.
//
// MIDI mapping (see docs/PHASE4.md):
//   velocity         dynamics (bow speed) of the note
//   CC11 / CC2       dynamics of all strings, once received
//   CC1              bow pressure, once received
//   CC74             bow position, tasto to ponticello (per note with MPE)
//   aftertouch       extra vibrato depth (per note with MPE)
//   pitch bend       +/- bend range; with MPE, member channels bend their own note
//                    and the master channel (1) bends everything
//   notes 24-33      keyswitches selecting the articulation (docs/PHASE5.md); a
//                    keyswitch holds until the next one or a change of the
//                    Articulation parameter
class Violin
{
public:
    static constexpr int numStrings = StringAllocator::numStrings;
    static constexpr double bowLengthMetres = 0.62; // usable bow hair
    static constexpr double bowChangeSeconds = 0.04;

    void prepare (double internalSampleRate);
    void reset();

    void setSettings (const PerformanceSettings& s);
    void handleMidi (const juce::MidiMessage& message);
    void render (float* out, int numSamples);

    // For tests and diagnostics.
    int noteOnString (int string) const { return allocator.noteOnString (string); }
    double stringF0 (int string) const { return voices[static_cast<std::size_t> (string)].currentF0(); }
    double stringLevel (int string) const { return voices[static_cast<std::size_t> (string)].level(); }
    double stringBowSpeed (int string) const { return voices[static_cast<std::size_t> (string)].currentBowSpeed(); }
    int bowChangeCount() const { return bowChanges; }
    std::array<bool, numStrings> openStrings() const
    {
        std::array<bool, numStrings> open {};
        for (std::size_t s = 0; s < open.size(); ++s)
            open[s] = voices[s].isOpen();
        return open;
    }
    double bowDirection() const { return direction; }
    Articulation currentArticulation() const { return articulation; }

private:
    void apply (const StringActions& actions);
    void setArticulation (Articulation a);
    NoteExpression expressionFor (int channel) const;

    std::array<StringVoice, numStrings> voices;
    StringAllocator allocator;
    PerformanceSettings settings;
    double fs = 192000.0;
    double time = 0.0; // seconds since prepare, for chord detection
    Articulation articulation = Articulation::legato; // active: keyswitch or parameter
    Articulation parameterArticulation = Articulation::legato;

    // The bow
    double direction = 1.0;
    double bowUsed = 0.0; // metres of hair used in the current stroke
    double bowChangePhase = -1.0; // 0..1 during an automatic bow change
    double lastStrokeTime = -1.0e9;
    int bowChanges = 0;

    // Controllers
    double globalBend = 0.0; // semitones
    double dynamicsOverride = -1.0;
    double pressureOverride = -1.0;
    double globalPressure = 0.0;
    double globalTimbre = -1.0;
    std::array<NoteExpression, 17> channelExpression {}; // MPE, by MIDI channel 1..16
};
} // namespace violinsynth::engine
