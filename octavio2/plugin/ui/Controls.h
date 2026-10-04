#pragma once

#include "Theme.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <optional>

namespace octavio2::ui
{
// Rotary knobs, menus and tooltips in the mockups' style.
class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();
    void
    drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&)
        override;
    juce::Font getPopupMenuFont() override { return Fonts::sans (13); }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return Fonts::sans (13); }
    void
    drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String&, juce::Point<int>, juce::Rectangle<int>) override;
    void drawTooltip (juce::Graphics&, const juce::String&, int w, int h) override;
};

// Opens the host's menu for a parameter (automation, MIDI learn), or a reset menu without one.
void showParameterMenu (juce::Component& control, juce::RangedAudioParameter&);

// A knob bound to a parameter: label above, value below (as the mockup's small knobs), or a
// card with an uppercase title, a big knob and a caption (the Play tab's six controls).
// Without a parameter it is a preview of a control that arrives later: dimmed, not draggable.
class Knob final : public juce::Component, public juce::SettableTooltipClient
{
public:
    enum class Style
    {
        small,
        card
    };
    Knob (juce::AudioProcessorValueTreeState*,
          const juce::String& paramID,
          const juce::String& label,
          Style,
          std::function<juce::String (float)> valueText = {});

    void setCaption (const juce::String& c) { caption = c; }
    void setBadge (std::optional<Mode> m) { badge = m; }
    void setColour (juce::Colour c) { colour = c; }
    // a preview of a later control: shown at `value` (0..1) with this text
    void setPreview (float value, const juce::String& text, const juce::String& when);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Dial final : juce::Slider
    {
        Dial()
            : juce::Slider (RotaryHorizontalVerticalDrag, NoTextBox)
        {
        }
        std::function<void()> onMenu;
        void mouseDown (const juce::MouseEvent& e) override;
        void mouseDrag (const juce::MouseEvent& e) override;
        void mouseUp (const juce::MouseEvent& e) override;
    };
    juce::Rectangle<float> dialArea() const;

    Dial dial;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    juce::RangedAudioParameter* param = nullptr;
    juce::String label, caption, previewText;
    Style style;
    std::function<juce::String (float)> valueText;
    std::optional<Mode> badge;
    juce::Colour colour = colours::amber;
    float previewValue = -1;
};

// A row or grid of choices (Live/Studio, rooms, articulations). Bound to a choice parameter, or
// a preview of choices that arrive later (shown, not clickable, the active one highlighted).
class Choices final : public juce::Component, public juce::SettableTooltipClient
{
public:
    struct Item
    {
        juce::String name, note; // note: right-aligned description (list layout)
        bool enabled = true;
        int value = -1; // the parameter's choice index, -1 = this item's position
    };
    Choices (juce::AudioProcessorValueTreeState*, const juce::String& paramID, std::vector<Item>);

    // columns 0 = one row, items as wide as their text (or itemWidth when set)
    void setLayout (int columns, float itemHeight, float gap, float itemWidth = 0);
    void setPreviewActive (int index) { previewActive = index; }
    void setOnChange (std::function<void (int)> f) { onChange = std::move (f); }
    // 2.3: what to highlight, from the parameter's value (e.g. what a keyswitch latched over it),
    // and a call on every click on an item (after the parameter is set)
    void setActiveSource (std::function<int (int paramValue)> f) { activeSource = std::move (f); }
    void setOnClick (std::function<void (int)> f) { onClick = std::move (f); }
    void setNote (int index, const juce::String& note);
    // an active source showing what a keyswitch or UACC latched (Telemetry) while the parameter
    // still has the value the latch was made over
    static std::function<int (int)> showLatch (const std::atomic<int>& latch, const std::atomic<int>& seen)
    {
        return [&latch, &seen] (int param)
        {
            const int l = latch.load();
            return l >= 0 && seen.load() == param ? l : param;
        };
    }
    int active() const;

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> itemBounds (int i) const;
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    std::vector<Item> items;
    int columns = 0, previewActive = 0, current = 0;
    float itemHeight = 30, gap = 4, itemWidth = 0;
    std::function<void (int)> onChange, onClick;
    std::function<int (int)> activeSource;
};

// 2.3: the Take (the Seed parameter): "Take 12" (drag up/down to change, double-click for the
// default, right-click for the host's menu) and a die that rolls a new one
class TakeBox final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit TakeBox (juce::RangedAudioParameter&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void roll(); // a new random take

private:
    juce::Rectangle<float> dieArea() const;
    void set (int take);
    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    int value = 1, dragStart = 1;
    bool dragging = false, onDie = false;
    juce::Random random;
};

// A button of a later feature (Guess curves, Drag as MIDI, Learn): drawn as in the mockups but
// dimmed, with a tooltip saying when it arrives.
class PreviewButton final : public juce::Component, public juce::SettableTooltipClient
{
public:
    PreviewButton (const juce::String& text, const juce::String& when, bool filled = false);
    void paint (juce::Graphics&) override;

private:
    juce::String text;
    bool filled;
};
} // namespace octavio2::ui
