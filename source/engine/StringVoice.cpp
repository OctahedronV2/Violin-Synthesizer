#include "engine/StringVoice.h"

#include "engine/HurdyFix.h"

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
// Singing vibrato (docs/REFERENCE_SOUND.md): it starts 0.1 s before the
// Vibrato Delay and blooms over 0.6 s. Within a slur it keeps going through
// the note change; crossing to another string, the hand was already moving.
constexpr double vibratoBloomSeconds = 0.6, vibratoBloomLead = 0.1, vibratoCrossingSeconds = 0.3;
// Intonation: each note's own offset, a standard deviation of 16 cents at
// 100% (real players measure 9.5), and the finger lands about 9 cents flat at
// 50% and settles in 90 ms.
constexpr double intonationSpreadCents = 16.0, landingFlatCents = -9.0, landingSettleSeconds = 0.09;
constexpr double silenceThreshold = 1.0e-5;
constexpr double noiseBandwidthHz = 0.7; // humanisation drift

// Slurs (docs/NATURAL_PLAYING.md). Within a hand position a new finger drops
// onto the string, or one lifts off it, and the pitch changes almost at once;
// only a shift of the hand slides, over the Portamento time. A position
// reaches handSpan semitones up from the lowest first finger; first position
// starts a semitone above the open string.
constexpr int handSpan = 6;
constexpr int firstPosition = 1;
constexpr double fingerChangeSeconds = 0.01;

// The player's arm and hand are not a machine (docs/NATURAL_PLAYING.md): bow
// speed and contact point wander slowly through a stroke, and the stopping
// finger's pitch drifts by a few cents. Standard deviations at full Humanise,
// set so the default (50%) matches the level and pitch drift of held notes in
// the Iowa recordings (about 1 dB and 3 cents).
constexpr double armBandwidthHz = 1.5;
constexpr double armSpeedWander = 0.22; // fraction of the bow speed
constexpr double armBetaWander = 0.1; // fraction of the distance from the bridge
constexpr double fingerWanderCents = 7.0;
// ... and both hands shake a little: physiological tremor, 4 to 14 Hz.
// Amounts at full Humanise, set against the Iowa held notes.
constexpr double tremorLowHz = 4.0, tremorHighHz = 14.0;
constexpr double tremorSpeed = 0.15; // fraction of the bow speed
constexpr double tremorCents = 2.0;

// Bow noise (docs/NATURAL_PLAYING.md): rosin and the hundred-odd separate
// hairs drag the string unevenly, so the bow's motion at the contact carries
// a fine, broadband flutter. It is injected at the bow point as a velocity,
// in proportion to the bow speed, full while the string slips and weaker
// while it sticks, and the string filters it like any other motion there.
// Amount (standard deviation, a fraction of the bow speed) at full Bow Noise;
// the default of 50% matches the noise in the Iowa held notes.
constexpr double hairNoise = 1.0;
constexpr double hairStick = 0.2;
constexpr double hairNoiseHz = 15000.0;

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
// (Measured on held notes G3 to E6, this comes out 1 cent sharp on the low
// strings and 0.5 flat at the top. A correction that follows the register was
// tried for v1.1, but the player, which listens at the note itself, then
// settled fewer notes of a scale into clean Helmholtz motion: 35 of 64, not 42.)
inline double torsionTuning (double)
{
    return 1.000809;
}
// Near the bridge with a firm bow, the twisting string can also lock onto the
// bow: it sticks and travels with the hair, silent, however long the note is
// held (v1.0.1: Bright Soloist's G and A strings went dead). In Helmholtz
// motion the string sticks for less than a period, so sticking this long
// means it has locked. A player feels the string grab and eases the weight
// until it lets go; once it is sounding, the full weight holds it there.
// The weight falls to lockedEase after lockedPeriods, and on from there.
constexpr double lockedPeriods = 2.0;
constexpr double lockedEase = 0.3;

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
// The finger stays the same distance from the bridge (InstrumentSpec::
// pluckDistance) whatever note is stopped, so the shorter the string, the
// nearer its middle the pluck lands.
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
    // Allow a semitone of bend below the lowest open string.
    const auto lowest = midiToHz (lowestOpenNote - 1.5);
    string.prepare (fs, lowest);
    vertical.prepare (fs, lowest);
    player.prepare (fs, lowest);

    smoothingCoeff = onePoleCoeff (smoothingSeconds, fs);
    noiseCoeff = std::exp (-2.0 * std::numbers::pi * noiseBandwidthHz * noiseFilterStep / fs);
    // Scales the drifting noise to unit variance.
    noiseScale = 1.0 / (0.577 * std::sqrt ((1.0 - noiseCoeff) / (1.0 + noiseCoeff)));
    // Two one-poles in series, scaled to unit variance: the wander is slow
    // and smooth, with no fast jitter for the string to turn into noise.
    armCoeff = std::exp (-2.0 * std::numbers::pi * armBandwidthHz * controlInterval / fs);
    const auto c = armCoeff;
    armScale = 1.0 / std::sqrt (std::pow (1.0 - c, 4.0) * (1.0 + c * c) / (3.0 * std::pow (1.0 - c * c, 3.0)));
    biteDecay = onePoleCoeff (staccatoBiteSeconds, fs);
    peakDecay = onePoleCoeff (0.3, fs);
    configure (violinSpec, stringIndex);
}

