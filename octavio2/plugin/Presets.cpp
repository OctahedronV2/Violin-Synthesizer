#include "Presets.h"

namespace octavio2
{
namespace
{
const juce::Identifier presetProperty { "preset" };
constexpr const char* fileTag = "Octavio2Preset";
} // namespace

// Rooms: 0 none (close mics), 1 Arvedi near, 2 Arvedi far, 3 Detmold chamber hall, 4 church,
// 5 Maida Vale 4, 6 Maida Vale 5, 7 WDR control room, 8 WDR small studio (params::roomNames).
// Articulation: 0 arco, 1 pizz, 2 Bartok, 3 left-hand pizz, 4 harmonics. Bow style: 0 auto,
// 1 legato, 2 detache, 3 staccato, 4 martele, 5 spiccato. Mute: 0 off, 1 con sordino, 2 practice.
// Violin: 0 Stoppani, 1 Klimke, 2 Levaggi, 3 Iowa. Mic: 0 front, 1 above, 2 player's ear, 3 side.
const std::vector<Presets::Factory>& Presets::factory()
{
    static const std::vector<Factory> list {
        { "Concert Soloist", "The default: a soloist two metres away in a concert hall's near seats.", {} },
        { "Chamber Hall",
          "A smaller, warmer hall from three metres, the player's sympathetic strings a little freer.",
          { { "room", 3 }, { "distance", 3.0f }, { "sympathetic", 60 } } },
        { "Intimate Studio",
          "Close in a studio with a short room: every bow change and finger is heard.",
          { { "room", 5 }, { "distance", 1.0f }, { "reverb", -4 }, { "movement", 30 } } },
        { "Distant Hall",
          "Far back in the concert hall: a wide, soft-edged sound for slow lines.",
          { { "room", 2 }, { "distance", 6.0f }, { "reverb", 2 }, { "width", 130 }, { "phrasing", 120 } } },
        { "Cathedral Solo",
          "A church's long tail; the player phrases broadly and slurs more.",
          { { "room", 4 }, { "distance", 3.5f }, { "phrasing", 140 }, { "bowStyle", 1 }, { "vibrato", 1.15f } } },
        { "Sordino Ballad",
          "Con sordino, legato bowing, warmer vibrato and a soft velocity response.",
          { { "mute", 1 },
            { "bowStyle", 1 },
            { "vibrato", 1.25f },
            { "phrasing", 130 },
            { "velocityCurve", 0.8f },
            { "room", 3 },
            { "distance", 2.5f } } },
        { "Folk Fiddle Dry",
          "Bright and close in a small room: little vibrato, separate bows, ringing open strings, more hiss.",
          { { "room", 8 },
            { "distance", 0.8f },
            { "brightness", 8 },
            { "vibrato", 0.45f },
            { "bowStyle", 2 },
            { "phrasing", 60 },
            { "sympathetic", 85 },
            { "hiss", 1 },
            { "movement", 70 },
            { "volume", -3 } } },
        { "Close Mic Dry",
          "The two microphones only, no room: for your own reverb.",
          { { "room", 0 }, { "distance", 0.6f }, { "movement", 20 } } },
        { "Dark Romantic",
          "A darker violin and bridge, wide slow vibrato and big phrase shapes.",
          { { "violin", 2 },
            { "bridge", 2600 },
            { "brightness", 2 },
            { "vibrato", 1.35f },
            { "phrasing", 150 },
            { "room", 1 },
            { "distance", 2.5f } } },
        { "Bright Studio Lead",
          "Forward and bright for a pop or film lead: brighter bridge and top, studio room.",
          { { "brightness", 9 }, { "bridge", 3300 }, { "room", 6 }, { "distance", 1.5f }, { "volume", -1.5f } } },
        { "Spiccato Etude",
          "Off-the-string bowing in a chamber hall for fast detached passages.",
          { { "bowStyle", 5 }, { "room", 3 }, { "distance", 2.5f }, { "vibrato", 0.8f } } },
        { "Pizzicato Ensemble Seat",
          "Plucked, heard from a section seat in the chamber hall.",
          { { "articulation", 1 }, { "room", 3 }, { "distance", 3.5f }, { "width", 120 } } },
        { "Bartok Snap",
          "Snap pizzicato: the string slaps the fingerboard.",
          { { "articulation", 2 }, { "room", 1 }, { "distance", 2.5f }, { "volume", -8 } } },
        { "Harmonics Glass",
          "Natural and artificial harmonics with a gentle vibrato and a longer hall tail.",
          { { "articulation", 4 }, { "vibrato", 0.6f }, { "room", 2 }, { "distance", 3.0f }, { "reverb", 2 } } },
        { "Practice Mute",
          "The heavy practice mute in a small dry room: quiet, nasal, intimate.",
          { { "mute", 2 }, { "room", 7 }, { "distance", 1.0f }, { "movement", 10 }, { "volume", 6 } } },
    };
    return list;
}

Presets::Presets (juce::AudioProcessorValueTreeState& s, juce::File folder)
    : state (s),
      userFolder (folder == juce::File() ? defaultUserFolder() : folder)
{
    refresh();
    // a new instance plays the default sound, which is the first factory preset
    current = 0;
    loaded = valuesFor (0);
    state.state.setProperty (presetProperty, currentName(), nullptr);
}

juce::File Presets::defaultUserFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("OctahedronV2")
        .getChildFile ("Octavio 2")
        .getChildFile ("Presets");
}

