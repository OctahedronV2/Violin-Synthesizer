#include "plugin/PlaceholderVoice.h"

#include <cmath>

namespace violinsynth
{
namespace
{
constexpr float pitchBendRangeSemitones = 2.0f;
constexpr float voiceGain = 0.15f;

float pitchWheelToSemitones (int pitchWheelPosition)
{
    return (static_cast<float> (pitchWheelPosition) - 8192.0f) / 8192.0f * pitchBendRangeSemitones;
}
} // namespace

bool PlaceholderVoice::canPlaySound (juce::SynthesiserSound* sound)
{
    return dynamic_cast<PlaceholderSound*> (sound) != nullptr;
}

void PlaceholderVoice::startNote (int midiNoteNumber, float velocity, juce::SynthesiserSound*, int pitchWheelPosition)
{
    noteNumber = midiNoteNumber;
    pitchBendSemitones = pitchWheelToSemitones (pitchWheelPosition);
    level = velocity * voiceGain;
    phase = 0.0;
    updatePhaseIncrement();

    envelope.setSampleRate (getSampleRate());
    envelope.setParameters ({ 0.02f, 0.1f, 0.8f, 0.25f });
    envelope.noteOn();
}

void PlaceholderVoice::stopNote (float, bool allowTailOff)
{
    if (allowTailOff)
    {
        envelope.noteOff();
    }
    else
    {
        envelope.reset();
        clearCurrentNote();
    }
}

void PlaceholderVoice::pitchWheelMoved (int newPitchWheelValue)
{
    pitchBendSemitones = pitchWheelToSemitones (newPitchWheelValue);
    updatePhaseIncrement();
}

void PlaceholderVoice::updatePhaseIncrement()
{
    const auto frequency = juce::MidiMessage::getMidiNoteInHertz (noteNumber)
        * std::pow (2.0, static_cast<double> (pitchBendSemitones) / 12.0);
    phaseIncrement = juce::MathConstants<double>::twoPi * frequency / getSampleRate();
}

void PlaceholderVoice::renderNextBlock (juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples)
{
    if (! isVoiceActive())
        return;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto sample = static_cast<float> (std::sin (phase)) * level * envelope.getNextSample();
        phase = std::fmod (phase + phaseIncrement, juce::MathConstants<double>::twoPi);

        for (int channel = 0; channel < outputBuffer.getNumChannels(); ++channel)
            outputBuffer.addSample (channel, startSample + i, sample);

        if (! envelope.isActive())
        {
            clearCurrentNote();
            break;
        }
    }
}
} // namespace violinsynth
