#include "plugin/EditorComponents.h"

#include "engine/StringData.h"
#include "plugin/FactoryPresets.h"
#include "plugin/LookAndFeel.h"
#include "plugin/PluginProcessor.h"
#include "plugin/PresetManager.h"

namespace violinsynth
{
//==============================================================================
BowPad::BowPad (juce::RangedAudioParameter& positionParam, juce::RangedAudioParameter& pressureParam)
    : position (positionParam),
      pressure (pressureParam),
      positionAttachment (positionParam,
                          [this] (float v)
                          {
                              x = position.convertTo0to1 (v);
                              updateDescription();
                              repaint();
                          }),
      pressureAttachment (pressureParam,
                          [this] (float v)
                          {
                              y = pressure.convertTo0to1 (v);
                              updateDescription();
                              repaint();
                          })
{
    positionAttachment.sendInitialUpdate();
    pressureAttachment.sendInitialUpdate();
    setWantsKeyboardFocus (true);
    setTitle ("Bow pad");
    setHelpText ("Drag to move the bow between the bridge and the fingerboard (left to right) and to change its "
                 "pressure (up is firmer). Arrow keys nudge it; double-click resets.");
    setTooltip (getHelpText());
}

juce::Rectangle<float> BowPad::padArea() const
{
    return getLocalBounds().toFloat().reduced (1.0f).withTrimmedBottom (16.0f);
}

void BowPad::updateDescription()
{
    setDescription ("Bow position " + position.getCurrentValueAsText() + ", pressure "
                    + pressure.getCurrentValueAsText());
}

void BowPad::paint (juce::Graphics& g)
{
    const auto area = padArea();
    g.setColour (colours::track);
    g.fillRoundedRectangle (area, 6.0f);

    // Grid
    g.setColour (colours::panelEdge.withAlpha (0.6f));
    for (int i = 1; i < 4; ++i)
    {
        const auto fx = area.getX() + area.getWidth() * static_cast<float> (i) / 4.0f;
        const auto fy = area.getY() + area.getHeight() * static_cast<float> (i) / 4.0f;
        g.drawVerticalLine (juce::roundToInt (fx), area.getY() + 4.0f, area.getBottom() - 4.0f);
        g.drawHorizontalLine (juce::roundToInt (fy), area.getX() + 4.0f, area.getRight() - 4.0f);
    }

    // Axis captions
    g.setFont (juce::FontOptions { 11.0f });
    g.setColour (colours::dimText);
    const auto captions = getLocalBounds().toFloat().removeFromBottom (15.0f);
    g.drawText ("< bridge", captions, juce::Justification::centredLeft);
    g.drawText ("fingerboard >", captions, juce::Justification::centredRight);
    g.drawText ("firm", area.reduced (6.0f), juce::Justification::topLeft);
    g.drawText ("light", area.reduced (6.0f), juce::Justification::bottomLeft);

    // The bow
    const auto point = juce::Point<float> (area.getX() + x * area.getWidth(), area.getBottom() - y * area.getHeight());
    g.setColour (colours::accent.withAlpha (0.18f));
    g.fillEllipse (juce::Rectangle<float> (34.0f, 34.0f).withCentre (point));
    g.setColour (colours::accent);
    g.fillEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre (point));
    g.setColour (colours::text);
    g.drawEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre (point), 1.5f);

    if (hasKeyboardFocus (false))
    {
        g.setColour (colours::accent.withAlpha (0.6f));
        g.drawRoundedRectangle (area, 6.0f, 1.0f);
    }
}

void BowPad::setFromPoint (juce::Point<float> p)
{
    const auto area = padArea();
    x = juce::jlimit (0.0f, 1.0f, (p.x - area.getX()) / area.getWidth());
    y = juce::jlimit (0.0f, 1.0f, (area.getBottom() - p.y) / area.getHeight());
    positionAttachment.setValueAsPartOfGesture (position.convertFrom0to1 (x));
    pressureAttachment.setValueAsPartOfGesture (pressure.convertFrom0to1 (y));
}

void BowPad::mouseDown (const juce::MouseEvent& e)
{
    positionAttachment.beginGesture();
    pressureAttachment.beginGesture();
    setFromPoint (e.position);
}

void BowPad::mouseDrag (const juce::MouseEvent& e)
{
    setFromPoint (e.position);
}

