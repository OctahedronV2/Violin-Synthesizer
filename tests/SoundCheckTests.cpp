// The sound check (docs/PHASE7.md, step 7.0): optimisations must not change
// how the violin sounds. Reference phrases are rendered through the whole
// engine with default settings and measured; the measurements are compared
// with tests/golden/soundcheck.json, recorded before any optimisation:
//
//   pitch    every 50 ms window of a steady note within 0.2 cents
//   tone     third-octave band levels, 100 Hz to 16 kHz, within 0.5 dB
//            (bands within 40 dB of the loudest)
//   level    the loudest 100 ms within 0.1 dB
//   onsets   each note reaches half its peak within 1 ms of the reference
//
// `ViolinSynthTests "[.soundcheck-update]"` records a new reference (only when
// a change of sound is intended), and `"[.soundcheck-render]"` writes the
// phrases as WAV files to the working directory for listening.

#include "EngineTestUtilities.h"
#include "TestUtilities.h"

#include <catch2/catch_test_macros.hpp>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_events/juce_events.h>

#include <map>

using namespace violinsynth;
using namespace violinsynth::test;

namespace
{
struct PitchSegment
{
    double from, to, expectedHz;
};

struct Phrase
{
    std::string name;
    double seconds;
    std::vector<Event> events;
    std::vector<double> onsets; // note-on times to measure
    std::vector<PitchSegment> pitch; // steady single-note stretches
    bool mpe = false;
};

std::vector<Phrase> phrases()
{
    using engine::Articulation;
    std::vector<Phrase> list;

    // A sustained note with vibrato.
    list.push_back (
        { "sustain", 3.5, { { 0.05, on (69, 1, 0.6f) }, { 3.0, off (69) } }, { 0.05 }, { { 1.2, 2.9, 440.0 } } });

    // A slurred line with slides.
    {
        Phrase p { "legato", 3.6, {}, {}, {} };
        const int notes[] = { 72, 74, 76, 79, 76, 72 };
        for (int i = 0; i < 6; ++i)
        {
            const auto t = 0.05 + 0.5 * i;
            p.events.push_back ({ t, on (notes[i], 1, 0.6f) });
            p.events.push_back ({ t + 0.55, off (notes[i]) });
            p.pitch.push_back ({ t + 0.3, t + 0.5, midiToHz (notes[i]) });
        }
        p.onsets.push_back (0.05);
        list.push_back (p);
    }

    // A four-note chord.
    list.push_back ({ "chord",
                      2.5,
                      { { 0.05, on (55, 1, 0.6f) },
                        { 0.05, on (62, 1, 0.6f) },
                        { 0.05, on (69, 1, 0.6f) },
                        { 0.05, on (76, 1, 0.6f) },
                        { 2.0, off (55) },
                        { 2.0, off (62) },
                        { 2.0, off (69) },
                        { 2.0, off (76) } },
                      { 0.05 },
                      {} });

    // One note in each articulation. Sul ponticello is left out: a light bow
    // by the bridge is chaotic, and its level moves with rounding differences
    // between platforms (docs/PHASE6.md).
    {
        Phrase p { "articulations", 8.0, {}, {}, {} };
        const Articulation order[] = { Articulation::legato,    Articulation::detache,  Articulation::staccato,
                                       Articulation::spiccato,  Articulation::tremolo,  Articulation::pizzicato,
                                       Articulation::harmonics, Articulation::sulTasto, Articulation::conSordino };
        double t = 0.05;
        for (auto a : order)
        {
            const auto ks = engine::firstKeyswitch + static_cast<int> (a);
            p.events.push_back ({ t - 0.02, on (ks) });
            p.events.push_back ({ t - 0.01, off (ks) });
            p.events.push_back ({ t, on (69, 1, 0.6f) });
            p.events.push_back ({ t + 0.5, off (69) });
            p.onsets.push_back (t);
            t += 0.85;
        }
        list.push_back (p);
    }

    // MPE: two notes bending on their own channels.
    {
        Phrase p { "mpe", 3.0, {}, { 0.05 }, {}, true };
        p.events.push_back ({ 0.05, on (62, 2, 0.6f) });
        p.events.push_back ({ 0.05, on (69, 3, 0.6f) });
        for (double t = 0.1; t < 2.4; t += 0.01)
        {
            const auto s = std::sin (2.0 * juce::MathConstants<double>::pi * 0.8 * t);
            p.events.push_back ({ t, juce::MidiMessage::pitchWheel (2, 8192 + static_cast<int> (200.0 * s)) });
            p.events.push_back ({ t, juce::MidiMessage::channelPressureChange (3, 60 + static_cast<int> (40.0 * s)) });
        }
        p.events.push_back ({ 2.5, off (62, 2) });
        p.events.push_back ({ 2.5, off (69, 3) });
        list.push_back (p);
    }

    // A long, loud note: the bow runs out of hair and changes automatically.
    list.push_back ({ "bow-change", 4.5, { { 0.05, on (67, 1, 1.0f) }, { 4.0, off (67) } }, { 0.05 }, {} });

    return list;
}

void waitForBody (engine::ViolinEngine& e)
{
    juce::AudioBuffer<float> buffer (2, block);
    for (int i = 0; i < 200; ++i)
    {
        e.process (buffer, {});
        juce::Thread::sleep (2);
    }
    e.reset();
}

// Mono (mid) render of a phrase through the whole engine, default settings.
std::vector<double> render (const Phrase& p)
{
    engine::EngineSettings s;
    s.performance.mpe = p.mpe;
    engine::ViolinEngine e;
    e.setSettings (s);
    e.prepare (fs, block);
    e.updateConvolutionBody();
    waitForBody (e);

    std::vector<double> out;
    juce::AudioBuffer<float> buffer (2, block);
    const auto total = static_cast<int> (p.seconds * fs);
    for (int start = 0; start < total; start += block)
    {
        juce::MidiBuffer midi;
        for (const auto& ev : p.events)
        {
            const auto pos = static_cast<int> (ev.time * fs);
            if (pos >= start && pos < start + block)
                midi.addEvent (ev.message, pos - start);
        }
        e.process (buffer, midi);
        for (int i = 0; i < block; ++i)
            out.push_back (0.5 * static_cast<double> (buffer.getSample (0, i) + buffer.getSample (1, i)));
    }
    return out;
}

double rmsDb (const std::vector<double>& x, std::size_t from, std::size_t length)
{
    double sum = 0.0;
    for (auto i = from; i < std::min (x.size(), from + length); ++i)
        sum += x[i] * x[i];
    return 10.0 * std::log10 (sum / static_cast<double> (length) + 1.0e-24);
}

struct Features
{
    double level = 0.0;
    std::vector<double> bands; // dB
    std::vector<double> onsets; // seconds
    std::vector<double> pitch; // Hz, one per 50 ms window
};

Features measure (const Phrase& p, const std::vector<double>& x)
{
    Features f;

    // Level: loudest 100 ms.
    const auto window = static_cast<std::size_t> (0.1 * fs);
    f.level = -200.0;
    for (std::size_t i = 0; i + window < x.size(); i += window / 10)
        f.level = std::max (f.level, rmsDb (x, i, window));

    // Tone: averaged power spectrum (Hann, 8192 points, half overlap) in
    // third-octave bands.
    constexpr int order = 13, n = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<double> power (n / 2 + 1, 0.0);
    std::vector<float> frame (2 * n);
    for (std::size_t start = 0; start + n <= x.size(); start += n / 2)
    {
        std::fill (frame.begin(), frame.end(), 0.0f);
        for (int i = 0; i < n; ++i)
            frame[static_cast<std::size_t> (i)]
                = static_cast<float> (x[start + static_cast<std::size_t> (i)]
                                      * (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / n)));
        fft.performFrequencyOnlyForwardTransform (frame.data(), true);
        for (int k = 0; k <= n / 2; ++k)
            power[static_cast<std::size_t> (k)] += static_cast<double> (frame[static_cast<std::size_t> (k)])
                * static_cast<double> (frame[static_cast<std::size_t> (k)]);
    }
    for (int b = 0; b <= 22; ++b)
    {
        const auto centre = 100.0 * std::pow (2.0, b / 3.0);
        const auto lo = centre * std::pow (2.0, -1.0 / 6.0), hi = centre * std::pow (2.0, 1.0 / 6.0);
        double sum = 0.0;
        for (int k = 1; k <= n / 2; ++k)
        {
            const auto freq = k * fs / n;
            if (freq >= lo && freq < hi)
                sum += power[static_cast<std::size_t> (k)];
        }
        f.bands.push_back (10.0 * std::log10 (sum + 1.0e-24));
    }

