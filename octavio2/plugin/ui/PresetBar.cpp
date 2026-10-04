#include "PresetBar.h"

namespace octavio2::ui
{
namespace
{
void boxShape (juce::Graphics& g, juce::Rectangle<float> r, bool hot)
{
    g.setColour (hot ? colours::line : colours::panel2);
    g.fillRoundedRectangle (r, 7);
    g.setColour (colours::line);
    g.drawRoundedRectangle (r.reduced (0.5f), 7, 1);
}
} // namespace

PresetBar::PresetBar (Processor& p)
    : processor (p)
{
    setTooltip ("Presets: click the name for the list, Save and your preset folder; the arrows step through them. "
                "A dot means a setting differs from the preset. MIDI map, Octave and Live/Studio stay as they are.");
    setRepaintsOnMouseActivity (false);
    startTimerHz (4);
    timerCallback();
}

PresetBar::~PresetBar()
{
    if (saveWindow != nullptr)
        saveWindow->exitModalState (0);
}

void PresetBar::timerCallback()
{
    auto& presets = processor.getPresets();
    const auto name = presets.currentName();
    const bool modified = presets.isModified();
    if (name != shownName || modified != shownModified)
    {
        shownName = name;
        shownModified = modified;
        repaint();
    }
}

void PresetBar::paint (juce::Graphics& g)
{
    auto arrow = [&g] (juce::Rectangle<float> r, bool left)
    {
        juce::Path p;
        const float cx = r.getCentreX(), cy = r.getCentreY();
        p.startNewSubPath (cx + (left ? 3.0f : -3.0f), cy - 6);
        p.lineTo (cx + (left ? -3.0f : 3.0f), cy);
        p.lineTo (cx + (left ? 3.0f : -3.0f), cy + 6);
        g.setColour (colours::muted);
        g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };
    boxShape (g, prevBox(), hover == 0);
    arrow (prevBox(), true);
    boxShape (g, nextBox(), hover == 2);
    arrow (nextBox(), false);
    const auto r = nameBox();
    boxShape (g, r, hover == 1);
    const auto text = shownName.isEmpty() ? juce::String ("No preset") : shownName;
    const auto font = Fonts::sans (13);
    const float w = juce::GlyphArrangement::getStringWidth (font, text);
    drawText (g,
              text,
              r.getCentreX(),
              r.getCentreY() + 5,
              font,
              shownName.isEmpty() ? colours::muted : colours::text,
              juce::Justification::horizontallyCentred);
    if (shownModified)
    {
        g.setColour (colours::amber);
        g.fillEllipse (r.getCentreX() + w / 2 + 6, r.getCentreY() - 3, 6, 6);
    }
}

void PresetBar::mouseMove (const juce::MouseEvent& e)
{
    const int h = prevBox().contains (e.position) ? 0
        : nextBox().contains (e.position)         ? 2
        : nameBox().contains (e.position)         ? 1
                                                  : -1;
    if (h != hover)
    {
        hover = h;
        setMouseCursor (h >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void PresetBar::mouseExit (const juce::MouseEvent&)
{
    hover = -1;
    repaint();
}

void PresetBar::mouseUp (const juce::MouseEvent& e)
{
    auto& presets = processor.getPresets();
    if (prevBox().contains (e.position))
        presets.loadPrevious();
    else if (nextBox().contains (e.position))
        presets.loadNext();
    else if (nameBox().contains (e.position))
        showMenu();
    timerCallback();
}

void PresetBar::showMenu()
{
    auto& presets = processor.getPresets();
    presets.refresh();
    juce::PopupMenu menu;
    const auto& list = presets.list();
    menu.addSectionHeader ("Factory");
    for (int i = 0; i < (int) list.size(); ++i)
    {
        if (i == presets.numFactory())
            menu.addSectionHeader ("Yours");
        menu.addItem (i + 1, list[(size_t) i].name, true, i == presets.currentIndex());
    }
    if ((int) list.size() == presets.numFactory())
        menu.addItem (-1, "(none saved yet)", false);
    menu.addSeparator();
    constexpr int save = 10001, remove = 10002, folder = 10003, rescan = 10004;
    menu.addItem (save, juce::String::fromUTF8 ("Save as…"));
    const int cur = presets.currentIndex();
    menu.addItem (remove,
                  "Delete "
                      + (cur >= presets.numFactory() ? "\"" + presets.currentName() + "\"" : juce::String ("preset")),
                  cur >= presets.numFactory());
    menu.addItem (folder, "Show preset folder");
    menu.addItem (rescan, "Rescan folder");
    juce::Component::SafePointer<PresetBar> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (getWidth()),
                        [safe] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;
                            auto& ps = safe->processor.getPresets();
                            if (result == save)
                                safe->askSaveName();
                            else if (result == remove)
                            {
                                const auto r = ps.remove (ps.currentIndex());
                                if (r.failed())
                                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                                            "Delete preset",
                                                                            r.getErrorMessage());
                            }
                            else if (result == folder)
                            {
                                ps.getUserFolder().createDirectory();
                                ps.getUserFolder().revealToUser();
                            }
                            else if (result == rescan)
                                ps.refresh();
                            else if (result > 0)
                                ps.load (result - 1);
                            safe->timerCallback();
                        });
}

void PresetBar::askSaveName()
{
    auto& presets = processor.getPresets();
    saveWindow = std::make_unique<juce::AlertWindow> ("Save preset",
                                                      "Saved in " + presets.getUserFolder().getFullPathName(),
                                                      juce::MessageBoxIconType::NoIcon,
                                                      this);
    const auto suggestion = presets.currentIndex() >= presets.numFactory() ? presets.currentName() : juce::String();
    saveWindow->addTextEditor ("name", suggestion, "Name");
    saveWindow->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    saveWindow->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<PresetBar> safe (this);
    saveWindow->enterModalState (true,
                                 juce::ModalCallbackFunction::create (
                                     [safe] (int result)
                                     {
                                         if (safe == nullptr || safe->saveWindow == nullptr)
                                             return;
                                         const auto name = safe->saveWindow->getTextEditorContents ("name");
                                         safe->saveWindow.reset();
                                         if (result != 1)
                                             return;
                                         const auto r = safe->processor.getPresets().save (name);
                                         if (r.failed())
                                             juce::AlertWindow::showMessageBoxAsync (
                                                 juce::MessageBoxIconType::WarningIcon,
                                                 "Save preset",
                                                 r.getErrorMessage());
                                         safe->timerCallback();
                                     }),
                                 false);
}
} // namespace octavio2::ui