void BowPad::mouseUp (const juce::MouseEvent&)
{
    positionAttachment.endGesture();
    pressureAttachment.endGesture();
}

void BowPad::mouseDoubleClick (const juce::MouseEvent&)
{
    positionAttachment.setValueAsCompleteGesture (position.convertFrom0to1 (position.getDefaultValue()));
    pressureAttachment.setValueAsCompleteGesture (pressure.convertFrom0to1 (pressure.getDefaultValue()));
}

bool BowPad::keyPressed (const juce::KeyPress& key)
{
    const auto step = key.getModifiers().isShiftDown() ? 0.01f : 0.05f;
    auto nx = x, ny = y;
    if (key.isKeyCode (juce::KeyPress::leftKey))
        nx -= step;
    else if (key.isKeyCode (juce::KeyPress::rightKey))
        nx += step;
    else if (key.isKeyCode (juce::KeyPress::upKey))
        ny += step;
    else if (key.isKeyCode (juce::KeyPress::downKey))
        ny -= step;
    else
        return false;

    positionAttachment.setValueAsCompleteGesture (position.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, nx)));
    pressureAttachment.setValueAsCompleteGesture (pressure.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, ny)));
    return true;
}

//==============================================================================
StringDisplay::StringDisplay (ViolinSynthProcessor& p)
    : processor (p)
{
    setTitle ("Strings");
    setDescription ("Shows the note each string is playing and how loudly it sounds.");
    setAccessible (true);
    timerCallback();
    startTimerHz (30);
}

void StringDisplay::timerCallback()
{
    bool changed = false;
    for (int s = 0; s < 4; ++s)
    {
        const auto& state = processor.getStringState (s);
        const auto note = state.note.load();
        const auto level = state.level.load();
        const auto speed = state.bowSpeed.load();
        changed = changed || note != notes[static_cast<std::size_t> (s)]
            || std::abs (level - levels[static_cast<std::size_t> (s)]) > 1.0e-4f
            || std::abs (speed - speeds[static_cast<std::size_t> (s)]) > 1.0e-3f;
        notes[static_cast<std::size_t> (s)] = note;
        levels[static_cast<std::size_t> (s)] = level;
        speeds[static_cast<std::size_t> (s)] = speed;
    }

    if (changed)
    {
        juce::StringArray playing;
        for (int s = 0; s < 4; ++s)
            if (notes[static_cast<std::size_t> (s)] >= 0)
                playing.add (juce::String (engine::strings[static_cast<std::size_t> (s)].name) + " string "
                             + juce::MidiMessage::getMidiNoteName (notes[static_cast<std::size_t> (s)], true, true, 4));
        setDescription (playing.isEmpty() ? "No notes playing" : "Playing: " + playing.joinIntoString (", "));
        repaint();
    }
}

