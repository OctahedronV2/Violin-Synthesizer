// ViolinSynthBench: CPU benchmarks for the engine (docs/PHASE7.md, step 7.0).
//
//   ViolinSynthBench                     all scenarios, printed as a table
//   ViolinSynthBench --json out.json     also writes the results as JSON
//   ViolinSynthBench --ci                the shorter set run under Callgrind in CI
//   ViolinSynthBench --only chord4       scenarios whose name contains "chord4"
//   ViolinSynthBench --seconds 5         audio rendered per scenario (default 10)
//
// Each scenario plays MIDI into one or more engines, block by block, and times
// every block. Under Callgrind (valgrind --tool=callgrind --collect-atstart=no)
// each scenario's rendering is dumped separately, so
// scripts/bench_instructions.py can report instructions per output sample.

#include "engine/ViolinEngine.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#if __has_include(<valgrind/callgrind.h>)
#include <valgrind/callgrind.h>
#else
#define CALLGRIND_ZERO_STATS
#define CALLGRIND_START_INSTRUMENTATION
#define CALLGRIND_STOP_INSTRUMENTATION
#define CALLGRIND_DUMP_STATS_AT(name)
#endif

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <vector>

using namespace violinsynth;

// Scenarios are designated initialisers that leave most fields at their defaults.
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