    // Onsets: when a 2 ms RMS envelope first reaches half its peak in the
    // 300 ms after the note-on.
    const auto hop = static_cast<std::size_t> (0.0005 * fs), length = static_cast<std::size_t> (0.002 * fs);
    for (auto t0 : p.onsets)
    {
        const auto a = static_cast<std::size_t> (t0 * fs), b = static_cast<std::size_t> ((t0 + 0.3) * fs);
        double peak = -200.0;
        for (auto i = a; i < b; i += hop)
            peak = std::max (peak, rmsDb (x, i, length));
        auto onset = t0;
        for (auto i = a; i < b; i += hop)
            if (rmsDb (x, i, length) >= peak - 6.02)
            {
                onset = (static_cast<double> (i) + 0.5 * static_cast<double> (length)) / fs;
                break;
            }
        f.onsets.push_back (onset);
    }

    // Pitch: 50 ms windows over each steady stretch.
    const auto pitchWindow = static_cast<std::size_t> (0.05 * fs);
    for (const auto& segment : p.pitch)
        for (auto t = segment.from; t + 0.05 <= segment.to + 1.0e-9; t += 0.05)
        {
            const auto start = static_cast<std::size_t> (t * fs);
            f.pitch.push_back (
                estimateF0 (std::span<const double> (x.data() + start, pitchWindow), fs, segment.expectedHz));
        }
    return f;
}

