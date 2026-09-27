#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <vector>

namespace violinsynth
{
// Factory presets (built in) and user presets (XML files in the user's
// preset folder). Message thread only.
//
// A preset sets every parameter: values it doesn't mention go to their
// defaults. The current preset's name is kept in the parameter state, so it
// is saved with the host project.
class PresetManager
{
public:
    static constexpr const char* fileExtension = ".vspreset";

    struct Preset
    {
        juce::String name;
        juce::String category;
        juce::String description;
        bool factory = true;
        int factoryIndex = -1; // into presets::factoryPresets()
        juce::File file; // user presets
    };

    // `userFolder` defaults to defaultUserFolder(); tests pass a temporary one.
    explicit PresetManager (juce::AudioProcessorValueTreeState& state, juce::File userFolder = {});

    // <user documents>/OctahedronV2/Violin Synthesizer/Presets
    static juce::File defaultUserFolder();
    juce::File getUserFolder() const { return userFolder; }

    // Rescans the user folder (factory presets come first, then user presets by category and name).
    void refresh();
    const std::vector<Preset>& getPresets() const { return presets; }
    int getNumFactoryPresets() const;

    // -1 when no preset is loaded.
    int getCurrentIndex() const { return current; }
    juce::String getCurrentName() const;
    // Whether any parameter differs from the loaded preset.
    bool isModified() const;

    bool load (int index);
    void loadNext();
    void loadPrevious();

    // Saves the current settings as a user preset (overwriting one of the
    // same name) and makes it current.
    juce::Result saveUserPreset (const juce::String& name, const juce::String& category = "User");
    juce::Result deleteUserPreset (int index);

    // Call after the host restores the parameter state: finds the preset
    // named in the state again.
    void restoreFromState();

    // Plain values of all parameters for a preset, defaults filled in.
    std::vector<std::pair<juce::String, float>> valuesFor (int index) const;

private:
    void apply (const std::vector<std::pair<juce::String, float>>& values);
    void setCurrent (int index);
    juce::RangedAudioParameter* parameter (const juce::String& id) const;

    juce::AudioProcessorValueTreeState& state;
    juce::File userFolder;
    std::vector<Preset> presets;
    int current = -1;
    std::vector<std::pair<juce::String, float>> loadedValues; // for isModified()
};
} // namespace violinsynth