namespace
{
struct Event
{
    double time; // seconds
    juce::MidiMessage message;
};

struct Scenario
{
    std::string name;
    std::string description;
    double sampleRate = 48000.0;
    int blockSize = 128;
    bool randomBlockSizes = false;
    bool mpe = false;
    int engines = 1;
    engine::Body::Quality body = engine::Body::Quality::convolution;
    bool inCi = true;
    // MIDI for engine `index` of `engines`, over `seconds`.
    std::function<std::vector<Event> (int index, double seconds)> midi;
    // Called before each block with the time in seconds, to change settings.
    std::function<void (engine::ViolinEngine&, engine::EngineSettings&, double)> automate;
};

struct Result
{
    std::string name;
    double cpuPercent = 0.0; // mean, of one core
    double p99 = 0.0, p999 = 0.0, worst = 0.0; // block time as % of the block's duration
    double seconds = 0.0;
    double sampleRate = 0.0;
};

juce::MidiMessage on (int note, float velocity = 0.6f, int channel = 1)
{
    return juce::MidiMessage::noteOn (channel, note, velocity);
}

juce::MidiMessage off (int note, int channel = 1)
{
    return juce::MidiMessage::noteOff (channel, note);
}

std::vector<Event> held (std::vector<int> notes, double seconds, float velocity = 0.6f)
{
    std::vector<Event> events;
    for (auto n : notes)
        events.push_back ({ 0.0, on (n, velocity) });
    for (auto n : notes)
        events.push_back ({ 0.9 * seconds, off (n) });
    return events;
}

// Short notes with an articulation keyswitch, repeated.
std::vector<Event> repeated (int keyswitch, double noteLength, double gap, double seconds)
{
    std::vector<Event> events { { 0.0, on (keyswitch) }, { 0.001, off (keyswitch) } };
    const int notes[] = { 69, 71, 72, 74, 76, 74, 72, 71 };
    int i = 0;
    for (double t = 0.01; t + noteLength < 0.9 * seconds; t += noteLength + gap, ++i)
    {
        const auto n = notes[i % 8];
        events.push_back ({ t, on (n) });
        events.push_back ({ t + noteLength, off (n) });
    }
    return events;
}

std::vector<Event> legatoPhrase (double seconds)
{
    const int notes[] = { 67, 69, 71, 72, 74, 76, 79, 76, 74, 72, 71, 69 };
    std::vector<Event> events;
    int i = 0;
    for (double t = 0.0; t + 0.5 < 0.9 * seconds; t += 0.4, ++i)
    {
        events.push_back ({ t, on (notes[i % 12]) });
        events.push_back ({ t + 0.45, off (notes[i % 12]) }); // overlaps the next note: slurred
    }
    return events;
}

std::vector<Event> mpeChord (double seconds)
{
    std::vector<Event> events;
    const int notes[] = { 55, 62, 69, 76 };
    for (int i = 0; i < 4; ++i)
        events.push_back ({ 0.0, on (notes[i], 0.6f, i + 2) });
    for (double t = 0.05; t < 0.9 * seconds; t += 0.01) // every 10 ms: bend and pressure per note
        for (int i = 0; i < 4; ++i)
        {
            const auto phase = 2.0 * juce::MathConstants<double>::pi * (0.7 + 0.2 * i) * t;
            events.push_back (
                { t, juce::MidiMessage::pitchWheel (i + 2, 8192 + static_cast<int> (300.0 * std::sin (phase))) });
            events.push_back (
                { t,
                  juce::MidiMessage::channelPressureChange (i + 2, 64 + static_cast<int> (40.0 * std::cos (phase))) });
        }
    for (int i = 0; i < 4; ++i)
        events.push_back ({ 0.9 * seconds, off (notes[i], i + 2) });
    return events;
}

std::vector<Scenario> scenarios()
{
    std::vector<Scenario> list;
    auto add = [&] (Scenario s) { list.push_back (std::move (s)); };
    auto one = [] (int, double seconds) { return held ({ 69 }, seconds); };

    add ({ .name = "idle", .description = "No notes", .midi = [] (int, double) { return std::vector<Event> {}; } });
    add ({ .name = "tail", .description = "One short note, then its tail and silence", .midi = [] (int, double) {
              return std::vector<Event> { { 0.0, on (69) }, { 0.3, off (69) } };
          } });
    add ({ .name = "note", .description = "One sustained note, vibrato (T1)", .midi = one });
    add ({ .name = "note-light",
           .description = "One note, light body",
           .body = engine::Body::Quality::modal,
           .midi = one });
    add ({ .name = "chord2", .description = "Double stop", .midi = [] (int, double s) {
              return held ({ 62, 69 }, s);
          } });
    add ({ .name = "chord3", .description = "Triple stop", .midi = [] (int, double s) {
              return held ({ 55, 62, 69 }, s);
          } });
    add ({ .name = "chord4", .description = "Quadruple stop (T2)", .midi = [] (int, double s) {
              return held ({ 55, 62, 69, 76 }, s);
          } });
    add ({ .name = "legato", .description = "Slurred phrase with slides", .midi = [] (int, double s) {
              return legatoPhrase (s);
          } });
    add ({ .name = "tremolo", .description = "Tremolo notes", .midi = [] (int, double s) {
              return repeated (28, 0.8, 0.1, s);
          } });
    add ({ .name = "spiccato", .description = "Spiccato notes", .midi = [] (int, double s) {
              return repeated (27, 0.1, 0.05, s);
          } });
    add ({ .name = "pizzicato", .description = "Pizzicato notes", .midi = [] (int, double s) {
              return repeated (29, 0.15, 0.1, s);
          } });
    add ({ .name = "mpe4",
           .description = "MPE: four notes, own bend and pressure",
           .mpe = true,
           .midi = [] (int, double s) { return mpeChord (s); } });
    add ({ .name = "automation",
           .description = "Note with every parameter moving every block",
           .midi = one,
           .automate = [] (engine::ViolinEngine& e, engine::EngineSettings& s, double t)
           {
               const auto w = 0.5 + 0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * 0.5 * t);
               s.performance.voice.bowPosition = 0.06 + 0.12 * w;
               s.performance.voice.bowPressure = 0.3 + 0.4 * w;
               s.performance.voice.vibratoRateHz = 4.0 + 3.0 * w;
               s.performance.voice.vibratoDepthCents = 10.0 + 30.0 * w;
               s.performance.voice.resonance = 0.6 * w;
               s.output.width = static_cast<float> (w);
               s.output.room = static_cast<float> (0.1 + 0.3 * w);
               s.output.sordino = static_cast<float> (0.5 * w);
               s.output.gainDb = static_cast<float> (-3.0 * w);
               e.setSettings (s);
           } });
    add ({ .name = "body-changes",
           .description = "Note while the body changes every 0.5 s",
           .midi = one,
           .automate = [] (engine::ViolinEngine& e, engine::EngineSettings& s, double t)
           {
               const auto body = static_cast<int> (t / 0.5) % 4;
               if (body != s.body)
               {
                   s.body = body;
                   e.setSettings (s);
                   e.updateConvolutionBody();
               }
           } });

