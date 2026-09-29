// Offline renderer: plays a MIDI file through the engine and writes a WAV.
//
//   ViolinSynthRender in.mid out.wav [name=value ...]
//
// Settings start at the plugin's defaults (except Octave, which is 0 so
// MIDI files play at their written pitch). Names: bowNoise, imperfection,
// humanise, resonance, bowPressure, bowPosition, vibratoDepth, vibratoRate,
// room, width, velocityRange, octave, body, instrument, articulation,
// start (seconds skipped at the front of the file), length (seconds).

#include "engine/ViolinEngine.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <chrono>
#include <cstdio>
#include <iostream>
#include <map>
#include <string>

using namespace violinsynth;

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::cerr << "usage: ViolinSynthRender in.mid out.wav [name=value ...]\n";
        return 1;
    }

    constexpr double fs = 48000.0;
    constexpr int block = 256;

    engine::EngineSettings s;
    double startSeconds = 0.0, lengthSeconds = -1.0;
    for (int i = 3; i < argc; ++i)
    {
        const juce::String arg (argv[i]);
        const auto name = arg.upToFirstOccurrenceOf ("=", false, false);
        const auto value = arg.fromFirstOccurrenceOf ("=", false, false).getDoubleValue();
        auto& v = s.performance.voice;
        if (name == "bowNoise") v.bowNoise = value;
        else if (name == "imperfection") v.imperfection = value;
        else if (name == "humanise") v.humanise = value;
        else if (name == "resonance") v.resonance = value;
        else if (name == "bowPressure") v.bowPressure = value;
        else if (name == "bowPosition") v.bowPosition = value;
        else if (name == "vibratoDepth") v.vibratoDepthCents = value;
        else if (name == "vibratoRate") v.vibratoRateHz = value;
        else if (name == "room") s.output.room = static_cast<float> (value);
        else if (name == "width") s.output.width = static_cast<float> (value);
        else if (name == "velocityRange") s.performance.velocityTop = value;
        else if (name == "octave") s.performance.octaveShift = static_cast<int> (value);
        else if (name == "body") s.body = static_cast<int> (value);
        else if (name == "instrument") s.performance.instrument = static_cast<engine::Instrument> (static_cast<int> (value));
        else if (name == "articulation") s.performance.articulation = static_cast<engine::Articulation> (static_cast<int> (value));
        else if (name == "attack") v.attackSeconds = value;
        else if (name == "release") v.releaseSeconds = value;
        else if (name == "vibratoDelay") v.vibratoDelaySeconds = value;
        else if (name == "portamento") v.portamentoSeconds = value;
        else if (name == "autoBowChange") v.autoBowChange = value > 0.5;
        else if (name == "playMode") s.performance.playMode = static_cast<engine::PlayMode> (static_cast<int> (value));
        else if (name == "pickup") s.performance.pickup = static_cast<engine::Pickup> (static_cast<int> (value));
        else if (name == "drone") s.performance.drone = value;
        else if (name == "drive") s.drive = value;
        else if (name == "sordino") s.output.sordino = static_cast<float> (value);
        else if (name == "outputGain") s.output.gainDb = static_cast<float> (value);
        else if (name == "start") startSeconds = value;
        else if (name == "length") lengthSeconds = value;
        else
        {
            std::cerr << "unknown setting: " << name << "\n";
            return 1;
        }
    }

    juce::MidiFile file;
    juce::FileInputStream in (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));
    if (! in.openedOk() || ! file.readFrom (in))
    {
        std::cerr << "cannot read " << argv[1] << "\n";
        return 1;
    }
    file.convertTimestampTicksToSeconds();
    juce::MidiMessageSequence events;
    for (int t = 0; t < file.getNumTracks(); ++t)
        events.addSequence (*file.getTrack (t), 0.0);
    events.updateMatchedPairs();

    const auto fileEnd = events.getEndTime() + 2.0; // let the last note ring
    const auto endSeconds = lengthSeconds > 0.0 ? std::min (fileEnd, startSeconds + lengthSeconds) : fileEnd;
    const auto total = static_cast<int> (endSeconds * fs);
    const auto skip = static_cast<int> (startSeconds * fs);

    const auto t0 = std::chrono::steady_clock::now();
    engine::ViolinEngine e;
    e.prepare (fs, block);
    e.setSettings (s);

    juce::AudioBuffer<float> buffer (2, block);
    juce::AudioBuffer<float> out (2, std::max (1, total - skip));
    int next = 0;
    for (int start = 0; start < total; start += block)
    {
        juce::MidiBuffer midi;
        for (; next < events.getNumEvents(); ++next)
        {
            const auto& m = events.getEventPointer (next)->message;
            const auto pos = static_cast<int> (m.getTimeStamp() * fs);
            if (pos >= start + block)
                break;
            if (! m.isMetaEvent())
                midi.addEvent (m, pos - start);
        }
        e.setSettings (s);
        e.process (buffer, midi);
        const auto n = std::min (block, total - start);
        for (int i = 0; i < n; ++i)
            if (start + i >= skip)
                for (int ch = 0; ch < 2; ++ch)
                    out.setSample (ch, start + i - skip, buffer.getSample (ch, i));
    }
    const auto seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();

    juce::File wav = juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]);
    wav.deleteFile();
    juce::WavAudioFormat format;
    std::unique_ptr<juce::OutputStream> stream = wav.createOutputStream();
    std::unique_ptr<juce::AudioFormatWriter> writer (
        format.createWriterFor (stream.get(), fs, 2, 24, {}, 0));
    if (writer == nullptr)
    {
        std::cerr << "cannot write " << argv[2] << "\n";
        return 1;
    }
    stream.release(); // the writer owns it now
    writer->writeFromAudioSampleBuffer (out, 0, out.getNumSamples());

    std::printf ("rendered %.1f s of audio in %.1f s (peak %.2f)\n",
                 out.getNumSamples() / fs, seconds, out.getMagnitude (0, out.getNumSamples()));
    return 0;
}
