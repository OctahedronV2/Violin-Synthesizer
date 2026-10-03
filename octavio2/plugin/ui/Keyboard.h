#pragma once

#include "../PluginProcessor.h"
#include "Theme.h"

#include <juce_audio_utils/juce_audio_utils.h>

namespace octavio2::ui
{
// The mockups' keyboard, C1 to C7 named as they sound: the keyswitch octave (C1-B1) in steel,
// keys below the violin's G3 greyed (silent), the sounding note in gold. Clicks play the note
// they show (the processor takes the Octave shift back off).
class Keyboard final : public juce::MidiKeyboardComponent
{
public:
    explicit Keyboard (Processor&);
    void resized() override;

private:
    void poll();
    void drawWhiteNote (int note,
                        juce::Graphics&,
                        juce::Rectangle<float>,
                        bool down,
                        bool over,
                        juce::Colour,
                        juce::Colour) override;
    void drawBlackNote (int note, juce::Graphics&, juce::Rectangle<float>, bool down, bool over, juce::Colour) override;

    Processor& processor;
    int sounding = -1;
    juce::TimedCallback timer { [this] { poll(); } };
};
} // namespace octavio2::ui
