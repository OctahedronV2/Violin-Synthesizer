#pragma once

#include "dsp/BowedString.h"
#include "engine/StringData.h"

#include <array>

namespace violinsynth::engine
{
// Performance settings, updated from the plugin parameters once per block.
struct VoiceSettings
{
    double bowPosition = 0.11; // beta, fraction of string length from the bridge
    double bowPressure = 0.5; // 0..1 across the string's playable force window
    double attackSeconds = 0.08;
    double releaseSeconds = 0.15;
    double vibratoRateHz = 5.5;
    double vibratoDepthCents = 25.0;
    double vibratoDelaySeconds = 0.3;
    double portamentoSeconds = 0.05;
    double pitchBendRangeSemitones = 2.0;
};

// A single bowed violin voice, rendered at the internal (oversampled) rate.
//
// This is the minimal Phase 2 performance layer: last-note-priority legato,
// automatic string choice, a bow-speed envelope with force following speed
// (docs/PHASE1_FINDINGS.md, section 3.1), automatic vibrato and basic MIDI
// control. Multi-string playing, MPE and articulations come in Phases 4-5.
//
// MIDI mapping:
//   velocity        initial dynamics (bow speed)
//   CC11 / CC2      dynamics, overriding velocity once received
//   CC1             bow pressure, overriding the Bow Pressure setting once received
//   aftertouch      extra vibrato depth
//   pitch bend      +/- pitchBendRangeSemitones
class ViolinVoice
{
public:
    static constexpr double minBowSpeed = 0.08; // m/s at dynamics 0
    static constexpr double maxBowSpeed = 0.6; // m/s at dynamics 1
    static constexpr double lowestNoteHz = 150.0; // G3 minus a generous pitch bend

    void prepare (double internalSampleRate);
    void reset();

    void setSettings (const VoiceSettings& s) { settings = s; }

    void noteOn (int midiNote, float velocity);
    void noteOff (int midiNote);
    void allNotesOff();
    void pitchBend (double normalised); // -1..1
    void controller (int number, double value); // value 0..1
    void aftertouch (double value); // 0..1

    // Renders numSamples at the internal rate, replacing the contents of out.
    void render (float* out, int numSamples);

    bool isActive() const { return stage != Stage::idle; }
    int currentNote() const { return heldCount > 0 ? held[static_cast<std::size_t> (heldCount - 1)] : lastNote; }

private:
    enum class Stage
    {
        idle,
        attack,
        sustain,
        release,
        ringing, // bow off, string decaying
    };

    void startStroke();
    void moveToNote (int note, bool legato);
    void updateControlRate();
    double envelopeShape() const;

    VoiceSettings settings;
    dsp::BowedString string;
    double fs = 192000.0;

    // Held notes, oldest first (last-note priority).
    std::array<int, 16> held {};
    int heldCount = 0;
    int lastNote = 69;
    const StringSpec* currentString = &strings[2];

    // Pitch
    double logF0 = 0.0; // current, smoothed (natural log of Hz)
    double targetLogF0 = 0.0;
    double glideCoeff = 0.0;
    double bend = 0.0; // semitones
    double vibratoPhase = 0.0;
    double secondsSinceNoteChange = 0.0;
    double aftertouchDepth = 0.0;

    // Bow
    Stage stage = Stage::idle;
    double envelopePosition = 0.0; // 0..1 within attack or release
    double releaseStartLevel = 1.0;
    double direction = 1.0;
    double dynamics = 0.5, dynamicsTarget = 0.5;
    bool dynamicsFromController = false;
    double pressure = 0.5;
    double pressureController = -1.0; // < 0 until CC1 is received
    double beta = 0.11;
    double betaFloor = 0.02;
    double smoothingCoeff = 0.0;
    double silentSeconds = 0.0;
    int controlCounter = 0;
};
} // namespace violinsynth::engine
