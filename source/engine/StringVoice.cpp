#include "engine/StringVoice.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace violinsynth::engine
{
namespace
{
// Pitch (glide, bends, vibrato) and the bow position and force targets are
// computed every controlInterval internal samples (5.8 kHz at 192 kHz); pitch
// is interpolated in between. It is also the step of the humanising noise,
// which has always run every 33 samples with its filter set for 32; both are
// kept so humanised vibrato and tremolo jitter are unchanged.
constexpr int controlInterval = 33;
constexpr int noiseFilterStep = 32;
constexpr double smoothingSeconds = 0.02;
constexpr double legatoDynamicsSeconds = 0.15; // dynamics change smoothly across a slur
constexpr double legatoEntrySeconds = 0.025; // bow arriving on a new string mid-stroke
constexpr double vibratoOnsetSeconds = 0.25;
constexpr double silenceThreshold = 1.0e-5;
constexpr double noiseBandwidthHz = 0.7; // humanisation drift

// Clean bowing (docs/CLEAN_BOWING.md). The player's weight never takes the
// force past this fraction of F_max, where the model turns to noise
// (docs/PHASE1_FINDINGS.md, section 3.1).
constexpr double maxForceFraction = 0.9;
// At the end of a note a player lifts the bow off the string rather than
// scraping it to a stop: the force falls this much faster than the speed.
constexpr double releaseLift = 3.0;

// The string twists as well as bends where the bow drags it (docs/CLEAN_BOWING.md).
// The twist travels this much faster than the bend, and dies within a couple
// of its own periods, which steadies the stick-slip at the bow.
constexpr double torsionSpeedRatio = 5.0;
constexpr double torsionImpedanceRatio = 3.0;
constexpr double torsionQ = 2.0;
// The twist takes a share of the bow's motion; the bow moves this much faster
// so the string bends as far as it did without it.
constexpr double torsionMakeup = (1.0 + torsionImpedanceRatio) / torsionImpedanceRatio;
// ... and lengthens each period a little: the note sounds 0.85 cents flatter
// on average (up to 1.8). The string is tuned 1.4 cents sharp, which also
// takes out the 0.55 cents the bowed model was already flat.
constexpr double torsionTuning = 1.000809;

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

// Pizzicato (docs/PIZZICATO.md). A fingertip grips the string over the end of
// the fingerboard, draws it aside and lets go as the string rolls off it.
// The finger stays the same distance from the bridge whatever note is
// stopped, so the shorter the string, the nearer its middle the pluck lands.
constexpr double scaleLength = 0.328; // m, open violin string
constexpr double pluckDistance = 0.07; // m from the bridge
constexpr double pluckDistanceSpread = 0.008; // m, from one pluck to the next at full Humanise
constexpr double maxPluckBeta = 0.42;
constexpr double pluckDrawSeconds = 0.0025; // the finger drawing the string aside
constexpr double fingerHold = 0.9; // how firmly the fingertip holds the string
constexpr double pluckDisplacement = 0.0017; // m, how far the string is drawn at full velocity
constexpr double pluckReleaseSoft = 0.0002; // s, the string rolling off the fingertip: softest pluck ...
constexpr double pluckReleaseHard = 0.00005; // ... and hardest
// The string swings both sideways (horizontal) and towards the top plate
// (vertical). The bridge rocks easily sideways, so the horizontal swing sounds
// loud and gives its energy away fast; the vertical one rings on. Together
// they give the fast, then slow decay of real plucked notes.
constexpr double pluckAngle = 0.5; // rad from the top plate, a typical pluck
constexpr double pluckAngleSpread = 0.2; // rad, from one pluck to the next at full Humanise
constexpr double verticalGain = 0.7; // how strongly the vertical swing drives the bridge
constexpr double verticalTuning = 1.0004; // it sees a stiffer bridge: 0.7 cents sharp
// Decay, fitted to the level of recorded notes over their first second
// (T60 at the fundamental and at 4 kHz). An open string rings about twice as
// long as a stopped note. A stopped note rings shorter the higher it is, as
// 1 / sqrt (f0) (T60 given at A4). The stopping fingertip damps the horizontal
// swing most: the string can roll across the soft fingertip, but is pressed
// vertically into the hard fingerboard.
constexpr dsp::LossSpec openVerticalLoss { 2.0, 0.8, 4000.0 };
constexpr dsp::LossSpec openHorizontalLoss { 0.6, 0.1, 4000.0 };
constexpr dsp::LossSpec stoppedVerticalLoss { 1.1, 0.25, 4000.0 };
constexpr dsp::LossSpec stoppedHorizontalLoss { 0.4, 0.08, 4000.0 };

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
    vertical.prepare (fs, midiToHz (spec->openMidiNote - 1.5));
    player.prepare (fs, midiToHz (spec->openMidiNote - 1.5));
    bowedParams = string.getParams();
    bowedParams.friction.impedance = spec->impedance;
    bowedParams.torsion = { torsionSpeedRatio, torsionImpedanceRatio, torsionQ };
    string.setParams (bowedParams);

    smoothingCoeff = onePoleCoeff (smoothingSeconds, fs);
    noiseCoeff = std::exp (-2.0 * std::numbers::pi * noiseBandwidthHz * noiseFilterStep / fs);
    // Scales the drifting noise to unit variance.
    noiseScale = 1.0 / (0.577 * std::sqrt ((1.0 - noiseCoeff) / (1.0 + noiseCoeff)));
    biteDecay = onePoleCoeff (staccatoBiteSeconds, fs);
    minF0 = midiToHz (spec->openMidiNote - 1.0);
    peakDecay = onePoleCoeff (0.3, fs);
    random = 0x9e3779b9u * static_cast<std::uint32_t> (stringIndex + 1);
    reset();
}

void StringVoice::reset()
{
    string.reset();
    vertical.reset();
    verticalActive = false;
    player.reset();
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
    controlJump = true;
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
    // Take the new pitch, or start the glide, from the next sample.
    controlJump = jumpNote = true;
    jumpGlides = glide;
    jumpFresh = stage == Stage::open;
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
            p.torsion.speedRatio = 0.0; // a lightly touched string: its twist would brighten the flageolet
            break;
        case Damping::soft:
            p.loss.t60High = 0.08;
            break;
        case Damping::plucked:
            p.tuning = dsp::Tuning::fundamental;
            p.torsion.speedRatio = 0.0; // the fingertip rolls with the string
            p.loss = pluckLoss;
            vertical.setParams (p);
            p.loss = pluckLossHorizontal;
            break;
        case Damping::shortRing:
            p.loss = { 0.4, 0.1, 4000.0 };
            break;
        case Damping::damped:
            p.tuning = dsp::Tuning::fundamental;
            p.loss = { 0.08, 0.03, 4000.0 };
            vertical.setParams (p);
            break;
    }
    string.setParams (p);
    if (d != Damping::plucked && d != Damping::damped && verticalActive)
    {
        // A plucked note's vertical swing dies away under the new stroke.
        auto quiet = bowedParams;
        quiet.tuning = dsp::Tuning::fundamental;
        quiet.loss = { 0.08, 0.03, 4000.0 };
        vertical.setParams (quiet);
    }
}