void StringVoice::configure (const InstrumentSpec& newInstrument, int stringIndex)
{
    instrument = &newInstrument;
    spec = &instrument->string (stringIndex);
    string.setLowestF0 (midiToHz (spec->openMidiNote - 1.5));
    vertical.setLowestF0 (midiToHz (spec->openMidiNote - 1.5));
    bowedParams = {};
    bowedParams.friction.impedance = spec->impedance;
    bowedParams.torsion = { torsionSpeedRatio, torsionImpedanceRatio, torsionQ };
    bowedParams.loss = instrument->loss;
    string.setParams (bowedParams);
    if (! instrument->pickup)
        setPickup ({}, 0);

    minF0 = midiToHz (spec->openMidiNote - 1.0);
    random = 0x9e3779b9u * static_cast<std::uint32_t> (stringIndex + 1);
    armRandom = 0x85ebca6bu * static_cast<std::uint32_t> (stringIndex + 1);
    hairRandom = 0xc2b2ae35u * static_cast<std::uint32_t> (stringIndex + 1);
    tremorLowCoeff = std::exp (-2.0 * std::numbers::pi * tremorLowHz * controlInterval / fs);
    tremorHighCoeff = std::exp (-2.0 * std::numbers::pi * tremorHighHz * controlInterval / fs);
    hairCoeff = std::exp (-2.0 * std::numbers::pi * hairNoiseHz / fs);
    // Both scaled to unit variance (uniform noise has variance 1/3).
    hairScale = 1.0 / std::sqrt ((1.0 - hairCoeff) / (3.0 * (1.0 + hairCoeff)));
    {
        const auto a = tremorHighCoeff, b = tremorLowCoeff;
        const auto variance = ((1.0 - a) * (1.0 - a) / (1.0 - a * a) + (1.0 - b) * (1.0 - b) / (1.0 - b * b)
                               - 2.0 * (1.0 - a) * (1.0 - b) / (1.0 - a * b))
            / 3.0;
        tremorScale = 1.0 / std::sqrt (variance);
    }
    droning = false;
    droneWeight = 1.0;
    reset();
}

void StringVoice::setPickup (const std::array<double, dsp::BowedString::maxCoils>& coilMetres, int numCoils)
{
    const auto openF0 = midiToHz (spec->openMidiNote);
    string.setPickup (coilMetres, numCoils, instrument->scaleLength, openF0);
    vertical.setPickup (coilMetres, numCoils, instrument->scaleLength, openF0);
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
    glideProgress = 1.0;
    handPosition = firstPosition;
    armSpeedGain = 1.0;
    speedWander = betaWander = pitchWander = speedDrive = betaDrive = pitchDrive = 0.0;
    speedTremor = {};
    pitchTremor = {};
    hairLevel = 0.0;
    expression = {};
    silentSeconds = silenceSeconds + 1.0;
    droning = false;
    droneWeight = 1.0;
    forceFraction = 0.48;
    lastSpeed = 0.0;
    peakLevel = 0.0;
    stuckSamples = 0.0;
    controlJump = true;
    betaFloor = std::max (0.02, 1.2 * string.minBeta (midiToHz (spec->openMidiNote + 14)));
}

