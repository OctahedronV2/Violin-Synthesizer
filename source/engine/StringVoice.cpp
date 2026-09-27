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
constexpr double silenceThreshold = 1.0e-5;
constexpr double noiseBandwidthHz = 0.7; // humanisation drift

// Articulations (docs/PHASE5.md)
constexpr double detacheMaxAttack = 0.04;
constexpr double staccatoAttack = 0.012;
constexpr double staccatoStroke = 0.11; // bow moving at full speed
constexpr double staccatoStop = 0.035; // bow decelerating on the string
constexpr double staccatoBite = 0.3; // extra force at the onset ...
constexpr double staccatoBiteSeconds = 0.02; // ... decaying with this time constant
constexpr double spiccatoContact = 0.06; // bow on the string per bounce, at full dynamics
constexpr double tremoloRateHz = 13.0; // strokes per second
constexpr double tremoloJitter = 0.12;
constexpr double pluckBeta = 0.25; // plucked over the end of the fingerboard
constexpr double pluckVelocity = 4.0; // m/s peak at full velocity

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
    bowedParams = string.getParams();
    bowedParams.friction.impedance = spec->impedance;
    string.setParams (bowedParams);

    smoothingCoeff = onePoleCoeff (smoothingSeconds, fs);
    noiseCoeff = std::exp (-2.0 * std::numbers::pi * noiseBandwidthHz * controlInterval / fs);
    peakDecay = onePoleCoeff (0.3, fs);
    random = 0x9e3779b9u * static_cast<std::uint32_t> (stringIndex + 1);
    reset();
}

