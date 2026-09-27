#include "plugin/PluginEditor.h"

#include "engine/Articulation.h"
#include "plugin/Parameters.h"
#include "plugin/PluginProcessor.h"

namespace violinsynth
{
namespace
{
constexpr int margin = 12;
constexpr int gap = 10;
constexpr int titleHeight = 28;
constexpr int headerHeight = 64;
constexpr int keyboardHeight = 88;
constexpr int creditsHeight = 18;
} // namespace

//==============================================================================
class ViolinSynthEditor::Content final : public juce::Component, private juce::Timer
{
public:
    explicit Content (ViolinSynthProcessor& p)
        : processor (p),
          bowPad (*p.getParameters().getParameter (params::id::bowPosition.getParamID()),
                  *p.getParameters().getParameter (params::id::bowPressure.getParamID())),
          strings (p),
          presetBar (p.getPresetManager()),
          keyboard (p.getKeyboardState(), juce::MidiKeyboardComponent::horizontalKeyboard),
          articulationAttachment (*p.getParameters().getParameter (params::id::articulation.getParamID()),
                                  [] (float) {})
    {
        using namespace params;

        // Header
        addAndMakeVisible (presetBar);

        // Bow
        addAndMakeVisible (bowPad);
        bowKnobs = { &addKnob (id::bowPosition, "Position"),
                     &addKnob (id::bowPressure, "Pressure"),
                     &addKnob (id::attack, "Attack"),
                     &addKnob (id::release, "Release") };

        // Strings
        addAndMakeVisible (strings);

        // Articulation: one button per articulation; the highlighted one is
        // what plays now (from this control or a keyswitch).
        for (int a = 0; a < engine::numArticulations; ++a)
        {
            auto& button = articulationButtons[static_cast<std::size_t> (a)];
            button.setButtonText (displayName (a));
            button.setTitle (juce::String ("Articulation: ") + displayName (a));
            button.setTooltip ("Keyswitch "
                               + juce::MidiMessage::getMidiNoteName (engine::firstKeyswitch + a, true, true, 4)
                               + " (MIDI note " + juce::String (engine::firstKeyswitch + a) + ")");
            button.setRadioGroupId (1);
            button.onClick = [this, a] { articulationAttachment.setValueAsCompleteGesture (static_cast<float> (a)); };
            addAndMakeVisible (button);
        }
        keyswitchHint.setText ("Keyswitches: C1 to A1 (C2 to A2 in FL Studio)", juce::dontSendNotification);
        keyswitchHint.setFont (juce::FontOptions { 12.0f });
        keyswitchHint.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (keyswitchHint);

        // Pitch
        pitchKnobs = { &addKnob (id::vibratoRate, "Vib Rate"),
                       &addKnob (id::vibratoDepth, "Vib Depth"),
                       &addKnob (id::vibratoDelay, "Vib Delay"),
                       &addKnob (id::portamento, "Glide"),
                       &addKnob (id::bendRange, "Bend") };

        // Play
        playChoices = { &addChoice (id::playMode, "Mode") };
        playToggles = { &addToggle (id::autoBowChange, "Auto bow change"), &addToggle (id::mpe, "MPE") };
        playKnobs = { &addKnob (id::resonance, "Resonance"),
                      &addKnob (id::humanise, "Humanise"),
                      &addKnob (id::mpeBendRange, "MPE Bend") };

        // Body and output
        bodyChoices = { &addChoice (id::body, "Violin"), &addChoice (id::bodyQuality, "Quality") };
        bodyKnobs = { &addKnob (id::sordino, "Mute"),
                      &addKnob (id::width, "Width"),
                      &addKnob (id::room, "Room"),
                      &addKnob (id::outputGain, "Gain") };

        credits.setText ("Measured violin bodies: CNSM Dataset (Pauget Ballesteros 2026, CC BY 4.0) and University of "
                         "Iowa Musical Instrument Samples",
                         juce::dontSendNotification);
        credits.setFont (juce::FontOptions { 11.0f });
        credits.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (credits);

        // G3 is the lowest note on a violin.
        keyboard.setAvailableRange (55, 103);
        keyboard.setLowestVisibleKey (55);
        keyboard.setOctaveForMiddleC (4); // C4 = middle C, as in the rest of the editor
        keyboard.setTitle ("Keyboard");
        keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, colours::accent);
        keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, colours::accent.withAlpha (0.3f));
        addAndMakeVisible (keyboard);

