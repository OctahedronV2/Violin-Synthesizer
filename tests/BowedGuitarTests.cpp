#include "EngineTestUtilities.h"
#include "ProcessorTestUtilities.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

using namespace violinsynth;
using namespace violinsynth::test;
using engine::Instrument;

namespace
{
engine::EngineSettings guitarSettings()
{
    auto s = plainSettings();
    s.performance.instrument = Instrument::bowedGuitar;
    s.performance.drone = 0.0;
    s.drive = 0.0;
    return s;
}

// Pitch in Hz from the autocorrelation peak near `expected`, over a stretch of steady tone.
double measuredPitch (const std::vector<double>& x, double from, double to, double expected)
{
    const auto a = static_cast<std::size_t> (from * fs), b = static_cast<std::size_t> (to * fs);
    const auto lagFor = [] (double hz) { return fs / hz; };
    const auto lo = static_cast<int> (lagFor (expected * 1.06)), hi = static_cast<int> (lagFor (expected / 1.06)) + 1;
    auto correlation = [&] (int lag)
    {
        double sum = 0.0;
        for (auto i = a; i + static_cast<std::size_t> (lag) < b; ++i)
            sum += x[i] * x[i + static_cast<std::size_t> (lag)];
        return sum;
    };
    int best = lo;
    double bestValue = -1.0e300;
    for (int lag = lo; lag <= hi; ++lag)
        if (const auto c = correlation (lag); c > bestValue)
        {
            bestValue = c;
            best = lag;
        }
    // Parabolic interpolation around the peak.
    const auto l = correlation (best - 1), c = bestValue, r = correlation (best + 1);
    const auto shift = 0.5 * (l - r) / (l - 2.0 * c + r);
    return fs / (best + shift);
}

} // namespace

TEST_CASE ("Every string of the bowed guitar plays in tune", "[guitar]")
{
    engine::ViolinEngine e;
    e.setSettings (guitarSettings());
    e.prepare (fs, block);

    // Open strings, and a fretted note on each.
    for (const auto note : { 40, 45, 50, 55, 59, 64, 47, 57, 69, 76 })
    {
        e.reset();
        const auto out = run (e, 1.6, { { 0.0, on (note, 1, 0.7f) }, { 1.5, off (note) } });
        const auto expected = engine::midiToHz (note);
        const auto cents = 1200.0 * std::log2 (measuredPitch (out, 0.6, 1.4, expected) / expected);
        INFO ("note " << note << ": " << cents << " cents");
        CHECK (std::abs (cents) < 6.0);
        CHECK (rms (out, 0.6, 1.4) > 0.01);
    }
}

TEST_CASE ("The bowed guitar's range runs from its low E to the 22nd fret", "[guitar]")
{
    engine::ViolinEngine e;
    e.setSettings (guitarSettings());
    e.prepare (fs, block);

    // 39 (below the low E) and 87 are silent; the violin could not play 40 at all.
    for (const auto& [note, sounds] : { std::pair { 39, false }, { 40, true }, { 86, true }, { 87, false } })
    {
        e.reset();
        const auto out = run (e, 0.6, { { 0.0, on (note, 1, 0.7f) } });
        INFO ("note " << note);
        CHECK ((rms (out, 0.3, 0.6) > 1.0e-3) == sounds);
    }
}