    for (auto rate : { 44100.0, 88200.0, 96000.0, 176400.0, 192000.0 })
        add ({ .name = "rate-" + juce::String (rate / 1000.0, 1).toStdString(),
               .description = "One note at " + std::to_string (static_cast<int> (rate)) + " Hz",
               .sampleRate = rate,
               .midi = one });
    for (auto size : { 16, 32, 64, 256, 512, 1024, 2048 })
        add ({ .name = "buffer-" + std::to_string (size),
               .description = "One note, " + std::to_string (size) + "-sample buffer",
               .blockSize = size,
               .inCi = size == 32 || size == 1024,
               .midi = one });
    add ({ .name = "buffer-random",
           .description = "One note, random buffer sizes up to 512",
           .blockSize = 512,
           .randomBlockSizes = true,
           .midi = one });
    add ({ .name = "chord4-buffer-32",
           .description = "Quadruple stop, 32-sample buffer (T4)",
           .blockSize = 32,
           .midi = [] (int, double s) { return held ({ 55, 62, 69, 76 }, s); } });

    for (auto n : { 4, 8, 16, 32, 60 })
        add ({ .name = "section-" + std::to_string (n),
               .description = std::to_string (n) + " engines, one note each, summed (T7)",
               .engines = n,
               .inCi = n <= 8,
               .midi = [] (int index, double s)
               {
                   const int notes[] = { 67, 69, 71, 72, 74, 76, 62, 64 };
                   return held ({ notes[index % 8] }, s);
               } });
    return list;
}

double percentile (std::vector<double> v, double p)
{
    if (v.empty())
        return 0.0;
    std::sort (v.begin(), v.end());
    const auto i = std::min (v.size() - 1, static_cast<std::size_t> (p * static_cast<double> (v.size())));
    return v[i];
}

