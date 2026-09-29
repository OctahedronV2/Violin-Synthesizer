#include "TestUtilities.h"
#include "engine/ViolinEngine.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <chrono>
#include <iostream>

using namespace violinsynth;
using Catch::Approx;

namespace
{
struct Rendered
{
    std::vector<double> left;
    float peak = 0.0f;
    bool finite = true;
};

// Renders `seconds` of audio; `events` maps sample positions to MIDI messages.
Rendered render (engine::ViolinEngine& e,
                 double fs,
                 int blockSize,
                 double seconds,
                 const std::vector<std::pair<int, juce::MidiMessage>>& events)
{
    Rendered r;
    juce::AudioBuffer<float> buffer (2, blockSize);
    const auto total = static_cast<int> (seconds * fs);

    for (int start = 0; start < total; start += blockSize)
    {
        const auto n = std::min (blockSize, total - start);
        buffer.setSize (2, n, false, false, true);
        juce::MidiBuffer midi;
        for (const auto& [pos, msg] : events)
            if (pos >= start && pos < start + n)
                midi.addEvent (msg, pos - start);

        e.process (buffer, midi);

        for (int i = 0; i < n; ++i)
        {
            const auto l = buffer.getSample (0, i);
            const auto rr = buffer.getSample (1, i);
            r.finite = r.finite && std::isfinite (l) && std::isfinite (rr);
            r.peak = std::max ({ r.peak, std::abs (l), std::abs (rr) });
            r.left.push_back (l);
        }
    }
    return r;
}

engine::EngineSettings quietSettings (engine::Body::Quality quality = engine::Body::Quality::modal)
{
    engine::EngineSettings s;
    s.performance.voice.vibratoDepthCents = 0.0;
    s.performance.voice.humanise = 0.0;
    s.performance.voice.intonation = 0.0;
    s.output.room = 0.0f;
    s.output.width = 0.0f;
    s.performance.velocityTop = 1.0; // the model tests are calibrated on linear velocity
    s.bodyQuality = quality;
    return s;
}

std::vector<std::pair<int, juce::MidiMessage>>
note (int number, double fs, double onSeconds, double offSeconds, float velocity = 0.8f)
{
    return { { static_cast<int> (onSeconds * fs), juce::MidiMessage::noteOn (1, number, velocity) },
             { static_cast<int> (offSeconds * fs), juce::MidiMessage::noteOff (1, number) } };
}

} // namespace

