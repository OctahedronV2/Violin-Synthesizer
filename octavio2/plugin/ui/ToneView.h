#pragma once

#include "../PluginProcessor.h"
#include "Controls.h"

namespace octavio2::ui
{
// M7: a value in a box (the A4 reference): drag up/down to change it (shift: fine), double-click
// for the default, right-click for common values and the host's menu
class ValueBox final : public juce::Component, public juce::SettableTooltipClient
{
public:
    ValueBox (juce::RangedAudioParameter&, std::function<juce::String (float)> text, std::vector<float> presets);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    std::function<juce::String (float)> text;
    std::vector<float> presets;
    float value = 0, dragStart = 0;
    bool dragging = false;
};

// M7: a button drawn as the mockups' boxes
class ActionBox final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit ActionBox (const juce::String& t)
        : text (t)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }
    std::function<void()> onClick;
    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (isMouseOver() ? colours::panel2.brighter (0.08f) : colours::panel2);
        g.fillRoundedRectangle (r, 7);
        g.setColour (colours::line);
        g.drawRoundedRectangle (r, 7, 1);
        drawText (g,
                  text,
                  r.getCentreX(),
                  r.getCentreY() + 5,
                  Fonts::sans (13),
                  colours::text,
                  juce::Justification::horizontallyCentred);
    }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasClicked() && onClick)
            onClick();
    }

private:
    juce::String text;
};

// Tone tab (mockup tone.svg): the instrument's parts, bridge and resonance, microphones and room.
// The violin, mute, bridge, microphones and room (M1); strings, rosin and bow (M7, with a one-click
// Modern / Baroque violin, also the header's Instrument menu); Imperfection (2.3). Only Quality's
// Eco choice is still a preview (it arrives with the optimisation milestone, M8).
class ToneView final : public juce::Component, private juce::Timer
{
public:
    explicit ToneView (Processor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    // M7: one click sets the parts of a modern or a baroque violin (strings, rosin, bow, and the
    // Tuning row's A4, intonation and player style when those parameters exist). Lit when the
    // parts match.
    class Instruments final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit Instruments (juce::AudioProcessorValueTreeState&);
        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;

    private:
        int active() const;
        juce::Rectangle<float> itemBounds (int) const;
        void choose (int);
        juce::AudioProcessorValueTreeState& state;
    };

    void timerCallback() override;
    void paintScope (juce::Graphics&, juce::Rectangle<float>);
    void paintMics (juce::Graphics&, float x, float y);
    // the microphone positions on the violin drawing, in Mic Position order
    juce::Point<float> micPoint (int index) const;
    int micAt (juce::Point<float>) const;

    Processor& processor;
    Choices body, strings, rosin, bow, hiss, mute, quality, rooms;
    Instruments instruments;
    Knob bridge, sympathetic, wolf, hold, brilliance, imperfection;
    Knob reverb, volume, width, movement, distance;
    std::unique_ptr<juce::ParameterAttachment> micAttachment;
    // M7 tuning row: A4, system, key, Scala file
    void chooseScala();
    juce::String tuningStatus() const;
    std::unique_ptr<ValueBox> a4;
    juce::ComboBox intonation, key;
    ActionBox scalaButton { "Load Scala..." };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> intonationAttachment, keyAttachment;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String scalaError;
    int mic = 0;
    std::vector<float> scope;
};
} // namespace octavio2::ui
