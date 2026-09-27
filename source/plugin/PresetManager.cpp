#include "plugin/PresetManager.h"

#include "plugin/FactoryPresets.h"

namespace violinsynth
{
namespace
{
const juce::Identifier presetProperty { "preset" };
const juce::Identifier presetIsFactoryProperty { "presetIsFactory" };
constexpr const char* fileTag = "ViolinSynthPreset";
constexpr int fileVersion = 1;
} // namespace

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& s, juce::File folder)
    : state (s),
      userFolder (folder == juce::File() ? defaultUserFolder() : folder)
{
    refresh();

    // A new instance starts on the first factory preset, which is the default sound.
    setCurrent (0);
}

juce::File PresetManager::defaultUserFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("OctahedronV2")
        .getChildFile ("Violin Synthesizer")
        .getChildFile ("Presets");
}

void PresetManager::refresh()
{
    const auto currentName = getCurrentName();
    const auto currentIsFactory = current >= 0 && presets[static_cast<std::size_t> (current)].factory;

    presets.clear();
    const auto& factory = presets::factoryPresets();
    for (std::size_t i = 0; i < factory.size(); ++i)
        presets.push_back (
            { factory[i].name, factory[i].category, factory[i].description, true, static_cast<int> (i), {} });

    std::vector<Preset> user;
    for (const auto& file :
         userFolder.findChildFiles (juce::File::findFiles, false, juce::String ("*") + fileExtension))
    {
        const auto xml = juce::parseXML (file);
        if (xml == nullptr || ! xml->hasTagName (fileTag))
            continue;
        user.push_back ({ xml->getStringAttribute ("name", file.getFileNameWithoutExtension()),
                          xml->getStringAttribute ("category", "User"),
                          xml->getStringAttribute ("description"),
                          false,
                          -1,
                          file });
    }
    std::sort (user.begin(),
               user.end(),
               [] (const Preset& a, const Preset& b)
               {
                   const auto c = a.category.compareNatural (b.category);
                   return c != 0 ? c < 0 : a.name.compareNatural (b.name) < 0;
               });
    presets.insert (presets.end(), user.begin(), user.end());

    // Keep pointing at the same preset.
    if (current >= 0)
    {
        current = -1;
        for (std::size_t i = 0; i < presets.size(); ++i)
            if (presets[i].name == currentName && presets[i].factory == currentIsFactory)
                current = static_cast<int> (i);
    }
}

int PresetManager::getNumFactoryPresets() const
{
    return static_cast<int> (presets::factoryPresets().size());
}

juce::String PresetManager::getCurrentName() const
{
    return current >= 0 ? presets[static_cast<std::size_t> (current)].name : juce::String();
}

juce::RangedAudioParameter* PresetManager::parameter (const juce::String& id) const
{
    return state.getParameter (id);
}

std::vector<std::pair<juce::String, float>> PresetManager::valuesFor (int index) const
{
    std::vector<std::pair<juce::String, float>> values;
    for (auto* p : state.processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            values.emplace_back (ranged->getParameterID(), ranged->convertFrom0to1 (ranged->getDefaultValue()));

    auto set = [&values] (const juce::String& id, float value)
    {
        for (auto& [key, v] : values)
            if (key == id)
                v = value;
    };

    if (index < 0 || index >= static_cast<int> (presets.size()))
        return values;

    const auto& preset = presets[static_cast<std::size_t> (index)];
    if (preset.factory)
    {
        for (const auto& [id, value] : presets::factoryPresets()[static_cast<std::size_t> (preset.factoryIndex)].values)
            set (id, value);
    }
    else if (const auto xml = juce::parseXML (preset.file))
    {
        for (const auto* child : xml->getChildWithTagNameIterator ("PARAM"))
            set (child->getStringAttribute ("id"), static_cast<float> (child->getDoubleAttribute ("value")));
    }

    // Keep values inside the current ranges (a file may be older or edited by hand).
    for (auto& [id, value] : values)
        if (auto* p = parameter (id))
            value = p->convertFrom0to1 (p->convertTo0to1 (value));

    return values;
}

void PresetManager::apply (const std::vector<std::pair<juce::String, float>>& values)
{
    for (const auto& [id, value] : values)
    {
        if (auto* p = parameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (value));
            p->endChangeGesture();
        }
    }
}

