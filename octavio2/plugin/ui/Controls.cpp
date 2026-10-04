#include "Controls.h"

namespace octavio2::ui
{
LookAndFeel::LookAndFeel()
{
    setColour (juce::PopupMenu::backgroundColourId, colours::panel2);
    setColour (juce::PopupMenu::textColourId, colours::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::amber);
    setColour (juce::PopupMenu::highlightedTextColourId, colours::bg);
    setColour (juce::ComboBox::textColourId, colours::text);
    setColour (juce::ComboBox::backgroundColourId, colours::panel2);
    setColour (juce::ComboBox::outlineColourId, colours::line);
    setColour (juce::ComboBox::arrowColourId, colours::muted);
    setColour (juce::TooltipWindow::textColourId, colours::text);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g,
                                    int x,
                                    int y,
                                    int w,
                                    int h,
                                    float pos,
                                    float,
                                    float,
                                    juce::Slider& s)
{
    const auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const auto colour = juce::Colour (
        (juce::uint32) (juce::int64) s.getProperties().getWithDefault ("colour",
                                                                       (juce::int64) colours::amber.getARGB()));
    drawKnob (g,
              r.getCentre(),
              std::min (r.getWidth(), r.getHeight()) / 2 - 4,
              pos,
              s.isEnabled() ? colour : colours::dim);
}

void LookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (colours::panel2);
    g.fillRoundedRectangle (r, 7);
    g.setColour (colours::line);
    g.drawRoundedRectangle (r, 7, 1);
    juce::Path p;
    const float cx = (float) w - 20, cy = (float) h / 2 - 3;
    p.startNewSubPath (cx, cy);
    p.lineTo (cx + 5, cy + 6);
    p.lineTo (cx + 10, cy);
    g.setColour (box.isEnabled() ? colours::muted : colours::dim);
    g.strokePath (p, juce::PathStrokeType (1.5f));
}

void LookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (8, 1, box.getWidth() - 34, box.getHeight() - 2);
    label.setFont (Fonts::sans (13));
    label.setColour (juce::Label::textColourId, box.isEnabled() ? colours::text : colours::muted);
}

juce::Rectangle<int>
LookAndFeel::getTooltipBounds (const juce::String& tip, juce::Point<int> pos, juce::Rectangle<int> area)
{
    const int w
        = std::min (360, juce::roundToInt (juce::GlyphArrangement::getStringWidth (Fonts::sans (12), tip)) + 20);
    const int lines
        = juce::roundToInt (std::ceil (juce::GlyphArrangement::getStringWidth (Fonts::sans (12), tip) / 340.0f));
    return juce::Rectangle<int> (pos.x + 12, pos.y + 16, w, 10 + 17 * std::max (1, lines)).constrainedWithin (area);
}

void LookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& tip, int w, int h)
{
    g.fillAll (colours::panel2);
    g.setColour (colours::line);
    g.drawRect (0, 0, w, h);
    g.setColour (colours::text);
    g.setFont (Fonts::sans (12));
    g.drawFittedText (tip, 10, 4, w - 20, h - 8, juce::Justification::centredLeft, 6);
}

void showParameterMenu (juce::Component& control, juce::RangedAudioParameter& param)
{
    if (auto* editor = control.findParentComponentOfClass<juce::AudioProcessorEditor>())
        if (auto* context = editor->getHostContext())
            if (auto menu = context->getContextMenuForParameter (&param))
            {
                menu->showNativeMenu (editor->getMouseXYRelative());
                return;
            }
    juce::PopupMenu menu;
    menu.addItem ("Reset to default",
                  [&param]
                  {
                      param.beginChangeGesture();
                      param.setValueNotifyingHost (param.getDefaultValue());
                      param.endChangeGesture();
                  });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&control).withMousePosition());
}

//==============================================================================
void Knob::Dial::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && onMenu)
        onMenu();
    else
        juce::Slider::mouseDown (e);
}
void Knob::Dial::mouseDrag (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu())
        juce::Slider::mouseDrag (e);
}
void Knob::Dial::mouseUp (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu())
        juce::Slider::mouseUp (e);
}

