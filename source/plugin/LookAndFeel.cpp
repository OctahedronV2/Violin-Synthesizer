#include "plugin/LookAndFeel.h"

namespace violinsynth
{
ViolinLookAndFeel::ViolinLookAndFeel()
{
    using namespace colours;
    setColour (juce::ResizableWindow::backgroundColourId, background);
    setColour (juce::Slider::rotarySliderFillColourId, accent);
    setColour (juce::Slider::rotarySliderOutlineColourId, track);
    setColour (juce::Slider::thumbColourId, text);
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, accentDim);
    setColour (juce::Label::textColourId, dimText);
    setColour (juce::Label::textWhenEditingColourId, text);
    setColour (juce::TextEditor::backgroundColourId, control);
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::TextEditor::outlineColourId, panelEdge);
    setColour (juce::TextEditor::focusedOutlineColourId, accent);
    setColour (juce::ComboBox::backgroundColourId, control);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::outlineColourId, panelEdge);
    setColour (juce::ComboBox::arrowColourId, accent);
    setColour (juce::ComboBox::focusedOutlineColourId, accent);
    setColour (juce::PopupMenu::backgroundColourId, panel);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::headerTextColourId, accent);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accentDim);
    setColour (juce::PopupMenu::highlightedTextColourId, text);
    setColour (juce::TextButton::buttonColourId, control);
    setColour (juce::TextButton::buttonOnColourId, accent);
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, background);
    setColour (juce::ToggleButton::textColourId, text);
    setColour (juce::ToggleButton::tickColourId, accent);
    setColour (juce::AlertWindow::backgroundColourId, panel);
    setColour (juce::AlertWindow::textColourId, text);
    setColour (juce::AlertWindow::outlineColourId, panelEdge);
    setColour (juce::TooltipWindow::backgroundColourId, panel);
    setColour (juce::TooltipWindow::textColourId, text);
    setColour (juce::TooltipWindow::outlineColourId, panelEdge);
}

void ViolinLookAndFeel::drawRotarySlider (juce::Graphics& g,
                                          int x,
                                          int y,
                                          int width,
                                          int height,
                                          float sliderPos,
                                          float startAngle,
                                          float endAngle,
                                          juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const auto thickness = juce::jmax (2.5f, radius * 0.16f);
    const auto arcRadius = radius - thickness * 0.5f;
    const auto angle = startAngle + sliderPos * (endAngle - startAngle);
    const auto enabled = slider.isEnabled();

    juce::Path trackArc;
    trackArc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (colours::track);
    g.strokePath (trackArc, { thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });

    if (sliderPos > 0.0f)
    {
        juce::Path valueArc;
        valueArc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, angle, true);
        g.setColour (enabled ? colours::accent : colours::accentDim);
        g.strokePath (valueArc, { thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });
    }

    // Knob body and pointer.
    const auto bodyRadius = arcRadius - thickness * 1.1f;
    g.setColour (colours::control);
    g.fillEllipse (juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre));
    const auto tip = centre.getPointOnCircumference (bodyRadius * 0.8f, angle);
    const auto base = centre.getPointOnCircumference (bodyRadius * 0.25f, angle);
    g.setColour (colours::text);
    g.drawLine ({ base, tip }, juce::jmax (1.5f, thickness * 0.55f));

    if (slider.hasKeyboardFocus (true))
    {
        g.setColour (colours::accent.withAlpha (0.5f));
        g.drawEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre), 1.0f);
    }
}

