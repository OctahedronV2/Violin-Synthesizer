#include "Keyboard.h"

namespace octavio2::ui
{
namespace
{
constexpr int lowest = 24, highest = 96; // C1 .. C7
constexpr int violinLowest = 55; // G3
bool keyswitch (int note)
{
    return note >= 24 && note < 36;
}
} // namespace

Keyboard::Keyboard (Processor& p)
    : juce::MidiKeyboardComponent (p.getKeyboardState(), horizontalKeyboard),
      processor (p)
{
    setAvailableRange (lowest, highest);
    setLowestVisibleKey (lowest);
    setScrollButtonsVisible (false);
    setOctaveForMiddleC (middleCOctave());
    setVelocity (0.8f, true); // softer lower on the key; 100 near the top, as a typing keyboard
    // the host's typing keyboard plays the violin: this one takes no keys
    clearKeyMappings();
    setWantsKeyboardFocus (false);
    setBlackNoteLengthProportion (0.6f);
    setBlackNoteWidthProportion (0.6f);
    setColour (whiteNoteColourId, juce::Colour (0xff0d0a08)); // the gaps between keys
    setColour (keySeparatorLineColourId, juce::Colours::transparentBlack);
    setColour (shadowColourId, juce::Colours::transparentBlack);
    timer.startTimerHz (30);
}

void Keyboard::resized()
{
    setKeyWidth ((float) getWidth() / 43.0f); // 43 white keys, C1 to C7
    juce::MidiKeyboardComponent::resized();
}

void Keyboard::poll()
{
    const int n = processor.getTelemetry().note.load();
    if (n != sounding)
    {
        sounding = n;
        repaint();
    }
}

void Keyboard::drawWhiteNote (int note,
                              juce::Graphics& g,
                              juce::Rectangle<float> r,
                              bool down,
                              bool over,
                              juce::Colour,
                              juce::Colour)
{
    auto fill = keyswitch (note) ? juce::Colour (0xffc5d8e3)
        : note < violinLowest    ? juce::Colour (0xff8c847c)
                                 : juce::Colour (0xfff3ede4);
    if (note == sounding || down)
        fill = colours::gold;
    else if (over)
        fill = fill.interpolatedWith (colours::gold, 0.35f);
    g.setColour (fill);
    g.fillRoundedRectangle (r.reduced (0.5f, 0).withTrimmedBottom (1), 3);
    if (note % 12 == 0)
        drawText (g,
                  juce::MidiMessage::getMidiNoteName (note, true, true, getOctaveForMiddleC()),
                  r.getCentreX(),
                  r.getBottom() - 8,
                  Fonts::mono (9),
                  juce::Colour (0xff3a332d),
                  juce::Justification::horizontallyCentred);
}

void Keyboard::drawBlackNote (int note, juce::Graphics& g, juce::Rectangle<float> r, bool down, bool over, juce::Colour)
{
    auto fill = keyswitch (note) ? juce::Colour (0xff2b3a44) : juce::Colour (0xff1b1714);
    if (note == sounding || down)
        fill = colours::gold.darker (0.2f);
    else if (over)
        fill = fill.brighter (0.4f);
    g.setColour (fill);
    g.fillRoundedRectangle (r, 2);
}
} // namespace octavio2::ui
