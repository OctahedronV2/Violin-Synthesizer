#include "engine/Violin.h"

#include "engine/HurdyFix.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace violinsynth::engine
{
double velocityToDynamics (double velocity, double top)
{
    const auto v = std::clamp (velocity, 0.0, 1.0);
    top = std::clamp (top, 0.6, 1.0);
    const auto b = (1.0 - top) / (top - 0.5);
    return v * (1.0 + b) * top / (1.0 + b * v);
}

namespace
{
// The bowed guitar's humbuckers, as on a Les Paul: two coils each, 18 mm
// apart, centred this far from the bridge (m).
constexpr double neckPickup = 0.156, bridgePickup = 0.041, coilSpacing = 0.018;
} // namespace

void Violin::prepare (double internalSampleRate)
{
    fs = internalSampleRate;
    for (int s = 0; s < maxStrings; ++s)
        voices[static_cast<std::size_t> (s)].prepare (fs, s);
    currentInstrument = Instrument::violin;
    instrument = &violinSpec;
    allocator.setInstrument (violinSpec);
    setInstrument (settings.instrument);
    reset();
}

void Violin::setInstrument (Instrument i)
{
    currentInstrument = i;
    instrument = &engine::instrumentSpec (i);
    allocator.setInstrument (*instrument);
    for (int s = 0; s < instrument->numStrings; ++s)
        voices[static_cast<std::size_t> (s)].configure (*instrument, s);
    drones.fill (false);
    for (auto& channel : soundingNote)
        channel.fill (-1);
    setPickup (settings.pickup);
}

void Violin::setPickup (Pickup p)
{
    currentPickup = p;
    if (! instrument->pickup)
        return;
    std::array<double, dsp::BowedString::maxCoils> coils {};
    int count = 0;
    for (const auto centre : { neckPickup, bridgePickup })
    {
        if ((centre == neckPickup && p == Pickup::bridge) || (centre == bridgePickup && p == Pickup::neck))
            continue;
        coils[static_cast<std::size_t> (count++)] = centre - 0.5 * coilSpacing;
        coils[static_cast<std::size_t> (count++)] = centre + 0.5 * coilSpacing;
    }
    for (int s = 0; s < instrument->numStrings; ++s)
        voices[static_cast<std::size_t> (s)].setPickup (coils, count);
}

void Violin::reset()
{
    for (auto& v : voices)
        v.reset();
    allocator.reset();
    drones.fill (false);
    time = 0.0;
    direction = 1.0;
    bowUsed = 0.0;
    bowChangePhase = -1.0;
    lastStrokeTime = -1.0e9;
    bowChanges = 0;
    globalBend = 0.0;
    dynamicsOverride = pressureOverride = -1.0;
    globalPressure = 0.0;
    globalTimbre = -1.0;
    channelExpression.fill ({});
    for (auto& channel : soundingNote)
        channel.fill (-1);
}

void Violin::setSettings (const PerformanceSettings& s)
{
    settings = s;
    allocator.setMode (s.playMode);
    for (auto& voice : voices)
        voice.setIntonation (s.voice.intonation);
    if (s.instrument != currentInstrument)
        setInstrument (s.instrument);
    else if (s.pickup != currentPickup)
        setPickup (s.pickup);

    // A change of the parameter takes over from the last keyswitch.
    if (s.articulation != parameterArticulation)
    {
        parameterArticulation = s.articulation;
        setArticulation (s.articulation);
    }
}

void Violin::setArticulation (Articulation a)
{
    articulation = a;
    allocator.setSlurs (slurs (a));
}

NoteExpression Violin::expressionFor (int channel) const
{
    if (settings.mpe && channel >= 2 && channel <= 16)
        return channelExpression[static_cast<std::size_t> (channel)];

    return { 0.0, globalPressure, globalTimbre };
}

void Violin::apply (const StringActions& actions)
{
    bool newStroke = false;
    for (const auto& a : actions)
    {
        auto& voice = voices[static_cast<std::size_t> (a.string)];
        if (a.type != StringAction::Type::release)
        {
            // A played note takes the string over from a drone.
            drones[static_cast<std::size_t> (a.string)] = false;
            voice.setDrone (false);
            droneVelocity = a.velocity;
            droneChannel = a.channel;
        }
        switch (a.type)
        {
            case StringAction::Type::start:
                // One bow: notes of a chord share the stroke; a new stroke turns the bow.
                if (time - lastStrokeTime > 0.05)
                {
                    direction = -direction;
                    bowUsed = 0.0;
                    bowChangePhase = -1.0;
                }
                lastStrokeTime = time;
                voice.midiChannel = a.channel;
                voice.setExpression (expressionFor (a.channel));
                voice.start (a.note, a.velocity, articulation);
                newStroke = true;
                break;
            case StringAction::Type::legato:
                // A player changes bow with the note; a turn mid-note is exposed.
                if (settings.voice.autoBowChange && bowChangePhase < 0.0
                    && bowUsed > noteChangeTurnFraction * bowLengthMetres)
                    startBowChange();
                voice.midiChannel = a.channel;
                voice.setExpression (expressionFor (a.channel));
                voice.legato (a.note, a.velocity, articulation);
                break;
            case StringAction::Type::release:
                voice.release();
                break;
        }
        if (a.type != StringAction::Type::release && hg::on (hg::cleanEnds))
            for (int s = 0; s < instrument->numStrings; ++s)
                if (s != a.string)
                    voices[static_cast<std::size_t> (s)].liftFinger(); // the hand moves on
    }
    if (instrument->flatBridge)
        updateDrones (newStroke);
}

void Violin::updateDrones (bool newStroke)
{
    // A flat bridge puts every string in a line: the bow lies across all the
    // strings between the lowest and highest played ones, and touches one
    // more on each side. Those without a note sound open, more lightly.
    const auto n = instrument->numStrings;
    int lowest = n, highest = -1;
    for (int s = 0; s < n; ++s)
        if (allocator.noteOnString (s) >= 0)
        {
            lowest = std::min (lowest, s);
            highest = std::max (highest, s);
        }
    const bool bowing = highest >= 0 && settings.drone > 0.0 && articulation != Articulation::pizzicato;
    lowest = std::max (0, lowest - 1);
    highest = std::min (n - 1, highest + 1);

    for (int s = 0; s < n; ++s)
    {
        auto& voice = voices[static_cast<std::size_t> (s)];
        auto& drone = drones[static_cast<std::size_t> (s)];
        const bool wanted = bowing && s >= lowest && s <= highest && allocator.noteOnString (s) < 0;
        if (wanted && ! drone)
        {
            const auto open = instrument->string (s).openMidiNote;
            voice.midiChannel = droneChannel;
            voice.setExpression (expressionFor (droneChannel));
            if (newStroke)
                voice.start (open, droneVelocity, articulation);
            else
                voice.legato (open, droneVelocity, articulation);
            // Lighter than the played strings, but not so light that they
            // only whistle: much below 0.9 of the weight, the steel strings
            // mostly stop settling into the sawtooth (docs/BOWED_GUITAR.md).
            voice.setDrone (true, 0.8 + 0.2 * std::clamp (settings.drone, 0.0, 1.0));
            drone = true;
        }
        else if (! wanted && drone)
        {
            voice.release();
            drone = false;
        }
    }
}

void Violin::startBowChange()
{
    bowChangePhase = 0.0;
    bowUsed = 0.0;
    ++bowChanges;
}

void Violin::handleMidi (const juce::MidiMessage& m)
{
    const auto channel = m.getChannel();
    const bool member = settings.mpe && channel >= 2;

    auto updateChannel = [this, channel]
    {
        for (auto& v : voices)
            if (v.note() >= 0 && v.midiChannel == channel)
                v.setExpression (expressionFor (channel));
    };
    auto updateAll = [this]
    {
        for (auto& v : voices)
            v.setExpression (expressionFor (v.midiChannel));
    };

    if ((m.isNoteOn() || m.isNoteOff()) && isKeyswitch (m.getNoteNumber()))
    {
        if (m.isNoteOn())
            setArticulation (static_cast<Articulation> (m.getNoteNumber() - firstKeyswitch));
        return;
    }

    if (m.isNoteOn())
    {
        if (member)
            channelExpression[static_cast<std::size_t> (channel)].pressure = 0.0; // MPE: pressure starts at zero
        const auto note = m.getNoteNumber() + 12 * settings.octaveShift;
        auto& held = soundingNote[static_cast<std::size_t> (channel)][static_cast<std::size_t> (m.getNoteNumber())];
        if (note < instrument->lowestNote || note > instrument->highestNote)
        {
            held = outOfRange;
            return;
        }
        held = static_cast<std::int8_t> (note);
        const auto dynamics = velocityToDynamics (m.getFloatVelocity(), settings.velocityTop);
        apply (allocator.noteOn (note, channel, static_cast<float> (dynamics), time));
    }
    else if (m.isNoteOff())
    {
        auto& held = soundingNote[static_cast<std::size_t> (channel)][static_cast<std::size_t> (m.getNoteNumber())];
        const auto note = held >= 0 ? static_cast<int> (held) : m.getNoteNumber() + 12 * settings.octaveShift;
        const auto wasSilent = held == outOfRange;
        held = -1;
        if (wasSilent || note < instrument->lowestNote || note > instrument->highestNote)
            return;
        apply (allocator.noteOff (note, channel));
    }
    else if (m.isPitchWheel())
    {
        const auto value = (m.getPitchWheelValue() - 8192) / 8192.0;
        if (member)
        {
            channelExpression[static_cast<std::size_t> (channel)].bendSemitones
                = value * settings.mpeBendRangeSemitones;
            updateChannel();
        }
        else
        {
            globalBend = value * settings.voice.pitchBendRangeSemitones;
        }
    }
    else if (m.isChannelPressure() || m.isAftertouch())
    {
        const auto value = (m.isChannelPressure() ? m.getChannelPressureValue() : m.getAfterTouchValue()) / 127.0;
        if (member)
        {
            channelExpression[static_cast<std::size_t> (channel)].pressure = value;
            updateChannel();
        }
        else
        {
            globalPressure = value;
            updateAll();
        }
    }
    else if (m.isController())
    {
        const auto number = m.getControllerNumber();
        const auto value = m.getControllerValue() / 127.0;

        if (number == 74)
        {
            if (member)
            {
                channelExpression[static_cast<std::size_t> (channel)].timbre = value;
                updateChannel();
            }
            else
            {
                globalTimbre = value;
                updateAll();
            }
        }
        else if (number == 1)
        {
            pressureOverride = value;
        }
        else if (number == 11 || number == 2)
        {
            dynamicsOverride = value;
        }
        else if (number == 120 || number == 123)
        {
            apply (allocator.allNotesOff());
        }
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        apply (allocator.allNotesOff());
    }
}

void Violin::render (float* out, int numSamples)
{
    const auto dt = 1.0 / fs;

    StringContext context;
    context.globalBendSemitones = globalBend;
    context.dynamicsOverride = dynamicsOverride;
    context.pressureOverride = pressureOverride;

    for (int i = 0; i < numSamples; ++i)
    {
        time += dt;

        // Automatic bow change when the hair runs out: a quick turn in which
        // the speed passes smoothly through zero instead of jumping direction.
        // The force follows the speed, so the string is not jolted and keeps
        // ringing through the turn.
        context.bowChangeGain = 1.0;
        if (bowChangePhase >= 0.0)
        {
            const auto previous = bowChangePhase;
            bowChangePhase += dt / bowChangeSeconds;
            if (previous < 0.5 && bowChangePhase >= 0.5)
                direction = -direction;
            if (bowChangePhase >= 1.0)
                bowChangePhase = -1.0;
            else
                context.bowChangeGain = std::abs (std::cos (std::numbers::pi * bowChangePhase));
        }
        context.direction = direction;
        {
            const auto used = std::clamp (bowUsed / bowLengthMetres, 0.0, 1.0);
            context.bowPlace = direction > 0.0 ? used : 1.0 - used;
        }

        double sum = 0.0, fastest = 0.0;
        for (int s = 0; s < instrument->numStrings; ++s)
        {
            auto& v = voices[static_cast<std::size_t> (s)];
            sum += v.processSample (settings.voice, context);
            if (v.drawsBow())
                fastest = std::max (fastest, v.currentBowSpeed());
        }

        bowUsed += fastest * dt;
        if (settings.voice.autoBowChange && bowUsed > bowLengthMetres && bowChangePhase < 0.0)
            startBowChange();

        out[i] = static_cast<float> (sum);
    }
}
} // namespace violinsynth::engine
