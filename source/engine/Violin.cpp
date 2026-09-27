#include "engine/Violin.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace violinsynth::engine
{
void Violin::prepare (double internalSampleRate)
{
    fs = internalSampleRate;
    for (int s = 0; s < numStrings; ++s)
        voices[static_cast<std::size_t> (s)].prepare (fs, s);
    reset();
}

void Violin::reset()
{
    for (auto& v : voices)
        v.reset();
    allocator.reset();
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
}

void Violin::setSettings (const PerformanceSettings& s)
{
    settings = s;
    allocator.setMode (s.playMode);
}

NoteExpression Violin::expressionFor (int channel) const
{
    if (settings.mpe && channel >= 2 && channel <= 16)
        return channelExpression[static_cast<std::size_t> (channel)];

    return { 0.0, globalPressure, globalTimbre };
}

void Violin::apply (const StringActions& actions)
{
    for (const auto& a : actions)
    {
        auto& voice = voices[static_cast<std::size_t> (a.string)];
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
                voice.start (a.note, a.velocity);
                break;
            case StringAction::Type::legato:
                voice.midiChannel = a.channel;
                voice.setExpression (expressionFor (a.channel));
                voice.legato (a.note, a.velocity);
                break;
            case StringAction::Type::release:
                voice.release();
                break;
        }
    }
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

    if (m.isNoteOn())
    {
        if (member)
            channelExpression[static_cast<std::size_t> (channel)].pressure = 0.0; // MPE: pressure starts at zero
        apply (allocator.noteOn (m.getNoteNumber(), channel, m.getFloatVelocity(), time));
    }
    else if (m.isNoteOff())
    {
        apply (allocator.noteOff (m.getNoteNumber(), channel));
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

        // Automatic bow change when the hair runs out: speed dips, the bow
        // turns at the slowest point, then speeds up again.
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
                context.bowChangeGain = 1.0 - 0.85 * std::sin (std::numbers::pi * bowChangePhase);
        }
        context.direction = direction;

        double sum = 0.0, fastest = 0.0;
        for (auto& v : voices)
        {
            sum += v.processSample (settings.voice, context);
            fastest = std::max (fastest, v.currentBowSpeed());
        }

        bowUsed += fastest * dt;
        if (settings.voice.autoBowChange && bowUsed > bowLengthMetres && bowChangePhase < 0.0)
        {
            bowChangePhase = 0.0;
            bowUsed = 0.0;
            ++bowChanges;
        }

        out[i] = static_cast<float> (sum);
    }
}
} // namespace violinsynth::engine