void StringVoice::setTarget (int note, bool glide)
{
    // A string cannot sound below its open pitch.
    note = std::max (note, spec->openMidiNote);
    // Move the hand if the note is out of its reach: up, the new note falls
    // under the third finger; down, under the first. Leaving or landing on an
    // open string needs no slide.
    const auto above = note - spec->openMidiNote;
    auto shift = false;
    if (above > 0 && (above < handPosition || above > handPosition + handSpan))
    {
        shift = true;
        handPosition = above > handPosition + handSpan ? above - 4 : std::max (firstPosition, above - 1);
    }
    const auto currentNoteBeforeTarget = currentNote;
    shifting = shift && currentNote > spec->openMidiNote;
    currentNote = note;
    targetLogF0 = std::log (midiToHz (note));
    glideFrom = logF0;
    glideProgress = glide ? 0.0 : 1.0;
    if (! glide)
        logF0 = targetLogF0;
    secondsSinceNoteChange = 0.0;
    if (hg::on (hg::transitions) && note != currentNoteBeforeTarget)
    {
        // The finger lands a little low and rolls onto the note, and the bow feels the change.
        scoopCents = -hg::param ("SCOOP", 22.0) * (0.75 + 0.25 * nextNoise());
        transitionSeconds = 0.0;
    }
    if (const auto sigma = intonationSpreadCents * std::clamp (intonation, 0.0, 1.0);
        sigma > 0.0 && note != currentNoteBeforeTarget)
    {
        // A real finger lands a little off and the ear pulls it in: each note has
        // its own intonation, and starts slightly flat (docs/REFERENCE_SOUND.md).
        const auto g = nextNoise() + nextNoise() + nextNoise(); // about unit variance
        noteIntonationCents = sigma * g;
        landingCents = landingFlatCents * (sigma / 8.0) * (1.0 + 0.5 * nextNoise());
    }
    if (! glide)
    {
        // Each note's vibrato is a little different, and starts wherever the hand
        // was. A slur keeps the vibrato going through the note change.
        noteVibRate = 1.0 + 0.06 * nextNoise();
        noteVibDepth = 1.0 + 0.12 * nextNoise(); // Humanise adds its own depth drift
        vibratoPhase = 0.5 + 0.5 * nextNoise();
    }
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
        // A steel string on a solid body rings longer than the violin's.
        const auto ring = [r = instrument->pluckRing] (dsp::LossSpec loss)
        {
            loss.t60 *= r;
            loss.t60High *= r;
            return loss;
        };
        pluckLoss = ring (open ? openVerticalLoss : stopped (stoppedVerticalLoss));
        pluckLossHorizontal = ring (open ? openHorizontalLoss : stopped (stoppedHorizontalLoss));
        if (damping == Damping::plucked)
            damping = Damping::bowed; // apply this note's loss
    }
    setDamping (a == Articulation::pizzicato       ? Damping::plucked
                    : a == Articulation::harmonics ? Damping::harmonic
                    : a == Articulation::sulTasto  ? Damping::soft
                                                   : Damping::bowed);
}

void StringVoice::start (int note, float velocity, Articulation a)
{
    string.setFinger (0.0, 0.0);
    vertical.setFinger (0.0, 0.0);
    const bool ringingString = stage != Stage::open && peakLevel > 1.0e-3;
    const auto previousNote = currentNote;
    if (hg::on (hg::longing) && ringingString && previousNote > spec->openMidiNote && std::abs (note - previousNote) >= 2)
    {
        setTarget (note, true); // the finger slides to the new note through the bow change
        shifting = true;
    }
    else
        setTarget (note, false);
    setArticulation (a);
    dynamicsTarget = std::clamp (static_cast<double> (velocity), 0.0, 1.0);
    dynamics = dynamicsTarget;
    dynamicsCoeff = smoothingCoeff;
    envelopePosition = 0.0;
    strokeSeconds = 0.0;
    vibratoSeconds = hg::on (hg::vibrato) ? 0.12 : 0.0; // HG: the hand keeps its vibrato between strokes
    stage = Stage::attack;
    attackSeconds = -1.0; // use the Attack setting
    strokeBite = true;
    if (hg::on (hg::proBowing) && slurs (a))
        attackSeconds = ringingString ? hg::param ("CHANGE_T", 0.03) : hg::param ("FRESH_T", 0.05);
    else if (hg::on (hg::quickChange) && slurs (a) && ringingString)
        attackSeconds = hg::param ("CHANGE_T", 0.02); // HG: a bow change on a sounding string is quick

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
    // On a fretted string the note steps to the next fret.
    setTarget (note, sounding && ! instrument->fretted);
    dynamicsTarget = std::clamp (static_cast<double> (velocity), 0.0, 1.0);
    dynamicsCoeff = onePoleCoeff (legatoDynamicsSeconds, fs);

    if (! sounding)
    {
        // Crossing onto this string mid-bow: a quick entry, no new stroke.
        dynamics = dynamicsTarget;
        stage = Stage::attack;
        attackSeconds = legatoEntrySeconds;
        vibratoSeconds = vibratoCrossingSeconds; // the hand was already vibrating on the other string
        strokeBite = false;
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
        releaseOverride = -1.0;
        stage = Stage::release;
        envelopePosition = 0.0;
    }
}

