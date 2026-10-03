#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace octavio2::ui
{
// The look of the Octavio 2 mockups (octavio-2/mockups/gen.py): a dark faceplate, amber for what
// the player decides, steel for guided, coral for manual. 1200 x 780 design units.
namespace colours
{
inline const juce::Colour bg { 0xff15110e };
inline const juce::Colour panel { 0xff211a15 };
inline const juce::Colour panel2 { 0xff2a221b };
inline const juce::Colour line { 0xff3b3029 };
inline const juce::Colour text { 0xfff0e7da };
inline const juce::Colour muted { 0xffa5968a };
inline const juce::Colour dim { 0xff6e6157 };
inline const juce::Colour amber { 0xffdba25a };
inline const juce::Colour gold { 0xffecc87e };
inline const juce::Colour steel { 0xff8db6cf };
inline const juce::Colour coral { 0xffe2826a };
inline const juce::Colour good { 0xff8fc69a };
inline const juce::Colour well { 0xff120e0b };
} // namespace colours

inline constexpr int designWidth = 1200, designHeight = 780;

// The three faces of the mockups, built into the plugin (fonts/OFL.txt).
struct Fonts
{
    static juce::Font serif (float height);
    static juce::Font sans (float height, bool semibold = false);
    static juce::Font mono (float height);
};

// Auto / Guided / Manual: who sets a dimension (the player, the player around your curve, you).
enum class Mode
{
    autoMode,
    guided,
    manual
};

void drawBadge (juce::Graphics&, juce::Point<float> centre, Mode);
// A panel with an amber uppercase title on the left and a muted note on the right.
void drawPanel (juce::Graphics&,
                juce::Rectangle<float>,
                const juce::String& title = {},
                const juce::String& right = {});
// Small uppercase label with letter spacing.
void drawLabel (juce::Graphics&,
                const juce::String&,
                float x,
                float baseline,
                juce::Colour = colours::muted,
                float size = 9.5f);
// Text with its baseline at y (as SVG text is placed).
void drawText (juce::Graphics&,
               const juce::String&,
               float x,
               float baseline,
               const juce::Font&,
               juce::Colour,
               juce::Justification = juce::Justification::left);
// The mockup knob: grey track, coloured value arc, inner disc, pointer.
void drawKnob (juce::Graphics&,
               juce::Point<float> centre,
               float radius,
               float value,
               juce::Colour,
               float ghost = -1.0f);
// Hosts number middle C differently: C5 in FL Studio, C3 in Ableton Live, Cubase, Logic, Bitwig
// and Studio One, C4 elsewhere. Note names follow the host (as Octavio 1).
int middleCOctave();
juce::String noteName (int midi); // in the host's octave numbering, sharps as ♯
juce::String dynamicName (float d); // pp .. ff
} // namespace octavio2::ui
