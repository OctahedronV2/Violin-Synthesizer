#include "engine/ViolinVoice.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace violinsynth::engine
{
namespace
{
constexpr int controlInterval = 32; // samples between control-rate updates
constexpr double smoothingSeconds = 0.02;
constexpr double vibratoOnsetSeconds = 0.25;
constexpr double silenceThreshold = 1.0e-5; // N of bridge force
constexpr double silenceSeconds = 0.25;

double onePoleCoeff (double seconds, double rate)
{
    return seconds <= 0.0 ? 0.0 : std::exp (-1.0 / (seconds * rate));
}
} // namespace

void ViolinVoice::prepare (double internalSampleRate)
{
    fs = internalSampleRate;
    string.prepare (fs, lowestNoteHz);
    smoothingCoeff = onePoleCoeff (smoothingSeconds, fs);
    reset();
}

void ViolinVoice::reset()
{
    string.reset();
    heldCount = 0;
    stage = Stage::idle;
    envelopePosition = 0.0;
    bend = 0.0;
    vibratoPhase = 0.0;
    dynamicsFromController = false;
    pressureController = -1.0;
    logF0 = targetLogF0 = std::log (midiToHz (lastNote));
    silentSeconds = 0.0;
}

void ViolinVoice::noteOn (int midiNote, float velocity)
{
    const bool legato = heldCount > 0 && stage != Stage::idle && stage != Stage::ringing && stage != Stage::release;

    // Remove the note if it is already held, then push it as the newest.
    auto* end = held.begin() + heldCount;
    auto* it = std::remove (held.begin(), end, midiNote);
    heldCount = static_cast<int> (it - held.begin());
    if (heldCount == static_cast<int> (held.size()))
    {
        std::move (held.begin() + 1, held.end(), held.begin());
        --heldCount;
    }
    held[static_cast<std::size_t> (heldCount++)] = midiNote;

    if (! dynamicsFromController)
        dynamicsTarget = std::clamp (static_cast<double> (velocity), 0.0, 1.0);

    moveToNote (midiNote, legato);

    if (! legato)
        startStroke();
}

void ViolinVoice::noteOff (int midiNote)
{
    auto* end = held.begin() + heldCount;
    auto* it = std::remove (held.begin(), end, midiNote);
    const bool wasNewest = heldCount > 0 && held[static_cast<std::size_t> (heldCount - 1)] == midiNote;
    heldCount = static_cast<int> (it - held.begin());

    if (heldCount > 0)
    {
        if (wasNewest)
            moveToNote (held[static_cast<std::size_t> (heldCount - 1)], true);
        return;
    }

    if (stage == Stage::attack || stage == Stage::sustain)
    {
        releaseStartLevel = envelopeShape();
        stage = Stage::release;
        envelopePosition = 0.0;
    }
}

void ViolinVoice::allNotesOff()
{
    heldCount = 0;
    if (stage == Stage::attack || stage == Stage::sustain)
    {
        releaseStartLevel = envelopeShape();
        stage = Stage::release;
        envelopePosition = 0.0;
    }
}

void ViolinVoice::pitchBend (double normalised)
{
    bend = std::clamp (normalised, -1.0, 1.0) * settings.pitchBendRangeSemitones;
}

void ViolinVoice::controller (int number, double value)
{
    value = std::clamp (value, 0.0, 1.0);

    if (number == 1)
    {
        pressureController = value;
    }
    else if (number == 11 || number == 2)
    {
        dynamicsFromController = true;
        dynamicsTarget = value;
    }
    else if (number == 120 || number == 123)
    {
        allNotesOff();
    }
}

void ViolinVoice::aftertouch (double value)
{
    aftertouchDepth = std::clamp (value, 0.0, 1.0) * 30.0; // up to +30 cents
}

void ViolinVoice::startStroke()
{
    direction = -direction; // alternate up- and down-bows

    stage = Stage::attack;
    envelopePosition = 0.0;
    secondsSinceNoteChange = 0.0;
    silentSeconds = 0.0;
}

void ViolinVoice::moveToNote (int note, bool legato)
{
    lastNote = note;
    targetLogF0 = std::log (midiToHz (note));

    if (! legato || settings.portamentoSeconds <= 0.0)
        logF0 = targetLogF0;

    secondsSinceNoteChange = 0.0;

    const auto& spec = stringForNote (note);
    if (&spec != currentString || stage == Stage::idle)
    {
        currentString = &spec;
        auto params = string.getParams();
        params.friction.impedance = spec.impedance;
        string.setParams (params);
    }

    // Smallest bow position the delay lines support, with room for pitch bend.
    const auto highest = midiToHz (note + settings.pitchBendRangeSemitones + 1.0);
    betaFloor = std::max (0.02, 1.2 * string.minBeta (highest));
}

double ViolinVoice::envelopeShape() const
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
        case Stage::idle:
        case Stage::ringing:
            break;
    }
    return 0.0;
}