void StringVoice::liftFinger()
{
    if (stage == Stage::ringing && currentNote > spec->openMidiNote && damping != Damping::plucked)
        setDamping (Damping::damped);
}

void StringVoice::pluck (const VoiceSettings& settings)
{
    if (pluckPosition < 0.0)
    {
        // Where and how this pluck lands: no two are quite the same.
        const auto humanise = std::clamp (settings.humanise, 0.0, 1.0);
        const auto length = instrument->scaleLength * midiToHz (spec->openMidiNote) / f0Now;
        const auto distance = instrument->pluckDistance + pluckDistanceSpread * humanise * nextNoise();
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
    advanceGlide (settings, seconds);
    secondsSinceNoteChange += seconds;
    vibratoSeconds += seconds;
    const auto humanise = std::clamp (settings.humanise, 0.0, 1.0);
    // Singing vibrato: it quickens from 0.8 to about 1.05 times the rate as it blooms.
    const auto rate = settings.vibratoRateHz * (1.0 + 0.08 * humanise * noiseScale * rateNoise) * noteVibRate
        * (0.8 + 0.25 * vibratoBloom (settings)) * (hg::on (hg::vibrato) ? hg::param ("VIB_RATE", 1.1) * vibCycleRate : 1.0)
        * (hg::on (hg::somber) ? hg::param ("SOMBER_VIB_RATE", 0.87) : 1.0);
    vibratoPhase += rate * seconds;
    if (vibratoPhase >= 1.0 && hg::on (hg::vibrato))
        vibCycleRate = 1.0 + 0.09 * nextNoise(); // no two cycles the same
    vibratoPhase -= std::floor (vibratoPhase);
    transitionSeconds += seconds;
}

void StringVoice::advanceGlide (const VoiceSettings& settings, double seconds)
{
    // A slur changes finger quickly, or shifts over the Portamento time; the
    // pitch eases in and out of the move, as the hand does.
    if (glideProgress < 1.0)
    {
        const auto time
            = shifting ? (hg::on (hg::longing) ? hg::param ("SLIDE_T", 0.14)
                          : hg::on (hg::transitions) ? std::max (settings.portamentoSeconds, 0.12)
                                                     : settings.portamentoSeconds)
                       : std::min (fingerChangeSeconds, settings.portamentoSeconds);
        glideProgress = time <= 0.0 ? 1.0 : std::min (1.0, glideProgress + seconds / time);
    }
    if (glideProgress >= 1.0)
        logF0 = targetLogF0;
    else
    {
        auto c = 0.5 - 0.5 * std::cos (std::numbers::pi * glideProgress);
        if (shifting && ! hg::on (hg::longing))
            c = c * c * (3.0 - 2.0 * c); // a hidden shift: the finger moves late and fast, then lands
        logF0 = glideFrom + (targetLogF0 - glideFrom) * c;
    }
}

void StringVoice::updateArm()
{
    // Its own random sequence, so the vibrato and tremolo draws are unchanged.
    const auto draw = [this]
    {
        armRandom = armRandom * 1664525u + 1013904223u;
        return static_cast<double> (armRandom >> 8) / static_cast<double> (1u << 24) * 2.0 - 1.0;
    };
    const auto limit = 2.5 / armScale;
    for (auto& [smooth, w] : { std::pair { &speedDrive, &speedWander },
                               std::pair { &betaDrive, &betaWander },
                               std::pair { &pitchDrive, &pitchWander } })
    {
        *smooth = armCoeff * *smooth + (1.0 - armCoeff) * draw();
        *w = std::clamp (armCoeff * *w + (1.0 - armCoeff) * *smooth, -limit, limit);
    }
    if (hg::on (hg::living))
    {
        const auto c = std::sqrt (armCoeff); // slower than the arm's wander: the tone breathes
        livingDrive = c * livingDrive + (1.0 - c) * draw();
        livingForce = c * livingForce + (1.0 - c) * livingDrive;
        livingSpeedDrive = c * livingSpeedDrive + (1.0 - c) * draw();
        livingSpeed = c * livingSpeed + (1.0 - c) * livingSpeedDrive;
    }
    for (auto* t : { &speedTremor, &pitchTremor })
    {
        const auto x = draw();
        t->low = tremorLowCoeff * t->low + (1.0 - tremorLowCoeff) * x;
        t->high = tremorHighCoeff * t->high + (1.0 - tremorHighCoeff) * x;
    }
}

void StringVoice::updateNoise()
{
    // Slowly drifting noise for humanised vibrato, normalised to unit variance.
    rateNoise = noiseCoeff * rateNoise + (1.0 - noiseCoeff) * nextNoise();
    depthNoise = noiseCoeff * depthNoise + (1.0 - noiseCoeff) * nextNoise();
    rateNoise = std::clamp (rateNoise, -1.0 / noiseScale * 2.5, 1.0 / noiseScale * 2.5);
    depthNoise = std::clamp (depthNoise, -1.0 / noiseScale * 2.5, 1.0 / noiseScale * 2.5);
}

double StringVoice::vibratoBloom (const VoiceSettings& settings) const
{
    const auto x = hg::on (hg::vibrato)
                     ? std::clamp ((vibratoSeconds - hg::param ("VIB_DELAY", 0.06)) / hg::param ("VIB_BLOOM", 0.22), 0.0, 1.0)
                     : std::clamp ((vibratoSeconds - (settings.vibratoDelaySeconds - vibratoBloomLead)) / vibratoBloomSeconds,
                      0.0,
                      1.0);
    return 0.5 - 0.5 * std::cos (std::numbers::pi * x);
}

double StringVoice::controlF0 (const VoiceSettings& settings, const StringContext& context) const
{
    // Pitch: glide, bends (global and per note) and humanised vibrato.
    const auto humanise = std::clamp (settings.humanise, 0.0, 1.0);
    // The vibrato blooms, wider when louder and a little different on every
    // note. 0.8 brings the default depth to the 16 cents measured on real players.
    const auto grow = hg::on (hg::longing) ? 0.45 + 0.85 * std::min (1.0, secondsSinceNoteChange / 1.1) : 1.0;
    const auto onsetShape = grow * (hg::on (hg::vibrato) ? hg::param ("VIB_DEPTH", 1.7) : hg::on (hg::somber) ? 1.4 : 0.8) * vibratoBloom (settings) * noteVibDepth * (0.75 + 0.5 * dynamics);
    const auto depth = (onsetShape * settings.vibratoDepthCents * (1.0 + 0.25 * humanise * noiseScale * depthNoise)
                        + 30.0 * expression.pressure);
    if (droning)
        return std::exp (logF0); // an open string: nothing to bend it
    // A fretted string can only be pushed sharp: its vibrato bends up from the note.
    const auto wave = instrument->fretted ? 1.0 - std::cos (2.0 * std::numbers::pi * vibratoPhase)
                                          : std::sin (2.0 * std::numbers::pi * vibratoPhase);
    auto vibratoCents = 0.5 * std::max (depth, 0.0) * wave;
    if (! instrument->fretted)
    {
        // The hand lingers at the ends of the swing. It is centred on the note: the
        // flat finger landing already models the approach from below.
        const auto phi = 2.0 * std::numbers::pi * vibratoPhase;
        vibratoCents
            = 0.5 * std::max (depth, 0.0) * (std::sin (phi) + 0.15 * std::sin (2.0 * phi - 0.5 * std::numbers::pi));
        if (hg::on (hg::rollingFinger))
            vibratoCents -= 0.5 * std::max (depth, 0.0) * hg::param ("FLAT_LEAN", 0.0); // the hand rocks back from the note
    }
    const auto landing = landingCents * std::exp (-secondsSinceNoteChange / landingSettleSeconds)
        + scoopCents * std::exp (-secondsSinceNoteChange / hg::param ("SCOOP_T", 0.045));
    const auto bendCents = noteIntonationCents + landing
        + 100.0 * (context.globalBendSemitones + expression.bendSemitones)
        + fingerWanderCents * humanise * armScale * pitchWander
        + tremorCents * humanise * pitchTremor.band() * tremorScale;
    return std::max (std::exp (logF0) * std::pow (2.0, (bendCents + vibratoCents) / 1200.0), minF0);
}

void StringVoice::updateTargets (const VoiceSettings& settings, const StringContext& context)
{
    intonation = settings.intonation;
    // Bow position: the setting, or the per-note timbre (tasto 0 .. ponticello 1),
    // unless the articulation fixes it.
    betaTarget = settings.bowPosition;
    if (expression.timbre >= 0.0)
        betaTarget = 0.22 * std::pow (0.04 / 0.22, std::clamp (expression.timbre, 0.0, 1.0));
    betaTarget *= instrument->bowPositionScale;
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
    const auto humanise = std::clamp (settings.humanise, 0.0, 1.0);
    betaTarget *= 1.0 + armBetaWander * humanise * armScale * betaWander;
    armSpeedGain = 1.0 + armSpeedWander * humanise * armScale * speedWander
        + tremorSpeed * humanise * speedTremor.band() * tremorScale;
    if (shifting && glideProgress < 1.0)
        armSpeedGain *= 1.0 - 0.35 * std::sin (std::numbers::pi * glideProgress); // the bow lightens to hide the slide
    if (isBowed() && noteArticulation != Articulation::spiccato && noteArticulation != Articulation::staccato
        && noteArticulation != Articulation::tremolo)
    {
        // Note shaping: a small grip at the start of each stroke, then the bow
        // opens into the note (a swell peaking near 0.8 s) and eases back.
        const auto t = strokeSeconds;
        const auto grip = (hg::on (hg::bowArm) ? 0.0 : 0.12) * std::exp (-t / 0.07);
        // Within a slur each note still gets its own, smaller, swell.
        const auto u = secondsSinceNoteChange;
        const auto slurred = u < t - 0.01;
        const auto swellSize = hg::on (hg::bowArm) ? (slurred ? 0.2 : hg::param ("SWELL", 0.55)) : (slurred ? 0.0 : 0.22);
        const auto swellTime = hg::on (hg::bowArm) ? 0.6 : 0.5;
        const auto swell = swellSize * (u / swellTime) * std::exp (1.0 - u / swellTime);
        const auto shape = 1.0 + grip + swell;
        armSpeedGain *= shape;
        betaTarget *= 1.0 - 0.6 * swell; // louder, the bow moves toward the bridge
    }
    if (hg::on (hg::transitions) && transitionSeconds < 0.05 && secondsSinceNoteChange < strokeSeconds - 0.01)
        armSpeedGain *= 1.0 - 0.4 * std::sin (std::numbers::pi * transitionSeconds / 0.05); // the slur's finger change
    if (hg::on (hg::living))
    {
        armSpeedGain *= 1.0 + hg::param ("LIVE_SPEED", 0.14) * std::clamp (livingSpeed * livingScale(), -2.0, 2.0);
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
    fractionTarget = (lo + (hi - lo) * setting) * droneWeight;
    if (hg::on (hg::somber))
    {
        // Somber: the bow further from the bridge, lighter and slower.
        betaTarget *= hg::param ("SOMBER_BETA", 1.35);
        fractionTarget *= hg::param ("SOMBER_FORCE", 0.85);
        armSpeedGain *= 0.85;
    }
    if (hg::on (hg::proBowing) && isBowed())
    {
        // Bow distribution: long notes get a slow, heavy bow; quick notes a light, fast one.
        const auto d = std::clamp (std::pow (0.45 / std::max (context.expectedNote, 0.05), 0.3), 0.75, 1.35);
        armSpeedGain *= d;
        fractionTarget *= std::pow (d, -0.6);
    }
    if (hg::on (hg::longing) && isBowed())
    {
        // Lean into each note and sing higher notes out.
        const auto u = secondsSinceNoteChange;
        armSpeedGain *= (0.8 + 0.4 * (1.0 - std::exp (-u / 0.35))) * std::clamp (1.0 + 0.025 * (currentNote - 72), 0.7, 1.3);
    }
    if ((hg::on (hg::bowArm) || hg::on (hg::proBowing)) && isBowed())
    {
        // Heavy at the frog, light at the tip: down-bows fade, up-bows grow.
        const auto place = 0.5 - context.bowPlace;
        armSpeedGain *= 1.0 + hg::param ("ARM_SPEED", 0.5) * place;
        fractionTarget *= 1.0 + hg::param ("ARM_FORCE", 0.5) * place;
    }
    if (hg::on (hg::living))
        fractionTarget *= 1.0 + hg::param ("LIVE_FORCE", 0.3) * std::clamp (livingForce * livingScale(), -2.0, 2.0);
    if (hg::on (hg::playerDynamics) && isBowed() && noteArticulation != Articulation::pizzicato)
    {
        // Players make dynamics with weight and contact point, not bow speed:
        // piano far from the bridge and light, forte close in and heavy.
        const auto d = std::clamp (dynamics, 0.0, 1.0);
        const auto far = hg::param ("PD_FAR", 2.0), close = hg::param ("PD_CLOSE", 0.4);
        betaTarget = std::clamp (betaTarget * far * std::pow (close / far, d), 0.035, 0.3);
        fractionTarget *= hg::param ("PD_LIGHT", 0.8) + (hg::param ("PD_HEAVY", 1.25) - hg::param ("PD_LIGHT", 0.8)) * d;
    }
    if (hg::on (hg::weightBright) && isBowed() && damping == Damping::bowed)
    {
        // A heavier bow keeps the Helmholtz corner sharper: louder is brighter.
        const auto want = instrument->loss.t60High * std::pow (hg::param ("WB_RANGE", 4.0), std::clamp (dynamics, 0.0, 1.0) - 0.5);
        if (std::abs (want / bowedParams.loss.t60High - 1.0) > 0.03)
        {
            bowedParams.loss.t60High = want;
            string.setParams (bowedParams);
        }
    }
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

    updateArm();
    updateTargets (settings, context);
    if ((stage == Stage::attack || stage == Stage::sustain)
        && ! (hg::on (hg::settleHold) && std::min (strokeSeconds, secondsSinceNoteChange) < hg::param ("SETTLE_T", 0.12)))
        player.adjust (controlInterval / fs, 1.0 - settings.imperfection, hg::on (hg::leanOut));
    f0Now = controlJump ? controlF0 (settings, context) : f0End; // an event lands on this sample
    player.setPeriod (fs / (instrument->playerHearsTwist && twists() ? f0Now * torsionTuning (f0Now) : f0Now));
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
            advanceGlide (settings, remaining / fs);
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
        envelopePosition += dt / std::max (releaseOverride > 0.0 ? std::min (releaseOverride, settings.releaseSeconds) : settings.releaseSeconds, 1.0e-3);
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
        const auto dynSpeed = hg::on (hg::playerDynamics)
                                ? hg::param ("PD_SPEED_P", 0.3) + (hg::param ("PD_SPEED_F", 0.24) - hg::param ("PD_SPEED_P", 0.3)) * dynamics
                                : minBowSpeed + (maxBowSpeed - minBowSpeed) * dynamics * std::sqrt (dynamics);
        const auto nominal = dynSpeed * speedScale
            * context.bowChangeGain * (twists() ? torsionMakeup : 1.0) * armSpeedGain;
        auto forceSpeed = nominal * envelopeShape(); // the speed the force follows
        if (stage == Stage::release)
        {
            const auto lift = releaseLift * (1.0 - std::clamp (settings.imperfection, 0.0, 1.0));
            const auto shape = envelopeShape();
            forceSpeed *= lift == releaseLift ? shape * shape * shape : std::pow (shape, lift);
        }
        auto forceGain = 1.0;
        auto hairBoost = 1.0;
        if ((hg::on (hg::bowArm) || hg::on (hg::proBowing)) && slurs (noteArticulation) && strokeBite && strokeSeconds < 0.05)
        {
            // The hair bites into the string before it settles: a short consonant.
            const auto bite = 1.0 - strokeSeconds / 0.05;
            forceGain = 1.0 + hg::param ("BITE", hg::on (hg::somber) ? 0.5 : 0.9) * bite;
            hairBoost = 1.0 + 3.0 * bite;
        }
        if (hg::on (hg::transitions) && transitionSeconds < 0.04)
            hairBoost = std::max (hairBoost, 1.0 + 2.5 * (1.0 - transitionSeconds / 0.04));
        speed = forceSpeed;
        if (const auto ring = instrument->releaseRing;
            ring > 0.0 && stage == Stage::release && slurs (noteArticulation))
        {
            // A released stroke: the bow lifts off while it still moves, so the
            // string keeps its amplitude and rings on after the note ends.
            const auto lifted = 1.0 - envelopeShape() / std::max (releaseStartLevel, 1.0e-3);
            speed = nominal * (1.0 - (1.0 - ring) * lifted);
        }

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
        if (const auto locked = stuckSamples / (lockedPeriods * fs / f0); locked > 1.0)
            force *= std::pow (lockedEase, locked); // easing further the longer it holds
        hairRandom = hairRandom * 1664525u + 1013904223u;
        const auto grain = static_cast<double> (hairRandom >> 8) / static_cast<double> (1u << 24) * 2.0 - 1.0;
        hairLevel = hairCoeff * hairLevel + (1.0 - hairCoeff) * grain;
        hairVelocity = hairBoost * hairNoise * std::clamp (settings.bowNoise, 0.0, 1.0) * hairLevel * hairScale * speed
            * (string.isSticking() ? hairStick : 1.0);
    }

    if (hg::on (hg::wideBow))
        string.setBowWidth (hg::param ("BOW_W", 0.008) / (instrument->scaleLength * midiToHz (spec->openMidiNote) / f0));
    auto fingerTrim = hg::on (hg::wideBow) ? std::pow (2.0, -hg::param ("WIDE_TRIM", 5.0) * hg::param ("BOW_W", 0.008) / 0.008 / 1200.0) : 1.0;
    if (hg::on (hg::softFinger) && currentNote > spec->openMidiNote && damping != Damping::plucked)
    {
        // A fleshy fingertip: a lossy, slightly soft stop that rolls with the vibrato.
        auto pole = hg::param ("FINGER_SOFT", 0.7);
        auto gain = hg::param ("FINGER_GAIN", 0.996);
        if (hg::on (hg::rollingFinger))
        {
            const auto roll = std::sin (2.0 * std::numbers::pi * vibratoPhase) * vibratoBloom (settings);
            pole = std::clamp (pole + hg::param ("ROLL", 0.12) * roll, 0.0, 0.95);
            gain = std::min (gain * (1.0 - 0.002 * roll), 0.999);
        }
        string.setFingerStop (gain, pole);
        fingerTrim *= std::pow (2.0, hg::param ("FINGER_TRIM", 5.0) * pole / 0.7 / 1200.0); // the soft stop sits a little flat
    }
    else
        string.setFingerStop (1.0, 0.0);
    auto y = string.process (fingerTrim * (isBowed() && twists() ? f0 * torsionTuning (f0) : f0),
                             b,
                             context.direction * speed,
                             force,
                             isBowed() ? hairVelocity : 0.0);
    // Through a pickup the player's ear still follows the bridge force, but
    // the sound is the string's velocity under the coils.
    auto out = instrument->pickup ? string.pickupVelocity() : y;
    if (verticalActive)
    {
        const auto v = verticalGain * vertical.process (f0 * verticalTuning, b, 0.0, 0.0);
        y += v;
        out += instrument->pickup ? verticalGain * vertical.pickupVelocity() : v;
        verticalLevel = std::max (std::abs (v), verticalLevel * peakDecay);
        if (damping != Damping::plucked && verticalLevel < silenceThreshold)
        {
            vertical.reset();
            verticalActive = false;
        }
    }
    if (isBowed())
        player.listen (y, string.slipStarted());
    stuckSamples = isBowed() && speed != 0.0 && string.isSticking() ? stuckSamples + 1.0 : 0.0;
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
            droning = false;
            droneWeight = 1.0;
        }
    }

    return out;
}
} // namespace violinsynth::engine
