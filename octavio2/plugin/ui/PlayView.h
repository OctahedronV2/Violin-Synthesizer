#pragma once

#include "../PluginProcessor.h"
#include "Controls.h"
#include "ModeBadge.h"
#include "NoteTrack.h"

namespace octavio2::ui
{
// Play tab (mockups main.svg and studio.svg): what the player is doing on the fingerboard, the
// note now, then the six main controls in Live mode or the look-ahead plan in Studio mode, and the
// articulation strip in both.
class PlayView final : public juce::Component, private juce::Timer
{
public:
    explicit PlayView (Processor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void paintFingerboard (juce::Graphics&, juce::Rectangle<float>);
    void paintNow (juce::Graphics&, juce::Rectangle<float>);
    void paintLookAhead (juce::Graphics&, juce::Rectangle<float>);
    void paintPlan (juce::Graphics&, juce::Rectangle<float>);
    bool studio() const;
    void updateBadges();

    Processor& processor;
    Knob dynamics, expression, vibrato, portamento, stringPreference, room;
    // 2.3: the articulation strip, bound to the Bow style, Contact, Mute and Articulation
    // parameters; it shows what plays now, keyswitches and UACC included
    Choices bowStyle, contact, mute, articulation;
    juce::Rectangle<float> dragArea;

    NoteTrack track;

    // 2.3: clickable A / G / M badges (Auto / Guided / Manual)
    float param (const juce::ParameterID& id) const
    {
        return processor.getParameters().getRawParameterValue (id.getParamID())->load();
    }
    void setParam (const juce::ParameterID& id, float value)
    {
        if (auto* p = processor.getParameters().getParameter (id.getParamID()))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (value));
            p->endChangeGesture();
        }
    }
    ModeBadge dynamicsBadge { processor, o2::dimDynamics }, vibratoBadge { processor, o2::dimVibWidth },
        contactBadge { processor, o2::dimContact }, vibratoNowBadge { processor, o2::dimVibWidth },
        pressureNowBadge { processor, o2::dimPressure };
    // knob-guided: the knob away from its default guides the player; Auto resets it
    ModeBadge portamentoBadge { [this] {
                                   return std::abs (param (params::id::portamento) - 100.0f) >= 0.5f ? Mode::guided
                                                                                                     : Mode::autoMode;
                               },
                                [this] (Mode m)
                                {
                                    if (m == Mode::autoMode)
                                        setParam (params::id::portamento, 100.0f);
                                },
                                "Portamento",
                                "the player's own slides (100 %).",
                                "the knob scales the slides; turn it to guide." };
    ModeBadge stringPreferenceBadge {
        [this] { return std::abs (param (params::id::stringPreference)) >= 0.5f ? Mode::guided : Mode::autoMode; },
        [this] (Mode m)
        {
            if (m == Mode::autoMode)
                setParam (params::id::stringPreference, 0.0f);
        },
        "String preference",
        "the player picks strings and positions (centre).",
        "the knob leans the choice bright or dark; turn it to guide."
    };
};
} // namespace octavio2::ui
