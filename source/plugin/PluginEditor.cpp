#include "plugin/PluginEditor.h"

#include "plugin/Parameters.h"
#include "plugin/PluginProcessor.h"

namespace violinsynth
{
namespace
{
constexpr int defaultWidth = 900;
constexpr int defaultHeight = 540;
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
    const auto makeSection = [] (const juce::String& title, int row)
    {
        Section section;
        section.title = title;
        section.row = row;
        return section;
    };
    auto bow = makeSection ("Bow", 0);
    bow.knobs = { &addKnob (id::bowPosition, "Position"),
                  &addKnob (id::bowPressure, "Pressure"),
                  &addKnob (id::attack, "Attack"),
                  &addKnob (id::release, "Release") };
    auto pitch = makeSection ("Pitch", 0);
    pitch.knobs = { &addKnob (id::vibratoRate, "Vib Rate"),
                    &addKnob (id::vibratoDepth, "Vib Depth"),
                    &addKnob (id::vibratoDelay, "Vib Delay"),
                    &addKnob (id::portamento, "Glide"),
                    &addKnob (id::bendRange, "Bend") };
    auto articulation = makeSection ("Articulation", 0);
    articulation.choices = { &addChoice (id::articulation, "Default") };
    articulation.extras = { &articulationStatus, &keyswitchHint };
    articulationStatus.setColour (juce::Label::textColourId, text);
    articulationStatus.setFont (juce::FontOptions { 14.0f, juce::Font::bold });
    keyswitchHint.setText ("Keyswitches C1-A1", juce::dontSendNotification);
    keyswitchHint.setTooltip ("MIDI notes 24-33 (C2-A2 in FL Studio) select the articulation");
    keyswitchHint.setFont (juce::FontOptions { 11.0f });
    keyswitchHint.setMinimumHorizontalScale (0.7f);
    addAndMakeVisible (articulationStatus);
    addAndMakeVisible (keyswitchHint);

    auto play = makeSection ("Play", 1);
    play.choices = { &addChoice (id::playMode, "Mode") };
    play.toggles = { &addToggle (id::autoBowChange, "Auto bow change"), &addToggle (id::mpe, "MPE") };
    play.knobs = { &addKnob (id::resonance, "Resonance"),
                   &addKnob (id::humanise, "Humanise"),
                   &addKnob (id::mpeBendRange, "MPE Bend") };
    auto body = makeSection ("Body", 1);
    body.choices = { &addChoice (id::body, "Violin"), &addChoice (id::bodyQuality, "Quality") };
    body.knobs = { &addKnob (id::sordino, "Mute") };
    auto output = makeSection ("Output", 1);
    output.knobs = { &addKnob (id::width, "Width"), &addKnob (id::room, "Room"), &addKnob (id::outputGain, "Gain") };
    sections = { bow, pitch, articulation, play, body, output };

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
    setResizeLimits (760, 480, 1800, 1100);
    setSize (defaultWidth, defaultHeight);

    timerCallback();
    startTimerHz (10);
}

ViolinSynthEditor::~ViolinSynthEditor()
{
    setLookAndFeel (nullptr);
}

void ViolinSynthEditor::timerCallback()
{
    const auto active = static_cast<int> (processor.getActiveArticulation());
    if (active != shownArticulation)
    {
        shownArticulation = active;
        articulationStatus.setText (juce::String ("Playing: ")
                                        + engine::articulationNames[static_cast<std::size_t> (active)],
                                    juce::dontSendNotification);
    }
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

ViolinSynthEditor::Toggle& ViolinSynthEditor::addToggle (const juce::ParameterID& id, const juce::String& labelText)
{
    auto toggle = std::make_unique<Toggle>();
    toggle->button.setButtonText (labelText);
    toggle->button.setColour (juce::ToggleButton::textColourId, text);
    toggle->button.setColour (juce::ToggleButton::tickColourId, accent);
    toggle->attachment
        = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.getParameters(),
                                                                                  id.getParamID(),
                                                                                  toggle->button);
    addAndMakeVisible (toggle->button);
    toggles.push_back (std::move (toggle));
    return *toggles.back();
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

    constexpr int gap = 10;
    const auto rowHeight = (bounds.getHeight() - gap) / 2;

    for (int row = 0; row < 2; ++row)
    {
        auto rowBounds = bounds.removeFromTop (rowHeight);
        bounds.removeFromTop (gap);

        // Section widths proportional to their number of controls.
        int units = 0, count = 0;
        for (const auto& s : sections)
            if (s.row == row)
            {
                units += s.units();
                ++count;
            }
        const auto unitWidth = (rowBounds.getWidth() - gap * (count - 1)) / std::max (1, units);

        for (auto& s : sections)
        {
            if (s.row != row)
                continue;

            s.bounds = rowBounds.removeFromLeft (unitWidth * s.units());
            rowBounds.removeFromLeft (gap);

            auto inner = s.bounds.reduced (8, 6);
            inner.removeFromTop (22);

            if (! s.choices.empty() || ! s.toggles.empty())
            {
                // The section's side margins come out of this column, so the
                // knobs next to it are as wide as everywhere else.
                auto column = inner.removeFromLeft (unitWidth * 2 - 16).reduced (4, 0);
                for (auto* c : s.choices)
                {
                    c->label.setBounds (column.removeFromTop (18));
                    c->box.setBounds (column.removeFromTop (26));
                    column.removeFromTop (8);
                }
                for (auto* t : s.toggles)
                    t->button.setBounds (column.removeFromTop (26));
                for (auto* e : s.extras)
                    e->setBounds (column.removeFromTop (22));
            }

            const auto knobWidth = s.knobs.empty() ? 0 : inner.getWidth() / static_cast<int> (s.knobs.size());
            const auto knobHeight = std::min (inner.getHeight(), knobWidth + 40);
            auto knobRow = inner.withSizeKeepingCentre (inner.getWidth(), knobHeight);
            for (auto* k : s.knobs)
            {
                auto area = knobRow.removeFromLeft (knobWidth);
                k->label.setBounds (area.removeFromTop (18));
                k->slider.setBounds (area.reduced (2, 0));
            }
        }
    }
}
} // namespace violinsynth
