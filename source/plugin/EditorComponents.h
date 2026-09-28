#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <memory>
#include <vector>

namespace violinsynth
{
class ViolinSynthProcessor;
class PresetManager;

// Shows the host's menu for a parameter at the mouse, as a right-click on a
// plugin control does in FL Studio, Cubase and other VST3 hosts (create an
// automation clip, MIDI learn and so on). Hosts without one get a small menu
// of our own.
void showParameterMenu (juce::Component& control, juce::RangedAudioParameter& parameter);

// The same for a control that sets several parameters: one submenu each.
void showParameterMenu (juce::Component& control, const std::vector<juce::RangedAudioParameter*>& parameters);

// Stops clicks on the component or any of its children taking keyboard focus.
void stopClicksTakingFocus (juce::Component&);

// Note names as the host's piano roll shows them. Hosts number middle C
// (MIDI note 60) differently: C5 in FL Studio; C3 in Ableton Live, Cubase,
// Nuendo, Logic, GarageBand, Bitwig and Studio One; C4 elsewhere.
int hostMiddleCOctave();
juce::String noteName (int midiNote);

// A control whose right-click (or Ctrl-click on macOS) opens the parameter
// menu instead of moving the control.
template <typename Base>
class ParameterControl final : public Base
{
public:
    using Base::Base;

    std::function<void()> onParameterMenu;

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && onParameterMenu != nullptr)
            onParameterMenu();
        else
            Base::mouseDown (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! (e.mods.isPopupMenu() && onParameterMenu != nullptr))
            Base::mouseDrag (e);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! (e.mods.isPopupMenu() && onParameterMenu != nullptr))
            Base::mouseUp (e);
    }
};

// Bow position (x: bridge to fingerboard) against bow pressure (y) in one
// pad. Drag to play with both; double-click resets; arrow keys nudge.
class BowPad final : public juce::Component, public juce::SettableTooltipClient
{
public:
    BowPad (juce::RangedAudioParameter& position, juce::RangedAudioParameter& pressure);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    juce::Rectangle<float> padArea() const;
    void setFromPoint (juce::Point<float>);
    void updateDescription();

    juce::RangedAudioParameter& position;
    juce::RangedAudioParameter& pressure;
    juce::ParameterAttachment positionAttachment, pressureAttachment;
    float x = 0.0f, y = 0.0f; // normalised 0..1
};

// The four strings, each showing the note held on it (where the finger
// stops the string), how loud it is sounding and how fast the bow moves.
class StringDisplay final : public juce::Component, private juce::Timer
{
public:
    explicit StringDisplay (ViolinSynthProcessor&);

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    ViolinSynthProcessor& processor;
    std::array<int, 4> notes { -1, -1, -1, -1 };
    std::array<float, 4> levels {};
    std::array<float, 4> speeds {};
};

// Preset name with previous/next, a categorised menu, save and delete.
class PresetBar final : public juce::Component, private juce::Timer
{
public:
    explicit PresetBar (PresetManager&);
    ~PresetBar() override;

    void resized() override;

private:
    void timerCallback() override;
    void showMenu();
    void showSaveDialog();
    void confirmDelete();
    void refreshName();

    PresetManager& manager;
    juce::TextButton previous { "<" }, next { ">" }, name, save { "Save" }, remove { "Delete" };
    std::unique_ptr<juce::AlertWindow> dialog;
    juce::String shownName;
};
} // namespace violinsynth