Knob::Knob (juce::AudioProcessorValueTreeState* state,
            const juce::String& paramID,
            const juce::String& l,
            Style s,
            std::function<juce::String (float)> vt)
    : label (l),
      style (s),
      valueText (std::move (vt))
{
    if (state != nullptr && (param = state->getParameter (paramID)) != nullptr)
    {
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (*state, paramID, dial);
        dial.onMenu = [this] { showParameterMenu (dial, *param); };
        dial.onValueChange = [this] { repaint(); };
        dial.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
        dial.setTitle (l);
        addAndMakeVisible (dial);
    }
}

void Knob::setPreview (float value, const juce::String& text, const juce::String& when)
{
    previewValue = value;
    previewText = text;
    setTooltip (label + " arrives with " + when + ".");
}

juce::Rectangle<float> Knob::dialArea() const
{
    const auto b = getLocalBounds().toFloat();
    if (style == Style::card)
        return juce::Rectangle<float> (b.getCentreX() - 46, 92 - 46, 92, 92);
    return juce::Rectangle<float> (b.getCentreX() - 26, 44 - 26, 52, 52);
}

juce::Point<float> Knob::badgeCentre() const
{
    const auto b = getLocalBounds().toFloat();
    return style == Style::card ? juce::Point<float> (b.getRight() - 20, 20)
                                : juce::Point<float> (b.getCentreX() + 28, 6);
}

void Knob::resized()
{
    dial.getProperties().set ("colour", (juce::int64) colour.getARGB());
    dial.setBounds (dialArea().toNearestInt());
}

void Knob::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    juce::String value = previewText;
    if (param != nullptr)
    {
        const float v = (float) dial.getValue();
        value = valueText ? valueText (v) : param->getCurrentValueAsText();
    }
    const bool preview = param == nullptr;
    if (style == Style::card)
    {
        g.setColour (colours::panel);
        g.fillRoundedRectangle (b, 10);
        g.setColour (colours::line);
        g.drawRoundedRectangle (b.reduced (0.5f), 10, 1);
        drawText (g,
                  label.toUpperCase(),
                  14,
                  24,
                  Fonts::sans (10, true).withExtraKerningFactor (0.08f),
                  preview ? colours::dim : colours::amber);
        if (badge)
            drawBadge (g, badgeCentre(), *badge);
        drawText (g,
                  value,
                  b.getCentreX(),
                  152,
                  Fonts::mono (13),
                  preview ? colours::muted : colours::text,
                  juce::Justification::horizontallyCentred);
        drawText (g,
                  caption,
                  b.getCentreX(),
                  170,
                  Fonts::sans (10.5f),
                  colours::dim,
                  juce::Justification::horizontallyCentred);
    }
    else
    {
        drawText (g,
                  label,
                  b.getCentreX(),
                  10,
                  Fonts::sans (11),
                  colours::muted,
                  juce::Justification::horizontallyCentred);
        drawText (g,
                  value,
                  b.getCentreX(),
                  84,
                  Fonts::mono (12),
                  preview ? colours::muted : colours::text,
                  juce::Justification::horizontallyCentred);
        if (badge)
            drawBadge (g, badgeCentre(), *badge);
    }
    if (preview)
    {
        const auto d = dialArea();
        drawKnob (g, d.getCentre(), d.getWidth() / 2 - 4, previewValue, colours::dim);
    }
}

//==============================================================================
Choices::Choices (juce::AudioProcessorValueTreeState* state, const juce::String& paramID, std::vector<Item> it)
    : items (std::move (it))
{
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i].value < 0)
            items[i].value = (int) i;
    if (state != nullptr && (param = state->getParameter (paramID)) != nullptr)
        attachment = std::make_unique<juce::ParameterAttachment> (*param,
                                                                  [this] (float v)
                                                                  {
                                                                      current = juce::roundToInt (v);
                                                                      if (onChange)
                                                                          onChange (current);
                                                                      repaint();
                                                                  });
    if (attachment)
        attachment->sendInitialUpdate();
}

