// Phase 7: playing from a host's typing keyboard (FL Studio, Ableton) and
// automating every control (docs/PHASE7.md).

#include "EngineTestUtilities.h"
#include "plugin/EditorComponents.h"
#include "plugin/Parameters.h"
#include "plugin/PluginProcessor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <functional>

using namespace violinsynth;
using namespace violinsynth::test;
using Catch::Approx;

TEST_CASE ("Velocity curve keeps the middle and softens the top", "[keyboard]")
{
    using engine::velocityToDynamics;

    // Linear at the top of the range, as before Phase 7.
    for (auto v : { 0.0, 0.25, 0.5, 0.79, 1.0 })
        CHECK (velocityToDynamics (v, 1.0) == Approx (v));

    // Default: velocity 64 plays as before, 100 and 127 are gentler.
    CHECK (velocityToDynamics (64.0 / 127.0, 0.7) == Approx (64.0 / 127.0).margin (0.01));
    CHECK (velocityToDynamics (100.0 / 127.0, 0.7) < 0.65);
    CHECK (velocityToDynamics (1.0, 0.7) == Approx (0.7));

    // Rising throughout.
    double previous = -1.0;
    for (int v = 0; v <= 127; ++v)
    {
        const auto d = velocityToDynamics (v / 127.0, 0.7);
        CHECK (d > previous);
        previous = d;
    }
}

TEST_CASE ("Velocity 127 bows more gently by default", "[keyboard]")
{
    auto speedAt = [] (double velocityTop, float velocity)
    {
        auto s = plainSettings();
        s.performance.velocityTop = velocityTop;
        engine::ViolinEngine e;
        e.setSettings (s);
        e.prepare (fs, block);
        double speed = 0.0;
        run (e,
             1.0,
             { { 0.0, on (69, 1, velocity) } },
             [&] (double t)
             {
                 if (t > 0.5 && speed == 0.0)
                     speed = e.getViolin().stringBowSpeed (A);
             });
        return speed;
    };

    CHECK (speedAt (0.7, 1.0f) < 0.8 * speedAt (1.0, 1.0f));
    CHECK (speedAt (0.7, 64.0f / 127.0f) == Approx (speedAt (1.0, 64.0f / 127.0f)).epsilon (0.03));
}

TEST_CASE ("Octave shift moves played notes but not keyswitches", "[keyboard]")
{
    auto s = plainSettings();
    s.performance.octaveShift = 1;
    engine::ViolinEngine e;
    e.setSettings (s);
    e.prepare (fs, block);

    double f0 = 0.0;
    run (e,
         0.6,
         { { 0.0, on (engine::firstKeyswitch + 1) }, // Detache, not a note an octave up
           { 0.01, off (engine::firstKeyswitch + 1) },
           { 0.1, on (48) } }, // C3 on a typing keyboard plays C4
         [&] (double t)
         {
             if (t > 0.5 && f0 == 0.0)
                 f0 = e.getViolin().stringF0 (G);
         });

    CHECK (e.getViolin().currentArticulation() == engine::Articulation::detache);
    CHECK (e.getViolin().noteOnString (G) == 60);
    CHECK (f0 == Approx (261.63).epsilon (0.01));
}

TEST_CASE ("Changing the octave while a key is held releases the right note", "[keyboard]")
{
    auto s = plainSettings();
    engine::ViolinEngine e;
    e.setSettings (s);
    e.prepare (fs, block);

    run (e, 0.3, { { 0.0, on (60) } });
    s.performance.octaveShift = 1;
    e.setSettings (s);
    run (e, 0.3, { { 0.0, off (60) } });

    for (int string = 0; string < 4; ++string)
        CHECK (e.getViolin().noteOnString (string) == -1);
}

TEST_CASE ("Keyboard response is saved with the project", "[keyboard][state]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    ViolinSynthProcessor a, b;
    a.getParameters().getParameter (params::id::octave.getParamID())->setValueNotifyingHost (1.0f); // +2
    a.getParameters().getParameter (params::id::velocityRange.getParamID())->setValueNotifyingHost (1.0f);

    juce::MemoryBlock state;
    a.getStateInformation (state);
    b.setStateInformation (state.getData(), static_cast<int> (state.getSize()));

    CHECK (b.getParameters().getParameter (params::id::octave.getParamID())->getValue() == Approx (1.0f));
    CHECK (b.getParameters().getParameter (params::id::velocityRange.getParamID())->getValue() == Approx (1.0f));
}

TEST_CASE ("Every parameter is automatable", "[keyboard][parameters]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    ViolinSynthProcessor processor;
    for (auto* p : static_cast<juce::AudioProcessor&> (processor).getParameters())
    {
        CAPTURE (p->getName (64));
        CHECK (p->isAutomatable());
    }
}

namespace
{
// Every control in the editor, with the parameter it is attached to found by
// its title (knobs, drop-downs and switches) or its articulation button.
void forEachControl (juce::Component& root, const std::function<void (juce::Component&)>& f)
{
    std::function<void (juce::Component&)> visit = [&] (juce::Component& c)
    {
        f (c);
        for (auto* child : c.getChildren())
            visit (*child);
    };
    visit (root);
}
} // namespace

TEST_CASE ("Clicking the editor never takes the host's keyboard", "[keyboard][editor]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    ViolinSynthProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor { processor.createEditorAndMakeActive() };

    int components = 0;
    forEachControl (*editor,
                    [&] (juce::Component& c)
                    {
                        ++components;
                        CAPTURE (c.getTitle(), typeid (c).name());
                        CHECK_FALSE (c.getMouseClickGrabsKeyboardFocus());
                    });
    CHECK (components > 50);
}

TEST_CASE ("Right-clicking a control opens the parameter menu without moving it", "[keyboard][editor]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    ViolinSynthProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor { processor.createEditorAndMakeActive() };

    int sliders = 0, withMenu = 0;
    forEachControl (*editor,
                    [&] (juce::Component& c)
                    {
                        auto* slider = dynamic_cast<ParameterControl<juce::Slider>*> (&c);
                        if (slider != nullptr)
                            ++sliders;
                        if (slider == nullptr || ! slider->onParameterMenu)
                            return;
                        ++withMenu;

                        bool opened = false;
                        slider->onParameterMenu = [&] { opened = true; };
                        const auto before = slider->getValue();
                        auto source = juce::Desktop::getInstance().getMainMouseSource();
                        const auto centre = slider->getLocalBounds().getCentre().toFloat();
                        auto event = [&] (juce::Point<float> p)
                        {
                            return juce::MouseEvent (source,
                                                     p,
                                                     juce::ModifierKeys::rightButtonModifier,
                                                     0.0f,
                                                     0.0f,
                                                     0.0f,
                                                     0.0f,
                                                     0.0f,
                                                     slider,
                                                     slider,
                                                     juce::Time::getCurrentTime(),
                                                     centre,
                                                     juce::Time::getCurrentTime(),
                                                     1,
                                                     false);
                        };
                        slider->mouseDown (event (centre));
                        slider->mouseDrag (event (centre.translated (0.0f, -40.0f)));
                        slider->mouseUp (event (centre.translated (0.0f, -40.0f)));
                        CAPTURE (slider->getTitle());
                        CHECK (opened);
                        CHECK (slider->getValue() == before);
                    });
    CHECK (sliders > 15);
    CHECK (withMenu == sliders);
}
