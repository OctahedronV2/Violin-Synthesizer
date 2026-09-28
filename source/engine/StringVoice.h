#pragma once

#include "dsp/BowController.h"
#include "dsp/BowedString.h"
#include "engine/Articulation.h"
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
    double humanise = 0.5; // 0..1: random drift of vibrato, bow speed, contact point and finger pitch
    double imperfection = 0.0; // 0..1: 0 plays cleanly like a professional, 1 is the unassisted model
    double bowNoise = 0.5; // 0..1: grain of the rosin and hair in the friction
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
// A string is either bowed (attack/sustain/release), plucked, ringing after
// the bow has left it, or open. Open strings are silent here; their
// sympathetic resonance is modelled at the host rate by SympatheticStrings.
// Each note keeps the articulation it started with (docs/PHASE5.md).
class StringVoice
{
public:
    static constexpr double minBowSpeed = 0.08; // m/s at dynamics 0
    static constexpr double maxBowSpeed = 0.6; // m/s at dynamics 1

    void prepare (double internalSampleRate, int stringIndex);
    void reset();

    // Actions from the allocator.
    void start (int note, float velocity, Articulation a = Articulation::legato); // new stroke or pluck
    void legato (int note, float velocity, Articulation a = Articulation::legato); // glide, or enter mid-bow
    void release();

    void setExpression (const NoteExpression& e)
    {
        expression = e;
        controlJump = true;
    }
    int midiChannel = 1; // channel of the current note (for MPE routing)

    double processSample (const VoiceSettings& settings, const StringContext& context);

    bool isBowed() const { return stage == Stage::attack || stage == Stage::sustain || stage == Stage::release; }
    // Whether the note uses up bow hair (long strokes; not tremolo or short strokes).
    bool drawsBow() const;
    bool isSilent() const { return stage == Stage::open && silentSeconds > silenceSeconds; }
    bool isOpen() const { return stage == Stage::open; }
    int note() const { return currentNote; }
    Articulation articulation() const { return noteArticulation; }
    double currentBowSpeed() const { return lastSpeed; }
    double currentF0() const { return lastF0; }
    double level() const { return peakLevel; } // decaying peak of the bridge force, for meters
    // What the player hears (docs/CLEAN_BOWING.md): the share of the sound that
    // is scratch, and how often the string lets go of the bow per period.
    double scratch() const { return player.scratch(); }
    double slipsPerPeriod() const { return player.slipsPerPeriod(); }

private:
    bool twists() const { return string.getParams().torsion.speedRatio > 0.0; }
    enum class Stage
    {
        open, // undamped open string (sympathetic resonance only)
        attack,
        sustain,
        release,
        plucked, // pizzicato, finger down
        ringing, // bow off, fingered note decaying
    };

    // Loss settings of the string for the current technique.
    enum class Damping
    {
        bowed,
        harmonic, // light finger on a node: upper partials fade fast
        soft, // sul tasto: stands in for the wide, soft bow contact over the fingerboard
        plucked, // pizzicato: free vibration in two directions, damped by the stopping finger
        shortRing, // after a spiccato bounce
        damped, // finger lifted after pizzicato, or bow stopped after staccato
    };

    static constexpr double silenceSeconds = 0.25;

    // Pitch changes slowly, so it is computed every controlInterval samples
    // and interpolated in between (docs/PHASE7.md, 7.2); the bow position and
    // force targets are updated at the same rate. Envelopes and smoothing stay
    // per sample, so a steady note renders exactly as before.
    void updateControlRate (const VoiceSettings& settings, const StringContext& context);
    void jumpControl (const VoiceSettings& settings, const StringContext& context);
    void advanceControl (const VoiceSettings& settings, int samples);
    void updateTargets (const VoiceSettings& settings, const StringContext& context);
    double controlF0 (const VoiceSettings& settings, const StringContext& context) const;
    void rampPitchTo (double f0, int samples);
    double envelopeShape() const;
    double nextNoise();
    void updateNoise();
    void updateArm();
    void advanceGlide (const VoiceSettings& settings, double seconds);
    void setTarget (int note, bool glide);
    void setDamping (Damping d);
    void setArticulation (Articulation a);
    void pluck (const VoiceSettings& settings);