TEST_CASE ("The flat bow catches the strings beside the played one", "[guitar]")
{
    engine::ViolinEngine e;
    auto s = guitarSettings();
    s.performance.drone = 0.5;
    s.performance.playMode = engine::PlayMode::poly;
    e.setSettings (s);
    e.prepare (fs, block);
    auto& guitar = e.getViolin();

    // E3 on the D string: the bow also lies on the A and G strings.
    run (e, 0.3, { { 0.0, on (52, 1, 0.7f) } });
    CHECK (guitar.noteOnString (2) == 52);
    CHECK (guitar.stringDrones (1));
    CHECK (guitar.stringDrones (3));
    CHECK_FALSE (guitar.stringDrones (0));
    CHECK_FALSE (guitar.stringDrones (4));

    // A second note two strings up: the bow now spans from the A string to the B.
    run (e, 0.3, { { 0.0, on (60, 1, 0.7f) } });
    CHECK (guitar.noteOnString (4) == 60);
    for (const auto string : { 1, 3, 5 })
        CHECK (guitar.stringDrones (string));

    // The note on the B string ends: the bow goes back to the D string and its neighbours.
    run (e, 0.3, { { 0.0, off (60) } });
    CHECK (guitar.stringDrones (1));
    CHECK (guitar.stringDrones (3));
    CHECK_FALSE (guitar.stringDrones (4));
    CHECK_FALSE (guitar.stringDrones (5));

    // Everything lifts with the last note.
    run (e, 0.3, { { 0.0, off (52) } });
    for (int string = 0; string < 6; ++string)
        CHECK_FALSE (guitar.stringDrones (string));

    // Drone 0: the bow is tilted onto the played string only.
    s.performance.drone = 0.0;
    e.setSettings (s);
    run (e, 0.3, { { 0.0, on (52, 1, 0.7f) } });
    for (int string = 0; string < 6; ++string)
        CHECK_FALSE (guitar.stringDrones (string));
}

TEST_CASE ("Switching the instrument silences the strings and changes their number", "[guitar]")
{
    engine::ViolinEngine e;
    auto s = plainSettings();
    e.setSettings (s);
    e.prepare (fs, block);
    CHECK (e.getViolin().numStrings() == 4);

    const auto violin = run (e, 0.5, { { 0.0, on (69, 1, 0.7f) } });
    CHECK (rms (violin, 0.3, 0.5) > 1.0e-3);

    s.performance.instrument = Instrument::bowedGuitar;
    e.setSettings (s);
    CHECK (e.getViolin().numStrings() == 6);
    CHECK (e.getViolin().noteOnString (2) == -1);
    const auto guitar = run (e, 1.0, { { 0.2, off (69) }, { 0.3, on (45, 1, 0.7f) } });
    CHECK (e.getViolin().noteOnString (1) == 45);
    CHECK (rms (guitar, 0.7, 1.0) > 1.0e-3);

    s.performance.instrument = Instrument::violin;
    e.setSettings (s);
    CHECK (e.getViolin().numStrings() == 4);
    CHECK (e.getViolin().noteOnString (1) == -1);
}

TEST_CASE ("The pickup and drive change the guitar's sound but not its level much", "[guitar]")
{
    // A moderately bowed note plays within a few dB whatever the pickup and
    // drive, so switching them does not jump in level.
    std::vector<double> levels;
    for (const auto pickup : { engine::Pickup::neck, engine::Pickup::both, engine::Pickup::bridge })
        for (const auto drive : { 0.0, 0.5, 1.0 })
        {
            engine::ViolinEngine e;
            auto s = guitarSettings();
            s.performance.pickup = pickup;
            s.drive = drive;
            e.setSettings (s);
            e.prepare (fs, block);
            const auto out = run (e, 1.2, { { 0.0, on (52, 1, 0.6f) } });
            levels.push_back (20.0 * std::log10 (rms (out, 0.5, 1.2)));
        }
    const auto [lo, hi] = std::minmax_element (levels.begin(), levels.end());
    INFO ("levels from " << *lo << " to " << *hi << " dB");
    CHECK (*hi - *lo < 9.0);
    CHECK (*hi < -20.0);
    CHECK (*lo > -36.0);
}

