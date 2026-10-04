#include "MidiMap.h"

namespace octavio2
{
const juce::Identifier MidiMap::tag { "MIDIMAP" };

const std::vector<MidiMap::PlayerTarget>& MidiMap::playerTargets()
{
    // the numbers are Player::controller's (core/Player.h), which describes each in full
    static const std::vector<PlayerTarget> targets {
        { 1, "Dynamics", "pp - ff", true },
        { 11, "Expression (level)", "-40 dB - +4 dB, 100 = as played", false },
        { 26, "Vibrato width", "0 - 64 cents", true },
        { 24, "Vibrato amount", "0 - 8x, 64 = as played", false },
        { 19, "Vibrato rate", "4 - 8 Hz", true },
        { 25, "Vibrato relax", "10 ms per step", false },
        { 74, "Contact point", "tasto - ponticello", true },
        { 22, "Bow pressure", "64 = as played", false },
        { 23, "Attack bite", "64 = none", false },
        { 21, "Intonation", "64 = none, 1 cent per step", false },
        { slurPedal, "Slur everything", "on above half", false },
    };
    return targets;
}

juce::StringArray MidiMap::presetNames()
{
    return { "Octavio 2 (CC1 = dynamics)", "Legacy (CC1 = vibrato, CC11 = expression)", "None" };
}

std::vector<MidiMap::Entry> MidiMap::presetEntries (Preset p)
{
    if (p == Preset::none)
        return {};
    // the controllers every map shares: the player's own numbers, as the Curves tab exports them
    std::vector<Entry> e;
    if (p == Preset::octavio)
    {
        e.push_back ({ 1, 1 });
        e.push_back ({ 2, 1 }); // breath controllers
        e.push_back ({ 11, 11 });
    }
    else
    {
        // mod wheel = vibrato: 0 none, full = about twice the player's (Player: (v/64)^3)
        e.push_back ({ 1, 24, 0.0f, 0.63f });
        e.push_back ({ 2, 1 });
        // expression = volume, as sample libraries: 0 silent .. 127 a little above as played
        e.push_back ({ 11, 11 });
    }
    for (int cc : { 19, 21, 22, 23, 24, 25, 26, 74 })
        if (! (p == Preset::legacy && cc == 24))
            e.push_back ({ cc, cc });
    e.push_back ({ 64, slurPedal });
    return e;
}

MidiMap::MidiMap (juce::AudioProcessor& p)
    : processor (p)
{
    loadPreset (Preset::octavio);
}

std::vector<MidiMap::Entry> MidiMap::getEntries() const
{
    const juce::ScopedLock sl (entriesLock);
    return entries;
}

void MidiMap::setEntries (std::vector<Entry> e)
{
    const int nParams = processor.getParameters().size();
    std::vector<Entry> valid;
    for (auto& x : e)
    {
        if (x.cc < 0 || x.cc > 127 || (int) valid.size() >= maxEntries)
            continue;
        const bool ok = (x.target >= 0 && x.target < 128) || x.target == slurPedal
            || (x.target >= parameterBase && x.target < parameterBase + nParams);
        if (! ok)
            continue;
        x.lo = juce::jlimit (0.0f, 1.0f, x.lo);
        x.hi = juce::jlimit (0.0f, 1.0f, x.hi);
        valid.push_back (x);
    }
    {
        const juce::ScopedLock sl (entriesLock);
        entries = valid;
    }
    // Rows are written while hidden (cc = -1) and shown last, so the audio thread never acts on a
    // half-written row; a row it reads during an edit is the old one or the new one.
    const int n = (int) valid.size();
    const int old = count.load();
    if (n > old)
        for (int i = old; i < n; ++i)
            slots[(size_t) i].cc.store (-1, std::memory_order_release);
    for (int i = 0; i < n; ++i)
    {
        auto& s = slots[(size_t) i];
        const auto& x = valid[(size_t) i];
        if (s.cc.load() == x.cc && s.target.load() == x.target && s.lo.load() == x.lo && s.hi.load() == x.hi
            && s.invert.load() == x.invert)
            continue;
        s.cc.store (-1, std::memory_order_release);
        s.target.store (x.target, std::memory_order_relaxed);
        s.lo.store (x.lo, std::memory_order_relaxed);
        s.hi.store (x.hi, std::memory_order_relaxed);
        s.invert.store (x.invert, std::memory_order_relaxed);
        s.cc.store (x.cc, std::memory_order_release);
    }
    for (int i = n; i < old; ++i)
        slots[(size_t) i].cc.store (-1, std::memory_order_release);
    count.store (n, std::memory_order_release);
}

int MidiMap::matchingPreset() const
{
    const auto e = getEntries();
    for (int p = 0; p < 3; ++p)
        if (e == presetEntries ((Preset) p))
            return p;
    return -1;
}

juce::String MidiMap::targetName (int target) const
{
    for (const auto& t : playerTargets())
        if (t.code == target)
            return t.name;
    const auto& ps = processor.getParameters();
    if (target >= parameterBase && target - parameterBase < ps.size())
        return ps[target - parameterBase]->getName (64);
    return "CC" + juce::String (target);
}

juce::String MidiMap::targetRange (int target) const
{
    for (const auto& t : playerTargets())
        if (t.code == target)
            return t.range;
    return {};
}

juce::String MidiMap::sourceName (int cc)
{
    switch (cc)
    {
        case 1:
            return "Mod wheel";
        case 2:
            return "Breath";
        case 4:
            return "Foot";
        case 7:
            return "Volume";
        case 11:
            return "Expression";
        case 64:
            return "Sustain";
        case 65:
            return "Portamento";
        case 66:
            return "Sostenuto";
        case 67:
            return "Soft pedal";
        default:
            return {};
    }
}

juce::ValueTree MidiMap::toTree() const
{
    juce::ValueTree t (tag);
    t.setProperty ("version", 1, nullptr);
    const auto& ps = processor.getParameters();
    for (const auto& e : getEntries())
    {
        juce::ValueTree row ("MAP");
        row.setProperty ("cc", e.cc, nullptr);
        // parameters by ID, so the map survives parameters added later
        if (e.target >= parameterBase)
        {
            if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*> (ps[e.target - parameterBase]))
                row.setProperty ("param", p->paramID, nullptr);
        }
        else if (e.target == slurPedal)
            row.setProperty ("player", "slur", nullptr);
        else
            row.setProperty ("player", e.target, nullptr);
        row.setProperty ("lo", e.lo, nullptr);
        row.setProperty ("hi", e.hi, nullptr);
        row.setProperty ("invert", e.invert, nullptr);
        t.appendChild (row, nullptr);
    }
    return t;
}

void MidiMap::fromTree (const juce::ValueTree& t)
{
    if (! t.isValid() || ! t.hasType (tag))
    {
        loadPreset (Preset::octavio);
        return;
    }
    const auto& ps = processor.getParameters();
    std::vector<Entry> e;
    for (const auto row : t)
    {
        Entry x;
        x.cc = row.getProperty ("cc", -1);
        x.target = -1;
        if (row.hasProperty ("param"))
        {
            const auto id = row["param"].toString();
            for (int i = 0; i < ps.size(); ++i)
                if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*> (ps[i]))
                    if (p->paramID == id)
                        x.target = parameterBase + i;
        }
        else if (row["player"].toString() == "slur")
            x.target = slurPedal;
        else if (row.hasProperty ("player"))
            x.target = row["player"];
        x.lo = (float) (double) row.getProperty ("lo", 0.0);
        x.hi = (float) (double) row.getProperty ("hi", 1.0);
        x.invert = row.getProperty ("invert", false);
        e.push_back (x);
    }
    setEntries (std::move (e));
}
} // namespace octavio2
