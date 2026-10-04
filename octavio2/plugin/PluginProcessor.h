#pragma once

#include "../core/Scala.h"
#include "Mpe.h"
#include "Parameters.h"
#include "ui/Telemetry.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <memory>

struct MTSClient; // M7: third_party/mts-esp

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

    // What the player is doing, for the editor's displays (see Telemetry).
    const Telemetry& getTelemetry() const { return telemetry; }
    // for tests and the displays (the editor only reads the engine's lock-free logs)
    o2::Engine& getEngine() { return *engine; }
    int editorTab = 0; // the tab the editor shows, kept while the editor is closed
    double getLatencyMs() const { return getLatencySamples() * 1000.0 / hostRate; }

    // M7 tuning (message thread): a Scala scale (.scl, and optionally a .kbm keyboard map) for
    // the Intonation parameter's "Scala file" choice. Parsed here, handed to the audio thread
    // without blocking it, and saved with the project. Returns an error, or empty on success.
    juce::String loadScala (const juce::File& scl, const juce::File& kbm = {});
    juce::String loadScalaText (const juce::String& name, const juce::String& scl, const juce::String& kbm);
    void clearScala();
    juce::String getScalaName() const;
    // MTS-ESP: whether a master is running, and its scale's name
    bool mtsHasMaster() const;
    juce::String mtsScaleName() const;

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
    void updateTelemetry (double blockSeconds, juce::int64 startTicks);

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
    Telemetry telemetry;
    int lastVelocity = 0;
    double nextHistoryT = 0.0;

    // M7: MPE, the Scala table handed to the audio thread, the MTS-ESP client
    void updateTuning();
    Mpe mpe;
    juce::SpinLock tuningLock; // the audio thread only tries it
    struct TuningTable // plain data: copied on the audio thread
    {
        bool ok = false, hasKeyboardMap = false;
        double cents[128] = {};
    };
    TuningTable pendingScala; // written under tuningLock (message thread)
    std::atomic<bool> scalaChanged { false };
    TuningTable scalaNow; // audio thread's copy
    double mtsTable[128] = {};
    ::MTSClient* mts = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Processor)
};
} // namespace octavio2