    dsp::BowedString string; // bowed, or the horizontal swing of a plucked string
    dsp::BowedString vertical; // a plucked string's vertical swing
    bool verticalActive = false;
    double verticalLevel = 0.0;
    dsp::BowController player;
    dsp::StringParams bowedParams;
    Damping damping = Damping::bowed;
    double fs = 192000.0;
    const StringSpec* spec = &strings[0];

    Stage stage = Stage::open;
    int currentNote = -1;
    double logF0 = 0.0, targetLogF0 = 0.0;
    double envelopePosition = 0.0, releaseStartLevel = 1.0, attackSeconds = 0.08;
    double dynamics = 0.5, dynamicsTarget = 0.5, dynamicsCoeff = 0.0;
    double forceFraction = 0.48, beta = 0.11, betaFloor = 0.02, smoothingCoeff = 0.0;
    double vibratoPhase = 0.0, secondsSinceNoteChange = 0.0;
    double rateNoise = 0.0, depthNoise = 0.0, noiseCoeff = 0.0, noiseScale = 1.0;
    double glideFrom = 0.0, glideProgress = 1.0;
    bool shifting = false; // the slur moves the hand, rather than changing finger
    int handPosition = 1; // semitones from the open string to the lowest note the first finger reaches
    // The player's slow wander (unit variance times armScale), and its random sequence.
    double speedWander = 0.0, betaWander = 0.0, pitchWander = 0.0, armCoeff = 0.0, armScale = 1.0;
    double armSpeedGain = 1.0; // the bow speed's wander, set at the control rate
    double speedDrive = 0.0, betaDrive = 0.0, pitchDrive = 0.0; // the first of the two smoothing stages
    std::uint32_t armRandom = 1;
    // Tremor: band-passed noise, the difference of two one-poles.
    struct Tremor
    {
        double low = 0.0, high = 0.0;
        double band() const { return high - low; }
    };
    Tremor speedTremor, pitchTremor;
    double tremorLowCoeff = 0.0, tremorHighCoeff = 0.0, tremorScale = 1.0;
    // Bow noise: low-passed white noise at the internal rate.
    double hairLevel = 0.0, hairCoeff = 0.0, hairScale = 1.0, hairVelocity = 0.0;
    std::uint32_t hairRandom = 1;
    double minF0 = 0.0;

    // Control-rate state
    double betaTarget = 0.11, fractionTarget = 0.48, speedScale = 1.0;
    double f0Now = 440.0, f0End = 440.0, f0Ratio = 1.0;
    bool controlJump = true; // an event to apply on the next sample: a note, a slur or a bend
    bool jumpNote = false, jumpGlides = false, jumpFresh = false;
    double lastGlobalBend = 0.0;
    std::uint32_t random = 1;
    NoteExpression expression;
    double silentSeconds = 0.0, lastSpeed = 0.0, lastF0 = 0.0, peakLevel = 0.0, peakDecay = 0.0;
    int controlCounter = 0;
    double stuckSamples = 0.0; // samples the string has stuck to the moving bow without letting go

    // Articulation state
    Articulation noteArticulation = Articulation::legato;
    double strokeSeconds = 0.0; // since the stroke or pluck started
    double stopAt = 0.0; // staccato: when the bow starts to stop
    double biteLevel = 0.0, biteDecay = 0.0; // staccato onset force
    double tremoloPhase = 0.0, tremoloRate = 13.0, tremoloSign = 1.0;
    // Pizzicato: samples since the pluck started (< 0 before the first), the
    // finger's draw and release in samples, how far it draws the string (m),
    // where it plucks, the share of the pluck in each direction, and the loss
    // of each swing for this note.
    double pluckPosition = 0.0, pluckDraw = 1.0, pluckRelease = 1.0, pluckAmplitude = 0.0, pluckBeta = 0.25;
    double pluckHorizontal = 1.0, pluckVertical = 0.0;
    dsp::LossSpec pluckLoss, pluckLossHorizontal; // vertical, horizontal
};
} // namespace violinsynth::engine