        timerCallback();
        startTimerHz (15);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (colours::background);

        auto header = getLocalBounds().removeFromTop (headerHeight).reduced (20, 10);
        g.setColour (colours::text);
        g.setFont (juce::FontOptions { 25.0f, juce::Font::bold });
        g.drawText ("Violin Synthesizer", header.removeFromTop (28), juce::Justification::centredLeft);
        g.setColour (colours::dimText);
        g.setFont (juce::FontOptions { 12.5f });
        g.drawText ("v" JucePlugin_VersionString "  |  physically modelled violin",
                    header,
                    juce::Justification::centredLeft);

        for (const auto& [title, bounds] : panels)
        {
            g.setColour (colours::panel);
            g.fillRoundedRectangle (bounds.toFloat(), 8.0f);
            g.setColour (colours::panelEdge);
            g.drawRoundedRectangle (bounds.toFloat().reduced (0.5f), 8.0f, 1.0f);
            g.setColour (colours::accent);
            g.setFont (juce::FontOptions { 13.0f, juce::Font::bold });
            g.drawText (title.toUpperCase(),
                        bounds.reduced (14, 0).removeFromTop (titleHeight),
                        juce::Justification::centredLeft);
        }

        // Articulation now playing, in the panel's title row.
        g.setColour (colours::text);
        g.setFont (juce::FontOptions { 13.0f, juce::Font::bold });
        g.drawText (playingText, articulationStatusArea, juce::Justification::centredRight);
    }

    void resized() override
    {
        panels.clear();
        auto bounds = getLocalBounds();
        keyboard.setBounds (bounds.removeFromBottom (keyboardHeight));
        keyboard.setKeyWidth (static_cast<float> (keyboard.getWidth()) / 29.0f); // G3 to G7: 29 white keys
        credits.setBounds (bounds.removeFromBottom (creditsHeight).reduced (margin, 0));

        auto header = bounds.removeFromTop (headerHeight);
        presetBar.setBounds (header.withTrimmedLeft (360).withTrimmedRight (140).withSizeKeepingCentre (600, 32));

        bounds = bounds.reduced (margin, 4);
        const auto rowHeight = (bounds.getHeight() - gap) / 2;
        auto top = bounds.removeFromTop (rowHeight);
        bounds.removeFromTop (gap);
        auto bottom = bounds;

        // Top row: bow, strings, articulation.
        {
            auto bow = addPanel ("Bow", top.removeFromLeft (392));
            top.removeFromLeft (gap);
            auto stringArea = addPanel ("Strings", top.removeFromLeft (330));
            top.removeFromLeft (gap);
            auto articulation = addPanel ("Articulation", top);

            const auto padSize = juce::jmin (bow.getHeight() - 16, 214);
            bowPad.setBounds (bow.removeFromLeft (padSize).withHeight (padSize + 16));
            bow.removeFromLeft (8);
            layoutKnobGrid (bowKnobs, bow, 2);

            strings.setBounds (stringArea);

            articulationStatusArea
                = articulation.withY (articulation.getY() - titleHeight - 4).withHeight (titleHeight);
            keyswitchHint.setBounds (articulation.removeFromBottom (18));
            articulation.removeFromBottom (4);
            const auto columns = 2, rows = engine::numArticulations / columns;
            const auto buttonWidth = (articulation.getWidth() - gap) / columns;
            const auto buttonHeight = (articulation.getHeight() - (rows - 1) * 6) / rows;
            for (int a = 0; a < engine::numArticulations; ++a)
                articulationButtons[static_cast<std::size_t> (a)].setBounds (
                    articulation.getX() + (a % columns) * (buttonWidth + gap),
                    articulation.getY() + (a / columns) * (buttonHeight + 6),
                    buttonWidth,
                    buttonHeight);
        }

        // Bottom row: pitch, play, body and output, sized by their number of controls.
        {
            constexpr int pitchUnits = 5, playUnits = 5, bodyUnits = 6;
            const auto unit = (bottom.getWidth() - 2 * gap) / (pitchUnits + playUnits + bodyUnits);

            auto pitch = addPanel ("Pitch", bottom.removeFromLeft (unit * pitchUnits));
            bottom.removeFromLeft (gap);
            auto play = addPanel ("Play", bottom.removeFromLeft (unit * playUnits));
            bottom.removeFromLeft (gap);
            auto body = addPanel ("Body & Output", bottom);

            layoutKnobRow (pitchKnobs, pitch);

            auto playColumn = play.removeFromLeft (unit * 2 - 16).reduced (2, 0);
            layoutChoices (playChoices, playColumn);
            for (auto* t : playToggles)
                t->button.setBounds (playColumn.removeFromTop (28));
            layoutKnobRow (playKnobs, play);

            auto bodyColumn = body.removeFromLeft (unit * 2 - 16).reduced (2, 0);
            layoutChoices (bodyChoices, bodyColumn);
            layoutKnobRow (bodyKnobs, body);
        }
    }

