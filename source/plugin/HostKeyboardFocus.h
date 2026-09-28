#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace violinsynth
{
// Keeps the computer keyboard with the host (FL Studio's typing keyboard,
// Ableton's computer MIDI keyboard) except while a text box is being typed
// in. Clicking, dragging or choosing from any other control leaves the
// keyboard playing notes.
//
// JUCE only stops our own components taking focus on a click; the plugin
// window itself still took the host's keyboard focus (Windows gives it to
// the window clicked unless the window refuses), and kept it after a value
// was typed. So the window refuses focus on clicks, any other control that
// gains focus gives it straight back, and the host gets the keyboard back
// when typing ends. The Standalone app owns its window and is left alone.
class HostKeyboardFocus final : private juce::FocusChangeListener, private juce::MouseListener
{
public:
    explicit HostKeyboardFocus (juce::AudioProcessorEditor&);
    ~HostKeyboardFocus() override;

    // Call from the editor's parentHierarchyChanged(): the host's window
    // around the editor must not take focus on a click either.
    void editorParentChanged();

private:
    void globalFocusChanged (juce::Component* focused) override;
    void mouseDown (const juce::MouseEvent&) override;
    void returnToHost();

    juce::AudioProcessorEditor& editor;
    const bool inPlugin;
};
} // namespace violinsynth
