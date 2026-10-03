#pragma once

#include "Parameters.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <memory>

namespace octavio2
{
// Octavio 2: the o2::Engine (octavio2/core) in a plugin. The engine runs at 48 kHz, exactly as
// the offline renderer does, and is resampled when the host runs at another rate.
class Processor final : public juce::AudioProcessor, private juce::Timer, private juce::MidiKeyboardState::Listener
{
public:
    Processor();
    ~Processor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 7.0; } // the church's reverb

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override { }
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override { }

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getParameters() { return parameters; }

    // The editor's on-screen keyboard (message thread only; notes reach the audio thread
    // through a lock-free queue). It shows the notes that sound, so it is shifted by Octave.
    juce::MidiKeyboardState& getKeyboardState() { return keyboardState; }
    int getOctaveShift() const { return reader.octaveShift(); }

    // for tests
    o2::Engine& getEngine() { return *engine; }

private:
    struct KeyEvent
    {
        int note = -1;
        float velocity = 0.0f; // 0: note off
    };
    class KeyQueue
    {
    public:
        bool push (const KeyEvent&);
        template <typename Fn>
        void popAll (Fn&&);

    private:
        static constexpr int capacity = 256;
        juce::AbstractFifo fifo { capacity };
        std::array<KeyEvent, capacity> events;
    };

    void timerCallback() override;
    void handleNoteOn (juce::MidiKeyboardState*, int channel, int note, float velocity) override;
    void handleNoteOff (juce::MidiKeyboardState*, int channel, int note, float velocity) override;

    int64_t engineTime (int hostOffset) const;
    void handleMidi (const juce::MidiMessage&, int64_t when);
    void renderEngine (float* left, float* right, int numSamples);
    void updateLatency();

    juce::AudioProcessorValueTreeState parameters;
    params::Reader reader;
    std::unique_ptr<o2::Engine> engine;
    juce::MidiKeyboardState keyboardState;
    KeyQueue keysToAudio;
    std::array<std::int8_t, 128> clickedKeys {}; // message thread: note sent for each held key
    std::array<std::array<std::int8_t, 128>, 16> sentNotes {}; // audio thread: pitch played per incoming note

    // host rate <-> 48 kHz
    double hostRate = 48000.0, ratio = 1.0; // engine samples per host sample
    bool resampling = false;
    int64_t hostClock = 0;
    std::array<juce::WindowedSincInterpolator, 2> interpolators;
    std::array<std::vector<float>, 2> fifo; // engine output not yet resampled
    int fifoFill = 0;
    std::vector<float> scratchL, scratchR;
    std::atomic<int> latencyShown { -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Processor)
};
} // namespace octavio2
