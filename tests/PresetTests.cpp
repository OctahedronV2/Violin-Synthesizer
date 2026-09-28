#include "plugin/FactoryPresets.h"
#include "plugin/PluginProcessor.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <set>

using namespace violinsynth;

namespace
{
// A processor whose user presets live in a fresh temporary folder.
struct Fixture
{
    juce::ScopedJuceInitialiser_GUI juce;
    juce::TemporaryFile folder;
    ViolinSynthProcessor processor;
    PresetManager manager { processor.getParameters(), folder.getFile() };

    ~Fixture() { folder.getFile().deleteRecursively(); }

    float value (const char* id)
    {
        auto* p = processor.getParameters().getParameter (id);
        return p->convertFrom0to1 (p->getValue());
    }
    void set (const char* id, float plain)
    {
        auto* p = processor.getParameters().getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (plain));
    }
};
} // namespace

TEST_CASE ("Factory presets are complete and valid", "[presets]")
{
    Fixture f;
    const auto& list = presets::factoryPresets();
    CHECK (list.size() >= 20);
    CHECK (list.size() <= 30);

    std::set<std::string> names;
    for (const auto& preset : list)
    {
        CAPTURE (preset.name);
        CHECK (names.insert (preset.name).second); // unique
        CHECK (std::find_if (std::begin (presets::categories),
                             std::end (presets::categories),
                             [&] (const char* c) { return std::string (c) == preset.category; })
               != std::end (presets::categories));
        CHECK (std::string (preset.description).size() > 10);

        for (const auto& [id, value] : preset.values)
        {
            CAPTURE (id);
            auto* p = f.processor.getParameters().getParameter (id);
            REQUIRE (p != nullptr);
            const auto& range = p->getNormalisableRange();
            CHECK (value >= range.start);
            CHECK (value <= range.end);
        }
    }

    // The first preset is the default sound.
    CHECK (list.front().values.empty());
    CHECK (f.manager.getCurrentIndex() == 0);
    CHECK_FALSE (f.manager.isModified());
}

TEST_CASE ("Loading a preset sets every parameter", "[presets]")
{
    Fixture f;
    const auto& list = presets::factoryPresets();
    const auto romantic
        = static_cast<int> (std::find_if (list.begin(),
                                          list.end(),
                                          [] (const auto& p) { return std::string (p.name) == "Romantic Soloist"; })
                            - list.begin());
    const auto pizz = static_cast<int> (
        std::find_if (list.begin(), list.end(), [] (const auto& p) { return std::string (p.name) == "Pizzicato"; })
        - list.begin());

    REQUIRE (f.manager.load (romantic));
    CHECK (f.value ("vibratoDepth") == 38.0f);
    CHECK (std::abs (f.value ("room") - 0.32f) < 1.0e-4f);
    CHECK (f.manager.getCurrentName() == "Romantic Soloist");
    CHECK_FALSE (f.manager.isModified());

    // Parameters the next preset doesn't mention go back to their defaults.
    REQUIRE (f.manager.load (pizz));
    CHECK (f.value ("vibratoDepth") == 10.0f);
    CHECK (f.value ("portamento") == 0.05f); // default, not Romantic's 0.09
    CHECK (f.value ("articulation") == 5.0f);

    f.set ("room", 0.9f);
    CHECK (f.manager.isModified());

    f.manager.loadNext();
    CHECK (f.manager.getCurrentIndex() == pizz + 1);
    f.manager.loadPrevious();
    f.manager.loadPrevious();
    CHECK (f.manager.getCurrentIndex() == pizz - 1);
}

TEST_CASE ("Every factory preset plays an octave up", "[presets][keyboard]")
{
    // Typing keyboards start an octave below the violin (v1.0.1).
    Fixture f;
    f.set ("octave", 0.0f);
    for (int i = 0; i < f.manager.getNumFactoryPresets(); ++i)
    {
        REQUIRE (f.manager.load (i));
        CAPTURE (f.manager.getCurrentName());
        CHECK (f.value ("octave") == static_cast<float> (params::octaveChoiceOffset + 1));
    }
}

TEST_CASE ("User presets save, load and delete", "[presets]")
{
    Fixture f;
    const auto factoryCount = f.manager.getNumFactoryPresets();

    f.set ("vibratoDepth", 42.0f);
    f.set ("body", 2.0f);
    f.set ("articulation", 7.0f);
    REQUIRE (f.manager.saveUserPreset ("My: Ponticello/Test", "Mine").wasOk());
    CHECK (static_cast<int> (f.manager.getPresets().size()) == factoryCount + 1);
    CHECK (f.manager.getCurrentName() == "My: Ponticello/Test"); // shown as typed, stored under a legal file name
    CHECK_FALSE (f.manager.isModified());
    CHECK (f.manager.getPresets().back().category == "Mine");

    // Another manager (e.g. a second plugin instance) finds it.
    PresetManager other { f.processor.getParameters(), f.folder.getFile() };
    REQUIRE (static_cast<int> (other.getPresets().size()) == factoryCount + 1);

    f.manager.load (0);
    CHECK (f.value ("vibratoDepth") == 25.0f);
    f.manager.load (factoryCount);
    CHECK (f.value ("vibratoDepth") == 42.0f);
    CHECK (f.value ("body") == 2.0f);
    CHECK (f.value ("articulation") == 7.0f);

    // Saving under the same name overwrites.
    f.set ("vibratoDepth", 12.0f);
    REQUIRE (f.manager.saveUserPreset ("My: Ponticello/Test", "Mine").wasOk());
    CHECK (static_cast<int> (f.manager.getPresets().size()) == factoryCount + 1);

    CHECK (f.manager.saveUserPreset ("   ").failed());
    CHECK (f.manager.deleteUserPreset (0).failed()); // factory presets stay

    REQUIRE (f.manager.deleteUserPreset (factoryCount).wasOk());
    CHECK (static_cast<int> (f.manager.getPresets().size()) == factoryCount);
    CHECK (f.manager.getCurrentIndex() == -1);
}

