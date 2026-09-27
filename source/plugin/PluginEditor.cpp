#include "plugin/PluginEditor.h"

#include "plugin/Parameters.h"
#include "plugin/PluginProcessor.h"

namespace violinsynth
{
namespace
{
constexpr int defaultWidth = 960;
constexpr int defaultHeight = 360;
constexpr int headerHeight = 64;
constexpr int keyboardHeight = 80;
constexpr int creditsHeight = 18;

const juce::Colour background { 0xff1e1b18 };
const juce::Colour panel { 0xff2a2521 };
const juce::Colour accent { 0xffc8894a };
const juce::Colour text { 0xffe8d9c0 };
const juce::Colour dimText { 0xff9c8f7a };
} // namespace

ViolinSynthEditor::ViolinSynthEditor (ViolinSynthProcessor& owner)
    : AudioProcessorEditor (owner),
      processor (owner),
      keyboard (owner.getKeyboardState(), juce::MidiKeyboardComponent::horizontalKeyboard)
{
    lookAndFeel.setColour (juce::Slider::rotarySliderFillColourId, accent);
    lookAndFeel.setColour (juce::Slider::thumbColourId, text);
    lookAndFeel.setColour (juce::Slider::textBoxTextColourId, text);
    lookAndFeel.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    lookAndFeel.setColour (juce::Label::textColourId, dimText);
    lookAndFeel.setColour (juce::ComboBox::backgroundColourId, panel.brighter (0.1f));
    lookAndFeel.setColour (juce::ComboBox::textColourId, text);
    lookAndFeel.setColour (juce::ComboBox::outlineColourId, accent.withAlpha (0.4f));
    setLookAndFeel (&lookAndFeel);

    using namespace params;
    sections.push_back ({ "Bow",
                          { &addKnob (id::bowPosition, "Position"),
                            &addKnob (id::bowPressure, "Pressure"),
                            &addKnob (id::attack, "Attack"),
                            &addKnob (id::release, "Release") },
                          {},
                          {} });
    sections.push_back ({ "Pitch",
                          { &addKnob (id::vibratoRate, "Vib Rate"),
                            &addKnob (id::vibratoDepth, "Vib Depth"),
                            &addKnob (id::vibratoDelay, "Vib Delay"),
                            &addKnob (id::portamento, "Glide"),
                            &addKnob (id::bendRange, "Bend") },
                          {},
                          {} });
    sections.push_back ({ "Body",
                          { &addKnob (id::sordino, "Mute") },
                          { &addChoice (id::body, "Violin"), &addChoice (id::bodyQuality, "Quality") },
                          {} });
    sections.push_back (
        { "Output",
          { &addKnob (id::width, "Width"), &addKnob (id::room, "Room"), &addKnob (id::outputGain, "Gain") },
          {},
          {} });

    credits.setText ("Measured violin bodies: CNSM Dataset (Pauget Ballesteros 2026, CC BY 4.0) and University of "
                     "Iowa Musical Instrument Samples",
                     juce::dontSendNotification);
    credits.setFont (juce::FontOptions { 11.0f });
    credits.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (credits);

    // G3 is the lowest note on a violin.
    keyboard.setAvailableRange (55, 103);
    keyboard.setLowestVisibleKey (55);
    addAndMakeVisible (keyboard);

    setResizable (true, true);
    setResizeLimits (800, 330, 1800, 900);
    setSize (defaultWidth, defaultHeight);
}

ViolinSynthEditor::~ViolinSynthEditor()
{
    setLookAndFeel (nullptr);
}

ViolinSynthEditor::Knob& ViolinSynthEditor::addKnob (const juce::ParameterID& id, const juce::String& labelText)
{
    auto knob = std::make_unique<Knob>();
    knob->label.setText (labelText, juce::dontSendNotification);
    knob->label.setJustificationType (juce::Justification::centred);
    knob->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 18);
    knob->attachment
        = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.getParameters(),
                                                                                  id.getParamID(),
                                                                                  knob->slider);
    addAndMakeVisible (knob->label);
    addAndMakeVisible (knob->slider);
    knobs.push_back (std::move (knob));
    return *knobs.back();
}

