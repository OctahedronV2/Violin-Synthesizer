#include "EngineTestUtilities.h"
#include "TestUtilities.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_audio_formats/juce_audio_formats.h>

#include <vector>

using namespace violinsynth;
using namespace violinsynth::test;

namespace
{
std::pair<std::vector<double>, std::vector<double>>
renderStereo (engine::ViolinEngine& e, double seconds, const std::vector<Event>& events)
{
    std::vector<double> left, right;
    juce::AudioBuffer<float> buffer (2, block);
    const auto total = static_cast<int> (seconds * fs);
    for (int start = 0; start < total; start += block)
    {
        juce::MidiBuffer midi;
        for (const auto& ev : events)
        {
            const auto pos = static_cast<int> (ev.time * fs);
            if (pos >= start && pos < start + block)
                midi.addEvent (ev.message, pos - start);
        }
        e.process (buffer, midi);
        for (int i = 0; i < block; ++i)
        {
            left.push_back (buffer.getSample (0, i));
            right.push_back (buffer.getSample (1, i));
        }
    }
    return { left, right };
}

void writeWav (const juce::String& name, const std::vector<double>& left, const std::vector<double>& right)
{
    juce::AudioBuffer<float> output (2, static_cast<int> (left.size()));
    for (std::size_t i = 0; i < left.size(); ++i)
    {
        output.setSample (0, static_cast<int> (i), static_cast<float> (left[i]));
        output.setSample (1, static_cast<int> (i), static_cast<float> (right[i]));
    }
    const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (name + ".wav");
    file.deleteFile();
    auto stream = std::unique_ptr<juce::OutputStream> (file.createOutputStream());
    juce::WavAudioFormat wav;
    const auto options
        = juce::AudioFormatWriterOptions {}.withSampleRate (fs).withNumChannels (2).withBitsPerSample (24);
    auto writer = wav.createWriterFor (stream, options);
    REQUIRE (writer != nullptr);
    writer->writeFromAudioSampleBuffer (output, 0, output.getNumSamples());
}
} // namespace

// Not run by default: `ViolinSynthTests "[.naturalrender]"` writes, to the
// working directory, held notes G3 to E6 at velocity 64 with no room
// (natural_held.wav, one note every 4 s, for comparison with the Iowa
// recordings), and two phrases with the default settings for listening:
// a slow slurred melody (natural_melody.wav) and slurred runs (natural_runs.wav).
TEST_CASE ("Render held notes and phrases with the default player", "[.naturalrender]")
{
    const auto velocity = 64.0f / 127.0f;
    {
        engine::EngineSettings s;
        s.output.room = 0.0f;
        engine::ViolinEngine e;
        e.setSettings (s);
        e.prepare (fs, block);
        std::vector<Event> events;
        auto t = 0.2;
        for (int note : { 55, 57, 62, 64, 69, 71, 76, 81, 88 })
        {
            events.push_back ({ t, on (note, 1, velocity) });
            events.push_back ({ t + 3.0, off (note) });
            t += 4.0;
        }
        const auto [left, right] = renderStereo (e, t, events);
        writeWav ("natural_held", left, right);
    }

    const auto phrase = [&] (const char* name, const std::vector<std::tuple<int, double, double>>& notes, double beat)
    {
        // notes: pitch, beats, velocity; a positive length slurs into the next
        // note (overlaps it by 30 ms), a negative one takes a new bow.
        std::vector<Event> events;
        auto t = 0.3;
        for (const auto& [pitch, beats, v] : notes)
        {
            const auto length = std::abs (beats) * beat;
            events.push_back ({ t, on (pitch, 1, static_cast<float> (v)) });
            events.push_back ({ t + (beats > 0.0 ? length + 0.03 : length * 0.92), off (pitch) });
            t += length;
        }
        engine::ViolinEngine e;
        e.setSettings (engine::EngineSettings {});
        e.prepare (fs, block);
        const auto [left, right] = renderStereo (e, t + 2.0, events);
        writeWav (name, left, right);
    };

    // A slow melody in D minor, 66 bpm, mostly slurred in pairs and threes.
    phrase ("natural_melody",
            { { 62, 1.0, 0.45 }, { 65, 1.0, 0.5 },  { 69, -2.0, 0.55 }, { 70, 0.5, 0.55 }, { 69, 0.5, 0.55 },
              { 67, 0.5, 0.5 },  { 65, -0.5, 0.5 }, { 64, 1.0, 0.5 },   { 65, 1.0, 0.5 },  { 62, -2.0, 0.45 },
              { 69, 1.0, 0.5 },  { 72, 1.0, 0.55 }, { 74, -1.5, 0.6 },  { 72, 0.5, 0.55 }, { 70, 0.5, 0.55 },
              { 69, 0.5, 0.55 }, { 67, -1.0, 0.5 }, { 69, 1.0, 0.55 },  { 74, 1.0, 0.6 },  { 77, -2.0, 0.65 },
              { 76, 0.5, 0.6 },  { 74, 0.5, 0.55 }, { 73, 1.0, 0.55 },  { 74, -3.0, 0.5 } },
            60.0 / 66.0);

    // Slurred runs at 100 bpm, eight sixteenths to a bow.
    std::vector<std::tuple<int, double, double>> runs;
    const int up[] = { 62, 64, 65, 67, 69, 70, 72, 74 };
    const int down[] = { 76, 74, 72, 70, 69, 67, 65, 64 };
    for (int bar = 0; bar < 2; ++bar)
    {
        for (int i = 0; i < 8; ++i)
            runs.emplace_back (up[i], i == 7 ? -0.25 : 0.25, 0.55);
        for (int i = 0; i < 8; ++i)
            runs.emplace_back (down[i], i == 7 ? -0.25 : 0.25, 0.55);
    }
    runs.emplace_back (62, -2.0, 0.55);
    phrase ("natural_runs", runs, 0.6);
}