// Not run by default: `ViolinSynthTests "[.rendermidi]"` renders a MIDI file
// through the plugin, for listening. Environment:
//   RENDER_MIDI    the MIDI file
//   RENDER_OUT     the WAV file to write
//   RENDER_PRESET  optional factory preset to start from, by name
//   RENDER_PARAMS  optional parameter values, as "instrument=1;drive=0.6" (plain values)
TEST_CASE ("Render a MIDI file through the plugin", "[.rendermidi]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const auto* midiPath = std::getenv ("RENDER_MIDI");
    const auto* outPath = std::getenv ("RENDER_OUT");
    REQUIRE (midiPath != nullptr);
    REQUIRE (outPath != nullptr);
    const double rate = 48000.0;
    const int blockSize = 256;

    juce::MidiFile file;
    {
        juce::FileInputStream stream { juce::File { juce::String (midiPath) } };
        REQUIRE (file.readFrom (stream));
    }
    file.convertTimestampTicksToSeconds();
    juce::MidiMessageSequence track;
    for (int t = 0; t < file.getNumTracks(); ++t)
        track.addSequence (*file.getTrack (t), 0.0);
    track.sort();

    ViolinSynthProcessor processor;
    if (const auto* name = std::getenv ("RENDER_PRESET"))
    {
        const auto& all = processor.getPresetManager().getPresets();
        const auto found = std::find_if (all.begin(), all.end(), [&] (const auto& p) { return p.name == name; });
        REQUIRE (found != all.end());
        processor.getPresetManager().load (static_cast<int> (found - all.begin()));
    }
    if (const auto* list = std::getenv ("RENDER_PARAMS"))
        for (const auto& item : juce::StringArray::fromTokens (list, ";", ""))
            if (item.contains ("="))
            {
                auto* parameter
                    = processor.getParameters().getParameter (item.upToFirstOccurrenceOf ("=", false, false));
                REQUIRE (parameter != nullptr);
                parameter->setValueNotifyingHost (
                    parameter->convertTo0to1 (item.fromFirstOccurrenceOf ("=", false, false).getFloatValue()));
            }
    processor.setRateAndBufferSizeDetails (rate, blockSize);
    processor.prepareToPlay (rate, blockSize);

    const auto total = static_cast<int> ((track.getEndTime() + 3.0) * rate);
    juce::AudioBuffer<float> output (2, total);
    juce::AudioBuffer<float> buffer (2, blockSize);
    int next = 0;
    for (int start = 0; start < total; start += blockSize)
    {
        const auto len = std::min (blockSize, total - start);
        buffer.setSize (2, len, false, false, true);
        juce::MidiBuffer midi;
        for (; next < track.getNumEvents(); ++next)
        {
            const auto& m = track.getEventPointer (next)->message;
            const auto position = static_cast<int> (m.getTimeStamp() * rate);
            if (position >= start + len)
                break;
            if (! m.isMetaEvent())
                midi.addEvent (m, std::max (0, position - start));
        }
        processor.processBlock (buffer, midi);
        for (int ch = 0; ch < 2; ++ch)
            output.copyFrom (ch, start, buffer, ch, 0, len);
    }

    const juce::File wavFile { juce::String (outPath) };
    wavFile.deleteFile();
    auto stream = std::unique_ptr<juce::OutputStream> (wavFile.createOutputStream());
    juce::WavAudioFormat wav;
    const auto options
        = juce::AudioFormatWriterOptions {}.withSampleRate (rate).withNumChannels (2).withBitsPerSample (24);
    auto writer = wav.createWriterFor (stream, options);
    REQUIRE (writer != nullptr);
    writer->writeFromAudioSampleBuffer (output, 0, total);
}

// Diagnostics, not run by default: `ViolinSynthTests "[.guitarlevels]"` prints
// the pickup signal's level (before the amplifier) across the range, the
// numbers GuitarAmp's calibration comes from.
TEST_CASE ("Print the bowed guitar's pickup levels", "[.guitarlevels]")
{
    const double rate = 192000.0;
    for (const auto pickup : { engine::Pickup::neck, engine::Pickup::both, engine::Pickup::bridge })
        for (const auto note : { 40, 45, 52, 57, 64, 69, 76, 84 })
        {
            engine::Violin guitar;
            engine::PerformanceSettings s;
            s.instrument = Instrument::bowedGuitar;
            s.pickup = pickup;
            s.drone = 0.0;
            s.velocityTop = 1.0;
            s.voice.vibratoDepthCents = 0.0;
            guitar.setSettings (s);
            guitar.prepare (rate);
            guitar.handleMidi (juce::MidiMessage::noteOn (1, note, 0.6f));
            std::vector<float> out (static_cast<std::size_t> (rate));
            guitar.render (out.data(), static_cast<int> (out.size()));
            double sum = 0.0, peak = 0.0;
            for (auto i = out.size() / 2; i < out.size(); ++i)
            {
                const auto x = static_cast<double> (out[i]);
                sum += x * x;
                peak = std::max (peak, std::abs (x));
            }
            std::printf ("pickup %d note %d: rms %.4f peak %.4f\n",
                         static_cast<int> (pickup),
                         note,
                         std::sqrt (sum / static_cast<double> (out.size() / 2)),
                         peak);
        }
}

