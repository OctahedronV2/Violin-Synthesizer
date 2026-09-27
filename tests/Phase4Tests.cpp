#include "TestUtilities.h"
#include "engine/ViolinEngine.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <numbers>

using namespace violinsynth;
using Catch::Approx;

namespace
{
constexpr double fs = 48000.0;
constexpr int block = 256;
constexpr int G = 0, D = 1, A = 2, E = 3;

engine::EngineSettings plainSettings()
{
    engine::EngineSettings s;
    s.performance.voice.vibratoDepthCents = 0.0;
    s.performance.voice.humanise = 0.0;
    s.performance.voice.resonance = 0.0;
    s.output.room = 0.0f;
    s.output.width = 0.0f;
    s.bodyQuality = engine::Body::Quality::modal;
    return s;
}

struct Event
{
    double time;
    juce::MidiMessage message;
};

// Renders the mono (left) output; `probe` is called after each block.
template <typename Probe>
std::vector<double> run (engine::ViolinEngine& e, double seconds, std::vector<Event> events, Probe&& probe)
{
    std::vector<double> out;
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
            out.push_back (buffer.getSample (0, i));
        probe (static_cast<double> (start + block) / fs);
    }
    return out;
}

std::vector<double> run (engine::ViolinEngine& e, double seconds, std::vector<Event> events)
{
    return run (e, seconds, std::move (events), [] (double) {});
}

// Amplitude of the component at `freq` (Goertzel over a Hann window).
double toneLevel (const std::vector<double>& x, double from, double to, double freq)
{
    const auto a = static_cast<std::size_t> (from * fs);
    const auto b = static_cast<std::size_t> (to * fs);
    const auto n = static_cast<double> (b - a);
    const auto w = 2.0 * std::numbers::pi * freq / fs;
    double re = 0.0, im = 0.0;
    for (auto i = a; i < b; ++i)
    {
        const auto k = static_cast<double> (i - a);
        const auto hann = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * k / n);
        re += x[i] * hann * std::cos (w * k);
        im -= x[i] * hann * std::sin (w * k);
    }
    return 2.0 * std::sqrt (re * re + im * im) / (0.5 * n);
}

double rms (const std::vector<double>& x, double from, double to)
{
    double sum = 0.0;
    for (auto i = static_cast<std::size_t> (from * fs); i < static_cast<std::size_t> (to * fs); ++i)
        sum += x[i] * x[i];
    return std::sqrt (sum / ((to - from) * fs));
}

juce::MidiMessage on (int note, int channel = 1, float velocity = 0.8f)
{
    return juce::MidiMessage::noteOn (channel, note, velocity);
}
juce::MidiMessage off (int note, int channel = 1)
{
    return juce::MidiMessage::noteOff (channel, note);
}
} // namespace

TEST_CASE ("A chord plays as a double stop with both pitches sounding", "[phase4]")
{
    engine::ViolinEngine e;
    e.setSettings (plainSettings());
    e.prepare (fs, block);

    const auto x = run (e, 1.5, { { 0.0, on (62) }, { 0.005, on (69) }, { 1.2, off (62) }, { 1.2, off (69) } });
    CHECK (e.getViolin().noteOnString (D) == -1); // released by now

    const auto d4 = test::midiToHz (62), a4 = 440.0;
    const auto between = 0.5 * (d4 + a4);
    CHECK (toneLevel (x, 0.4, 1.1, d4) > 10.0 * toneLevel (x, 0.4, 1.1, between));
    CHECK (toneLevel (x, 0.4, 1.1, a4) > 10.0 * toneLevel (x, 0.4, 1.1, between));
}

TEST_CASE ("The allocator's string choices reach the strings", "[phase4]")
{
    engine::ViolinEngine e;
    e.setSettings (plainSettings());
    e.prepare (fs, block);

    int onA = -2, onE = -2;
    run (e,
         1.0,
         { { 0.0, on (69) }, { 0.5, on (84) } },
         [&] (double t)
         {
             if (t > 0.3 && t < 0.4)
                 onA = e.getViolin().noteOnString (A);
             if (t > 0.8)
                 onE = e.getViolin().noteOnString (E);
         });
    CHECK (onA == 69);
    CHECK (onE == 84); // legato leap crossed to the E string
}

TEST_CASE ("Undamped open strings resonate sympathetically", "[phase4]")
{
    auto tail = [] (double resonance)
    {
        engine::ViolinEngine e;
        auto s = plainSettings();
        s.performance.voice.resonance = resonance;
        e.setSettings (s);
        e.prepare (fs, block);
        // A5 on the E string: its pitch is the 2nd harmonic of the open A string.
        const auto x = run (e, 2.5, { { 0.0, on (81) }, { 1.0, off (81) } });
        return toneLevel (x, 1.4, 2.4, 440.0) + toneLevel (x, 1.4, 2.4, 880.0);
    };

    const auto dry = tail (0.0);
    const auto resonant = tail (1.0);
    INFO ("dry " << dry << ", resonant " << resonant);
    CHECK (resonant > 1.5 * dry);
}

