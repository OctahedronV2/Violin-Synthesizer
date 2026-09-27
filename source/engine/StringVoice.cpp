#include "engine/StringVoice.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace violinsynth::engine
{
namespace
{
constexpr int controlInterval = 32;
constexpr double smoothingSeconds = 0.02;
constexpr double legatoDynamicsSeconds = 0.15; // dynamics change smoothly across a slur
constexpr double legatoEntrySeconds = 0.025; // bow arriving on a new string mid-stroke
constexpr double vibratoOnsetSeconds = 0.25;
constexpr double openStringBeta = 0.05; // where sympathetic drive enters (near the bridge)
constexpr double silenceThreshold = 1.0e-5;
constexpr double noiseBandwidthHz = 0.7; // humanisation drift

double onePoleCoeff (double seconds, double rate)
{
    return seconds <= 0.0 ? 0.0 : std::exp (-1.0 / (seconds * rate));
}
} // namespace

void StringVoice::prepare (double internalSampleRate, int stringIndex)
{
    fs = internalSampleRate;
    spec = &strings[static_cast<std::size_t> (stringIndex)];
    // Allow a semitone of bend below the open string.
    string.prepare (fs, midiToHz (spec->openMidiNote - 1.5));
    auto params = string.getParams();
    params.friction.impedance = spec->impedance;
    string.setParams (params);

    smoothingCoeff = onePoleCoeff (smoothingSeconds, fs);
    noiseCoeff = std::exp (-2.0 * std::numbers::pi * noiseBandwidthHz * controlInterval / fs);
    random = 0x9e3779b9u * static_cast<std::uint32_t> (stringIndex + 1);
    reset();
}

void StringVoice::reset()
{
    string.reset();
    stage = Stage::open;
    currentNote = -1;
    logF0 = targetLogF0 = std::log (midiToHz (spec->openMidiNote));
    envelopePosition = 0.0;
    vibratoPhase = 0.0;
    expression = {};
    silentSeconds = silenceSeconds + 1.0;
    lastSpeed = 0.0;
    betaFloor = std::max (0.02, 1.2 * string.minBeta (midiToHz (spec->openMidiNote + 14)));
}

void StringVoice::setTarget (int note, bool glide)
{
    // A string cannot sound below its open pitch.
    note = std::max (note, spec->openMidiNote);
    currentNote = note;
    targetLogF0 = std::log (midiToHz (note));
    if (! glide)
        logF0 = targetLogF0;
    secondsSinceNoteChange = 0.0;
    silentSeconds = 0.0;
}

void StringVoice::start (int note, float velocity)
{
    setTarget (note, false);
    dynamicsTarget = std::clamp (static_cast<double> (velocity), 0.0, 1.0);
    dynamics = dynamicsTarget;
    dynamicsCoeff = smoothingCoeff;
    stage = Stage::attack;
    attackSeconds = -1.0; // use the Attack setting
    envelopePosition = 0.0;
}

void StringVoice::legato (int note, float velocity)
{
    const bool sounding = isBowed() && stage != Stage::release;
    setTarget (note, sounding);
    dynamicsTarget = std::clamp (static_cast<double> (velocity), 0.0, 1.0);
    dynamicsCoeff = onePoleCoeff (legatoDynamicsSeconds, fs);

    if (! sounding)
    {
        // Crossing onto this string mid-bow: a quick entry, no new stroke.
        dynamics = dynamicsTarget;
        stage = Stage::attack;
        attackSeconds = legatoEntrySeconds;
        envelopePosition = 0.0;
    }
}

void StringVoice::release()
{
    if (stage == Stage::attack || stage == Stage::sustain)
    {
        releaseStartLevel = envelopeShape();
        stage = Stage::release;
        envelopePosition = 0.0;
    }
}

double StringVoice::envelopeShape() const
{
    const auto raised = [] (double x) { return 0.5 - 0.5 * std::cos (std::numbers::pi * std::clamp (x, 0.0, 1.0)); };

    switch (stage)
    {
        case Stage::attack:
            return raised (envelopePosition);
        case Stage::sustain:
            return 1.0;
        case Stage::release:
            return releaseStartLevel * (1.0 - raised (envelopePosition));
        case Stage::open:
        case Stage::ringing:
            break;
    }
    return 0.0;
}

double StringVoice::nextNoise()
{
    random = random * 1664525u + 1013904223u;
    return static_cast<double> (random >> 8) / static_cast<double> (1u << 24) * 2.0 - 1.0;
}

void StringVoice::updateControlRate (const VoiceSettings& settings)
{
    glideCoeff = onePoleCoeff (settings.portamentoSeconds / 3.0, fs);

    // Slowly drifting noise for humanised vibrato, normalised to unit variance.
    const auto scale = 1.0 / (0.577 * std::sqrt ((1.0 - noiseCoeff) / (1.0 + noiseCoeff)));
    rateNoise = noiseCoeff * rateNoise + (1.0 - noiseCoeff) * nextNoise();
    depthNoise = noiseCoeff * depthNoise + (1.0 - noiseCoeff) * nextNoise();
    rateNoise = std::clamp (rateNoise, -1.0 / scale * 2.5, 1.0 / scale * 2.5);
    depthNoise = std::clamp (depthNoise, -1.0 / scale * 2.5, 1.0 / scale * 2.5);
}

double StringVoice::processSample (const VoiceSettings& settings, const StringContext& context)
{
    const auto dt = 1.0 / fs;

    if (controlCounter-- <= 0)
    {
        controlCounter = controlInterval;
        updateControlRate (settings);
    }

    // An open string with nothing to resonate with costs nothing.
    if (stage == Stage::open && silentSeconds > silenceSeconds && std::abs (context.sympatheticDrive) < 1.0e-9)
    {
        lastSpeed = 0.0;
        return 0.0;
    }

    // Envelope
    if (stage == Stage::attack)
    {
        envelopePosition += dt / std::max (attackSeconds > 0.0 ? attackSeconds : settings.attackSeconds, 1.0e-3);
        if (envelopePosition >= 1.0)
            stage = Stage::sustain;
    }
    else if (stage == Stage::release)
    {
        envelopePosition += dt / std::max (settings.releaseSeconds, 1.0e-3);
        if (envelopePosition >= 1.0)
            stage = Stage::ringing;
    }

    const auto& friction = string.getParams().friction;
    double f0, speed = 0.0, force = 0.0, drive = 0.0, b;

    if (stage == Stage::open)
    {
        f0 = midiToHz (spec->openMidiNote);
        b = openStringBeta;
        drive = context.sympatheticDrive;
    }
    else
    {
        // Pitch: glide, bends (global and per note) and humanised vibrato.
        logF0 = targetLogF0 + glideCoeff * (logF0 - targetLogF0);
        secondsSinceNoteChange += dt;
        const auto humanise = std::clamp (settings.humanise, 0.0, 1.0);
        const auto scale = 1.0 / (0.577 * std::sqrt ((1.0 - noiseCoeff) / (1.0 + noiseCoeff)));
        vibratoPhase += settings.vibratoRateHz * (1.0 + 0.08 * humanise * scale * rateNoise) * dt;
        vibratoPhase -= std::floor (vibratoPhase);
        const auto onset
            = std::clamp ((secondsSinceNoteChange - settings.vibratoDelaySeconds) / vibratoOnsetSeconds, 0.0, 1.0);
        const auto depth = (onset * settings.vibratoDepthCents * (1.0 + 0.25 * humanise * scale * depthNoise)
                            + 30.0 * expression.pressure);
        const auto vibratoCents = 0.5 * std::max (depth, 0.0) * std::sin (2.0 * std::numbers::pi * vibratoPhase);
        const auto bendCents = 100.0 * (context.globalBendSemitones + expression.bendSemitones);
        f0 = std::max (std::exp (logF0) * std::pow (2.0, (bendCents + vibratoCents) / 1200.0),
                       midiToHz (spec->openMidiNote - 1.0));

        // Bow position: the setting, or the per-note timbre (tasto 0 .. ponticello 1).
        auto betaTarget = settings.bowPosition;
        if (expression.timbre >= 0.0)
            betaTarget = 0.22 * std::pow (0.04 / 0.22, std::clamp (expression.timbre, 0.0, 1.0));
        betaTarget = std::clamp (betaTarget, betaFloor, 0.3);
        beta = betaTarget + smoothingCoeff * (beta - betaTarget);
        b = beta;

        if (isBowed())
        {
            const auto target = context.dynamicsOverride >= 0.0 ? context.dynamicsOverride : dynamicsTarget;
            dynamics = target + dynamicsCoeff * (dynamics - target);
            const auto pressureTarget
                = context.pressureOverride >= 0.0 ? context.pressureOverride : settings.bowPressure;
            pressure = pressureTarget + smoothingCoeff * (pressure - pressureTarget);

            // Speed from dynamics; force follows speed within the string's playable window.
            speed = (minBowSpeed + (maxBowSpeed - minBowSpeed) * std::pow (dynamics, 1.5)) * envelopeShape()
                * context.bowChangeGain;
            const auto fMax = 2.0 * friction.impedance * speed / (beta * (friction.muS - friction.muD));
            force = fMax * (spec->forceWindowLow + (spec->forceWindowHigh - spec->forceWindowLow) * pressure);
        }
        else
        {
            drive = context.sympatheticDrive; // a ringing string also resonates
        }
    }

    const auto y = string.process (f0, b, context.direction * speed, force, drive);
    lastSpeed = speed;
    lastF0 = f0;

    if (stage == Stage::ringing || stage == Stage::open)
    {
        silentSeconds = std::abs (y) < silenceThreshold ? silentSeconds + dt : 0.0;
        if (stage == Stage::ringing && silentSeconds > silenceSeconds)
        {
            // Finger lifted: the string becomes an undamped open string again.
            string.reset();
            stage = Stage::open;
            currentNote = -1;
            expression = {};
        }
    }

    return y;
}
} // namespace violinsynth::engine