juce::File referenceFile()
{
    return juce::File (VIOLINSYNTH_TEST_DATA_DIR).getChildFile ("golden/soundcheck.json");
}

juce::var toVar (const std::vector<double>& v)
{
    juce::Array<juce::var> a;
    for (auto x : v)
        a.add (x);
    return a;
}

std::vector<double> fromVar (const juce::var& v)
{
    std::vector<double> out;
    if (const auto* a = v.getArray())
        for (const auto& x : *a)
            out.push_back (static_cast<double> (x));
    return out;
}
} // namespace

TEST_CASE ("Sound check: phrases sound as recorded before optimisation", "[soundcheck]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    const auto reference = juce::JSON::parse (referenceFile());
    REQUIRE (reference.isObject());

    for (const auto& phrase : phrases())
    {
        CAPTURE (phrase.name);
        const auto& ref = reference[juce::Identifier (phrase.name)];
        REQUIRE (ref.isObject());
        const auto now = measure (phrase, render (phrase));

        CHECK (std::abs (now.level - static_cast<double> (ref["level"])) < 0.1);

        const auto bands = fromVar (ref["bands"]);
        REQUIRE (bands.size() == now.bands.size());
        const auto loudest = *std::max_element (bands.begin(), bands.end());
        for (std::size_t b = 0; b < bands.size(); ++b)
            if (bands[b] > loudest - 40.0)
            {
                CAPTURE (b, bands[b], now.bands[b]);
                CHECK (std::abs (now.bands[b] - bands[b]) < 0.5);
            }

        const auto onsets = fromVar (ref["onsets"]);
        REQUIRE (onsets.size() == now.onsets.size());
        for (std::size_t i = 0; i < onsets.size(); ++i)
        {
            CAPTURE (i, onsets[i], now.onsets[i]);
            CHECK (std::abs (now.onsets[i] - onsets[i]) < 0.001);
        }

        const auto pitch = fromVar (ref["pitch"]);
        REQUIRE (pitch.size() == now.pitch.size());
        for (std::size_t i = 0; i < pitch.size(); ++i)
        {
            CAPTURE (i, pitch[i], now.pitch[i]);
            CHECK (std::abs (cents (now.pitch[i], pitch[i])) < 0.2);
        }
    }
}

TEST_CASE ("Sound check: record the reference", "[.soundcheck-update]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    auto* root = new juce::DynamicObject();
    for (const auto& phrase : phrases())
    {
        const auto f = measure (phrase, render (phrase));
        auto* o = new juce::DynamicObject();
        o->setProperty ("level", f.level);
        o->setProperty ("bands", toVar (f.bands));
        o->setProperty ("onsets", toVar (f.onsets));
        o->setProperty ("pitch", toVar (f.pitch));
        root->setProperty (juce::Identifier (phrase.name), juce::var (o));
    }
    REQUIRE (referenceFile().replaceWithText (juce::JSON::toString (juce::var (root))));
}

TEST_CASE ("Sound check: render the phrases", "[.soundcheck-render]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    for (const auto& phrase : phrases())
    {
        const auto x = render (phrase);
        juce::AudioBuffer<float> buffer (1, static_cast<int> (x.size()));
        for (std::size_t i = 0; i < x.size(); ++i)
            buffer.setSample (0, static_cast<int> (i), static_cast<float> (x[i]));
        const auto file = juce::File::getCurrentWorkingDirectory().getChildFile ("soundcheck-" + phrase.name + ".wav");
        file.deleteFile();
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        juce::WavAudioFormat wav;
        const auto options
            = juce::AudioFormatWriterOptions {}.withSampleRate (fs).withNumChannels (1).withBitsPerSample (24);
        auto writer = wav.createWriterFor (stream, options);
        REQUIRE (writer != nullptr);
        writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
    }
}
