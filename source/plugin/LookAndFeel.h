#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace violinsynth
{
namespace colours
{
inline const juce::Colour background { 0xff1b1815 };
inline const juce::Colour panel { 0xff27221e };
inline const juce::Colour panelEdge { 0xff3a322b };
inline const juce::Colour control { 0xff342d27 };
inline const juce::Colour accent { 0xffd39a5a }; // varnish amber
inline const juce::Colour accentDim { 0xff7a5a38 };
inline const juce::Colour text { 0xffeee1c8 };
inline const juce::Colour dimText { 0xffa89a84 };
inline const juce::Colour track { 0xff171411 };
} // namespace colours

// Flat, warm vector look for every control; everything is drawn from paths so
// the editor scales cleanly to any size.
class ViolinLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    ViolinLookAndFeel();

    void drawRotarySlider (juce::Graphics&,
                           int x,
                           int y,
                           int width,
                           int height,
                           float sliderPos,
                           float startAngle,
                           float endAngle,
                           juce::Slider&) override;

    void drawComboBox (juce::Graphics&,
                       int width,
                       int height,
                       bool isButtonDown,
                       int buttonX,
                       int buttonY,
                       int buttonW,
                       int buttonH,
                       juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;

    void drawButtonBackground (juce::Graphics&,
                               juce::Button&,
                               const juce::Colour& backgroundColour,
                               bool highlighted,
                               bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    juce::Label* createSliderTextBox (juce::Slider&) override;
    void drawLabel (juce::Graphics&, juce::Label&) override;
    juce::Font getPopupMenuFont() override;
};
} // namespace violinsynth
