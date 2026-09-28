#pragma once

#include "engine/ViolinEngine.h"
#include "plugin/Parameters.h"
#include "plugin/PresetManager.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

namespace violinsynth
{
class ViolinSynthProcessor final : public juce::AudioProcessor,
                                   private juce::Timer,
                                   private juce::MidiKeyboardState::Listener
{
public:
    ViolinSynthProcessor();
    ~ViolinSynthProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    // Audio thread: checked for allocations, locks and system calls in the
    // RealtimeSanitizer build (source/engine/Realtime.h).
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) VIOLINSYNTH_NONBLOCKING override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }

    // Presets live in the editor's preset browser, not in host programs: a
    // host program change would rewrite every parameter behind the host's
    // back (and some hosts select program 0 when loading a project).
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override { }
    // VST3 hosts list the one program by name; an empty name fails Steinberg's validator.
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override { }

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getParameters() { return parameters; }
    PresetManager& getPresetManager() { return presets; }

    // Notes played on the editor's on-screen keyboard, merged into the MIDI
    // stream so the Standalone app can be tested without a MIDI controller.
    // Message thread only: MidiKeyboardState takes a lock, so the audio thread
    // never touches it. Notes cross between the threads through lock-free
    // queues, and notes from the host light up the keyboard a timer tick later.
    juce::MidiKeyboardState& getKeyboardState() { return keyboardState; }

    // For tests and diagnostics.
    engine::ViolinEngine& getEngine() { return engine; }

    // The articulation playing now (the parameter or the last keyswitch), for the editor.
    engine::Articulation getActiveArticulation() const
    {
        return static_cast<engine::Articulation> (activeArticulation.load());
    }

    // What each string is doing, for the editor's string display (updated every block).
    struct StringState
    {
        std::atomic<int> note { -1 }; // MIDI note held on the string, or -1
        std::atomic<float> level { 0.0f }; // decaying peak of the string's output
        std::atomic<float> bowSpeed { 0.0f }; // m/s
    };
    const StringState& getStringState (int string) const { return stringStates[static_cast<std::size_t> (string)]; }

    // Message thread: applies pending body changes (also run by a timer).
    void applyBodyChange() { engine.updateConvolutionBody(); }

    // Message thread: shows the host's notes on the keyboard state. Also run by the timer.
    void updateKeyboardState();

private:
    // A note on or off crossing between the audio and message threads.
    struct KeyEvent
    {
        int channel = 1;
        int note = -1; // -1: all notes off on the channel
        float velocity = 0.0f; // 0: note off
    };

    // Single-producer, single-consumer queue that never blocks or allocates.
    class KeyEventQueue
    {
    public:
        bool push (const KeyEvent& e);
        template <typename Fn>
        void popAll (Fn&& fn);

    private:
        static constexpr int capacity = 256;
        juce::AbstractFifo fifo { capacity };
        std::array<KeyEvent, capacity> events;
    };

    void timerCallback() override
    {
        applyBodyChange();
        updateKeyboardState();
    }

    void handleNoteOn (juce::MidiKeyboardState*, int channel, int note, float velocity) override;
    void handleNoteOff (juce::MidiKeyboardState*, int channel, int note, float velocity) override;

    juce::AudioProcessorValueTreeState parameters;
    params::Reader reader;
    PresetManager presets { parameters };
    juce::MidiKeyboardState keyboardState;
    KeyEventQueue keysToAudio; // on-screen keyboard -> audio thread
    KeyEventQueue keysToDisplay; // host notes -> keyboard state
    bool showingHostNotes = false; // message thread: set while the host's notes update keyboardState
    juce::MidiBuffer mergedMidi; // host MIDI plus on-screen notes, preallocated
    engine::ViolinEngine engine;
    std::atomic<int> activeArticulation { 0 };
    std::array<StringState, engine::Violin::numStrings> stringStates;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ViolinSynthProcessor)
};
} // namespace violinsynth
