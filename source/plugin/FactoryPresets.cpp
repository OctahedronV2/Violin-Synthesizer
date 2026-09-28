#include "plugin/FactoryPresets.h"

namespace violinsynth::presets
{
namespace
{
// Choice indices (see Parameters.cpp).
constexpr float klimke = 1.0f, stoppani = 2.0f, iowa = 3.0f;
constexpr float monoLegato = 1.0f, poly = 2.0f;
constexpr float detache = 1.0f, staccato = 2.0f, spiccato = 3.0f, tremolo = 4.0f, pizzicato = 5.0f, harmonics = 6.0f,
                ponticello = 7.0f, tasto = 8.0f, sordino = 9.0f;
} // namespace

const std::vector<FactoryPreset>& factoryPresets()
{
    // clang-format off
    static const std::vector<FactoryPreset> list {
        // Solo
        { "Default Violin", "Solo",
          "The plugin's default: a balanced solo violin in a small room.",
          {} },
        { "Romantic Soloist", "Solo",
          "Wide, warm vibrato that blooms early, expressive slides and a generous hall.",
          { { "vibratoDepth", 38.0f }, { "vibratoRate", 6.0f }, { "vibratoDelay", 0.18f }, { "portamento", 0.09f },
            { "attack", 0.12f }, { "release", 0.3f }, { "bowPressure", 0.55f }, { "bowPosition", 0.1f },
            { "resonance", 0.4f }, { "humanise", 0.6f }, { "room", 0.32f }, { "width", 0.6f }, { "outputGain", -2.0f } } },
        { "Intimate Close-Mic", "Solo",
          "Close and dry, as if the microphone were a foot from the f-holes.",
          { { "body", klimke }, { "vibratoDepth", 20.0f }, { "attack", 0.06f }, { "room", 0.04f },
            { "width", 0.3f }, { "outputGain", 2.0f } } },
        { "Bright Soloist", "Solo",
          "Bowed nearer the bridge with a firmer grip: a projecting, brilliant tone.",
          { { "body", klimke }, { "bowPosition", 0.075f }, { "bowPressure", 0.65f }, { "vibratoDepth", 30.0f },
            { "vibratoRate", 6.2f }, { "room", 0.2f }, { "outputGain", -1.0f } } },
        { "Dark & Warm", "Solo",
          "Bowed towards the fingerboard on the darker Iowa violin, with a slower vibrato.",
          { { "body", iowa }, { "bowPosition", 0.16f }, { "bowPressure", 0.45f }, { "vibratoDepth", 22.0f },
            { "vibratoRate", 5.2f }, { "room", 0.25f }, { "outputGain", 3.5f } } },
        { "Singing Legato", "Solo",
          "One singing line: every overlapping note slurs, with audible shifts between them.",
          { { "playMode", monoLegato }, { "portamento", 0.12f }, { "attack", 0.15f }, { "release", 0.35f },
            { "vibratoDepth", 32.0f }, { "vibratoDelay", 0.35f }, { "room", 0.3f }, { "outputGain", -1.5f } } },

        // Styles
        { "Baroque", "Styles",
          "Almost no vibrato, a lighter bow and ringing open strings, in a resonant church.",
          { { "body", iowa }, { "vibratoDepth", 6.0f }, { "vibratoDelay", 0.6f }, { "vibratoRate", 5.0f },
            { "attack", 0.1f }, { "bowPosition", 0.13f }, { "bowPressure", 0.45f }, { "humanise", 0.3f },
            { "resonance", 0.55f }, { "portamento", 0.02f }, { "room", 0.35f }, { "outputGain", 1.0f } } },
        { "Folk Fiddle", "Styles",
          "Quick, gritty bowing, little vibrato and droning open strings.",
          { { "body", klimke }, { "attack", 0.03f }, { "release", 0.08f }, { "bowPosition", 0.09f },
            { "bowPressure", 0.62f }, { "vibratoDepth", 10.0f }, { "vibratoDelay", 0.5f }, { "vibratoRate", 6.0f },
            { "portamento", 0.03f }, { "resonance", 0.6f }, { "room", 0.1f }, { "outputGain", -3.0f } } },
        { "Gypsy Swing", "Styles",
          "A fast, wide vibrato from the first moment and slides between notes.",
          { { "vibratoDepth", 45.0f }, { "vibratoRate", 6.8f }, { "vibratoDelay", 0.12f }, { "portamento", 0.1f },
            { "attack", 0.05f }, { "bowPosition", 0.09f }, { "bowPressure", 0.6f }, { "room", 0.2f }, { "outputGain", -2.0f } } },
        { "Fiddle Double Stops", "Styles",
          "Every note gets its own string: play chords and drones with a sharp attack.",
          { { "playMode", poly }, { "resonance", 0.7f }, { "attack", 0.04f }, { "vibratoDepth", 12.0f },
            { "bowPressure", 0.6f }, { "room", 0.12f }, { "outputGain", 0.5f } } },
        { "Film Lyrical", "Styles",
          "Slow, swelling attacks in a large, wide hall for sustained cinematic lines.",
          { { "vibratoDepth", 30.0f }, { "vibratoRate", 5.4f }, { "vibratoDelay", 0.4f }, { "attack", 0.25f },
            { "release", 0.6f }, { "portamento", 0.08f }, { "room", 0.55f }, { "width", 0.8f },
            { "humanise", 0.6f }, { "outputGain", -2.0f } } },

        // Articulations
        { "Detache", "Articulations",
          "A new bow stroke for every note, even when notes overlap.",
          { { "articulation", detache }, { "room", 0.2f }, { "outputGain", 2.0f } } },
        { "Staccato", "Articulations",
          "Short, bitten martele strokes. The bow stops on the string.",
          { { "articulation", staccato }, { "room", 0.25f }, { "outputGain", 3.5f } } },
        { "Spiccato", "Articulations",
          "Light bouncing strokes for fast passages.",
          { { "articulation", spiccato }, { "room", 0.25f }, { "outputGain", 5.5f } } },
        { "Tremolo", "Articulations",
          "Rapid bow reversals on every note.",
          { { "articulation", tremolo }, { "vibratoDepth", 15.0f }, { "room", 0.35f }, { "outputGain", 4.5f } } },
        { "Pizzicato", "Articulations",
          "Plucked strings with open-string resonance.",
          { { "articulation", pizzicato }, { "resonance", 0.5f }, { "vibratoDepth", 10.0f }, { "room", 0.25f }, { "outputGain", 6.0f } } },
        { "Pizzicato Hall", "Articulations",
          "Plucked strings in a big hall.",
          { { "articulation", pizzicato }, { "body", stoppani }, { "resonance", 0.5f }, { "vibratoDepth", 10.0f },
            { "room", 0.6f }, { "width", 0.8f }, { "outputGain", 8.5f } } },
        { "Harmonics", "Articulations",
          "Pure, flute-like natural harmonics.",
          { { "articulation", harmonics }, { "vibratoDepth", 12.0f }, { "room", 0.45f }, { "outputGain", 3.0f } } },
        { "Sul Ponticello", "Articulations",
          "Bowed right by the bridge: glassy and ghostly.",
          { { "articulation", ponticello }, { "vibratoDepth", 8.0f }, { "room", 0.3f }, { "outputGain", -2.0f } } },
        { "Sul Tasto", "Articulations",
          "Bowed over the fingerboard: soft and veiled (flautando).",
          { { "articulation", tasto }, { "vibratoDepth", 18.0f }, { "room", 0.35f }, { "outputGain", 3.5f } } },
        { "Con Sordino", "Articulations",
          "With a mute on the bridge: hushed and intimate.",
          { { "articulation", sordino }, { "vibratoDepth", 24.0f }, { "room", 0.3f }, { "outputGain", 0.5f } } },

        // Character
        { "Practice Mute", "Character",
          "A heavy practice mute in a small, dry room.",
          { { "sordino", 1.0f }, { "vibratoDepth", 20.0f }, { "room", 0.02f }, { "width", 0.2f }, { "outputGain", 1.0f } } },
        { "Eerie Tremolo", "Character",
          "A light tremolo near the bridge in a vast space: horror-film tension.",
          { { "articulation", tremolo }, { "bowPosition", 0.065f }, { "bowPressure", 0.25f }, { "vibratoDepth", 0.0f },
            { "room", 0.65f }, { "width", 0.9f }, { "outputGain", 2.0f } } },
        { "Dry Studio", "Character",
          "No room at all and a narrow image: add your own reverb in the mix.",
          { { "room", 0.0f }, { "width", 0.25f } } },
        { "Slow Swells", "Character",
          "Long swelling attacks and releases for pads and textures.",
          { { "attack", 0.45f }, { "release", 0.8f }, { "vibratoDelay", 0.6f }, { "bowPressure", 0.45f },
            { "room", 0.45f }, { "width", 0.7f }, { "outputGain", 1.5f } } },

        // Expressive
        { "MPE Expressive", "Expressive",
          "For MPE controllers: per-note bend, pressure adds vibrato and slide moves the bow.",
          { { "mpe", 1.0f }, { "vibratoDepth", 15.0f }, { "vibratoDelay", 0.2f }, { "humanise", 0.4f },
            { "room", 0.25f } } },
        { "Mono Lead", "Expressive",
          "A monophonic lead line with quick, natural shifts: ideal for a keyboard melody.",
          { { "playMode", monoLegato }, { "portamento", 0.07f }, { "attack", 0.07f }, { "vibratoDepth", 28.0f },
            { "room", 0.25f }, { "outputGain", -0.5f } } },
    };
    // clang-format on
    return list;
}
} // namespace violinsynth::presets