void StringDisplay::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    const auto rowHeight = bounds.getHeight() / 4.0f;
    constexpr float thickness[] { 3.2f, 2.5f, 1.9f, 1.3f };

    // Highest string at the top, as seen by the player.
    for (int row = 0; row < 4; ++row)
    {
        const auto s = 3 - row;
        auto area = bounds.removeFromTop (rowHeight).reduced (0.0f, 4.0f);
        const auto note = notes[static_cast<std::size_t> (s)];
        const auto levelDb = juce::Decibels::gainToDecibels (levels[static_cast<std::size_t> (s)], -60.0f);
        const auto meter = juce::jlimit (0.0f, 1.0f, (levelDb + 50.0f) / 50.0f);
        const auto& spec = engine::strings[static_cast<std::size_t> (s)];

        // String name
        auto badge = area.removeFromLeft (area.getHeight()).reduced (4.0f);
        g.setColour (note >= 0 ? colours::accent : colours::control);
        g.fillEllipse (badge);
        g.setColour (note >= 0 ? colours::background : colours::dimText);
        g.setFont (juce::FontOptions { badge.getHeight() * 0.55f, juce::Font::bold });
        g.drawText (spec.name, badge, juce::Justification::centred);

        // Level meter
        auto meterArea = area.removeFromRight (10.0f).reduced (0.0f, 4.0f);
        g.setColour (colours::track);
        g.fillRoundedRectangle (meterArea, 3.0f);
        g.setColour (colours::accent);
        g.fillRoundedRectangle (meterArea.withTrimmedTop (meterArea.getHeight() * (1.0f - meter)), 3.0f);
        area.removeFromRight (8.0f);
        area.removeFromLeft (6.0f);

        // The string from the nut (left) to the bridge (right); it glows while sounding.
        const auto centreY = area.getCentreY();
        g.setColour (colours::dimText.interpolatedWith (colours::accent, meter));
        g.drawLine (area.getX(), centreY, area.getRight(), centreY, thickness[s] + 1.5f * meter);
        g.setColour (colours::panelEdge);
        g.fillRect (juce::Rectangle<float> (3.0f, area.getHeight() * 0.7f).withCentre ({ area.getX(), centreY }));
        g.fillRect (juce::Rectangle<float> (3.0f, area.getHeight() * 0.7f).withCentre ({ area.getRight(), centreY }));

        if (note >= 0)
        {
            // The finger stops the string at 1 - 2^(-semitones / 12) of its length.
            const auto semitones = static_cast<float> (juce::jmax (0, note - spec.openMidiNote));
            const auto fraction = 1.0f - std::pow (2.0f, -semitones / 12.0f);
            const auto fingerX = area.getX() + fraction * area.getWidth();
            g.setColour (colours::text);
            g.fillEllipse (juce::Rectangle<float> (11.0f, 11.0f).withCentre ({ fingerX, centreY }));

            g.setFont (juce::FontOptions { 13.0f, juce::Font::bold });
            const auto label
                = semitones == 0.0f ? juce::String ("open") : juce::MidiMessage::getMidiNoteName (note, true, true, 4);
            const auto labelArea
                = juce::Rectangle<float> (60.0f, 16.0f)
                      .withCentre (
                          { juce::jlimit (area.getX() + 30.0f, area.getRight() - 30.0f, fingerX), centreY - 13.0f });
            g.drawText (label, labelArea, juce::Justification::centred);
        }
    }
}

//==============================================================================
PresetBar::PresetBar (PresetManager& m)
    : manager (m)
{
    previous.setTitle ("Previous preset");
    next.setTitle ("Next preset");
    name.setTitle ("Preset");
    name.setTooltip ("Choose a preset");
    save.setTooltip ("Save the current settings as a user preset");
    remove.setTooltip ("Delete the current user preset");

    previous.onClick = [this]
    {
        manager.loadPrevious();
        refreshName();
    };
    next.onClick = [this]
    {
        manager.loadNext();
        refreshName();
    };
    name.onClick = [this] { showMenu(); };
    save.onClick = [this] { showSaveDialog(); };
    remove.onClick = [this] { confirmDelete(); };

    for (auto* b : { &previous, &name, &next, &save, &remove })
        addAndMakeVisible (b);

    refreshName();
    startTimerHz (8);
}

PresetBar::~PresetBar()
{
    if (dialog != nullptr)
        dialog->exitModalState (0);
}

void PresetBar::resized()
{
    auto area = getLocalBounds();
    const auto h = area.getHeight();
    remove.setBounds (area.removeFromRight (70));
    area.removeFromRight (6);
    save.setBounds (area.removeFromRight (62));
    area.removeFromRight (10);
    previous.setBounds (area.removeFromLeft (h));
    area.removeFromLeft (4);
    next.setBounds (area.removeFromRight (h));
    area.removeFromRight (4);
    name.setBounds (area);
}

void PresetBar::timerCallback()
{
    refreshName();
}

void PresetBar::refreshName()
{
    const auto index = manager.getCurrentIndex();
    auto text = index >= 0 ? manager.getCurrentName() : juce::String ("Custom settings");
    if (index >= 0 && manager.isModified())
        text += " *";

    if (text != shownName)
    {
        shownName = text;
        name.setButtonText (text);
        name.setDescription (index >= 0 ? manager.getPresets()[static_cast<std::size_t> (index)].description
                                        : juce::String());
        name.setTooltip (name.getDescription().isEmpty() ? juce::String ("Choose a preset") : name.getDescription());
    }

    remove.setEnabled (index >= 0 && ! manager.getPresets()[static_cast<std::size_t> (index)].factory);
}