void StringVoice::reset()
{
    string.reset();
    damping = Damping::bowed;
    string.setParams (bowedParams);
    noteArticulation = Articulation::legato;
    stage = Stage::open;
    currentNote = -1;
    logF0 = targetLogF0 = std::log (midiToHz (spec->openMidiNote));
    envelopePosition = 0.0;
    vibratoPhase = 0.0;
    expression = {};
    silentSeconds = silenceSeconds + 1.0;
    forceFraction = 0.48;
    lastSpeed = 0.0;
    peakLevel = 0.0;
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

void StringVoice::setDamping (Damping d)
{
    if (d == damping)
        return;

    damping = d;
    auto p = bowedParams;
    switch (d)
    {
        case Damping::bowed:
            break;
        case Damping::harmonic:
            p.loss.t60High = 0.06;
            break;
        case Damping::soft:
            p.loss.t60High = 0.08;
            break;
        case Damping::plucked:
            p.tuning = dsp::Tuning::fundamental;
            p.loss = { 1.0, 0.08, 4000.0 };
            break;
        case Damping::shortRing:
            p.loss = { 0.4, 0.1, 4000.0 };
            break;
        case Damping::damped:
            p.tuning = dsp::Tuning::fundamental;
            p.loss = { 0.08, 0.03, 4000.0 };
            break;
    }
    string.setParams (p);
}

void StringVoice::setArticulation (Articulation a)
{
    noteArticulation = a;
    setDamping (a == Articulation::pizzicato       ? Damping::plucked
                    : a == Articulation::harmonics ? Damping::harmonic
                    : a == Articulation::sulTasto  ? Damping::soft
                                                   : Damping::bowed);
}

bool StringVoice::drawsBow() const
{
    return isBowed() && slurs (noteArticulation);
}

void StringVoice::start (int note, float velocity, Articulation a)
{
    setArticulation (a);
    setTarget (note, false);
    dynamicsTarget = std::clamp (static_cast<double> (velocity), 0.0, 1.0);
    dynamics = dynamicsTarget;
    dynamicsCoeff = smoothingCoeff;
    envelopePosition = 0.0;
    strokeSeconds = 0.0;
    stage = Stage::attack;
    attackSeconds = -1.0; // use the Attack setting

    switch (a)
    {
        case Articulation::staccato:
            attackSeconds = staccatoAttack;
            stopAt = staccatoStroke;
            break;
        case Articulation::spiccato:
            attackSeconds = 0.002; // the bow is already moving when it lands
            stopAt = spiccatoContact * (1.4 - 0.4 * dynamics); // softer bounces stay longer
            break;
        case Articulation::tremolo:
            tremoloPhase = 0.0;
            tremoloSign = 1.0;
            tremoloRate = tremoloRateHz;
            break;
        case Articulation::pizzicato:
            stage = Stage::plucked;
            pluckPosition = 0.0;
            // A raised-cosine pulse a fraction of the period long (harder plucks are
            // shorter and brighter); a pulse near the period would cancel itself.
            pluckLength
                = std::max (1.0, std::min (0.25 - 0.15 * dynamics, 0.0015 * midiToHz (note)) * fs / midiToHz (note));
            pluckAmplitude = pluckVelocity * (0.2 + 0.8 * dynamics);
            beta = pluckBeta;
            break;
        case Articulation::legato:
        case Articulation::detache:
        case Articulation::harmonics:
        case Articulation::sulPonticello:
        case Articulation::sulTasto:
        case Articulation::conSordino:
            break;
    }
}

void StringVoice::legato (int note, float velocity, Articulation a)
{
    const bool sounding = isBowed() && stage != Stage::release;
    setArticulation (a);
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
    if (stage == Stage::plucked)
    {
        // The finger lifts off the string and damps it.
        setDamping (Damping::damped);
        stage = Stage::ringing;
        return;
    }

    if (noteArticulation == Articulation::staccato && (stage == Stage::attack || stage == Stage::sustain))
    {
        stopAt = std::min (stopAt, strokeSeconds); // stop the bow now
        return;
    }

    if (noteArticulation == Articulation::spiccato)
        return; // the bounce finishes by itself

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
        case Stage::plucked:
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

    // Open strings are handled by SympatheticStrings at the host rate.
    if (stage == Stage::open)
    {
        lastSpeed = 0.0;
        peakLevel *= peakDecay;
        return 0.0;
    }

    // Envelope
    if (stage == Stage::attack)
    {
        auto attack = attackSeconds > 0.0 ? attackSeconds : settings.attackSeconds;
        if (noteArticulation == Articulation::detache || noteArticulation == Articulation::tremolo)
            attack = std::min (attack, detacheMaxAttack);
        envelopePosition += dt / std::max (attack, 1.0e-3);
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
    double f0, speed = 0.0, force = 0.0, excitation = 0.0, b;

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

        // Bow position: the setting, or the per-note timbre (tasto 0 .. ponticello 1),
        // unless the articulation fixes it.
        auto betaTarget = settings.bowPosition;
        if (expression.timbre >= 0.0)
            betaTarget = 0.22 * std::pow (0.04 / 0.22, std::clamp (expression.timbre, 0.0, 1.0));
        switch (noteArticulation)
        {
            case Articulation::sulPonticello:
                betaTarget = 0.07;
                break;
            case Articulation::sulTasto:
                betaTarget = 0.15; // further out, the model turns subharmonic (docs/PHASE1_FINDINGS.md)
                break;
            case Articulation::harmonics:
                betaTarget = 0.13;
                break;
            case Articulation::pizzicato:
                betaTarget = pluckBeta;
                break;
            default:
                break;
        }
        betaTarget = std::clamp (betaTarget, betaFloor, 0.3);
        beta = betaTarget + smoothingCoeff * (beta - betaTarget);
        b = beta;

        strokeSeconds += dt;

        if (stage == Stage::plucked && pluckPosition < pluckLength)
        {
            // Raised-cosine velocity pulse at the plucking point.
            excitation = pluckAmplitude * 0.5 * (1.0 - std::cos (2.0 * std::numbers::pi * pluckPosition / pluckLength));
            pluckPosition += 1.0;
        }

        if (isBowed())
        {
            const auto target = context.dynamicsOverride >= 0.0 ? context.dynamicsOverride : dynamicsTarget;
            dynamics = target + dynamicsCoeff * (dynamics - target);
            // Bow force as a fraction of Schelleng's F_max: the Bow Pressure setting
            // (or CC1) across the string's clean window, unless the articulation
            // fixes it. Articulations set the fraction itself, so they sound the
            // same on every string.
            const auto setting = context.pressureOverride >= 0.0 ? context.pressureOverride : settings.bowPressure;
            auto fractionTarget = spec->forceWindowLow + (spec->forceWindowHigh - spec->forceWindowLow) * setting;
            auto speedScale = 1.0;
            switch (noteArticulation)
            {
                case Articulation::sulPonticello:
                    // Light bow near the bridge: weak fundamental, strong upper partials.
                    fractionTarget = std::min (fractionTarget, 0.21);
                    break;
                case Articulation::sulTasto:
                    // Over the fingerboard: slower, firmer bow for a steady, soft tone.
                    fractionTarget = std::max (fractionTarget, 0.48);
                    speedScale = 0.85;
                    break;
                case Articulation::harmonics:
                    fractionTarget = std::min (fractionTarget, 0.25);
                    speedScale = 0.9;
                    break;
                default:
                    break;
            }
            // A short stroke lands with its weight already set; smoothing from the
            // previous note would give it that note's weight for its first 20 ms.
            const bool shortStroke
                = noteArticulation == Articulation::staccato || noteArticulation == Articulation::spiccato;
            if (shortStroke && strokeSeconds <= dt)
                forceFraction = fractionTarget;
            forceFraction = fractionTarget + smoothingCoeff * (forceFraction - fractionTarget);

            // Speed from dynamics; force follows speed within the string's playable window.
            const auto nominal = (minBowSpeed + (maxBowSpeed - minBowSpeed) * std::pow (dynamics, 1.5)) * speedScale
                * context.bowChangeGain;
            auto forceSpeed = nominal * envelopeShape(); // the speed the force follows
            auto forceGain = 1.0;
            speed = forceSpeed;

            switch (noteArticulation)
            {
                case Articulation::staccato:
                {
                    // Bitten onset, then the bow stops on the string. The weight eases
                    // with the speed: at full weight on a slowing bow the string crunches.
                    forceGain = 1.0 + staccatoBite * std::exp (-strokeSeconds / staccatoBiteSeconds);
                    const auto stopping = (strokeSeconds - stopAt) / staccatoStop;
                    const auto slowing
                        = stopping <= 0.0 ? 1.0 : 0.5 + 0.5 * std::cos (std::numbers::pi * std::min (stopping, 1.0));
                    speed *= slowing;
                    forceSpeed *= slowing;
                    if (stopping >= 1.0)
                    {
                        setDamping (Damping::damped);
                        stage = Stage::ringing;
                        speed = forceSpeed = 0.0;
                    }
                    break;
                }
                case Articulation::spiccato:
                {
                    // The bow moves throughout; the force is a half-sine bounce.
                    speed = forceSpeed = nominal;
                    const auto contact = strokeSeconds / stopAt;
                    forceGain = contact < 1.0 ? std::sin (std::numbers::pi * contact) : 0.0;
                    if (contact >= 1.0)
                    {
                        setDamping (Damping::shortRing);
                        stage = Stage::ringing;
                        speed = forceSpeed = 0.0;
                    }
                    break;
                }
                case Articulation::tremolo:
                {
                    // Rapid reversals: speed falls to zero at each turn, the bow stays on.
                    tremoloPhase += tremoloRate * dt;
                    if (tremoloPhase >= 1.0)
                    {
                        tremoloPhase -= 1.0;
                        tremoloSign = -tremoloSign;
                        tremoloRate = tremoloRateHz * (1.0 + tremoloJitter * nextNoise());
                    }
                    const auto shape = std::sqrt (std::sin (std::numbers::pi * tremoloPhase));
                    speed = forceSpeed * shape * tremoloSign;
                    forceSpeed *= 0.5 + 0.5 * shape;
                    break;
                }
                default:
                    break;
            }

            const auto fMax = 2.0 * friction.impedance * forceSpeed / (beta * (friction.muS - friction.muD));
            force = forceGain * fMax * forceFraction;
        }
    }

    const auto y = string.process (f0, b, context.direction * speed, force, excitation);
    lastSpeed = std::abs (speed);
    lastF0 = f0;
    peakLevel = std::max (std::abs (y), peakLevel * peakDecay);

    if (stage == Stage::ringing)
    {
        silentSeconds = std::abs (y) < silenceThreshold ? silentSeconds + dt : 0.0;
        if (silentSeconds > silenceSeconds)
        {
            // Finger lifted: the string becomes an undamped open string again.
            string.reset();
            setDamping (Damping::bowed);
            noteArticulation = Articulation::legato;
            stage = Stage::open;
            currentNote = -1;
            expression = {};
        }
    }

    return y;
}
} // namespace violinsynth::engine
