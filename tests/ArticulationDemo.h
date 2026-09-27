#pragma once

// The Phase 5 demo: every articulation in turn, selected by keyswitches.
// `ViolinSynthTests "[.demo]"` writes it as docs/demo/articulations.mid.

#include "engine/Articulation.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>

namespace violinsynth::test
{
// Timestamps in seconds.
inline juce::MidiMessageSequence articulationDemo()
{
    using engine::Articulation;
    juce::MidiMessageSequence seq;
    double t = 0.0;

    auto select = [&] (Articulation a)
    {
        const auto key = engine::firstKeyswitch + static_cast<int> (a);
        seq.addEvent (juce::MidiMessage::noteOn (1, key, 0.8f), t);
        seq.addEvent (juce::MidiMessage::noteOff (1, key), t + 0.05);
        t += 0.1;
    };
    auto note = [&] (int number, double start, double length, float velocity)
    {
        seq.addEvent (juce::MidiMessage::noteOn (1, number, velocity), t + start);
        seq.addEvent (juce::MidiMessage::noteOff (1, number), t + start + length);
    };
    // Equally spaced notes; notes longer than the step overlap (and slur in legato).
    auto phrase = [&] (std::initializer_list<int> notes, double step, double length, float velocity)
    {
        double start = 0.0;
        for (auto n : notes)
        {
            note (n, start, length, velocity);
            start += step;
        }
        t += start + 0.6;
    };

    const std::initializer_list<int> dMajor { 62, 64, 66, 67, 69, 71, 73, 74, 73, 71, 69, 67, 66, 64, 62 };

    // Legato: overlapping notes slur in one bow.
    select (Articulation::legato);
    phrase ({ 69, 71, 73, 74, 76, 74, 73, 71, 69 }, 0.4, 0.45, 0.7f);

    // Détaché: the same line, a new bow for each note.
    select (Articulation::detache);
    phrase ({ 69, 71, 73, 74, 76, 74, 73, 71, 69 }, 0.4, 0.45, 0.7f);

    // Staccato (martelé): short, bitten strokes.
    select (Articulation::staccato);
    phrase (dMajor, 0.25, 0.2, 0.8f);

    // Spiccato: fast bouncing strokes.
    select (Articulation::spiccato);
    phrase ({ 69, 73, 76, 81, 76, 73, 69, 73, 76, 81, 76, 73, 69, 73, 76, 81 }, 0.14, 0.1, 0.7f);

    // Tremolo: a long note, then a double stop.
    select (Articulation::tremolo);
    note (69, 0.0, 2.0, 0.6f);
    note (62, 2.2, 2.0, 0.8f);
    note (71, 2.2, 2.0, 0.8f);
    t += 4.8;

    // Pizzicato: a walking line, then a four-string chord.
    select (Articulation::pizzicato);
    phrase ({ 55, 59, 62, 67, 71, 74, 79, 74 }, 0.3, 0.25, 0.8f);
    for (int n : { 55, 62, 71, 79 })
        note (n, 0.0, 1.2, 0.9f);
    t += 1.8;

    // Natural harmonics.
    select (Articulation::harmonics);
    phrase ({ 81, 88, 93 }, 1.2, 1.1, 0.6f);

    // Sul ponticello, then sul tasto: the same long notes.
    select (Articulation::sulPonticello);
    phrase ({ 69, 74 }, 1.5, 1.4, 0.7f);
    select (Articulation::sulTasto);
    phrase ({ 69, 74 }, 1.5, 1.4, 0.7f);

    // Con sordino: a legato line with the mute on.
    select (Articulation::conSordino);
    phrase ({ 67, 71, 74, 79, 78, 76, 74 }, 0.5, 0.55, 0.7f);

    // Back to the default.
    select (Articulation::legato);

    seq.updateMatchedPairs();
    return seq;
}

// As a standard MIDI file at 120 bpm (1920 ticks per second).
inline juce::MidiFile articulationDemoFile()
{
    constexpr double ticksPerSecond = 1920.0;
    juce::MidiMessageSequence track;
    track.addEvent (juce::MidiMessage::textMetaEvent (3, "Violin Synthesizer articulations"), 0.0);
    track.addEvent (juce::MidiMessage::tempoMetaEvent (500000), 0.0);
    const auto demo = articulationDemo();
    for (const auto* e : demo)
        track.addEvent (e->message, std::round (e->message.getTimeStamp() * ticksPerSecond));
    track.updateMatchedPairs();

    juce::MidiFile file;
    file.setTicksPerQuarterNote (960);
    file.addTrack (track);
    return file;
}
} // namespace violinsynth::test
