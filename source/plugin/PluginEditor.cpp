#include "plugin/PluginEditor.h"

#include "plugin/Parameters.h"
#include "plugin/PluginProcessor.h"

namespace violinsynth
{
namespace
{
constexpr int defaultWidth = 640;
constexpr int defaultHeight = 320;
constexpr int keyboardHeight = 90;
} // namespace

ViolinSynthEditor::ViolinSynthEditor (ViolinSynthProcessor& owner)
    : AudioProcessorEditor (owner),
      gainAttachment (owner.getParameters(), params::id::outputGain.getParamID(), gainSlider),
      keyboard (owner.getKeyboardState(), juce::MidiKeyboardComponent::horizontalKeyboard)
{
    gainLabel.setText ("Output Gain", juce::dontSendNotification);
    gainLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (gainLabel);

    gainSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    gainSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 20);
    addAndMakeVisible (gainSlider);

    // G3 is the lowest note on a violin.
    keyboard.setAvailableRange (55, 103);
    keyboard.setLowestVisibleKey (55);
    addAndMakeVisible (keyboard);

    setResizable (true, true);
    setResizeLimits (480, 260, 1600, 900);
    setSize (defaultWidth, defaultHeight);
}

void ViolinSynthEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour { 0xff1e1b18 });

    auto header = getLocalBounds().removeFromTop (70).reduced (20, 12);

    g.setColour (juce::Colour { 0xffe8d9c0 });
    g.setFont (juce::FontOptions { 26.0f, juce::Font::bold });
    g.drawText ("Violin Synthesizer", header.removeFromTop (30), juce::Justification::centredLeft);

    g.setColour (juce::Colour { 0xff9c8f7a });
    g.setFont (juce::FontOptions { 14.0f });
    g.drawText ("v" JucePlugin_VersionString "  |  Phase 0 build: placeholder sine voice",
                header,
                juce::Justification::centredLeft);
}

void ViolinSynthEditor::resized()
{
    auto bounds = getLocalBounds();
    keyboard.setBounds (bounds.removeFromBottom (keyboardHeight));

    bounds.removeFromTop (70);
    auto gainArea = bounds.withSizeKeepingCentre (120, bounds.getHeight()).reduced (0, 8);
    gainLabel.setBounds (gainArea.removeFromTop (20));
    gainSlider.setBounds (gainArea);
}
} // namespace violinsynth