TEST_CASE ("Strong resonance stays stable", "[phase4]")
{
    engine::ViolinEngine e;
    auto s = plainSettings();
    s.performance.voice.resonance = 1.0;
    e.setSettings (s);
    e.prepare (fs, block);

    const auto x = run (e,
                        6.0,
                        { { 0.0, on (55, 1, 1.0f) },
                          { 0.0, on (62, 1, 1.0f) },
                          { 0.0, on (69, 1, 1.0f) },
                          { 0.0, on (76, 1, 1.0f) },
                          { 2.0, off (55) },
                          { 2.0, off (62) },
                          { 2.0, off (69) },
                          { 2.0, off (76) } });
    for (auto v : x)
        REQUIRE (std::isfinite (v));
    CHECK (rms (x, 5.0, 6.0) < 0.1 * rms (x, 1.0, 2.0)); // decays after release

    // One loud note driving three open strings at maximum resonance.
    for (int note : { 57, 64, 69, 81 })
    {
        CAPTURE (note);
        engine::ViolinEngine single;
        single.setSettings (s);
        single.prepare (fs, block);
        const auto y = run (single, 6.0, { { 0.0, on (note, 1, 1.0f) }, { 2.0, off (note) } });
        CHECK (rms (y, 5.0, 6.0) < 0.1 * rms (y, 1.0, 2.0));
    }
}

TEST_CASE ("MPE: member channels bend only their own note", "[phase4][mpe]")
{
    engine::ViolinEngine e;
    auto s = plainSettings();
    s.performance.mpe = true;
    s.performance.mpeBendRangeSemitones = 48.0;
    e.setSettings (s);
    e.prepare (fs, block);

    const auto oneSemitoneUp = 8192 + static_cast<int> (std::lround (8192.0 / 48.0));
    double fA = 0.0, fE = 0.0;
    run (e,
         1.0,
         { { 0.0, on (69, 2) }, { 0.0, on (76, 3) }, { 0.3, juce::MidiMessage::pitchWheel (2, oneSemitoneUp) } },
         [&] (double t)
         {
             if (t > 0.8 && fA == 0.0)
             {
                 fA = e.getViolin().stringF0 (A);
                 fE = e.getViolin().stringF0 (E);
             }
         });
    CHECK (test::cents (fA, test::midiToHz (70)) == Approx (0.0).margin (3.0));
    CHECK (test::cents (fE, test::midiToHz (76)) == Approx (0.0).margin (3.0));
}

TEST_CASE ("MPE: the master channel bends every note", "[phase4][mpe]")
{
    engine::ViolinEngine e;
    auto s = plainSettings();
    s.performance.mpe = true;
    e.setSettings (s);
    e.prepare (fs, block);

    double fA = 0.0, fE = 0.0;
    run (e,
         1.0,
         { { 0.0, on (69, 2) }, { 0.0, on (76, 3) }, { 0.3, juce::MidiMessage::pitchWheel (1, 16383) } },
         [&] (double t)
         {
             if (t > 0.8 && fA == 0.0)
             {
                 fA = e.getViolin().stringF0 (A);
                 fE = e.getViolin().stringF0 (E);
             }
         });
    CHECK (test::cents (fA, test::midiToHz (71)) == Approx (0.0).margin (3.0)); // +2 semitones
    CHECK (test::cents (fE, test::midiToHz (78)) == Approx (0.0).margin (3.0));
}

TEST_CASE ("Long notes get automatic bow changes", "[phase4]")
{
    auto changes = [] (bool enabled)
    {
        engine::ViolinEngine e;
        auto s = plainSettings();
        s.performance.voice.autoBowChange = enabled;
        e.setSettings (s);
        e.prepare (fs, block);
        run (e, 8.0, { { 0.0, on (69, 1, 1.0f) }, { 7.9, off (69) } }); // fast bow: 0.6 m/s
        return e.getViolin().bowChangeCount();
    };

    CHECK (changes (true) >= 5); // 62 cm of bow at 0.6 m/s lasts about a second
    CHECK (changes (false) == 0);
}

TEST_CASE ("Every play mode keeps the engine finite under random input", "[phase4]")
{
    for (auto mode : { engine::PlayMode::automatic, engine::PlayMode::monoLegato, engine::PlayMode::poly })
    {
        engine::ViolinEngine e;
        auto s = plainSettings();
        s.performance.playMode = mode;
        s.performance.voice.resonance = 1.0;
        s.performance.voice.humanise = 1.0;
        s.performance.voice.vibratoDepthCents = 40.0;
        s.performance.mpe = mode == engine::PlayMode::poly;
        e.setSettings (s);
        e.prepare (fs, block);

        juce::Random rng (static_cast<juce::int64> (mode) + 3);
        std::vector<Event> events;
        for (int i = 0; i < 300; ++i)
        {
            const auto t = rng.nextDouble() * 6.0;
            const auto ch = 1 + rng.nextInt (4);
            switch (rng.nextInt (6))
            {
                case 0:
                case 1:
                    events.push_back ({ t, on (55 + rng.nextInt (40), ch, rng.nextFloat()) });
                    break;
                case 2:
                    events.push_back ({ t, off (55 + rng.nextInt (40), ch) });
                    break;
                case 3:
                    events.push_back ({ t, juce::MidiMessage::pitchWheel (ch, rng.nextInt (16384)) });
                    break;
                case 4:
                    events.push_back ({ t, juce::MidiMessage::channelPressureChange (ch, rng.nextInt (128)) });
                    break;
                default:
                    events.push_back (
                        { t, juce::MidiMessage::controllerEvent (ch, rng.nextBool() ? 74 : 11, rng.nextInt (128)) });
                    break;
            }
        }
        std::sort (events.begin(), events.end(), [] (const Event& a, const Event& b) { return a.time < b.time; });

        const auto x = run (e, 6.5, events);
        bool finite = true;
        double peak = 0.0;
        for (auto v : x)
        {
            finite = finite && std::isfinite (v);
            peak = std::max (peak, std::abs (v));
        }
        CHECK (finite);
        CHECK (peak <= 1.0);
    }
}
