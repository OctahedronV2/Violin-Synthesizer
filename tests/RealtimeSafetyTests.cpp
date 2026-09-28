// Phase 7.4: real-time safety (docs/PHASE7.md).
//
// These tests drive the audio thread through everything a host and the
// editor do while the plugin plays. In the RealtimeSanitizer build
// (-DVIOLINSYNTH_ENABLE_RTSAN=ON) any allocation, lock or system call they
// reach inside processBlock aborts the run with a stack trace; in other
// builds they check that the output stays sound.

#include "ProcessorTestUtilities.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>

using violinsynth::ViolinSynthProcessor;
using namespace violinsynth;
using namespace violinsynth::test;

namespace
{
struct Player
{
    ViolinSynthProcessor& processor;
    juce::AudioBuffer<float> buffer { 2, 256 };
    bool finite = true;
    float peak = 0.0f;

    void play (juce::MidiBuffer midi = {})
    {
        processor.processBlock (buffer, midi);
        for (int c = 0; c < buffer.getNumChannels(); ++c)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                finite = finite && std::isfinite (buffer.getSample (c, i));
                peak = std::max (peak, std::abs (buffer.getSample (c, i)));
            }
    }
};
} // namespace

TEST_CASE ("The audio thread keeps playing through host and editor activity", "[realtime]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    ViolinSynthProcessor processor;
    processor.prepareToPlay (48000.0, 256);
    Player player { processor };
    std::mt19937 random (11);
    auto chance = [&] (int oneIn) { return std::uniform_int_distribution<int> (1, oneIn) (random) == 1; };

    std::unique_ptr<juce::AudioProcessorEditor> editor;
    auto& parameters = processor.getParameters().processor.getParameters();
    juce::MemoryBlock savedState;
    processor.getStateInformation (savedState);

    for (int block = 0; block < 2000; ++block)
    {
        // Between blocks, the message thread does what hosts and users do.
        if (block % 4 == 0) // host automation: every parameter moves
            for (auto* p : parameters)
                p->setValueNotifyingHost (std::uniform_real_distribution<float> (0.0f, 1.0f) (random));
        if (chance (50))
            processor.getPresetManager().load (std::uniform_int_distribution<int> (
                0,
                static_cast<int> (processor.getPresetManager().getPresets().size()) - 1) (random));
        if (chance (80))
            processor.setStateInformation (savedState.getData(), static_cast<int> (savedState.getSize()));
        if (chance (30))
            processor.applyBodyChange(); // the timer's impulse-response load
        if (chance (40))
            processor.getKeyboardState().noteOn (1, std::uniform_int_distribution<int> (55, 100) (random), 0.6f);
        if (chance (40))
            processor.getKeyboardState().allNotesOff (1);
        if (chance (5))
            processor.updateKeyboardState();
        if (block == 500)
            editor.reset (processor.createEditor());
        if (block == 1500)
            editor.reset();

        // The host's MIDI: notes, bends, pressure, controllers and SysEx.
        juce::MidiBuffer midi;
        const auto at = [&] { return std::uniform_int_distribution<int> (0, 255) (random); };
        if (chance (6))
            midi.addEvent (juce::MidiMessage::noteOn (1, std::uniform_int_distribution<int> (48, 100) (random), 0.7f),
                           at());
        if (chance (6))
            midi.addEvent (juce::MidiMessage::noteOff (1, std::uniform_int_distribution<int> (48, 100) (random)), at());
        if (chance (8))
            midi.addEvent (juce::MidiMessage::pitchWheel (1, std::uniform_int_distribution<int> (0, 16383) (random)),
                           at());
        if (chance (8))
            midi.addEvent (juce::MidiMessage::controllerEvent (1, 1, 64), at());
        if (chance (8))
            midi.addEvent (juce::MidiMessage::channelPressureChange (1, 90), at());
        if (chance (100))
        {
            const std::array<juce::uint8, 12> sysex { 0x7e, 0x7f, 0x06, 0x01, 1, 2, 3, 4, 5, 6, 7, 8 };
            midi.addEvent (juce::MidiMessage::createSysExMessage (sysex.data(), static_cast<int> (sysex.size())), at());
        }
        if (chance (200))
            midi.addEvent (juce::MidiMessage::allNotesOff (1), at());

        player.play (midi);
    }

    CHECK (player.finite);
    CHECK (player.peak > 0.01f);
}

TEST_CASE ("On-screen keyboard notes reach the engine without the audio thread locking", "[realtime][keyboard]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    ViolinSynthProcessor processor;
    processor.prepareToPlay (48000.0, 256);
    Player player { processor };

    processor.getKeyboardState().noteOn (1, 74, 0.6f); // a click on the keyboard, on the message thread
    for (int i = 0; i < 20; ++i)
        player.play();
    CHECK (processor.getStringState (2).note.load() == 74); // on the A string

    processor.getKeyboardState().noteOff (1, 74, 0.0f);
    player.play();
    CHECK (processor.getStringState (2).note.load() == -1);
}

TEST_CASE ("Notes from the host light up the on-screen keyboard", "[realtime][keyboard]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    ViolinSynthProcessor processor;
    processor.prepareToPlay (48000.0, 256);
    Player player { processor };

    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.6f), 10); // plays C5 at the default Octave +1
    player.play (midi);
    processor.updateKeyboardState(); // the timer, on the message thread
    CHECK (processor.getKeyboardState().isNoteOn (1, 72));

    // Shown notes are not played a second time.
    for (int i = 0; i < 4; ++i)
        player.play();
    CHECK (processor.getStringState (2).note.load() == 72);

    midi.clear();
    midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
    player.play (midi);
    processor.updateKeyboardState();
    CHECK_FALSE (processor.getKeyboardState().isNoteOn (1, 72));
    for (int i = 0; i < 4; ++i)
        player.play();
    CHECK (processor.getStringState (2).note.load() == -1);
}

TEST_CASE ("Idle cost does not rise as the tails decay towards zero", "[realtime][denormals]")
{
    // Denormal numbers in decaying filters and delay lines can make a silent
    // plugin cost many times more than a playing one. processBlock flushes
    // them to zero; this plays a note, lets it die for 30 s and compares the
    // cost of the early and late silence.
    juce::ScopedJuceInitialiser_GUI juce;
    ViolinSynthProcessor processor;
    processor.prepareToPlay (48000.0, 256);
    juce::AudioBuffer<float> buffer (2, 256);
    std::vector<double> seconds;

    for (int block = 0; block < 48000 * 32 / 256; ++block)
    {
        juce::MidiBuffer midi;
        if (block == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, 69, 0.9f), 0);
        if (block == 48000 / 256)
            midi.addEvent (juce::MidiMessage::noteOff (1, 69), 0);
        const auto start = std::chrono::steady_clock::now();
        processor.processBlock (buffer, midi);
        seconds.push_back (std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count());
    }

    // Median of each window, so a descheduled block on a busy machine doesn't count.
    auto median = [&] (double from, double to)
    {
        std::vector<double> window (seconds.begin() + static_cast<std::ptrdiff_t> (from * 48000 / 256),
                                    seconds.begin() + static_cast<std::ptrdiff_t> (to * 48000 / 256));
        std::nth_element (window.begin(),
                          window.begin() + static_cast<std::ptrdiff_t> (window.size() / 2),
                          window.end());
        return window[window.size() / 2];
    };
    const auto early = median (2.0, 4.0);
    const auto late = median (29.0, 31.0);
    UNSCOPED_INFO ("early silence " << early * 1.0e6 << " us per block, late " << late * 1.0e6 << " us");
    CHECK (late < 2.0 * early);
}
