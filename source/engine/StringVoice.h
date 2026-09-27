#pragma once

#include "dsp/BowedString.h"
#include "engine/StringData.h"

#include <cstdint>

namespace violinsynth::engine
{
// Performance settings shared by all strings, updated once per block.
struct VoiceSettings
{
    double bowPosition = 0.11; // beta, fraction of string length from the bridge
    double bowPressure = 0.5; // 0..1 across the string's playable force window
    double attackSeconds = 0.08;
    double releaseSeconds = 0.15;
    double vibratoRateHz = 5.5;
    double vibratoDepthCents = 25.0; // peak to peak
    double vibratoDelaySeconds = 0.3;
    double portamentoSeconds = 0.05;
    double pitchBendRangeSemitones = 2.0;
    double humanise = 0.5; // 0..1: random drift of vibrato rate and depth
    double resonance = 0.3; // 0..1: sympathetic resonance of undamped strings
    bool autoBowChange = true;
};

// Per-sample inputs a string voice gets from the Violin (shared bow and MIDI state).
struct StringContext
{
    double direction = 1.0; // bow direction, +1 or -1
    double bowChangeGain = 1.0; // dips during an automatic bow change
    double globalBendSemitones = 0.0;
    double dynamicsOverride = -1.0; // CC11/CC2 value, or < 0 to use note velocity
    double pressureOverride = -1.0; // CC1 value, or < 0 to use the Bow Pressure setting
    double sympatheticDrive = 0.0; // velocity injected into undamped strings (m/s)
};

// Per-note expression (MPE, or channel-wide controllers when MPE is off).
struct NoteExpression
{
    double bendSemitones = 0.0;
    double pressure = 0.0; // 0..1, adds vibrato depth
    double timbre = -1.0; // 0..1 bow position from tasto to ponticello, < 0 = not set
};

// One of the four violin strings, rendered at the internal rate.
//
// A string is either bowed (attack/sustain/release), ringing after the bow
// has left it, or open and undamped, where it only resonates
// sympathetically with the other strings.
class StringVoice
{
public:
    static constexpr double minBowSpeed = 0.08; // m/s at dynamics 0
    static constexpr double maxBowSpeed = 0.6; // m/s at dynamics 1

    void prepare (double internalSampleRate, int stringIndex);
    void reset();

    // Actions from the allocator.
    void start (int note, float velocity); // new bow stroke
    void legato (int note, float velocity); // glide, or enter mid-bow from another string
    void release();

    void setExpression (const NoteExpression& e) { expression = e; }
    int midiChannel = 1; // channel of the current note (for MPE routing)

    double processSample (const VoiceSettings& settings, const StringContext& context);

    bool isBowed() const { return stage == Stage::attack || stage == Stage::sustain || stage == Stage::release; }
    bool isSilent() const { return stage == Stage::open && silentSeconds > silenceSeconds; }
    int note() const { return currentNote; }
    double currentBowSpeed() const { return lastSpeed; }
    double currentF0() const { return lastF0; }

private:
    enum class Stage
    {
        open, // undamped open string (sympathetic resonance only)
        attack,
        sustain,
        release,
        ringing, // bow off, fingered note decaying
    };

    static constexpr double silenceSeconds = 0.25;

    void updateControlRate (const VoiceSettings& settings);
    double envelopeShape() const;
    double nextNoise();
    void setTarget (int note, bool glide);

    dsp::BowedString string;
    double fs = 192000.0;
    const StringSpec* spec = &strings[0];

    Stage stage = Stage::open;
    int currentNote = -1;
    double logF0 = 0.0, targetLogF0 = 0.0, glideCoeff = 0.0;
    double envelopePosition = 0.0, releaseStartLevel = 1.0, attackSeconds = 0.08;
    double dynamics = 0.5, dynamicsTarget = 0.5, dynamicsCoeff = 0.0;
    double pressure = 0.5, beta = 0.11, betaFloor = 0.02, smoothingCoeff = 0.0;
    double vibratoPhase = 0.0, secondsSinceNoteChange = 0.0;
    double rateNoise = 0.0, depthNoise = 0.0, noiseCoeff = 0.0;
    std::uint32_t random = 1;
    NoteExpression expression;
    double silentSeconds = 0.0, lastSpeed = 0.0, lastF0 = 0.0;
    int controlCounter = 0;
};
} // namespace violinsynth::engine