void PresetManager::setCurrent (int index)
{
    current = index >= 0 && index < static_cast<int> (presets.size()) ? index : -1;
    loadedValues = valuesFor (current);
    state.state.setProperty (presetProperty, getCurrentName(), nullptr);
    state.state.setProperty (presetIsFactoryProperty,
                             current >= 0 && presets[static_cast<std::size_t> (current)].factory,
                             nullptr);
}

bool PresetManager::load (int index)
{
    if (index < 0 || index >= static_cast<int> (presets.size()))
        return false;

    apply (valuesFor (index));
    setCurrent (index);
    return true;
}

void PresetManager::loadNext()
{
    if (! presets.empty())
        load ((current + 1) % static_cast<int> (presets.size()));
}

void PresetManager::loadPrevious()
{
    if (! presets.empty())
    {
        const auto n = static_cast<int> (presets.size());
        load (current < 0 ? n - 1 : (current + n - 1) % n);
    }
}

bool PresetManager::isModified() const
{
    for (const auto& [id, value] : loadedValues)
    {
        if (auto* p = parameter (id))
        {
            const auto& range = p->getNormalisableRange();
            const auto now = p->convertFrom0to1 (p->getValue());
            if (std::abs (now - value) > 1.0e-4f * (range.end - range.start))
                return true;
        }
    }
    return false;
}

juce::Result PresetManager::saveUserPreset (const juce::String& name, const juce::String& category)
{
    const auto trimmed = name.trim();
    if (trimmed.isEmpty())
        return juce::Result::fail ("Please enter a name for the preset.");

    if (! userFolder.createDirectory())
        return juce::Result::fail ("Could not create the preset folder " + userFolder.getFullPathName());

    juce::XmlElement xml (fileTag);
    xml.setAttribute ("version", fileVersion);
    xml.setAttribute ("name", trimmed);
    xml.setAttribute ("category", category.trim().isEmpty() ? juce::String ("User") : category.trim());
    xml.setAttribute ("plugin", JucePlugin_Name " " JucePlugin_VersionString);
    for (auto* p : state.processor.getParameters())
    {
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            auto* child = xml.createNewChildElement ("PARAM");
            child->setAttribute ("id", ranged->getParameterID());
            child->setAttribute ("value", ranged->convertFrom0to1 (ranged->getValue()));
        }
    }

    const auto file = userFolder.getChildFile (juce::File::createLegalFileName (trimmed) + fileExtension);
    if (! xml.writeTo (file))
        return juce::Result::fail ("Could not write " + file.getFullPathName());

    refresh();
    for (std::size_t i = 0; i < presets.size(); ++i)
        if (! presets[i].factory && presets[i].file == file)
            setCurrent (static_cast<int> (i));

    return juce::Result::ok();
}

juce::Result PresetManager::deleteUserPreset (int index)
{
    if (index < 0 || index >= static_cast<int> (presets.size()) || presets[static_cast<std::size_t> (index)].factory)
        return juce::Result::fail ("Factory presets can't be deleted.");

    if (! presets[static_cast<std::size_t> (index)].file.deleteFile())
        return juce::Result::fail ("Could not delete "
                                   + presets[static_cast<std::size_t> (index)].file.getFullPathName());

    const auto wasCurrent = index == current;
    refresh();
    if (wasCurrent)
        setCurrent (-1); // the settings stay; they just no longer belong to a preset
    return juce::Result::ok();
}

void PresetManager::restoreFromState()
{
    refresh();
    const auto name = state.state.getProperty (presetProperty).toString();
    const auto factory = static_cast<bool> (state.state.getProperty (presetIsFactoryProperty, true));

    int found = -1;
    for (std::size_t i = 0; i < presets.size() && found < 0; ++i)
        if (presets[i].name == name && presets[i].factory == factory)
            found = static_cast<int> (i);

    // Only remember which preset it was; the restored parameters stay as saved.
    current = found;
    loadedValues = valuesFor (current);
}
} // namespace violinsynth
