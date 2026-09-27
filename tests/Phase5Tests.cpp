#include "ArticulationDemo.h"
#include "EngineTestUtilities.h"
#include "TestUtilities.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <map>

using namespace violinsynth;
using namespace violinsynth::test;
using Catch::Approx;
using engine::Articulation;

namespace
{
juce::MidiMessage keyswitch (Articulation a)
{
    return on (engine::firstKeyswitch + static_cast<int> (a));
}

// One held note in the given articulation (selected by keyswitch).
std::vector<double> playNote (Articulation a, int note, double hold, double total, float velocity = 0.8f)
{
    engine::ViolinEngine e;
    e.setSettings (plainSettings());
    e.prepare (fs, block);
    return run (e,
                total,
                { { 0.0, keyswitch (a) },
                  { 0.001, off (engine::firstKeyswitch + static_cast<int> (a)) },
                  { 0.01, on (note, 1, velocity) },
                  { 0.01 + hold, off (note) } });
}

// Share of the harmonics' energy between 2.5 and 10 kHz, in dB: the glassy or
// airy part of the tone that bow position, mutes and harmonics change most.
double brightness (const std::vector<double>& x, double from, double to, double f0)
{
    double high = 0.0, total = 0.0;
    for (int h = 1; h * f0 < 10000.0; ++h)
    {
        const auto level = toneLevel (x, from, to, h * f0);
        total += level * level;
        if (h * f0 > 2500.0)
            high += level * level;
    }
    return 10.0 * std::log10 (high / total);
}

double peak (const std::vector<double>& x, double from, double to)
{
    double p = 0.0;
    for (auto i = static_cast<std::size_t> (from * fs); i < static_cast<std::size_t> (to * fs); ++i)
        p = std::max (p, std::abs (x[i]));
    return p;
}

double maxStep (const std::vector<double>& x)
{
    double m = 0.0;
    for (std::size_t i = 1; i < x.size(); ++i)
        m = std::max (m, std::abs (x[i] - x[i - 1]));
    return m;
}
} // namespace

TEST_CASE ("Keyswitches select the articulation and never sound", "[phase5]")
{
    engine::ViolinEngine e;
    e.setSettings (plainSettings());
    e.prepare (fs, block);

    const auto x = run (e, 0.5, { { 0.0, keyswitch (Articulation::pizzicato) }, { 0.2, off (29) } });
    CHECK (rms (x, 0.0, 0.5) == 0.0);
    CHECK (e.getViolin().currentArticulation() == Articulation::pizzicato);

    // The keyswitch holds while the parameter is unchanged ...
    auto s = plainSettings();
    e.setSettings (s);
    CHECK (e.getViolin().currentArticulation() == Articulation::pizzicato);

    // ... and a change of the parameter takes over.
    s.performance.articulation = Articulation::sulTasto;
    e.setSettings (s);
    CHECK (e.getViolin().currentArticulation() == Articulation::sulTasto);
}

TEST_CASE ("Staccato stops the note short while it is held", "[phase5]")
{
    const auto staccato = playNote (Articulation::staccato, 69, 1.0, 1.2);
    const auto legato = playNote (Articulation::legato, 69, 1.0, 1.2);

    const auto body = rms (staccato, 0.03, 0.12);
    INFO ("staccato body " << body << ", tail " << rms (staccato, 0.4, 0.9));
    CHECK (body > 0.3 * rms (legato, 0.3, 0.9)); // clearly audible
    CHECK (rms (staccato, 0.4, 0.9) < 0.05 * body); // stopped, though the key is still down
    CHECK (rms (legato, 0.4, 0.9) > 0.5 * rms (legato, 0.2, 0.3));
}

TEST_CASE ("Spiccato is a short bounce that rings briefly", "[phase5]")
{
    const auto x = playNote (Articulation::spiccato, 69, 0.5, 1.2);
    const auto onset = rms (x, 0.02, 0.07);
    INFO ("onset " << onset);
    CHECK (onset > 0.01);
    CHECK (rms (x, 0.3, 0.45) < 0.4 * onset); // decaying while the key is still down
    CHECK (rms (x, 0.9, 1.2) < 0.1 * onset);
}