namespace
{
// The aperiodic share of the signal in dB, as the scratch meter in
// BowNoiseTests.cpp. The bowed string sounds 1.4 cents sharp of the note
// (StringVoice.cpp, torsionTuning); predicting at the note itself would read
// the guitar's bright pickup signal as noise, so this predicts at the pitch
// the string sounds.
double noiseDb (const std::vector<double>& x, double from, double to, double f0)
{
    const auto period = fs / (f0 * 1.000809);
    const auto whole = static_cast<std::size_t> (period);
    const auto frac = period - static_cast<double> (whole);
    const auto chunk = std::max<std::size_t> (2 * whole, 64);
    const auto first = std::max (static_cast<std::size_t> (from * fs), whole + 1);
    const auto last = static_cast<std::size_t> (to * fs);
    double residual = 0.0, energy = 0.0;
    for (auto start = first; start < last; start += chunk)
    {
        const auto end = std::min (start + chunk, last);
        double xy = 0.0, yy = 0.0, xx = 0.0;
        for (auto i = start; i < end; ++i)
        {
            const auto delayed = (1.0 - frac) * x[i - whole] + frac * x[i - whole - 1];
            xy += x[i] * delayed;
            yy += delayed * delayed;
            xx += x[i] * x[i];
        }
        const auto g = yy > 0.0 ? std::clamp (xy / yy, 0.5, 2.0) : 1.0;
        residual += xx - 2.0 * g * xy + g * g * yy;
        energy += xx;
    }
    return 10.0 * std::log10 (std::max (residual, 1.0e-30) / std::max (energy, 1.0e-30));
}

// How cleanly a set of single notes sounds: whether each string settles into
// Helmholtz motion (one slip per period, from 0.6 s), how many notes have a
// 50 ms stretch of scratch louder than -20 dB after the first 100 ms, and the
// average scratch from 0.3 s.
struct NoteStats
{
    int notes = 0, scratchy = 0, samples = 0, helmholtz = 0;
    double slips = 0.0, steadyDb = 0.0;

    void print (const char* label) const
    {
        std::printf ("%s: slips per period %.2f, Helmholtz %3.0f%%, scratchy %2d of %d, steady %.1f dB\n",
                     label,
                     slips / samples,
                     100.0 * helmholtz / samples,
                     scratchy,
                     notes,
                     steadyDb / notes);
    }
};

void measureNote (engine::EngineSettings s, int note, float velocity, NoteStats& stats)
{
    s.performance.playMode = engine::PlayMode::poly;
    engine::ViolinEngine e;
    e.setSettings (s);
    e.prepare (fs, block);
    const auto x = run (e,
                        1.2,
                        { { 0.0, on (note, 1, velocity) } },
                        [&] (double t)
                        {
                            if (t < 0.6)
                                return;
                            const auto& g = e.getViolin();
                            for (int string = 0; string < g.numStrings(); ++string)
                                if (g.noteOnString (string) == note)
                                {
                                    const auto slips = g.stringSlipsPerPeriod (string);
                                    stats.slips += slips;
                                    stats.helmholtz += std::abs (slips - 1.0) < 0.2 ? 1 : 0;
                                    ++stats.samples;
                                }
                        });
    double worst = -100.0;
    for (auto u = 0.1; u < 1.1; u += 0.05)
        worst = std::max (worst, noiseDb (x, u, u + 0.05, engine::midiToHz (note)));
    stats.scratchy += worst > -20.0 ? 1 : 0;
    stats.steadyDb += noiseDb (x, 0.3, 1.15, engine::midiToHz (note));
    ++stats.notes;
}
} // namespace