TEST_CASE ("Engine runs the string at 176.4 kHz or more", "[engine]")
{
    for (auto fs : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
    {
        const auto order = engine::ViolinEngine::oversamplingOrderFor (fs);
        CAPTURE (fs, order);
        CHECK (fs * (1 << order) >= 176400.0 - 1.0);
        CHECK (fs * (1 << order) < 2.0 * 176400.0);
    }
}

TEST_CASE ("Engine is silent without notes", "[engine]")
{
    engine::ViolinEngine e;
    e.setSettings (quietSettings());
    e.prepare (48000.0, 256);
    const auto r = render (e, 48000.0, 256, 0.5, {});
    CHECK (r.finite);
    CHECK (r.peak == 0.0f);
}

TEST_CASE ("Engine plays a bowed note in tune and releases it", "[engine]")
{
    const auto fs = GENERATE (44100.0, 48000.0, 96000.0);
    const auto blockSize = GENERATE (32, 512);
    const auto quality = GENERATE (engine::Body::Quality::modal, engine::Body::Quality::convolution);
    CAPTURE (fs, blockSize, quality == engine::Body::Quality::modal);

    engine::ViolinEngine e;
    e.setSettings (quietSettings (quality));
    e.prepare (fs, blockSize);

    const auto r = render (e, fs, blockSize, 5.0, note (69, fs, 0.0, 1.5));
    REQUIRE (r.finite);
    CHECK (r.peak > 0.01f);
    CHECK (r.peak < 1.0f);

    const std::span<const double> steady (r.left.data() + static_cast<std::size_t> (0.6 * fs),
                                          static_cast<std::size_t> (0.8 * fs));
    CHECK (std::abs (test::cents (test::estimateF0 (steady, fs, 440.0), 440.0)) < 2.0);

    // Released: silent after the string has rung out.
    double tail = 0.0;
    for (auto i = static_cast<std::size_t> (4.8 * fs); i < r.left.size(); ++i)
        tail = std::max (tail, std::abs (r.left[i]));
    CHECK (tail < 1e-3);
}

TEST_CASE ("Engine handles blocks larger than announced in prepare", "[engine]")
{
    const auto fs = 48000.0;
    engine::ViolinEngine e;
    e.setSettings (quietSettings());
    e.prepare (fs, 32);
    const auto r = render (e, fs, 1000, 1.0, note (69, fs, 0.01, 0.9));
    CHECK (r.finite);
    CHECK (r.peak > 0.01f);
}

TEST_CASE ("Engine glides to a new note when playing legato", "[engine]")
{
    const auto fs = 48000.0;
    engine::ViolinEngine e;
    e.setSettings (quietSettings());
    e.prepare (fs, 256);

    std::vector<std::pair<int, juce::MidiMessage>> events {
        { 0, juce::MidiMessage::noteOn (1, 69, 0.8f) },
        { static_cast<int> (0.8 * fs), juce::MidiMessage::noteOn (1, 71, 0.8f) },
        { static_cast<int> (0.9 * fs), juce::MidiMessage::noteOff (1, 69) },
        { static_cast<int> (2.0 * fs), juce::MidiMessage::noteOff (1, 71) },
    };
    const auto r = render (e, fs, 256, 2.0, events);
    const std::span<const double> second (r.left.data() + static_cast<std::size_t> (1.3 * fs),
                                          static_cast<std::size_t> (0.6 * fs));
    const auto b4 = test::midiToHz (71);
    CHECK (std::abs (test::cents (test::estimateF0 (second, fs, b4), b4)) < 2.0);
    CHECK (e.getViolin().noteOnString (2) == 71); // A string
}

TEST_CASE ("Every body and quality produces finite, bounded sound", "[engine][body]")
{
    const auto body = GENERATE (0, 1, 2, 3);
    const auto quality = GENERATE (engine::Body::Quality::modal, engine::Body::Quality::convolution);
    CAPTURE (body, quality == engine::Body::Quality::modal);

    const auto fs = 48000.0;
    engine::ViolinEngine e;
    auto s = quietSettings (quality);
    s.body = body;
    s.output.sordino = 0.5f;
    s.output.width = 1.0f;
    s.output.room = 0.5f;
    e.setSettings (s);
    e.prepare (fs, 512);

    const auto r = render (e, fs, 512, 1.0, note (55, fs, 0.0, 0.8, 1.0f));
    CHECK (r.finite);
    CHECK (r.peak > 0.01f);
    CHECK (r.peak <= 1.0f);
}

TEST_CASE ("Engine survives random MIDI and settings changes", "[engine]")
{
    const auto fs = 44100.0;
    engine::ViolinEngine e;
    e.setSettings (quietSettings());
    e.prepare (fs, 128);

    juce::Random rng (11);
    juce::AudioBuffer<float> buffer (2, 128);
    bool finite = true;

    for (int block = 0; block < 2000; ++block)
    {
        juce::MidiBuffer midi;
        if (rng.nextInt (8) == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, 55 + rng.nextInt (46), rng.nextFloat()), rng.nextInt (128));
        if (rng.nextInt (10) == 0)
            midi.addEvent (juce::MidiMessage::noteOff (1, 55 + rng.nextInt (46)), rng.nextInt (128));
        if (rng.nextInt (20) == 0)
            midi.addEvent (juce::MidiMessage::pitchWheel (1, rng.nextInt (16384)), rng.nextInt (128));
        if (rng.nextInt (20) == 0)
            midi.addEvent (juce::MidiMessage::controllerEvent (1, rng.nextBool() ? 1 : 11, rng.nextInt (128)), 0);

        auto s = quietSettings();
        s.performance.voice.bowPosition = 0.03 + 0.27 * rng.nextDouble();
        s.performance.voice.bowPressure = rng.nextDouble();
        s.output.sordino = rng.nextFloat();
        s.body = rng.nextInt (4);
        e.setSettings (s);
        e.process (buffer, midi);

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 128; ++i)
                finite = finite && std::isfinite (buffer.getSample (ch, i));
    }
    CHECK (finite);
}

// Not run by default: `ViolinSynthTests "[.diagnostics]"` prints levels and CPU use.
TEST_CASE ("Diagnostics: level and CPU per body", "[.diagnostics]")
{
    const auto fs = 48000.0;
    for (int body = 0; body < 4; ++body)
    {
        for (auto quality : { engine::Body::Quality::convolution, engine::Body::Quality::modal })
        {
            engine::ViolinEngine e;
            auto s = quietSettings (quality);
            s.body = body;
            s.output.gainDb = -40.0f; // measure below the limiter
            e.setSettings (s);
            e.prepare (fs, 512);

            const auto start = std::chrono::steady_clock::now();
            const auto r = render (e, fs, 512, 10.0, note (69, fs, 0.0, 9.0));
            const auto seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();

            double sum = 0.0;
            for (auto i = static_cast<std::size_t> (1.0 * fs); i < static_cast<std::size_t> (8.0 * fs); ++i)
                sum += r.left[i] * r.left[i];
            const auto rms = std::sqrt (sum / (7.0 * fs));

            std::cout << engine::Body::names[static_cast<std::size_t> (body)]
                      << (quality == engine::Body::Quality::modal ? " modal" : " convolution") << ": peak "
                      << juce::Decibels::gainToDecibels (r.peak) + 40.0f << " dBFS, rms "
                      << juce::Decibels::gainToDecibels (static_cast<float> (rms)) + 40.0f << " dBFS, CPU "
                      << 100.0 * seconds / 10.0 << "% of one core\n";
        }
    }
}