void StringVoice::setArticulation (Articulation a)
{
    noteArticulation = a;
    if (a == Articulation::pizzicato)
    {
        const bool open = currentNote == spec->openMidiNote;
        const auto stopped
            = [scale = std::sqrt (440.0 / midiToHz (std::max (currentNote, spec->openMidiNote)))] (dsp::LossSpec loss)
        {
            loss.t60 *= scale;
            return loss;
        };
        pluckLoss = open ? openVerticalLoss : stopped (stoppedVerticalLoss);
        pluckLossHorizontal = open ? openHorizontalLoss : stopped (stoppedHorizontalLoss);
        if (damping == Damping::plucked)
            damping = Damping::bowed; // apply this note's loss
    }
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
    string.setFinger (0.0, 0.0);
    vertical.setFinger (0.0, 0.0);
    setTarget (note, false);
    setArticulation (a);
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
            biteLevel = staccatoBite;
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
            pluckPosition = -1.0; // chosen on the first sample, with the Humanise setting
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
    string.setFinger (0.0, 0.0);
    vertical.setFinger (0.0, 0.0);
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
        string.setFinger (0.0, 0.0);
        vertical.setFinger (0.0, 0.0);
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

void StringVoice::pluck (const VoiceSettings& settings)
{
    if (pluckPosition < 0.0)
    {
        // Where and how this pluck lands: no two are quite the same.
        const auto humanise = std::clamp (settings.humanise, 0.0, 1.0);
        const auto length = scaleLength * midiToHz (spec->openMidiNote) / f0Now;
        const auto distance = pluckDistance + pluckDistanceSpread * humanise * nextNoise();
        pluckBeta = std::clamp (distance / length, 0.05, maxPluckBeta);
        beta = betaTarget = pluckBeta;
        pluckAmplitude = pluckDisplacement * (0.15 + 0.85 * dynamics) * (1.0 + 0.1 * humanise * nextNoise());
        pluckDraw = pluckDrawSeconds * fs;
        pluckRelease = (pluckReleaseSoft + (pluckReleaseHard - pluckReleaseSoft) * dynamics)
            * (1.0 + 0.2 * humanise * nextNoise()) * fs;
        const auto angle = pluckAngle + pluckAngleSpread * humanise * nextNoise();
        pluckHorizontal = std::cos (angle);
        pluckVertical = std::sin (angle);
        if (! verticalActive)
            vertical.reset();
        verticalActive = true;
        verticalLevel = 1.0;
        pluckPosition = 0.0;
    }
    if (pluckPosition > pluckDraw + pluckRelease + 1.0)
        return; // the string is free

    // The fingertip draws the string aside (a raised-cosine velocity, so the
    // string barely sounds while it moves), then lets go.
    const auto draw = pluckPosition / pluckDraw;
    auto velocity = 0.0, hold = 0.0;
    if (draw < 1.0)
    {
        velocity = pluckAmplitude / pluckDraw * fs * (1.0 - std::cos (2.0 * std::numbers::pi * draw));
        hold = fingerHold;
    }
    else
    {
        const auto rolled = (pluckPosition - pluckDraw) / pluckRelease;
        hold = rolled < 1.0 ? fingerHold * (0.5 + 0.5 * std::cos (std::numbers::pi * rolled)) : 0.0;
    }
    string.setFinger (velocity * pluckHorizontal, hold);
    vertical.setFinger (velocity * pluckVertical, hold);
    pluckPosition += 1.0;
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

void StringVoice::advanceControl (const VoiceSettings& settings, int samples)
{
    // Glide and vibrato, stepped over the whole interval.
    const auto seconds = samples / fs;
    logF0 = targetLogF0 + onePoleCoeff (settings.portamentoSeconds / 3.0, fs / samples) * (logF0 - targetLogF0);
    secondsSinceNoteChange += seconds;
    const auto humanise = std::clamp (settings.humanise, 0.0, 1.0);
    vibratoPhase += settings.vibratoRateHz * (1.0 + 0.08 * humanise * noiseScale * rateNoise) * seconds;
    vibratoPhase -= std::floor (vibratoPhase);
}

void StringVoice::updateNoise()
{
    // Slowly drifting noise for humanised vibrato, normalised to unit variance.
    rateNoise = noiseCoeff * rateNoise + (1.0 - noiseCoeff) * nextNoise();
    depthNoise = noiseCoeff * depthNoise + (1.0 - noiseCoeff) * nextNoise();
    rateNoise = std::clamp (rateNoise, -1.0 / noiseScale * 2.5, 1.0 / noiseScale * 2.5);
    depthNoise = std::clamp (depthNoise, -1.0 / noiseScale * 2.5, 1.0 / noiseScale * 2.5);
}

double StringVoice::controlF0 (const VoiceSettings& settings, const StringContext& context) const
{
    // Pitch: glide, bends (global and per note) and humanised vibrato.
    const auto humanise = std::clamp (settings.humanise, 0.0, 1.0);
    const auto onset
        = std::clamp ((secondsSinceNoteChange - settings.vibratoDelaySeconds) / vibratoOnsetSeconds, 0.0, 1.0);
    const auto depth = (onset * settings.vibratoDepthCents * (1.0 + 0.25 * humanise * noiseScale * depthNoise)
                        + 30.0 * expression.pressure);
    const auto vibratoCents = 0.5 * std::max (depth, 0.0) * std::sin (2.0 * std::numbers::pi * vibratoPhase);
    const auto bendCents = 100.0 * (context.globalBendSemitones + expression.bendSemitones);
    return std::max (std::exp (logF0) * std::pow (2.0, (bendCents + vibratoCents) / 1200.0), minF0);
}

void StringVoice::updateTargets (const VoiceSettings& settings, const StringContext& context)
{
    // Bow position: the setting, or the per-note timbre (tasto 0 .. ponticello 1),
    // unless the articulation fixes it.
    betaTarget = settings.bowPosition;
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
        default:
            break;
    }
    betaTarget = noteArticulation == Articulation::pizzicato ? pluckBeta : std::clamp (betaTarget, betaFloor, 0.3);

    // Bow force as a fraction of Schelleng's F_max: the Bow Pressure setting
    // (or CC1) across the string's clean window, unless the articulation
    // fixes it. Articulations set the fraction itself, so they sound the
    // same on every string.
    const auto setting = context.pressureOverride >= 0.0 ? context.pressureOverride : settings.bowPressure;
    // The clean player uses the whole range where Helmholtz motion exists; the
    // unassisted model scratches near its top, so it stops lower (docs/CLEAN_BOWING.md).
    const auto imperfection = std::clamp (settings.imperfection, 0.0, 1.0);
    const auto lo = spec->forceWindowLow;
    const auto hi = spec->playerWindowHigh + (spec->forceWindowHigh - spec->playerWindowHigh) * imperfection;
    fractionTarget = lo + (hi - lo) * setting;
    speedScale = 1.0;
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
}

void StringVoice::updateControlRate (const VoiceSettings& settings, const StringContext& context)
{
    controlCounter = controlInterval - 1;
    if (stage == Stage::open)
    {
        controlJump = jumpNote = false;
        updateNoise();
        return;
    }

    updateTargets (settings, context);
    if (stage == Stage::attack || stage == Stage::sustain)
        player.adjust (controlInterval / fs, 1.0 - settings.imperfection);
    f0Now = controlJump ? controlF0 (settings, context) : f0End; // an event lands on this sample
    player.setPeriod (fs / f0Now);
    controlJump = jumpNote = false;
    advanceControl (settings, controlInterval);
    updateNoise(); // drawn at the same samples as before, so tremolo's jitter is unchanged
    rampPitchTo (controlF0 (settings, context), controlInterval);
}

void StringVoice::jumpControl (const VoiceSettings& settings, const StringContext& context)
{
    // An event between control updates: a note, a slur, or a bend or pressure
    // change. The control state is already at the next update, `remaining`
    // samples ahead, so the event's own changes are brought up to it there.
    const auto note = jumpNote;
    controlJump = jumpNote = false;
    if (stage == Stage::open)
        return;
    updateTargets (settings, context);
    const auto remaining = controlCounter + 1;
    if (note)
    {
        secondsSinceNoteChange += remaining / fs;
        if (jumpGlides)
            logF0
                = targetLogF0 + onePoleCoeff (settings.portamentoSeconds / 3.0, fs / remaining) * (logF0 - targetLogF0);
    }

    const auto end = controlF0 (settings, context);
    if (note && jumpGlides)
    {
        rampPitchTo (end, remaining); // a slur glides from where the pitch is now
        return;
    }
    // A new note or a bend steps now, as before; within this interval the pitch
    // keeps the shape it had.
    f0Now = note && jumpFresh ? end : f0Now * (end / f0End);
    f0End = end;
    if (note && jumpFresh)
        f0Ratio = 1.0;
}

void StringVoice::rampPitchTo (double f0, int samples)
{
    // Geometric steps: linear in log frequency.
    f0End = f0;
    f0Ratio = f0End == f0Now ? 1.0 : std::exp (std::log (f0End / f0Now) / samples);
}

double StringVoice::processSample (const VoiceSettings& settings, const StringContext& context)
{
    const auto dt = 1.0 / fs;

    // A changed pitch bend lands on the sample it arrives, like a note (see jumpControl).
    if (controlCounter-- <= 0)
    {
        if (context.globalBendSemitones != lastGlobalBend)
        {
            lastGlobalBend = context.globalBendSemitones;
            controlJump = true;
        }
        updateControlRate (settings, context);
    }

    // Open strings are handled by SympatheticStrings at the host rate.
    if (stage == Stage::open)
    {
        lastSpeed = 0.0;
        peakLevel *= peakDecay;
        return 0.0;
    }

    if (controlJump || context.globalBendSemitones != lastGlobalBend)
    {
        lastGlobalBend = context.globalBendSemitones;
        jumpControl (settings, context);
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
    double speed = 0.0, force = 0.0;

    const auto f0 = f0Now;
    f0Now *= f0Ratio;
    beta = betaTarget + smoothingCoeff * (beta - betaTarget);
    const auto b = beta;

    strokeSeconds += dt;

    if (stage == Stage::plucked)
        pluck (settings);

    if (isBowed())
    {
        const auto target = context.dynamicsOverride >= 0.0 ? context.dynamicsOverride : dynamicsTarget;
        dynamics = target + dynamicsCoeff * (dynamics - target);

        // A short stroke lands with its weight already set; smoothing from the
        // previous note would give it that note's weight for its first 20 ms.
        const bool shortStroke
            = noteArticulation == Articulation::staccato || noteArticulation == Articulation::spiccato;
        if (shortStroke && strokeSeconds <= dt)
            forceFraction = fractionTarget;
        forceFraction = fractionTarget + smoothingCoeff * (forceFraction - fractionTarget);

        // Speed from dynamics; force follows speed within the string's playable window.
        const auto nominal = (minBowSpeed + (maxBowSpeed - minBowSpeed) * dynamics * std::sqrt (dynamics)) * speedScale
            * context.bowChangeGain * (twists() ? torsionMakeup : 1.0);
        auto forceSpeed = nominal * envelopeShape(); // the speed the force follows
        if (stage == Stage::release)
        {
            const auto lift = releaseLift * (1.0 - std::clamp (settings.imperfection, 0.0, 1.0));
            const auto shape = envelopeShape();
            forceSpeed *= lift == releaseLift ? shape * shape * shape : std::pow (shape, lift);
        }
        auto forceGain = 1.0;
        speed = forceSpeed;

        switch (noteArticulation)
        {
            case Articulation::staccato:
            {
                // Bitten onset, then the bow stops on the string. The weight eases
                // with the speed: at full weight on a slowing bow the string crunches.
                biteLevel *= biteDecay;
                forceGain = 1.0 + biteLevel;
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
        // The player's weight corrects the force, within the model's clean range.
        const auto fraction = std::min (forceFraction * player.weight(), std::max (maxForceFraction, forceFraction));
        force = forceGain * fMax * fraction;
    }

    auto y = string.process (isBowed() && twists() ? f0 * torsionTuning : f0, b, context.direction * speed, force);
    if (verticalActive)
    {
        const auto v = verticalGain * vertical.process (f0 * verticalTuning, b, 0.0, 0.0);
        y += v;
        verticalLevel = std::max (std::abs (v), verticalLevel * peakDecay);
        if (damping != Damping::plucked && verticalLevel < silenceThreshold)
        {
            vertical.reset();
            verticalActive = false;
        }
    }
    if (isBowed())
        player.listen (y, string.slipStarted());
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
            vertical.reset();
            verticalActive = false;
            player.reset();
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