void Presets::setUserFolder (const juce::File& f)
{
    userFolder = f;
    refresh();
}

bool Presets::isPerformanceParameter (const juce::String& id)
{
    static const juce::StringArray ids { "mode", "octave", "bendRange", "mpe", "mpeBendRange" };
    // 2.3: who is in charge of each dimension belongs to the project's curves, not the sound
    return ids.contains (id) || id.startsWith ("mode");
}

void Presets::refresh()
{
    const auto name = currentName();
    const bool wasFactory = current >= 0 && presets[(size_t) current].factoryIndex >= 0;
    presets.clear();
    for (size_t i = 0; i < factory().size(); ++i)
        presets.push_back ({ factory()[i].name, factory()[i].description, (int) i, {} });
    std::vector<Preset> user;
    if (userFolder.isDirectory())
        for (const auto& f :
             userFolder.findChildFiles (juce::File::findFiles, false, juce::String ("*") + fileExtension))
        {
            const auto xml = juce::parseXML (f);
            if (xml == nullptr || ! xml->hasTagName (fileTag))
                continue;
            user.push_back ({ xml->getStringAttribute ("name", f.getFileNameWithoutExtension()),
                              xml->getStringAttribute ("description"),
                              -1,
                              f });
        }
    std::sort (user.begin(),
               user.end(),
               [] (const Preset& a, const Preset& b) { return a.name.compareNatural (b.name) < 0; });
    presets.insert (presets.end(), user.begin(), user.end());
    if (current >= 0)
    {
        current = -1;
        for (size_t i = 0; i < presets.size(); ++i)
            if (presets[i].name == name && (presets[i].factoryIndex >= 0) == wasFactory)
                current = (int) i;
    }
}

juce::String Presets::currentName() const
{
    return current >= 0 && current < (int) presets.size() ? presets[(size_t) current].name : juce::String();
}