void PresetBar::showMenu()
{
    constexpr int saveId = 100000, folderId = 100001, refreshId = 100002;
    manager.refresh();

    juce::PopupMenu menu;
    const auto& list = manager.getPresets();
    for (const auto* category : presets::categories)
    {
        juce::PopupMenu sub;
        for (std::size_t i = 0; i < list.size(); ++i)
            if (list[i].factory && list[i].category == category)
                sub.addItem (static_cast<int> (i) + 1,
                             list[i].name,
                             true,
                             static_cast<int> (i) == manager.getCurrentIndex());
        menu.addSubMenu (category, sub);
    }

    // User presets, by their category.
    juce::StringArray userCategories;
    for (const auto& p : list)
        if (! p.factory)
            userCategories.addIfNotAlreadyThere (p.category);

    if (! userCategories.isEmpty())
    {
        menu.addSeparator();
        menu.addSectionHeader ("Your presets");
        for (const auto& category : userCategories)
        {
            juce::PopupMenu sub;
            for (std::size_t i = 0; i < list.size(); ++i)
                if (! list[i].factory && list[i].category == category)
                    sub.addItem (static_cast<int> (i) + 1,
                                 list[i].name,
                                 true,
                                 static_cast<int> (i) == manager.getCurrentIndex());
            menu.addSubMenu (category, sub);
        }
    }

    menu.addSeparator();
    menu.addItem (saveId, "Save preset...");
    menu.addItem (folderId, "Show preset folder");
    menu.addItem (refreshId, "Rescan presets");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&name),
                        [safe = juce::Component::SafePointer<PresetBar> (this)] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;
                            if (result == saveId)
                                safe->showSaveDialog();
                            else if (result == folderId)
                            {
                                const auto folder = safe->manager.getUserFolder();
                                folder.createDirectory();
                                folder.revealToUser();
                            }
                            else if (result == refreshId)
                                safe->manager.refresh();
                            else
                                safe->manager.load (result - 1);
                            safe->refreshName();
                        });
}

void PresetBar::showSaveDialog()
{
    if (dialog != nullptr && dialog->isCurrentlyModal())
        return;

    const auto index = manager.getCurrentIndex();
    const auto& list = manager.getPresets();
    const auto isUser = index >= 0 && ! list[static_cast<std::size_t> (index)].factory;

    dialog = std::make_unique<juce::AlertWindow> ("Save preset",
                                                  "Saved presets appear under \"Your presets\" in the menu.",
                                                  juce::MessageBoxIconType::NoIcon,
                                                  this);
    dialog->addTextEditor ("name", isUser ? manager.getCurrentName() : juce::String(), "Name");
    dialog->addTextEditor ("category", isUser ? list[static_cast<std::size_t> (index)].category : "User", "Category");
    dialog->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    dialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    dialog->enterModalState (
        true,
        juce::ModalCallbackFunction::create (
            [safe = juce::Component::SafePointer<PresetBar> (this)] (int result)
            {
                if (safe == nullptr || result != 1 || safe->dialog == nullptr)
                    return;
                const auto saved = safe->manager.saveUserPreset (safe->dialog->getTextEditorContents ("name"),
                                                                 safe->dialog->getTextEditorContents ("category"));
                if (saved.failed())
                    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                                      .withIconType (juce::MessageBoxIconType::WarningIcon)
                                                      .withTitle ("Preset not saved")
                                                      .withMessage (saved.getErrorMessage())
                                                      .withButton ("OK"),
                                                  nullptr);
                safe->refreshName();
            }),
        false);
}

void PresetBar::confirmDelete()
{
    const auto index = manager.getCurrentIndex();
    if (index < 0 || manager.getPresets()[static_cast<std::size_t> (index)].factory)
        return;

    juce::AlertWindow::showAsync (
        juce::MessageBoxOptions()
            .withIconType (juce::MessageBoxIconType::QuestionIcon)
            .withTitle ("Delete preset")
            .withMessage ("Delete \"" + manager.getCurrentName() + "\"? This can't be undone.")
            .withButton ("Delete")
            .withButton ("Cancel")
            .withAssociatedComponent (this),
        [safe = juce::Component::SafePointer<PresetBar> (this), index] (int result)
        {
            if (safe == nullptr || result != 1)
                return;
            safe->manager.deleteUserPreset (index);
            safe->refreshName();
        });
}
} // namespace violinsynth