void Choices::setLayout (int c, float h, float g, float w)
{
    columns = c;
    itemHeight = h;
    gap = g;
    itemWidth = w;
}

int Choices::active() const
{
    return param != nullptr ? current : previewActive;
}

juce::Rectangle<float> Choices::itemBounds (int i) const
{
    if (columns <= 0)
    {
        float x = 0;
        for (int k = 0; k < i; ++k)
            x += (itemWidth > 0
                      ? itemWidth
                      : juce::GlyphArrangement::getStringWidth (Fonts::sans (12), items[(size_t) k].name) + 24)
                + gap;
        const float w = itemWidth > 0
            ? itemWidth
            : juce::GlyphArrangement::getStringWidth (Fonts::sans (12), items[(size_t) i].name) + 24;
        return { x, 0, w, itemHeight };
    }
    const float w = (getWidth() - gap * (columns - 1)) / columns;
    return { (i % columns) * (w + gap), (i / columns) * (itemHeight + gap * 1.5f), w, itemHeight };
}

void Choices::paint (juce::Graphics& g)
{
    const int on = active();
    for (size_t i = 0; i < items.size(); ++i)
    {
        const auto r = itemBounds ((int) i);
        const auto& it = items[i];
        const bool isOn = it.value == on;
        const bool live = param != nullptr && it.enabled;
        if (isOn)
        {
            g.setColour (live ? colours::amber : colours::amber.withAlpha (0.55f));
            g.fillRoundedRectangle (r, 6);
        }
        else
        {
            g.setColour (colours::panel2);
            g.fillRoundedRectangle (r, 6);
            g.setColour (colours::line);
            g.drawRoundedRectangle (r.reduced (0.5f), 6, 1);
        }
        const auto textColour = isOn ? colours::bg : (live ? colours::text : colours::dim);
        const float baseline = r.getCentreY() + 4.5f;
        if (it.note.isNotEmpty())
        {
            drawText (g, it.name, r.getX() + 14, baseline, Fonts::sans (13, isOn), textColour);
            drawText (g,
                      it.note,
                      r.getRight() - 14,
                      baseline,
                      Fonts::sans (11.5f),
                      isOn ? colours::bg : colours::dim,
                      juce::Justification::right);
        }
        else if (columns > 0)
            drawText (g, it.name, r.getX() + 12, baseline, Fonts::sans (12, isOn), textColour);
        else
            drawText (g,
                      it.name,
                      r.getCentreX(),
                      baseline,
                      Fonts::sans (12, isOn),
                      textColour,
                      juce::Justification::horizontallyCentred);
    }
}

void Choices::mouseMove (const juce::MouseEvent& e)
{
    for (size_t i = 0; i < items.size(); ++i)
        if (itemBounds ((int) i).contains (e.position))
        {
            const bool live = param != nullptr && items[i].enabled;
            setMouseCursor (live ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
            return;
        }
    setMouseCursor (juce::MouseCursor::NormalCursor);
}

void Choices::mouseUp (const juce::MouseEvent& e)
{
    if (param == nullptr)
        return;
    if (e.mods.isPopupMenu())
    {
        showParameterMenu (*this, *param);
        return;
    }
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i].enabled && itemBounds ((int) i).contains (e.position))
            attachment->setValueAsCompleteGesture ((float) items[i].value);
}

//==============================================================================
PreviewButton::PreviewButton (const juce::String& t, const juce::String& when, bool f)
    : text (t),
      filled (f)
{
    setTooltip (t.trimCharactersAtStart (juce::String::fromUTF8 ("⠿↻◉ ")) + " arrives with " + when + ".");
}

void PreviewButton::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (filled ? colours::amber.withAlpha (0.35f) : colours::panel2);
    g.fillRoundedRectangle (r, 7);
    g.setColour (colours::line);
    g.drawRoundedRectangle (r, 7, 1);
    drawText (g,
              text,
              r.getCentreX(),
              r.getCentreY() + 4.5f,
              Fonts::sans (12.5f, filled),
              filled ? colours::text.withAlpha (0.6f) : colours::dim,
              juce::Justification::horizontallyCentred);
}
} // namespace octavio2::ui