// Diagnostics, not run by default: `ViolinSynthTests "[.guitarscratch]"`
// prints how cleanly the bowed guitar speaks across its range at three
// pressures, with the violin for comparison (docs/BOWED_GUITAR.md).
TEST_CASE ("Print how cleanly the bowed guitar speaks", "[.guitarscratch]")
{
    for (const auto instrument : { Instrument::violin, Instrument::bowedGuitar })
        for (const auto pressure : { 0.2, 0.5, 0.8 })
        {
            NoteStats stats;
            for (const auto note : { 40, 43, 45, 48, 50, 53, 55, 57, 59, 62, 64, 67, 71, 76 })
                for (const auto velocity : { 0.5f, 0.8f })
                {
                    if (note < instrumentSpec (instrument).lowestNote)
                        continue;
                    auto s = guitarSettings();
                    s.performance.instrument = instrument;
                    s.performance.voice.bowPressure = pressure;
                    measureNote (s, note, velocity, stats);
                }
            const auto label = juce::String (instrument == Instrument::violin ? "violin" : "guitar") + ", pressure "
                + juce::String (pressure, 1);
            stats.print (label.toRawUTF8());
        }
}

// Diagnostics, not run by default: `ViolinSynthTests "[.guitarstrings]"`
// prints each guitar string's behaviour across its force window, the data
// its window in StringData.h was set from.
TEST_CASE ("Print each bowed guitar string across its force window", "[.guitarstrings]")
{
    for (int string = 0; string < engine::bowedGuitarSpec.numStrings; ++string)
        for (const auto pressure : { 0.0, 0.25, 0.5, 0.75, 1.0 })
        {
            const auto& spec = engine::bowedGuitarSpec.string (string);
            NoteStats stats;
            for (const auto fret : { 0, 3, 5, 7, 10 })
                for (const auto velocity : { 0.5f, 0.8f })
                {
                    auto s = guitarSettings();
                    s.performance.voice.bowPressure = pressure;
                    measureNote (s, spec.openMidiNote + fret, velocity, stats);
                }
            const auto fraction = spec.forceWindowLow + (spec.playerWindowHigh - spec.forceWindowLow) * pressure;
            const auto label = juce::String (spec.name) + " string, force " + juce::String (fraction, 2) + " F_max";
            stats.print (label.toRawUTF8());
        }
}

// Diagnostics, not run by default: `ViolinSynthTests "[.guitardrones]"` prints
// how the open strings the flat bow catches sound: the player's scratch
// measure and slips per period on each drone, over many notes.
TEST_CASE ("Print how cleanly the bowed guitar's drones sound", "[.guitardrones]")
{
    for (const auto drone : { 0.0001, 0.5, 1.0 })
    {
        double scratch = 0.0, slips = 0.0;
        int count = 0, helmholtz = 0;
        for (const auto note : { 42, 47, 52, 54, 57, 60, 62, 66, 69, 72 })
            for (const auto velocity : { 0.5f, 0.8f })
            {
                auto s = guitarSettings();
                s.performance.drone = drone;
                engine::ViolinEngine e;
                e.setSettings (s);
                e.prepare (fs, block);
                run (e,
                     1.2,
                     { { 0.0, on (note, 1, velocity) } },
                     [&] (double t)
                     {
                         if (t < 0.6)
                             return;
                         const auto& g = e.getViolin();
                         for (int string = 0; string < g.numStrings(); ++string)
                             if (g.stringDrones (string))
                             {
                                 scratch += g.stringScratch (string);
                                 slips += g.stringSlipsPerPeriod (string);
                                 helmholtz += std::abs (g.stringSlipsPerPeriod (string) - 1.0) < 0.2 ? 1 : 0;
                                 ++count;
                             }
                     });
            }
        std::printf ("drone %.2f: scratch %.1f dB, slips per period %.2f, Helmholtz %.0f%%\n",
                     drone,
                     10.0 * std::log10 (scratch / count),
                     slips / count,
                     100.0 * helmholtz / count);
    }
}