Result run (const Scenario& scenario, double seconds)
{
    std::vector<std::unique_ptr<engine::ViolinEngine>> engines;
    std::vector<engine::EngineSettings> settings (static_cast<std::size_t> (scenario.engines));
    std::vector<std::vector<Event>> midi;
    for (int i = 0; i < scenario.engines; ++i)
    {
        auto e = std::make_unique<engine::ViolinEngine>();
        auto& s = settings[static_cast<std::size_t> (i)];
        s.bodyQuality = scenario.body;
        s.performance.mpe = scenario.mpe;
        e->setSettings (s);
        e->prepare (scenario.sampleRate, scenario.blockSize);
        e->updateConvolutionBody();
        engines.push_back (std::move (e));
        auto events = scenario.midi (i, seconds);
        std::stable_sort (events.begin(),
                          events.end(),
                          [] (const Event& a, const Event& b) { return a.time < b.time; });
        midi.push_back (std::move (events));
    }
    std::vector<std::size_t> nextEvent (engines.size(), 0);
    std::vector<juce::MidiBuffer> blockMidi (engines.size());

    // Let the convolution load its impulse response (on a background thread).
    juce::AudioBuffer<float> buffer (2, scenario.blockSize), mix (2, scenario.blockSize);
    for (int i = 0; i < 100; ++i)
    {
        for (auto& e : engines)
            e->process (buffer, {});
        juce::Thread::sleep (2);
    }
    for (auto& e : engines)
        e->reset();

    std::mt19937 random (1234);
    std::vector<double> blockPercent;
    const auto total = static_cast<int> (seconds * scenario.sampleRate);
    double busy = 0.0;

    CALLGRIND_ZERO_STATS;
    CALLGRIND_START_INSTRUMENTATION;
    for (int start = 0; start < total;)
    {
        auto length = std::min (scenario.blockSize, total - start);
        if (scenario.randomBlockSizes)
            length = std::min (length, 1 + static_cast<int> (random() % static_cast<unsigned> (scenario.blockSize)));
        buffer.setSize (2, length, false, false, true);
        mix.setSize (2, length, false, false, true);

        // This block's MIDI, outside the timed part.
        for (std::size_t i = 0; i < engines.size(); ++i)
        {
            blockMidi[i].clear();
            auto& n = nextEvent[i];
            for (; n < midi[i].size() && static_cast<int> (midi[i][n].time * scenario.sampleRate) < start + length; ++n)
                blockMidi[i].addEvent (midi[i][n].message,
                                       std::max (0, static_cast<int> (midi[i][n].time * scenario.sampleRate) - start));
        }

        const auto begin = std::chrono::steady_clock::now();
        mix.clear();
        for (std::size_t i = 0; i < engines.size(); ++i)
        {
            if (scenario.automate)
                scenario.automate (*engines[i], settings[i], start / scenario.sampleRate);
            engines[i]->process (buffer, blockMidi[i]);
            for (int c = 0; c < 2; ++c)
                mix.addFrom (c, 0, buffer, c, 0, length);
        }
        const auto elapsed = std::chrono::duration<double> (std::chrono::steady_clock::now() - begin).count();

        busy += elapsed;
        blockPercent.push_back (100.0 * elapsed / (length / scenario.sampleRate));
        start += length;
    }
    CALLGRIND_DUMP_STATS_AT (scenario.name.c_str());
    CALLGRIND_STOP_INSTRUMENTATION;

    Result r;
    r.name = scenario.name;
    r.seconds = seconds;
    r.sampleRate = scenario.sampleRate;
    r.cpuPercent = 100.0 * busy / seconds;
    r.p99 = percentile (blockPercent, 0.99);
    r.p999 = percentile (blockPercent, 0.999);
    r.worst = blockPercent.empty() ? 0.0 : *std::max_element (blockPercent.begin(), blockPercent.end());
    return r;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juce; // the convolution loads on a background thread
    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add (argv[i]);

    const auto ci = args.contains ("--ci");
    const auto seconds = args.contains ("--seconds") ? args[args.indexOf ("--seconds") + 1].getDoubleValue() : 10.0;
    const auto only = args.contains ("--only") ? args[args.indexOf ("--only") + 1] : juce::String();
    const auto jsonPath = args.contains ("--json") ? args[args.indexOf ("--json") + 1] : juce::String();

    std::printf ("CPU: %s, %d cores, %d MHz\n",
                 juce::SystemStats::getCpuModel().toRawUTF8(),
                 juce::SystemStats::getNumPhysicalCpus(),
                 juce::SystemStats::getCpuSpeedInMegahertz());
    std::printf ("OS: %s\n\n", juce::SystemStats::getOperatingSystemName().toRawUTF8());
    std::printf ("%-18s %8s %8s %8s %8s  %s\n", "scenario", "CPU %", "p99 %", "p99.9 %", "worst %", "description");

    juce::Array<juce::var> results;
    for (const auto& s : scenarios())
    {
        if ((ci && ! s.inCi) || (only.isNotEmpty() && ! juce::String (s.name).contains (only)))
            continue;
        const auto r = run (s, seconds);
        std::printf ("%-18s %8.2f %8.1f %8.1f %8.1f  %s\n",
                     r.name.c_str(),
                     r.cpuPercent,
                     r.p99,
                     r.p999,
                     r.worst,
                     s.description.c_str());
        std::fflush (stdout);

        auto* o = new juce::DynamicObject();
        o->setProperty ("name", juce::String (r.name));
        o->setProperty ("description", juce::String (s.description));
        o->setProperty ("cpuPercent", r.cpuPercent);
        o->setProperty ("p99", r.p99);
        o->setProperty ("p999", r.p999);
        o->setProperty ("worst", r.worst);
        o->setProperty ("outputSamples", r.seconds * r.sampleRate);
        results.add (juce::var (o));
    }

    if (jsonPath.isNotEmpty())
    {
        auto* root = new juce::DynamicObject();
        root->setProperty ("cpu", juce::SystemStats::getCpuModel());
        root->setProperty ("os", juce::SystemStats::getOperatingSystemName());
        root->setProperty ("seconds", seconds);
        root->setProperty ("scenarios", results);
        juce::File::getCurrentWorkingDirectory().getChildFile (jsonPath).replaceWithText (
            juce::JSON::toString (juce::var (root)));
    }
    return 0;
}