private:
    struct Knob
    {
        juce::Label label;
        juce::Slider slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    struct Choice
    {
        juce::Label label;
        juce::ComboBox box;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
    };

    struct Toggle
    {
        juce::ToggleButton button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
    };

    static juce::String displayName (int articulation)
    {
        // Parameter choice names are plain ASCII for hosts; the buttons can use accents.
        return articulation == static_cast<int> (engine::Articulation::detache)
            ? juce::String (juce::CharPointer_UTF8 ("D\xc3\xa9tach\xc3\xa9"))
            : juce::String (engine::articulationNames[static_cast<std::size_t> (articulation)]);
    }

    Knob& addKnob (const juce::ParameterID& id, const juce::String& text)
    {
        auto knob = std::make_unique<Knob>();
        knob->label.setText (text, juce::dontSendNotification);
        knob->label.setJustificationType (juce::Justification::centred);
        knob->label.setFont (juce::FontOptions { 13.0f });
        knob->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        knob->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
        knob->slider.setTitle (text);
        knob->slider.setDoubleClickReturnValue (
            true,
            processor.getParameters()
                .getParameter (id.getParamID())
                ->convertFrom0to1 (processor.getParameters().getParameter (id.getParamID())->getDefaultValue()));
        knob->attachment
            = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.getParameters(),
                                                                                      id.getParamID(),
                                                                                      knob->slider);
        addAndMakeVisible (knob->label);
        addAndMakeVisible (knob->slider);
        knobs.push_back (std::move (knob));
        return *knobs.back();
    }

    Choice& addChoice (const juce::ParameterID& id, const juce::String& text)
    {
        auto choice = std::make_unique<Choice>();
        choice->label.setText (text, juce::dontSendNotification);
        choice->label.setFont (juce::FontOptions { 13.0f });
        choice->box.setTitle (text);
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

    Toggle& addToggle (const juce::ParameterID& id, const juce::String& text)
    {
        auto toggle = std::make_unique<Toggle>();
        toggle->button.setButtonText (text);
        toggle->button.setTitle (text);
        toggle->attachment
            = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.getParameters(),
                                                                                      id.getParamID(),
                                                                                      toggle->button);
        addAndMakeVisible (toggle->button);
        toggles.push_back (std::move (toggle));
        return *toggles.back();
    }

    // Records a panel for painting and returns its inner area.
    juce::Rectangle<int> addPanel (const juce::String& title, juce::Rectangle<int> bounds)
    {
        panels.emplace_back (title, bounds);
        return bounds.reduced (12, 8).withTrimmedTop (titleHeight - 6);
    }

    static void layoutKnob (Knob& k, juce::Rectangle<int> area)
    {
        k.label.setBounds (area.removeFromTop (18));
        k.slider.setBounds (area.reduced (2, 0));
    }

    static void layoutKnobRow (const std::vector<Knob*>& row, juce::Rectangle<int> area)
    {
        if (row.empty())
            return;
        const auto width = area.getWidth() / static_cast<int> (row.size());
        const auto height = juce::jmin (area.getHeight(), width + 44);
        auto strip = area.withSizeKeepingCentre (area.getWidth(), height);
        for (auto* k : row)
            layoutKnob (*k, strip.removeFromLeft (width));
    }

    static void layoutKnobGrid (const std::vector<Knob*>& grid, juce::Rectangle<int> area, int columns)
    {
        const auto rows = (static_cast<int> (grid.size()) + columns - 1) / columns;
        const auto width = area.getWidth() / columns;
        const auto height = area.getHeight() / rows;
        for (std::size_t i = 0; i < grid.size(); ++i)
        {
            const auto c = static_cast<int> (i) % columns, r = static_cast<int> (i) / columns;
            layoutKnob (*grid[i], { area.getX() + c * width, area.getY() + r * height, width, height });
        }
    }

    static void layoutChoices (const std::vector<Choice*>& list, juce::Rectangle<int>& column)
    {
        for (auto* c : list)
        {
            c->label.setBounds (column.removeFromTop (18));
            c->box.setBounds (column.removeFromTop (28));
            column.removeFromTop (8);
        }
    }

    void timerCallback() override
    {
        const auto active = static_cast<int> (processor.getActiveArticulation());
        if (active != shownArticulation)
        {
            shownArticulation = active;
            for (int a = 0; a < engine::numArticulations; ++a)
                articulationButtons[static_cast<std::size_t> (a)].setToggleState (a == active,
                                                                                  juce::dontSendNotification);
            playingText = "Playing: " + displayName (active);
            repaint (articulationStatusArea);
        }
    }

    ViolinSynthProcessor& processor;
    BowPad bowPad;
    StringDisplay strings;
    PresetBar presetBar;
    juce::MidiKeyboardComponent keyboard;
    juce::ParameterAttachment articulationAttachment;
    std::array<juce::TextButton, engine::numArticulations> articulationButtons;
    juce::Label keyswitchHint, credits;

    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::unique_ptr<Choice>> choices;
    std::vector<std::unique_ptr<Toggle>> toggles;
    std::vector<Knob*> bowKnobs, pitchKnobs, playKnobs, bodyKnobs;
    std::vector<Choice*> playChoices, bodyChoices;
    std::vector<Toggle*> playToggles;

    std::vector<std::pair<juce::String, juce::Rectangle<int>>> panels;
    juce::Rectangle<int> articulationStatusArea;
    juce::String playingText;
    int shownArticulation = -1;
};

//==============================================================================
ViolinSynthEditor::ViolinSynthEditor (ViolinSynthProcessor& owner)
    : AudioProcessorEditor (owner)
{
    setLookAndFeel (&lookAndFeel);
    content = std::make_unique<Content> (owner);
    addAndMakeVisible (*content);

    setResizable (true, true);
    setResizeLimits (baseWidth * 7 / 10, baseHeight * 7 / 10, baseWidth * 2, baseHeight * 2);
    if (auto* sizeLimits = getConstrainer())
        sizeLimits->setFixedAspectRatio (static_cast<double> (baseWidth) / baseHeight);
    setSize (baseWidth, baseHeight);
}

ViolinSynthEditor::~ViolinSynthEditor()
{
    content.reset();
    setLookAndFeel (nullptr);
}

void ViolinSynthEditor::resized()
{
    const auto scale = static_cast<float> (getWidth()) / static_cast<float> (baseWidth);
    content->setBounds (0, 0, baseWidth, baseHeight);
    content->setTransform (juce::AffineTransform::scale (scale));
}
} // namespace violinsynth