void ViolinLookAndFeel::drawComboBox (juce::Graphics& g,
                                      int width,
                                      int height,
                                      bool,
                                      int,
                                      int,
                                      int,
                                      int,
                                      juce::ComboBox& box)
{
    auto bounds = juce::Rectangle<int> (width, height).toFloat().reduced (0.5f);
    const auto corner = 5.0f;
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (bounds, corner);
    g.setColour (box.hasKeyboardFocus (true) ? colours::accent : colours::panelEdge);
    g.drawRoundedRectangle (bounds, corner, 1.0f);

    const auto arrowZone
        = bounds.removeFromRight (static_cast<float> (height)).reduced (static_cast<float> (height) * 0.35f);
    juce::Path arrow;
    arrow.startNewSubPath (arrowZone.getX(), arrowZone.getY() + arrowZone.getHeight() * 0.3f);
    arrow.lineTo (arrowZone.getCentreX(), arrowZone.getBottom() - arrowZone.getHeight() * 0.2f);
    arrow.lineTo (arrowZone.getRight(), arrowZone.getY() + arrowZone.getHeight() * 0.3f);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId).withAlpha (box.isEnabled() ? 1.0f : 0.3f));
    g.strokePath (arrow, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void ViolinLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (8, 1, box.getWidth() - box.getHeight() - 6, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

juce::Font ViolinLookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    return juce::FontOptions { juce::jmin (15.0f, static_cast<float> (box.getHeight()) * 0.55f) };
}

void ViolinLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool)
{
    const auto height = static_cast<float> (button.getHeight());
    const auto boxSize = juce::jmin (16.0f, height * 0.7f);
    const auto box = juce::Rectangle<float> (2.0f, (height - boxSize) * 0.5f, boxSize, boxSize);

    g.setColour (button.getToggleState() ? colours::accent : colours::control);
    g.fillRoundedRectangle (box, 3.0f);
    g.setColour (highlighted || button.hasKeyboardFocus (true) ? colours::accent : colours::panelEdge);
    g.drawRoundedRectangle (box, 3.0f, 1.0f);

    if (button.getToggleState())
    {
        juce::Path tick;
        tick.startNewSubPath (box.getX() + boxSize * 0.22f, box.getCentreY());
        tick.lineTo (box.getX() + boxSize * 0.43f, box.getBottom() - boxSize * 0.25f);
        tick.lineTo (box.getRight() - boxSize * 0.2f, box.getY() + boxSize * 0.25f);
        g.setColour (colours::background);
        g.strokePath (tick, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    g.setColour (colours::text.withAlpha (button.isEnabled() ? 1.0f : 0.4f));
    g.setFont (juce::FontOptions { juce::jmin (14.0f, height * 0.6f) });
    g.drawFittedText (button.getButtonText(),
                      button.getLocalBounds().withTrimmedLeft (static_cast<int> (boxSize) + 8),
                      juce::Justification::centredLeft,
                      1);
}

void ViolinLookAndFeel::drawButtonBackground (juce::Graphics& g,
                                              juce::Button& button,
                                              const juce::Colour& backgroundColour,
                                              bool highlighted,
                                              bool down)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    auto fill = button.getToggleState() ? button.findColour (juce::TextButton::buttonOnColourId) : backgroundColour;
    if (down)
        fill = fill.darker (0.2f);
    else if (highlighted)
        fill = fill.brighter (0.12f);

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, 5.0f);
    g.setColour (button.hasKeyboardFocus (true) ? colours::accent : colours::panelEdge);
    g.drawRoundedRectangle (bounds, 5.0f, 1.0f);
}

juce::Font ViolinLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return juce::FontOptions { juce::jmin (14.0f, static_cast<float> (buttonHeight) * 0.5f) };
}

juce::Label* ViolinLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* label = LookAndFeel_V4::createSliderTextBox (slider);
    label->setFont (juce::FontOptions { 13.0f });
    label->setColour (juce::Label::textColourId, colours::text);
    label->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    label->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    return label;
}

void ViolinLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    g.fillAll (label.findColour (juce::Label::backgroundColourId));
    if (label.isBeingEdited())
        return;

    // Value readouts under knobs get a subtle rounded field instead of a box.
    if (dynamic_cast<juce::Slider*> (label.getParentComponent()) != nullptr)
    {
        g.setColour (colours::track.withAlpha (0.6f));
        g.fillRoundedRectangle (label.getLocalBounds().toFloat().reduced (4.0f, 1.0f), 4.0f);
    }

    g.setColour (label.findColour (juce::Label::textColourId).withMultipliedAlpha (label.isEnabled() ? 1.0f : 0.5f));
    g.setFont (getLabelFont (label));
    g.drawFittedText (label.getText(),
                      label.getBorderSize().subtractedFrom (label.getLocalBounds()),
                      label.getJustificationType(),
                      1,
                      label.getMinimumHorizontalScale());
}

juce::Font ViolinLookAndFeel::getPopupMenuFont()
{
    return juce::FontOptions { 15.0f };
}
} // namespace violinsynth