std::vector<std::pair<juce::String, float>> Presets::valuesFor (int index) const
{
    std::vector<std::pair<juce::String, float>> values;
    for (auto* p : state.processor.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (! isPerformanceParameter (r->getParameterID()))
                values.emplace_back (r->getParameterID(), r->convertFrom0to1 (r->getDefaultValue()));
    auto set = [&values] (const juce::String& id, float v)
    {
        for (auto& [k, value] : values)
            if (k == id)
                value = v;
    };
    if (index < 0 || index >= (int) presets.size())
        return values;
    const auto& p = presets[(size_t) index];
    if (p.factoryIndex >= 0)
        for (const auto& [id, v] : factory()[(size_t) p.factoryIndex].values)
            set (id, v);
    else if (const auto xml = juce::parseXML (p.file))
        for (const auto* c : xml->getChildWithTagNameIterator ("PARAM"))
            set (c->getStringAttribute ("id"), (float) c->getDoubleAttribute ("value"));
    // inside the current ranges, snapped as the parameter would be
    for (auto& [id, v] : values)
        if (auto* r = state.getParameter (id))
            v = r->convertFrom0to1 (r->convertTo0to1 (v));
    return values;
}

void Presets::apply (const std::vector<std::pair<juce::String, float>>& values)
{
    for (const auto& [id, v] : values)
        if (auto* r = state.getParameter (id))
        {
            const float norm = r->convertTo0to1 (v);
            if (std::abs (norm - r->getValue()) < 1e-6f)
                continue;
            r->beginChangeGesture();
            r->setValueNotifyingHost (norm);
            r->endChangeGesture();
        }
}

void Presets::setCurrent (int index)
{
    current = index >= 0 && index < (int) presets.size() ? index : -1;
    loaded = valuesFor (current);
    state.state.setProperty (presetProperty, currentName(), nullptr);
}

bool Presets::load (int index)
{
    if (index < 0 || index >= (int) presets.size())
        return false;
    apply (valuesFor (index));
    setCurrent (index);
    return true;
}

void Presets::loadNext()
{
    if (! presets.empty())
        load ((current + 1) % (int) presets.size());
}

void Presets::loadPrevious()
{
    if (! presets.empty())
        load ((current <= 0 ? (int) presets.size() : current) - 1);
}

bool Presets::isModified() const
{
    if (current < 0)
        return false;
    for (const auto& [id, v] : loaded)
        if (auto* r = state.getParameter (id))
            if (std::abs (r->convertTo0to1 (v) - r->getValue()) > 1e-4f)
                return true;
    return false;
}

juce::Result Presets::save (const juce::String& rawName)
{
    const auto name = rawName.trim();
    if (name.isEmpty())
        return juce::Result::fail ("The preset needs a name.");
    for (const auto& f : factory())
        if (name.equalsIgnoreCase (f.name))
            return juce::Result::fail ("\"" + name + "\" is a factory preset's name.");
    if (! userFolder.createDirectory())
        return juce::Result::fail ("Can't create " + userFolder.getFullPathName());
    juce::XmlElement xml (fileTag);
    xml.setAttribute ("name", name);
    xml.setAttribute ("version", 1);
    xml.setAttribute ("plugin", JucePlugin_VersionString);
    for (auto* p : state.processor.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (! isPerformanceParameter (r->getParameterID()))
            {
                auto* c = xml.createNewChildElement ("PARAM");
                c->setAttribute ("id", r->getParameterID());
                c->setAttribute ("value", r->convertFrom0to1 (r->getValue()));
            }
    const auto file = userFolder.getChildFile (juce::File::createLegalFileName (name) + fileExtension);
    if (! xml.writeTo (file))
        return juce::Result::fail ("Can't write " + file.getFullPathName());
    refresh();
    for (size_t i = 0; i < presets.size(); ++i)
        if (presets[i].factoryIndex < 0 && presets[i].file == file)
            setCurrent ((int) i);
    return juce::Result::ok();
}

juce::Result Presets::remove (int index)
{
    if (index < 0 || index >= (int) presets.size() || presets[(size_t) index].factoryIndex >= 0)
        return juce::Result::fail ("Only your own presets can be deleted.");
    if (! presets[(size_t) index].file.deleteFile())
        return juce::Result::fail ("Can't delete " + presets[(size_t) index].file.getFullPathName());
    if (index == current)
        current = -1;
    refresh();
    state.state.setProperty (presetProperty, currentName(), nullptr);
    return juce::Result::ok();
}

void Presets::restoreFromState()
{
    const auto name = state.state.getProperty (presetProperty).toString();
    current = -1;
    for (size_t i = 0; i < presets.size() && current < 0; ++i)
        if (presets[i].name == name)
            current = (int) i;
    // only which preset it was: the restored parameters stay as saved (modified or not)
    loaded = valuesFor (current);
}
} // namespace octavio2
