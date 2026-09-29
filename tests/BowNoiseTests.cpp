#include "EngineTestUtilities.h"
#include "dsp/BowController.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

// Bow noise ("scratch"): see docs/BOW_NOISE.md.
//
// The bowed string is chaotic while it settles into the sawtooth (Helmholtz)
// motion, so a single note is a poor measure: one extra sample of force can
// flip it between a clean and a scratchy start. Everything here is averaged
// over many notes.

using namespace violinsynth;
using namespace violinsynth::test;
using engine::Articulation;

namespace
{
// Scratch meter: the aperiodic share of the signal in dB. Each sample is
// predicted from one period of f0 earlier, with a gain fitted over each
// two-period chunk so that a swelling or fading note does not count. A clean,
// steady tone reads about -40 dB; pure noise reads 0 dB.
double noiseDb (const std::vector<double>& x, double from, double to, double f0)
{
    const auto period = fs / f0;
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

engine::EngineSettings noiseSettings (Articulation a, double bowPosition = 0.11, double bowPressure = 0.5)
{
    auto s = plainSettings();
    s.performance.articulation = a;
    s.performance.voice.bowPosition = bowPosition;
    s.performance.voice.bowPressure = bowPressure;
    return s;
}

std::vector<double> render (const engine::EngineSettings& s, double seconds, std::vector<Event> events)
{
    engine::ViolinEngine e;
    e.setSettings (s);
    e.prepare (fs, block);
    return run (e, seconds, std::move (events));
}

// G3 to C6, across all four strings.
const std::vector<int> scaleNotes { 55, 57, 59, 60, 62, 64, 65, 67, 69, 71, 72, 74, 76, 79, 81, 84 };

// Scratch in the first 100 ms of each note (from 100 to 200 ms for the long
// articulations, whose attack takes up the first 100 ms), averaged.
double
attackNoise (Articulation a, double beta, double pressure, const std::vector<int>& notes, double* worst = nullptr)
{
    const auto shortNote = a == Articulation::staccato || a == Articulation::spiccato;
    double sum = 0.0;
    for (auto note : notes)
    {
        const auto x = render (noiseSettings (a, beta, pressure),
                               1.0,
                               { { 0.1, on (note) }, { shortNote ? 0.3 : 0.9, off (note) } });
        const auto nd = noiseDb (x, 0.1, shortNote ? 0.2 : 0.3, engine::midiToHz (note));
        sum += nd;
        if (worst != nullptr)
            *worst = std::max (*worst, nd);
    }
    return sum / static_cast<double> (notes.size());
}

// Scratch while a staccato stroke stops (the bow decelerating on the string).
double staccatoStopNoise (const std::vector<int>& notes)
{
    double sum = 0.0;
    for (auto note : notes)
    {
        const auto x
            = render (noiseSettings (Articulation::staccato), 0.5, { { 0.1, on (note) }, { 0.3, off (note) } });
        sum += noiseDb (x, 0.21, 0.31, engine::midiToHz (note));
    }
    return sum / static_cast<double> (notes.size());
}

struct BowChangeStats
{
    int count = 0;
    double steady = 0.0; // scratch just before the change
    double worst = 0.0; // loudest scratch around the change
    double recovery = 0.0; // scratch from 50 to 300 ms after it
    double dip = 0.0; // lowest level through the change, relative to before it
};

// Held legato notes through their automatic bow changes, averaged.
BowChangeStats bowChanges (double beta, double pressure, const std::vector<int>& notes, double seconds)
{
    BowChangeStats stats;
    for (auto note : notes)
    {
        engine::ViolinEngine e;
        e.setSettings (noiseSettings (Articulation::legato, beta, pressure));
        e.prepare (fs, block);
        std::vector<double> changes;
        int seen = 0;
        const auto x = run (e,
                            seconds,
                            { { 0.0, on (note) } },
                            [&] (double t)
                            {
                                if (e.getViolin().bowChangeCount() != seen)
                                {
                                    seen = e.getViolin().bowChangeCount();
                                    changes.push_back (t);
                                }
                            });
        const auto f0 = engine::midiToHz (note);
        for (auto t : changes)
        {
            if (t < 1.0 || t > seconds - 0.5)
                continue; // the first stroke's attack, or too close to the end
            const auto before = rms (x, t - 0.3, t - 0.05);
            double worst = -100.0, lowest = 10.0;
            for (double u = t - 0.02; u < t + 0.25; u += 0.01)
            {
                worst = std::max (worst, noiseDb (x, u, u + 0.03, f0));
                lowest = std::min (lowest, rms (x, u, u + 0.01) / before);
            }
            stats.steady += noiseDb (x, t - 0.3, t - 0.05, f0);
            stats.worst += worst;
            stats.recovery += noiseDb (x, t + 0.05, t + 0.3, f0);
            stats.dip += lowest;
            ++stats.count;
        }
    }
    if (stats.count > 0)
    {
        const auto n = static_cast<double> (stats.count);
        stats.steady /= n;
        stats.worst /= n;
        stats.recovery /= n;
        stats.dip /= n;
    }
    return stats;
}
} // namespace

TEST_CASE ("Held notes keep sounding through automatic bow changes", "[bownoise]")
{
    // A held note used to drop to 14% of its level at every bow change and
    // restart with a scratch. At this dynamic the bow now turns every 4.4 s.
    const auto stats = bowChanges (0.11, 0.5, { 55, 60, 67, 72 }, 10.0);
    INFO ("changes " << stats.count << ", level dips to " << stats.dip << ", recovery " << stats.recovery << " dB");
    REQUIRE (stats.count >= 4);
    CHECK (stats.dip > 0.25);
    CHECK (stats.recovery < -16.0);
}

TEST_CASE ("Slurred lines turn the bow on a note change", "[bownoise]")
{
    // A turn mid-note scratched where nothing covered it (the showcase at 4 s
    // and 15 s). In a slur the bow now turns with the next note once half of
    // it is used.
    engine::ViolinEngine e;
    e.setSettings (noiseSettings (Articulation::legato));
    e.prepare (fs, block);
    std::vector<Event> events;
    std::vector<double> starts;
    const std::vector<int> line { 69, 71, 72, 74, 76, 74, 72, 71 };
    for (int i = 0; i < 24; ++i)
    {
        const auto t = 0.1 + 0.6 * i;
        const auto note = line[static_cast<std::size_t> (i) % line.size()];
        events.push_back ({ t, on (note) });
        events.push_back ({ t + 0.65, off (note) }); // overlapping: one slur
        starts.push_back (t);
    }
    int seen = 0, turns = 0, midNote = 0;
    run (e,
         15.0,
         events,
         [&] (double t)
         {
             if (e.getViolin().bowChangeCount() == seen)
                 return;
             seen = e.getViolin().bowChangeCount();
             ++turns;
             const auto nearest
                 = *std::min_element (starts.begin(),
                                      starts.end(),
                                      [t] (double a, double b) { return std::abs (a - t) < std::abs (b - t); });
             if (t - nearest > 2.0 * block / fs || t < nearest)
                 ++midNote;
         });
    INFO ("turns " << turns << ", mid-note " << midNote);
    CHECK (turns >= 3);
    CHECK (midNote == 0);
}

TEST_CASE ("Staccato stops without a crunch", "[bownoise]")
{
    // Stopping the bow at full weight crunched (about -7 dB).
    const auto stop = staccatoStopNoise ({ 55, 60, 64, 67, 69, 72, 76, 79 });
    INFO ("stop noise " << stop << " dB");
    CHECK (stop < -11.0);
}

TEST_CASE ("Long notes speak cleanly at the default pressure", "[bownoise]")
{
    // Legato and detache attacks read -13 and -10 dB when the Bow Pressure
    // range reached into force levels where the model scratches.
    const std::vector<int> notes { 55, 59, 62, 65, 69, 72, 76, 81 };
    const auto legato = attackNoise (Articulation::legato, 0.11, 0.5, notes);
    const auto detache = attackNoise (Articulation::detache, 0.11, 0.5, notes);
    INFO ("legato " << legato << " dB, detache " << detache << " dB");
    CHECK (legato < -16.0);
    CHECK (detache < -13.0);
}

// Not run by default: `ViolinSynthTests "[.bownoisereport]"` prints the scratch
// meter readings behind docs/BOW_NOISE.md.
TEST_CASE ("Bow noise scorecard", "[.bownoisereport]")
{
    std::printf ("Scratch in the first 100 ms of each note (mean / worst of %zu notes, G3 to C6)\n", scaleNotes.size());
    auto attack = [] (Articulation a, double beta, double p)
    {
        double worst = -100.0;
        const auto mean = attackNoise (a, beta, p, scaleNotes, &worst);
        std::printf ("  %-15s beta %.3f pressure %.2f: %6.1f / %6.1f dB\n",
                     engine::articulationNames[static_cast<std::size_t> (a)],
                     beta,
                     p,
                     mean,
                     worst);
    };
    for (int a = 0; a < engine::numArticulations; ++a)
        if (static_cast<Articulation> (a) != Articulation::pizzicato)
            attack (static_cast<Articulation> (a), 0.11, 0.5);
    attack (Articulation::legato, 0.075, 0.65); // Bright Soloist
    attack (Articulation::staccato, 0.075, 0.65);
    std::printf ("  staccato stop: %6.1f dB\n", staccatoStopNoise (scaleNotes));

    std::printf ("Held legato notes through automatic bow changes (means)\n");
    for (auto [beta, p] : { std::pair { 0.11, 0.5 }, std::pair { 0.075, 0.65 } })
    {
        const auto s = bowChanges (beta, p, scaleNotes, 10.0);
        std::printf ("  beta %.3f pressure %.2f: %d changes, before %6.1f dB, worst %6.1f dB, "
                     "recovery %6.1f dB, level dips to %.2f\n",
                     beta,
                     p,
                     s.count,
                     s.steady,
                     s.worst,
                     s.recovery,
                     s.dip);
    }
}

// Not run by default: steady scratch (0.9-1.3 s) and the time until a
// detache note first reads below -28 dB, across the Bow Pressure range.
// Used to set each string's force window (engine/StringData.h).
TEST_CASE ("Bow noise across the pressure range", "[.bownoisereport]")
{
    for (auto velocity : { 0.35f, 0.8f })
        for (auto beta : { 0.075, 0.11, 0.15 })
            for (auto note : { 55, 62, 69, 76, 83 })
            {
                std::printf ("velocity %.2f beta %.3f note %d:", static_cast<double> (velocity), beta, note);
                for (int i = 0; i <= 10; ++i)
                {
                    const auto p = i / 10.0;
                    const auto x = render (noiseSettings (Articulation::detache, beta, p),
                                           1.3,
                                           { { 0.1, on (note, 1, velocity) } });
                    const auto f0 = engine::midiToHz (note);
                    double clean = -1.0;
                    for (double t = 0.1; t < 1.25; t += 0.01)
                        if (noiseDb (x, t, t + 0.04, f0) < -28.0)
                        {
                            clean = t - 0.1;
                            break;
                        }
                    std::printf (" %5.1f/%4.0f", noiseDb (x, 0.9, 1.3, f0), clean * 1000.0);
                }
                std::printf ("\n");
            }
}

// Clean bowing (docs/CLEAN_BOWING.md): the player listens to the string and
// adjusts the bow weight, so the default plays like a professional.
namespace
{
struct ScaleStats
{
    int notes = 0;
    int loud = 0; // notes with a 50 ms stretch louder than -10 dB of scratch
    int scratchy = 0; // ... louder than -20 dB
    int helmholtz = 0; // notes whose string ends in clean Helmholtz motion
};

// Major scales of separate notes, a new stroke each, as in a typed or
// quantised line. Each note is judged after its first 100 ms.
ScaleStats scaleStats (double imperfection, const std::vector<int>& roots)
{
    constexpr double length = 1.2;
    ScaleStats stats;
    for (auto root : roots)
        for (auto velocity : { 0.5f, 0.8f })
        {
            auto s = noiseSettings (Articulation::legato);
            s.performance.voice.imperfection = imperfection;
            engine::ViolinEngine e;
            e.setSettings (s);
            e.prepare (fs, block);

            std::vector<int> notes;
            std::vector<Event> events;
            for (auto step : { 0, 2, 4, 5, 7, 9, 11, 12 })
            {
                const auto t = 0.05 + length * static_cast<double> (notes.size());
                notes.push_back (root + step);
                events.push_back ({ t, on (root + step, 1, velocity) });
                events.push_back ({ t + length, off (root + step) });
            }

            // Helmholtz motion over the last 300 ms of each note: one slip per
            // period and no scratch, as the player hears it.
            // Scratch is measured on the played string itself, with the same
            // meter the player listens with: in the output, the notes before
            // it ring on under it (v1.1), and two pitches at once read as noise.
            std::vector<bool> clean (notes.size(), true);
            std::vector<double> worst (notes.size(), 0.0);
            run (e,
                 0.05 + length * static_cast<double> (notes.size()) + 0.3,
                 events,
                 [&] (double t)
                 {
                     const auto i = static_cast<std::size_t> ((t - 0.05) / length);
                     const auto into = t - 0.05 - length * static_cast<double> (i);
                     if (i >= notes.size() || into < 0.15)
                         return;
                     const auto& violin = e.getViolin();
                     for (int string = 0; string < 4; ++string)
                         if (violin.noteOnString (string) == notes[i])
                         {
                             if (into < length - 0.1)
                                 worst[i] = std::max (worst[i], violin.stringScratch (string));
                             const auto slips = violin.stringSlipsPerPeriod (string);
                             if (into >= length - 0.3
                                 && (slips < 0.8 || slips > 1.2
                                     || violin.stringScratch (string) > dsp::BowController::scratchThreshold))
                                 clean[i] = false;
                         }
                 });

            for (std::size_t i = 0; i < notes.size(); ++i)
            {
                ++stats.notes;
                stats.loud += worst[i] > 0.1 ? 1 : 0; // -10 dB
                stats.scratchy += worst[i] > 0.01 ? 1 : 0; // -20 dB
                stats.helmholtz += clean[i] ? 1 : 0;
            }
        }
    return stats;
}
} // namespace

TEST_CASE ("The player keeps scales free of scratch", "[bownoise][cleanbowing]")
{
    const std::vector<int> roots { 55, 60, 65, 69 };
    const auto clean = scaleStats (0.0, roots);
    const auto unassisted = scaleStats (1.0, roots);
    INFO ("of " << clean.notes << " notes: loud " << clean.loud << " (unassisted " << unassisted.loud << "), scratchy "
                << clean.scratchy << " (" << unassisted.scratchy << "), in Helmholtz motion " << clean.helmholtz << " ("
                << unassisted.helmholtz << ")");
    REQUIRE (unassisted.scratchy >= 5); // the unassisted model does scratch on these notes
    // The model is chaotic, so one note in 64 may still flare up (and which
    // one moves between platforms); the player removes most of the scratch.
    CHECK (clean.loud <= 1);
    CHECK (clean.scratchy * 3 <= unassisted.scratchy);
    // With the string's twist both settle on most notes; which few do not is
    // chaotic, so this only checks the player does not make it worse.
    CHECK (clean.helmholtz + 3 >= unassisted.helmholtz);
}

TEST_CASE ("Imperfection at 100% leaves the bow weight alone", "[bownoise][cleanbowing]")
{
    // A string that scratches: the player would ease off, but at skill 0
    // (Imperfection 100%) the weight stays exactly 1.
    dsp::BowController player;
    player.prepare (fs * 4.0, 180.0);
    player.setPeriod (200.0);
    std::uint32_t random = 1;
    for (int i = 0; i < 20000; ++i)
    {
        random = random * 1664525u + 1013904223u;
        player.listen (static_cast<double> (random >> 8) / static_cast<double> (1u << 24) - 0.5, i % 200 == 0);
        if (i % 33 == 0)
            player.adjust (33.0 / (fs * 4.0), 0.0);
    }
    CHECK (player.scratch() > dsp::BowController::scratchThreshold);
    CHECK (player.weight() == 1.0);
    player.adjust (33.0 / (fs * 4.0), 1.0);
    CHECK (player.weight() < 1.0);
}

TEST_CASE ("A firm bow near the bridge never locks the string silent", "[bownoise][cleanbowing]")
{
    // v1.0.1: with Bright Soloist (bow position 0.075, pressure 0.65) the G
    // string and the low A string stuck to the bow and moved with it in
    // silence, for as long as the note was held.
    auto level = [] (double bowPosition, double pressure, std::vector<Event> events, double from)
    {
        auto s = plainSettings();
        s.performance.voice.bowPosition = bowPosition;
        s.performance.voice.bowPressure = pressure;
        engine::ViolinEngine e;
        e.setSettings (s);
        e.prepare (fs, block);
        const auto out = run (e, from + 0.5, std::move (events));
        return rms (out, from + 0.2, from + 0.5);
    };

    for (auto note : { 57, 64, 72, 75 })
        for (auto velocity : { 0.5f, 0.8f, 1.0f })
            for (auto [bowPosition, pressure] : { std::pair { 0.075, 0.65 }, { 0.06, 0.8 }, { 0.11, 1.0 } })
            {
                CAPTURE (note, velocity, bowPosition, pressure);
                const auto reference = level (0.11, 0.5, { { 0.0, on (note, 1, velocity) } }, 0.0);
                CHECK (level (bowPosition, pressure, { { 0.0, on (note, 1, velocity) } }, 0.0)
                       > 0.1 * reference); // a locked string is 40 dB down
            }

    // Jake's case: G#4 held, then D#5 slurred across to the A string.
    const auto slurred = level (0.075, 0.65, { { 0.0, on (68) }, { 1.0, on (75) }, { 1.0, off (68) } }, 1.0);
    const auto reference = level (0.11, 0.5, { { 0.0, on (75) } }, 0.0);
    CHECK (slurred > 0.1 * reference); // a locked string is 40 dB down
}