TEST_CASE ("Tremolo reverses the bow many times a second", "[phase5]")
{
    // Amplitude dips per second: count downward crossings of the 5 ms RMS
    // envelope through 60 % of its local mean.
    auto dips = [] (const std::vector<double>& x)
    {
        const auto frame = static_cast<std::size_t> (0.005 * fs);
        std::vector<double> env;
        for (auto i = static_cast<std::size_t> (0.3 * fs); i + frame < static_cast<std::size_t> (1.3 * fs); i += frame)
        {
            double sum = 0.0;
            for (std::size_t k = 0; k < frame; ++k)
                sum += x[i + k] * x[i + k];
            env.push_back (std::sqrt (sum / static_cast<double> (frame)));
        }
        double mean = 0.0;
        for (auto v : env)
            mean += v;
        mean /= static_cast<double> (env.size());
        int count = 0;
        for (std::size_t i = 1; i < env.size(); ++i)
            count += env[i - 1] >= 0.6 * mean && env[i] < 0.6 * mean ? 1 : 0;
        return count;
    };

    const auto tremolo = dips (playNote (Articulation::tremolo, 69, 1.5, 1.5));
    const auto legato = dips (playNote (Articulation::legato, 69, 1.5, 1.5));
    INFO ("tremolo " << tremolo << ", legato " << legato);
    CHECK (tremolo >= 9); // about 13 strokes per second
    CHECK (tremolo <= 20);
    CHECK (legato <= 1);
}

TEST_CASE ("Pizzicato is plucked in tune, decays and is damped on release", "[phase5]")
{
    for (int note : { 55, 62, 69, 76, 88 })
    {
        CAPTURE (note);
        const auto x = playNote (Articulation::pizzicato, note, 0.6, 1.0);

        // Fast attack: the peak comes within the first 30 ms.
        const auto early = peak (x, 0.01, 0.04);
        CHECK (early > 0.8 * peak (x, 0.0, 1.0));

        // In tune (the string is free, not bowed).
        const auto start = static_cast<std::size_t> (0.06 * fs);
        const auto f = test::estimateF0 (std::span (x).subspan (start, static_cast<std::size_t> (0.15 * fs)),
                                         fs,
                                         test::midiToHz (note));
        CHECK (test::cents (f, test::midiToHz (note)) == Approx (0.0).margin (3.0));

        // Decays while held, and is damped quickly when released.
        CHECK (rms (x, 0.45, 0.6) < 0.6 * rms (x, 0.03, 0.15));
        CHECK (rms (x, 0.75, 0.95) < 0.05 * rms (x, 0.45, 0.6));
    }
}

TEST_CASE ("Pizzicato is about as loud as a bowed note", "[phase5]")
{
    const auto pizz = peak (playNote (Articulation::pizzicato, 69, 0.5, 0.6), 0.0, 0.6);
    const auto bowed = peak (playNote (Articulation::legato, 69, 0.5, 0.6), 0.0, 0.6);
    INFO ("pizzicato peak " << pizz << ", bowed peak " << bowed);
    CHECK (pizz > 0.5 * bowed);
    CHECK (pizz < 2.0 * bowed);
}

TEST_CASE ("Tone colour follows the articulation", "[phase5]")
{
    std::map<Articulation, double> c;
    for (auto a : { Articulation::legato,
                    Articulation::sulPonticello,
                    Articulation::sulTasto,
                    Articulation::harmonics,
                    Articulation::conSordino })
        c[a] = brightness (playNote (a, 69, 1.0, 1.0), 0.4, 0.9, 440.0);

    INFO ("legato " << c[Articulation::legato] << ", ponticello " << c[Articulation::sulPonticello] << ", tasto "
                    << c[Articulation::sulTasto] << ", harmonics " << c[Articulation::harmonics] << ", sordino "
                    << c[Articulation::conSordino]);
    CHECK (c[Articulation::sulPonticello] > c[Articulation::legato] + 6.0);
    CHECK (c[Articulation::sulTasto] < c[Articulation::legato] - 6.0);
    CHECK (c[Articulation::harmonics] < c[Articulation::legato] - 6.0);
    CHECK (c[Articulation::conSordino] < c[Articulation::legato] - 6.0);
}