ViolinSynthEditor::Choice& ViolinSynthEditor::addChoice (const juce::ParameterID& id, const juce::String& labelText)
{
    auto choice = std::make_unique<Choice>();
    choice->label.setText (labelText, juce::dontSendNotification);
    if (auto* param
        = dynamic_cast<juce::AudioParameterChoice*> (processor.getParameters().getParameter (id.getParamID())))
        choice->box.addItemList (param->choices, 1);
    choice->attachment
        = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.getParameters(),
                                                                                    id.getParamID(),
                                                                                    choice->box);
    addAndMakeVisible (choice->label);
    addAndMakeVisible (choice->box);
    choices.push_back (std::move (choice));
    return *choices.back();
}

void ViolinSynthEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);

    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (20, 10);
    g.setColour (text);
    g.setFont (juce::FontOptions { 26.0f, juce::Font::bold });
    g.drawText ("Violin Synthesizer", header.removeFromTop (30), juce::Justification::centredLeft);
    g.setColour (dimText);
    g.setFont (juce::FontOptions { 13.0f });
    g.drawText ("v" JucePlugin_VersionString "  |  physically modelled bowed string",
                header,
                juce::Justification::centredLeft);

    for (const auto& section : sections)
    {
        g.setColour (panel);
        g.fillRoundedRectangle (section.bounds.toFloat(), 8.0f);
        g.setColour (accent);
        g.setFont (juce::FontOptions { 13.0f, juce::Font::bold });
        g.drawText (section.title.toUpperCase(),
                    section.bounds.reduced (12, 6).removeFromTop (18),
                    juce::Justification::centredLeft);
    }
}

void ViolinSynthEditor::resized()
{
    auto bounds = getLocalBounds();
    keyboard.setBounds (bounds.removeFromBottom (keyboardHeight));
    // Fill the width with the violin's range (G3 to G7: 29 white keys).
    keyboard.setKeyWidth (static_cast<float> (keyboard.getWidth()) / 29.0f);
    credits.setBounds (bounds.removeFromBottom (creditsHeight).reduced (12, 0));
    bounds.removeFromTop (headerHeight);
    bounds = bounds.reduced (12, 6);

    // Section widths proportional to their number of controls.
    int units = 0;
    for (const auto& s : sections)
        units += static_cast<int> (s.knobs.size()) + (s.choices.empty() ? 0 : 2);

    const auto gap = 10;
    const auto unitWidth = (bounds.getWidth() - gap * (static_cast<int> (sections.size()) - 1)) / std::max (1, units);

    for (auto& s : sections)
    {
        const auto sectionUnits = static_cast<int> (s.knobs.size()) + (s.choices.empty() ? 0 : 2);
        s.bounds = bounds.removeFromLeft (unitWidth * sectionUnits);
        bounds.removeFromLeft (gap);

        auto inner = s.bounds.reduced (8, 6);
        inner.removeFromTop (22);

        if (! s.choices.empty())
        {
            // The section's side margins come out of the choice column, so the
            // knobs next to it are as wide as everywhere else.
            auto column = inner.removeFromLeft (unitWidth * 2 - 16).reduced (4, 0);
            for (auto* c : s.choices)
            {
                c->label.setBounds (column.removeFromTop (18));
                c->box.setBounds (column.removeFromTop (26));
                column.removeFromTop (10);
            }
        }

        const auto knobWidth = s.knobs.empty() ? 0 : inner.getWidth() / static_cast<int> (s.knobs.size());
        const auto knobHeight = std::min (inner.getHeight(), knobWidth + 40);
        auto row = inner.withSizeKeepingCentre (inner.getWidth(), knobHeight);
        for (auto* k : s.knobs)
        {
            auto area = row.removeFromLeft (knobWidth);
            k->label.setBounds (area.removeFromTop (18));
            k->slider.setBounds (area.reduced (2, 0));
        }
    }
}
} // namespace violinsynth
