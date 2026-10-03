#include "PluginEditor.h"

namespace octavio2
{
namespace
{
constexpr int width = 960, height = 400, keyboardHeight = 90;

// Hosts number middle C differently: C5 in FL Studio, C3 in Ableton Live, Cubase, Logic,
// Bitwig and Studio One, C4 elsewhere (as Octavio 1's keyboard).
int hostMiddleCOctave()
{
    const juce::PluginHostType host;
    if (host.isFruityLoops())
        return 5;
    if (host.isAbletonLive() || host.isCubase() || host.isNuendo() || host.isLogic() || host.isGarageBand()
        || host.isBitwigStudio() || host.isStudioOne())
        return 3;
    return 4;
}

void stopClicksTakingFocus (juce::Component& c)
{
    c.setMouseClickGrabsKeyboardFocus (false);
    for (auto* child : c.getChildren())
        stopClicksTakingFocus (*child);
}
} // namespace

Editor::Editor (Processor& p)
    : AudioProcessorEditor (p),
      processor (p),
      keyboard (p.getKeyboardState(), juce::MidiKeyboardComponent::horizontalKeyboard)
{
    setLookAndFeel (&lookAndFeel);
    addChoice (mode, params::id::mode, "Mode");
    addChoice (octave, params::id::octave, "Octave");
    addChoice (room, params::id::room, "Room");
    addKnob (velocityCurve, params::id::velocityCurve, "Velocity curve");
    addKnob (vibrato, params::id::vibrato, "Vibrato");
    addKnob (brightness, params::id::brightness, "Brightness");
    addKnob (reverb, params::id::reverb, "Reverb");
    addKnob (volume, params::id::volume, "Volume");

    mode.box.setTooltip ("Live plays at once. Studio looks 1.2 s ahead so the player knows each note's length; "
                         "the DAW compensates the delay.");
    credits.setText ("Halls: Arvedi Auditorium, Detmold Brahmssaal, ChurchIR Cremona, BBC Maida Vale, "
                     "WDR Broadcast Studios (CC BY)",
                     juce::dontSendNotification);
    credits.setColour (juce::Label::textColourId, violinsynth::colours::dimText);
    credits.setFont (juce::FontOptions (11.0f));
    addAndMakeVisible (credits);

    keyboard.setOctaveForMiddleC (hostMiddleCOctave());
    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, violinsynth::colours::accent);
    keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId,
                        violinsynth::colours::accent.withAlpha (0.3f));
    keyboard.setVelocity (0.8f, true); // softer lower on the key; 100 near the top, as a typing keyboard
    // the host's typing keyboard plays the violin: this one takes no keys
    keyboard.clearKeyMappings();
    keyboard.setWantsKeyboardFocus (false);
    // the keys that sound, G3 to E7, named as they sound (clicks are sent an Octave lower)
    keyboard.setAvailableRange (55, 104);
    keyboard.setLowestVisibleKey (55);
    addAndMakeVisible (keyboard);

    setSize (width, height);
    // clicks must not take the computer keyboard from the host (FL Studio, Ableton); runs last
    stopClicksTakingFocus (*this);
}

Editor::~Editor()
{
    setLookAndFeel (nullptr);
}

void Editor::addKnob (Knob& k, const juce::ParameterID& id, const juce::String& name)
{
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 20);
    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.getParameters(),
                                                                                           id.getParamID(),
                                                                                           k.slider);
    k.slider.setPopupMenuEnabled (false);
    k.slider.onMenu = [this, &k, pid = id.getParamID()] { showParameterMenu (k.slider, juce::ParameterID { pid, 1 }); };
    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (k.slider);
    addAndMakeVisible (k.label);
}

void Editor::addChoice (Choice& c, const juce::ParameterID& id, const juce::String& name)
{
    if (auto* param
        = dynamic_cast<juce::AudioParameterChoice*> (processor.getParameters().getParameter (id.getParamID())))
        c.box.addItemList (param->choices, 1);
    c.attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.getParameters(),
                                                                                             id.getParamID(),
                                                                                             c.box);
    c.label.setText (name, juce::dontSendNotification);
    addAndMakeVisible (c.box);
    addAndMakeVisible (c.label);
}

void Editor::showParameterMenu (juce::Component& control, const juce::ParameterID& id)
{
    auto* param = processor.getParameters().getParameter (id.getParamID());
    if (param == nullptr)
        return;
    if (auto* context = getHostContext())
        if (auto menu = context->getContextMenuForParameter (param))
        {
            menu->showNativeMenu (getMouseXYRelative());
            return;
        }
    // hosts without a menu (Standalone): reset only
    juce::PopupMenu menu;
    menu.addItem ("Reset to default",
                  [param]
                  {
                      param->beginChangeGesture();
                      param->setValueNotifyingHost (param->getDefaultValue());
                      param->endChangeGesture();
                  });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&control).withMousePosition());
}

void Editor::paint (juce::Graphics& g)
{
    g.fillAll (violinsynth::colours::background);
    auto top = getLocalBounds().removeFromTop (56).reduced (20, 10);
    g.setColour (violinsynth::colours::accent);
    g.setFont (juce::FontOptions (28.0f, juce::Font::bold));
    g.drawText ("Octavio 2", top, juce::Justification::centredLeft);
    g.setColour (violinsynth::colours::dimText);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText ("preview " JucePlugin_VersionString, top, juce::Justification::centredRight);
    g.setColour (violinsynth::colours::panel);
    g.fillRoundedRectangle (
        getLocalBounds().withTrimmedTop (56).withTrimmedBottom (keyboardHeight + 22).reduced (14, 4).toFloat(),
        8.0f);
}

void Editor::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop (56);
    keyboard.setBounds (area.removeFromBottom (keyboardHeight));
    credits.setBounds (area.removeFromBottom (22).reduced (16, 0));
    keyboard.setKeyWidth (static_cast<float> (keyboard.getWidth()) / 29.0f); // 29 white keys, G3 to E7
    area = area.reduced (28, 14);

    auto choices = area.removeFromLeft (300);
    for (auto* c : { &mode, &octave, &room })
    {
        auto row = choices.removeFromTop (choices.getHeight() / 3).withSizeKeepingCentre (choices.getWidth(), 30);
        c->label.setBounds (row.removeFromLeft (70));
        c->box.setBounds (row);
    }
    area.removeFromLeft (20);
    const int w = area.getWidth() / 5;
    for (auto* k : { &velocityCurve, &vibrato, &brightness, &reverb, &volume })
    {
        auto col = area.removeFromLeft (w);
        k->label.setBounds (col.removeFromTop (22));
        k->slider.setBounds (col.reduced (6, 0));
    }
}
} // namespace octavio2
