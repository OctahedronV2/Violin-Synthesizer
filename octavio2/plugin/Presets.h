#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <utility>
#include <vector>

namespace octavio2
{
// Sound presets (M6): the factory ones built in, the user's as files in
// <Documents>/OctahedronV2/Octavio 2/Presets. Message thread only.
//
// A preset sets every sound parameter: what it doesn't mention goes to the default. The
// performance settings (Live/Studio, Octave, Bend range, MPE) and the MIDI map are left alone,
// so changing the sound never changes how the keyboard plays. Parameters are set through the
// host parameters on the message thread (as a host's automation is), so loading never touches
// the audio thread's data directly.
class Presets
{
public:
    static constexpr const char* fileExtension = ".o2preset";

    struct Factory
    {
        const char* name;
        const char* description;
        std::vector<std::pair<const char*, float>> values; // plain values by parameter ID
    };
    static const std::vector<Factory>& factory();

    struct Preset
    {
        juce::String name, description;
        int factoryIndex = -1; // -1: a user file
        juce::File file;
    };

    explicit Presets (juce::AudioProcessorValueTreeState&, juce::File userFolder = {});

    static juce::File defaultUserFolder();
    void setUserFolder (const juce::File& f);
    juce::File getUserFolder() const { return userFolder; }

    void refresh(); // rescans the user folder
    const std::vector<Preset>& list() const { return presets; }
    int numFactory() const { return (int) factory().size(); }

    int currentIndex() const { return current; }
    juce::String currentName() const;
    bool isModified() const;

    bool load (int index);
    void loadNext();
    void loadPrevious();
    juce::Result save (const juce::String& name);
    juce::Result remove (int index);

    // after the host restores the state: the preset named in it becomes current again
    void restoreFromState();

    // parameters a preset never changes
    static bool isPerformanceParameter (const juce::String& id);
    std::vector<std::pair<juce::String, float>> valuesFor (int index) const;

private:
    void apply (const std::vector<std::pair<juce::String, float>>&);
    void setCurrent (int index);

    juce::AudioProcessorValueTreeState& state;
    juce::File userFolder;
    std::vector<Preset> presets;
    int current = -1;
    std::vector<std::pair<juce::String, float>> loaded; // for isModified
};
} // namespace octavio2
