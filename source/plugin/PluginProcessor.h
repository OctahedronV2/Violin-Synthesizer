#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

namespace violinsynth
{
class ViolinSynthProcessor final : public juce::AudioProcessor
{
public:
    ViolinSynthProcessor();
    ~ViolinSynthProcessor() override = default;

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
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override { }
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override { }

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getParameters() { return parameters; }

    // Notes played on the editor's on-screen keyboard, merged into the MIDI
    // stream so the Standalone app can be tested without a MIDI controller.
    juce::MidiKeyboardState& getKeyboardState() { return keyboardState; }

private:
    static constexpr int numPlaceholderVoices = 8;

    juce::AudioProcessorValueTreeState parameters;
    std::atomic<float>* outputGainDb = nullptr;

    juce::MidiKeyboardState keyboardState;
    juce::Synthesiser synth;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> outputGain;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ViolinSynthProcessor)
};
} // namespace violinsynth
