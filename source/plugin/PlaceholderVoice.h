#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace violinsynth
{
// Temporary sine-wave voice so the Phase 0 plugin makes a sound in a host.
// It only exists to prove that MIDI in and audio out work end to end, and
// will be replaced by the bowed-string model in Phase 2.
class PlaceholderSound final : public juce::SynthesiserSound
{
public:
    bool appliesToNote (int) override { return true; }
    bool appliesToChannel (int) override { return true; }
};

class PlaceholderVoice final : public juce::SynthesiserVoice
{
public:
    bool canPlaySound (juce::SynthesiserSound* sound) override;
    void startNote (int midiNoteNumber, float velocity, juce::SynthesiserSound*, int pitchWheelPosition) override;
    void stopNote (float velocity, bool allowTailOff) override;
    void pitchWheelMoved (int newPitchWheelValue) override;
    void controllerMoved (int, int) override { }
    void renderNextBlock (juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples) override;

private:
    void updatePhaseIncrement();

    juce::ADSR envelope;
    double phase = 0.0;
    double phaseIncrement = 0.0;
    float level = 0.0f;
    int noteNumber = 0;
    float pitchBendSemitones = 0.0f;
};
} // namespace violinsynth