TEST_CASE ("Every articulation plays in tune", "[phase5]")
{
    for (int a = 0; a < engine::numArticulations; ++a)
    {
        const auto articulation = static_cast<Articulation> (a);
        if (articulation == Articulation::staccato || articulation == Articulation::spiccato
            || articulation == Articulation::pizzicato || articulation == Articulation::tremolo)
            continue; // short notes or reversals; pizzicato is checked above

        CAPTURE (engine::articulationNames[static_cast<std::size_t> (a)]);
        const auto x = playNote (articulation, 69, 1.0, 1.0);
        const auto f = test::estimateF0 (
            std::span (x).subspan (static_cast<std::size_t> (0.5 * fs), static_cast<std::size_t> (0.3 * fs)),
            fs,
            440.0);
        CHECK (test::cents (f, 440.0) == Approx (0.0).margin (5.0));
    }
}

TEST_CASE ("Detache gives overlapping notes their own strokes", "[phase5]")
{
    auto strokes = [] (Articulation a)
    {
        engine::ViolinEngine e;
        auto s = plainSettings();
        s.performance.voice.autoBowChange = false;
        e.setSettings (s);
        e.prepare (fs, block);
        std::vector<Event> events { { 0.0, keyswitch (a) } };
        for (int i = 0; i < 6; ++i)
        {
            events.push_back ({ 0.05 + 0.25 * i, on (69 + (i % 2) * 2) });
            events.push_back ({ 0.05 + 0.25 * i + 0.3, off (69 + (i % 2) * 2) }); // overlapping
        }
        int flips = 0;
        double last = 0.0;
        run (e,
             2.0,
             events,
             [&] (double)
             {
                 const auto d = e.getViolin().bowDirection();
                 flips += last != 0.0 && d != last ? 1 : 0;
                 last = d;
             });
        return flips;
    };

    CHECK (strokes (Articulation::legato) == 1); // one slurred stroke
    CHECK (strokes (Articulation::detache) == 6); // a new stroke per note
}

TEST_CASE ("Articulation changes cause no stuck notes and no clicks", "[phase5]")
{
    for (auto mode : { engine::PlayMode::automatic, engine::PlayMode::monoLegato, engine::PlayMode::poly })
    {
        CAPTURE (static_cast<int> (mode));
        auto settings = plainSettings();
        settings.performance.playMode = mode;
        settings.performance.voice.resonance = 0.5;

        juce::Random rng (static_cast<juce::int64> (mode) + 11);
        std::vector<Event> notes, keyswitches;
        for (int i = 0; i < 240; ++i)
        {
            const auto t = rng.nextDouble() * 8.0;
            switch (rng.nextInt (5))
            {
                case 0:
                    keyswitches.push_back (
                        { t, keyswitch (static_cast<Articulation> (rng.nextInt (engine::numArticulations))) });
                    break;
                case 1:
                case 2:
                    notes.push_back ({ t, on (55 + rng.nextInt (40), 1, 0.2f + 0.8f * rng.nextFloat()) });
                    break;
                default:
                    notes.push_back ({ t, off (55 + rng.nextInt (40)) });
                    break;
            }
        }
        for (int note = 55; note < 95; ++note)
            notes.push_back ({ 8.0, off (note) });

        auto render = [&] (std::vector<Event> events, Articulation parameter, engine::ViolinEngine& e)
        {
            std::stable_sort (events.begin(),
                              events.end(),
                              [] (const Event& x, const Event& y) { return x.time < y.time; });
            auto s = settings;
            s.performance.articulation = parameter;
            e.setSettings (s);
            e.prepare (fs, block);
            return run (e, 12.0, events); // released bowed notes ring for up to ~2.5 s
        };

        // The same notes with each articulation held throughout: the largest
        // sample step the articulations produce by themselves.
        double reference = 0.0;
        for (int a = 0; a < engine::numArticulations; ++a)
        {
            engine::ViolinEngine e;
            reference = std::max (reference, maxStep (render (notes, static_cast<Articulation> (a), e)));
        }

        auto events = notes;
        events.insert (events.end(), keyswitches.begin(), keyswitches.end());
        engine::ViolinEngine e;
        const auto x = render (events, Articulation::legato, e);

        bool finite = true;
        for (auto v : x)
            finite = finite && std::isfinite (v);
        CHECK (finite);
        CHECK (peak (x, 0.0, 12.0) <= 1.0);

        // No stuck notes: every string is back to open, and the output dies away.
        for (auto open : e.getViolin().openStrings())
            CHECK (open);
        CHECK (rms (x, 11.5, 12.0) < 0.01 * rms (x, 0.0, 8.0));

        // Switching articulations adds no clicks.
        INFO ("max step " << maxStep (x) << ", reference " << reference);
        CHECK (maxStep (x) <= 1.1 * reference);
    }
}