void ViolinVoice::updateControlRate()
{
    glideCoeff = onePoleCoeff (settings.portamentoSeconds / 3.0, fs);
}

void ViolinVoice::render (float* out, int numSamples)
{
    if (stage == Stage::idle)
    {
        std::fill (out, out + numSamples, 0.0f);
        return;
    }

    const auto dt = 1.0 / fs;
    const auto& friction = string.getParams().friction;
    const auto dmu = friction.muS - friction.muD;

    for (int i = 0; i < numSamples; ++i)
    {
        if (controlCounter-- <= 0)
        {
            controlCounter = controlInterval;
            updateControlRate();
        }

        // Envelope
        if (stage == Stage::attack)
        {
            envelopePosition += dt / std::max (settings.attackSeconds, 1.0e-3);
            if (envelopePosition >= 1.0)
                stage = Stage::sustain;
        }
        else if (stage == Stage::release)
        {
            envelopePosition += dt / std::max (settings.releaseSeconds, 1.0e-3);
            if (envelopePosition >= 1.0)
                stage = Stage::ringing;
        }
        const auto envelope = envelopeShape();

        // Smoothed controls
        dynamics = dynamicsTarget + smoothingCoeff * (dynamics - dynamicsTarget);
        const auto pressureTarget = pressureController >= 0.0 ? pressureController : settings.bowPressure;
        pressure = pressureTarget + smoothingCoeff * (pressure - pressureTarget);
        const auto betaTarget = std::clamp (settings.bowPosition, betaFloor, 0.3);
        beta = betaTarget + smoothingCoeff * (beta - betaTarget);
        logF0 = targetLogF0 + glideCoeff * (logF0 - targetLogF0);

        // Vibrato: delayed onset after each note change, centred on the note
        // (listeners hear roughly the mean pitch of a vibrato, so an
        // off-centre vibrato sounds out of tune). Depth is peak to peak.
        secondsSinceNoteChange += dt;
        vibratoPhase += settings.vibratoRateHz * dt;
        vibratoPhase -= std::floor (vibratoPhase);
        const auto onset
            = std::clamp ((secondsSinceNoteChange - settings.vibratoDelaySeconds) / vibratoOnsetSeconds, 0.0, 1.0);
        const auto depth = onset * settings.vibratoDepthCents + aftertouchDepth;
        const auto vibratoCents = 0.5 * depth * std::sin (2.0 * std::numbers::pi * vibratoPhase);

        const auto f0 = std::exp (logF0) * std::pow (2.0, (bend * 100.0 + vibratoCents) / 1200.0);

        // Bow: speed from dynamics; force follows speed, placed within the
        // string's playable window by the pressure control.
        const auto speed = (minBowSpeed + (maxBowSpeed - minBowSpeed) * std::pow (dynamics, 1.5)) * envelope;
        const auto fMax = 2.0 * friction.impedance * speed / (beta * dmu);
        const auto window = currentString->forceWindowLow
            + (currentString->forceWindowHigh - currentString->forceWindowLow) * pressure;
        const auto force = fMax * window;

        const auto y = string.process (f0, beta, direction * speed, force);
        out[i] = static_cast<float> (y);

        if (stage == Stage::ringing)
        {
            silentSeconds = std::abs (y) < silenceThreshold ? silentSeconds + dt : 0.0;
            if (silentSeconds > silenceSeconds)
            {
                stage = Stage::idle;
                string.reset();
                std::fill (out + i + 1, out + numSamples, 0.0f);
                return;
            }
        }
    }
}
} // namespace violinsynth::engine
