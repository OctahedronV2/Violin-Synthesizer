#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "plugin/HostKeyboardFocus.h"

namespace violinsynth
{
namespace
{
bool isTextBox (juce::Component* c)
{
    return dynamic_cast<juce::TextEditor*> (c) != nullptr;
}
} // namespace

HostKeyboardFocus::HostKeyboardFocus (juce::AudioProcessorEditor& e)
    : editor (e),
      inPlugin (e.processor.wrapperType != juce::AudioProcessor::wrapperType_Standalone)
{
    if (! inPlugin)
        return;
    juce::Desktop::getInstance().addFocusChangeListener (this);
    editor.addMouseListener (this, true);
}

HostKeyboardFocus::~HostKeyboardFocus()
{
    if (! inPlugin)
        return;
    editor.removeMouseListener (this);
    juce::Desktop::getInstance().removeFocusChangeListener (this);
}

void HostKeyboardFocus::editorParentChanged()
{
    if (! inPlugin)
        return;
    // On Windows the plugin window's top component decides whether a click
    // activates the window (JUCE answers WM_MOUSEACTIVATE with MA_NOACTIVATE).
    for (auto* c = editor.getParentComponent(); c != nullptr; c = c->getParentComponent())
        c->setMouseClickGrabsKeyboardFocus (false);
}

void HostKeyboardFocus::globalFocusChanged (juce::Component* focused)
{
    if (focused != nullptr && focused != &editor && ! editor.isParentOf (focused))
        return; // another window of this process, such as another plugin
    if (isTextBox (focused))
        return; // typing a value
    if (focused != nullptr)
    {
        // Tab, a host focusing the window, or a menu restoring focus: give
        // it back. That calls this again with nothing focused.
        focused->giveAwayKeyboardFocus();
        return;
    }
    returnToHost();
}

void HostKeyboardFocus::mouseDown (const juce::MouseEvent& e)
{
    // A click anywhere else in the editor ends typing, as it would in the host.
    auto* focused = juce::Component::getCurrentlyFocusedComponent();
    if (isTextBox (focused) && editor.isParentOf (focused) && e.eventComponent != focused
        && ! focused->isParentOf (e.eventComponent))
        focused->giveAwayKeyboardFocus();
}

void HostKeyboardFocus::returnToHost()
{
#if defined(_WIN32)
    if (auto* peer = editor.getPeer())
    {
        const auto window = static_cast<HWND> (peer->getNativeHandle());
        if (GetFocus() == window)
            if (const auto parent = GetParent (window))
                SetFocus (parent);
    }
#endif
}
} // namespace violinsynth
