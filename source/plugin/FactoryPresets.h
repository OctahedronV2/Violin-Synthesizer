#pragma once

#include <utility>
#include <vector>

namespace violinsynth::presets
{
// A preset built into the plugin. Values are plain parameter values (choices
// and booleans as indices); parameters not listed take their defaults, so a
// preset always defines the whole sound.
struct FactoryPreset
{
    const char* name;
    const char* category;
    const char* description;
    std::vector<std::pair<const char*, float>> values;
};

// Categories in display order.
inline constexpr const char* categories[] {
    "Solo", "Styles", "Articulations", "Character", "Expressive", "Bowed Guitar"
};

// The factory presets in display order. The first is the plugin's default sound.
const std::vector<FactoryPreset>& factoryPresets();
} // namespace violinsynth::presets