TEST_CASE ("The demo MIDI file plays every articulation", "[phase5]")
{
    const auto path
        = juce::File (VIOLINSYNTH_TEST_DATA_DIR).getSiblingFile ("docs").getChildFile ("demo/articulations.mid");
    REQUIRE (path.existsAsFile());
    juce::MidiFile file;
    {
        juce::FileInputStream stream (path);
        REQUIRE (file.readFrom (stream));
    }
    file.convertTimestampTicksToSeconds();

    // The committed file is up to date with the generator.
    const auto& track = *file.getTrack (0);
    const auto expected = articulationDemo();
    int notes = 0;
    for (const auto* e : track)
        notes += e->message.isNoteOnOrOff() ? 1 : 0;
    CHECK (notes == expected.getNumEvents());

    std::vector<Event> events;
    for (const auto* e : track)
        if (! e->message.isMetaEvent())
            events.push_back ({ e->message.getTimeStamp(), e->message });

    engine::ViolinEngine engine;
    engine.setSettings (plainSettings());
    engine.prepare (fs, block);
    std::array<bool, engine::numArticulations> heard {};
    const auto end = track.getEndTime();
    const auto x = run (engine,
                        end + 3.0,
                        events,
                        [&] (double t)
                        {
                            bool sounding = false;
                            for (int s = 0; s < 4; ++s)
                                sounding = sounding || engine.getViolin().noteOnString (s) >= 0;
                            if (sounding && t < end)
                                heard[static_cast<std::size_t> (engine.getViolin().currentArticulation())] = true;
                        });

    for (int a = 0; a < engine::numArticulations; ++a)
    {
        CAPTURE (engine::articulationNames[static_cast<std::size_t> (a)]);
        CHECK (heard[static_cast<std::size_t> (a)]);
    }
    CHECK (peak (x, 0.0, end + 3.0) <= 1.0);
    for (auto open : engine.getViolin().openStrings())
        CHECK (open);
}

TEST_CASE ("Articulation harmonic levels", "[.diagnostics]")
{
    for (int a = 0; a < engine::numArticulations; ++a)
    {
        const auto x = playNote (static_cast<Articulation> (a), 69, 1.0, 1.0);
        std::string line = engine::articulationNames[static_cast<std::size_t> (a)];
        line += ": rms " + std::to_string (rms (x, 0.4, 0.9)) + " h:";
        for (int h = 1; h <= 12; ++h)
            line += " "
                + std::to_string (static_cast<int> (
                    std::lround (20.0 * std::log10 (toneLevel (x, 0.4, 0.9, h * 440.0) + 1e-12))));
        line += " bright " + std::to_string (brightness (x, 0.4, 0.9, 440.0));
        WARN (line);
    }
    for (int note : { 55, 62, 69, 76, 88, 96 })
        for (float v : { 0.2f, 0.8f })
        {
            const auto p = playNote (Articulation::pizzicato, note, 0.5, 0.6, v);
            const auto b = playNote (Articulation::legato, note, 0.5, 0.6, v);
            WARN ("pizz " << note << " v " << v << ": peak " << peak (p, 0, 0.6) << " rms " << rms (p, 0.01, 0.3)
                          << " | bowed peak " << peak (b, 0, 0.6) << " rms " << rms (b, 0.2, 0.5));
        }
}