TEST_CASE ("Presets are not exposed as host programs", "[presets]")
{
    // A host program change would rewrite every parameter behind the host's back.
    juce::ScopedJuceInitialiser_GUI juce;
    ViolinSynthProcessor processor;
    CHECK (processor.getNumPrograms() == 1);
    processor.setCurrentProgram (3);
    CHECK (processor.getPresetManager().getCurrentIndex() == 0);
}

TEST_CASE ("The current preset is saved with the project", "[presets]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    juce::MemoryBlock saved;
    {
        ViolinSynthProcessor processor;
        processor.getPresetManager().load (5);
        auto* room = processor.getParameters().getParameter ("room");
        room->setValueNotifyingHost (0.77f); // an edit on top of the preset
        processor.getStateInformation (saved);
    }

    ViolinSynthProcessor restored;
    restored.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    CHECK (restored.getPresetManager().getCurrentIndex() == 5);
    CHECK (restored.getPresetManager().isModified());
    CHECK (std::abs (restored.getParameters().getParameter ("room")->getValue() - 0.77f) < 1.0e-4f);
}

namespace
{
// Loudness (dB) of a short phrase played with the processor's current
// settings: the loudest 100 ms RMS, so short and plucked notes count as
// loud as sustained ones.
double phraseLevelDb (ViolinSynthProcessor& processor, float velocity)
{
    constexpr double fs = 48000.0;
    constexpr int block = 256;
    auto settings = params::Reader (processor.getParameters()).read();
    settings.bodyQuality = engine::Body::Quality::modal; // no background IR loading in tests
    settings.performance.octaveShift = 0; // the phrase is written where it sounds
    // Humanise lets the bow speed wander by a dB or so, and bow noise can tip
    // a light bow by the bridge in or out of clean motion; a 3 s phrase's
    // loudest moment picks either up by chance. The presets' own levels are
    // compared.
    settings.performance.voice.humanise = 0.0;
    settings.performance.voice.bowNoise = 0.0;

    engine::ViolinEngine e;
    e.setSettings (settings);
    e.prepare (fs, block);

    // A few notes across the range, some overlapping, then a double stop.
    const std::vector<std::tuple<double, double, int>> notes {
        { 0.0, 0.6, 62 }, { 0.5, 1.1, 69 }, { 1.0, 1.6, 76 }, { 1.5, 2.1, 81 }, { 2.2, 3.2, 62 }, { 2.2, 3.2, 69 },
    };
    juce::AudioBuffer<float> buffer (2, block);
    bool finite = true;
    std::vector<double> power;
    for (int start = 0; start < static_cast<int> (3.5 * fs); start += block)
    {
        juce::MidiBuffer midi;
        for (const auto& [on, off, note] : notes)
        {
            const auto a = static_cast<int> (on * fs), b = static_cast<int> (off * fs);
            if (a >= start && a < start + block)
                midi.addEvent (juce::MidiMessage::noteOn (1, note, velocity), a - start);
            if (b >= start && b < start + block)
                midi.addEvent (juce::MidiMessage::noteOff (1, note), b - start);
        }
        e.process (buffer, midi);
        for (int i = 0; i < block; ++i)
        {
            const auto l = static_cast<double> (buffer.getSample (0, i));
            const auto r = static_cast<double> (buffer.getSample (1, i));
            finite = finite && std::isfinite (l) && std::isfinite (r);
            power.push_back (0.5 * (l * l + r * r));
        }
    }
    CHECK (finite);

    const auto window = static_cast<std::size_t> (0.1 * fs);
    double sum = 0.0, loudest = 0.0;
    for (std::size_t i = 0; i < power.size(); ++i)
    {
        sum += power[i];
        if (i >= window)
            sum -= power[i - window];
        loudest = std::max (loudest, sum / static_cast<double> (window));
    }
    return 10.0 * std::log10 (loudest + 1.0e-20);
}

// The bowed string is chaotic: one render's loudest moment moves by a dB or
// two with tiny changes. Averaging over five velocities around mf steadies it.
double phraseLevelDb (ViolinSynthProcessor& processor)
{
    double sum = 0.0;
    for (auto velocity : { 0.65f, 0.7f, 0.75f, 0.8f, 0.85f })
        sum += phraseLevelDb (processor, velocity);
    return sum / 5.0;
}
} // namespace

TEST_CASE ("Factory presets are level-matched", "[presets]")
{
    Fixture f;
    f.manager.load (0);
    const auto reference = phraseLevelDb (f.processor);

    for (int i = 0; i < f.manager.getNumFactoryPresets(); ++i)
    {
        f.manager.load (i);
        const auto level = phraseLevelDb (f.processor);
        CAPTURE (f.manager.getCurrentName(), level, reference);
        UNSCOPED_INFO (f.manager.getCurrentName() << ": " << level - reference << " dB");
        // The loudest moment of a slurred preset (the default among them) is
        // a sustained note, and of a detached one a fresh stroke's onset,
        // which peaks up to about 2.5 dB higher. (Until slurs changed finger
        // instead of sliding, the default's loudest moment was a swell on the
        // slide from the open D, level with the detached onsets.) A light bow
        // right by the bridge is also chaotic: the level moves by a dB or two
        // with tiny changes, including floating-point differences between
        // platforms. So does the heavy mute: a 0.05 cent change in the
        // vibrato moves it by a dB.
        CHECK (std::abs (level - reference) < 3.0);
    }
}
