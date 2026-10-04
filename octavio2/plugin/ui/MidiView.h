#pragma once

#include "../PluginProcessor.h"
#include "Controls.h"
#include "ToneView.h" // ValueBox

namespace octavio2::ui
{
// MIDI tab (mockup midi.svg, M6): the controller map that plays (any CC to the player's
// dimensions, the slur pedal or any parameter, with range, invert and Learn), the map presets,
// the velocity response (curve, dynamics offset, sensitivity, attack weight), octave, pitch bend
// range, keyswitches (start key, behaviour; UACC on CC32) and MPE.
class MidiView final : public juce::Component, private juce::Timer
{
public:
    explicit MidiView (Processor&);
    ~MidiView() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    // for the tests: Learn for a new row of this target / for row `row`
    void startLearn (int target, int row = -1);
    bool isLearning() const { return learnRow != noLearn; }
    void poll() { timerCallback(); } // what the timer does (tests)

    // a box drawn as in the mockups that does something when clicked
    class Pill;
    // a value 0..1 shown as text, changed by dragging (the range cells)
    class DragValue;
    class Row;

private:
    void timerCallback() override;
    void paintVelocity (juce::Graphics&, juce::Rectangle<float>);
    void rebuildRows();
    void edit (int row, const std::function<void (MidiMap::Entry&)>&, bool rebuild);
    void removeRow (int row);
    void showLearnMenu();
    juce::PopupMenu targetMenu (int current) const;
    std::vector<int> allTargets() const;

    static constexpr int noLearn = -100, newRow = -1;

    Processor& processor;
    Knob curve, dynamics, bend;
    Knob sensitivity, attackWeight; // 2.3
    Choices octave, behaviour, mpe;
    std::unique_ptr<ValueBox> keyswitchStart; // 2.3
    juce::ComboBox mapPreset;
    std::unique_ptr<Pill> learn;
    juce::Viewport viewport;
    juce::Component rowsHolder;
    std::vector<std::unique_ptr<Row>> rows;
    std::vector<MidiMap::Entry> shown;
    int shownVelocity = -1;
    std::array<float, 4> shownResponse {}; // 2.3: curve, dynamics, sensitivity, keyswitch start
    int learnRow = noLearn, learnTarget = 0, learnStart = 0;
    int lastIncoming = 0;
    bool shownPedal = false;
    std::array<int, 5> shownModes {}; // 2.3: per o2::Dim, as its badge shows it
    juce::String incomingText;
};
} // namespace octavio2::ui
