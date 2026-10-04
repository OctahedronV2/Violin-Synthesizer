// Octavio 2 plugin tests: the engine plays, Studio mode looks ahead, the processor runs at the
// host's rate, notes outside the violin stay silent, state round-trips.

#include "../plugin/PluginEditor.h"
#include "../plugin/PluginProcessor.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>

namespace
{
struct Level
{
    double rms = 0, peak = 0;
    bool finite = true;
};

Level measure (const juce::AudioBuffer<float>& b, int from = 0, int to = -1)
{
    Level l;
    if (to < 0)
        to = b.getNumSamples();
    double sum = 0;
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = from; i < to; ++i)
        {
            const float x = b.getSample (c, i);
            l.finite = l.finite && std::isfinite (x);
            sum += x * x;
            l.peak = std::max (l.peak, (double) std::abs (x));
        }
    l.rms = std::sqrt (sum / std::max (1, (to - from) * b.getNumChannels()));
    return l;
}

double dB (double x)
{
    return 20 * std::log10 (x + 1e-30);
}

// Plays one note through the processor (on at the start, off after `noteSeconds`) and returns
// `seconds` of output.
juce::AudioBuffer<float>
play (octavio2::Processor& p, double rate, int note, double noteSeconds, double seconds, int velocity = 100)
{
    const int block = 480;
    p.prepareToPlay (rate, block);
    const int total = (int) (seconds * rate);
    juce::AudioBuffer<float> out (2, total);
    juce::AudioBuffer<float> buf (2, block);
    const int off = (int) (noteSeconds * rate);
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = std::min (block, total - pos);
        buf.setSize (2, n, false, false, true);
        juce::MidiBuffer midi;
        if (pos == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) velocity), 0);
        if (off >= pos && off < pos + n)
            midi.addEvent (juce::MidiMessage::noteOff (1, note), off - pos);
        p.processBlock (buf, midi);
        for (int c = 0; c < 2; ++c)
            out.copyFrom (c, pos, buf, c, 0, n);
    }
    return out;
}

void setParam (octavio2::Processor& p, const juce::String& id, float value)
{
    auto* param = p.getParameters().getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}
} // namespace

TEST_CASE ("Octavio 2 plays a violin note at 48 kHz and at 44.1 kHz", "[octavio2]")
{
    for (const double rate : { 48000.0, 44100.0 })
    {
        octavio2::Processor p;
        setParam (p, "octave", 2.0f); // index 2 = no shift: MIDI 69 sounds A4
        const auto out = play (p, rate, 69, 1.0, 1.5);
        const auto sounding = measure (out, (int) (0.2 * rate), (int) (1.0 * rate));
        INFO ("rate " << rate << ": " << dB (sounding.rms) << " dB RMS, peak " << dB (sounding.peak) << " dB");
        CHECK (sounding.finite);
        CHECK (dB (sounding.rms) > -45.0);
        CHECK (sounding.peak < 1.0);
    }
}

TEST_CASE ("Octavio 2 is silent for notes outside the violin", "[octavio2]")
{
    octavio2::Processor p;
    setParam (p, "octave", 2.0f);
    setParam (p, "room", 0.0f); // no hall, so nothing else can sound
    const auto out = play (p, 48000.0, 40, 0.5, 1.0); // E2, below the G string
    CHECK (measure (out).peak < 1e-6);
}

TEST_CASE ("Octavio 2 plays through every violin, microphone, mute and distance", "[octavio2]")
{
    // each setting loads its own impulse responses or filters: all must sound, none blow up
    struct Setting
    {
        const char* id;
        float value;
    };
    const Setting settings[]
        = { { "violin", 1 }, { "violin", 2 },  { "violin", 3 },     { "mic", 1 },         { "mic", 2 },
            { "mic", 3 },    { "mute", 1 },    { "mute", 2 },       { "distance", 0.5f }, { "distance", 10 },
            { "width", 0 },  { "width", 200 }, { "movement", 100 }, { "bridge", 2400 },   { "bridge", 3600 } };
    for (const auto& s : settings)
    {
        octavio2::Processor p;
        setParam (p, "octave", 2.0f);
        setParam (p, s.id, s.value);
        const auto out = play (p, 48000.0, 69, 1.0, 1.5);
        const auto sounding = measure (out, (int) (0.2 * 48000), 48000);
        INFO (s.id << " = " << s.value << ": " << dB (sounding.rms) << " dB RMS, peak " << dB (sounding.peak) << " dB");
        CHECK (sounding.finite);
        CHECK (dB (sounding.rms) > (std::string (s.id) == "mute" ? -60.0 : -45.0));
        CHECK (sounding.peak < 1.0);
    }
}

TEST_CASE ("Octavio 2 plays every articulation, bow style and bridge setting", "[octavio2]")
{
    struct Setting
    {
        const char* id;
        float value;
        double minDb;
    };
    // plucked notes die away and harmonics are quieter, so they get a lower floor
    const Setting settings[] = { { "articulation", 1, -65 }, { "articulation", 2, -65 },  { "articulation", 3, -75 },
                                 { "articulation", 4, -60 }, { "bowStyle", 1, -45 },      { "bowStyle", 2, -45 },
                                 { "bowStyle", 3, -60 },     { "bowStyle", 4, -60 },      { "bowStyle", 5, -65 },
                                 { "sympathetic", 0, -45 },  { "sympathetic", 100, -45 }, { "wolf", 100, -45 },
                                 { "hold", 0, -45 },         { "hold", 100, -45 },        { "phrasing", 0, -45 },
                                 { "phrasing", 200, -45 },   { "fingerPlan", 1, -45 },    { "mode", 1, -45 } };
    for (const auto& s : settings)
    {
        octavio2::Processor p;
        setParam (p, "octave", 2.0f);
        setParam (p, s.id, s.value);
        const auto out = play (p, 48000.0, 69, 1.0, 2.5);
        const auto sounding = measure (out, 0, (int) (2.5 * 48000));
        INFO (s.id << " = " << s.value << ": " << dB (sounding.rms) << " dB RMS, peak " << dB (sounding.peak) << " dB");
        CHECK (sounding.finite);
        CHECK (dB (sounding.rms) > s.minDb);
        CHECK (sounding.peak < 1.0);
    }
}

TEST_CASE ("Octavio 2 keyswitches pick the articulation whatever the Octave", "[octavio2]")
{
    // C#1 (MIDI 25) = pizzicato: the plucked note has died well before a bowed one would
    auto lateLevel = [] (bool keyswitch)
    {
        octavio2::Processor p;
        setParam (p, "room", 0.0f);
        const int block = 480;
        p.prepareToPlay (48000.0, block);
        juce::AudioBuffer<float> buf (2, block);
        double late = 0;
        for (int pos = 0, k = 0; pos < 3 * 48000; pos += block, ++k)
        {
            juce::MidiBuffer midi;
            if (k == 0 && keyswitch)
                midi.addEvent (juce::MidiMessage::noteOn (1, 25, (juce::uint8) 100), 0);
            if (k == 10)
                midi.addEvent (juce::MidiMessage::noteOn (1, 57, (juce::uint8) 100), 0); // A4 at Octave +1
            p.processBlock (buf, midi);
            if (pos > 2 * 48000)
                late = std::max (late, (double) buf.getMagnitude (0, block));
        }
        return late;
    };
    const double bowed = lateLevel (false), plucked = lateLevel (true);
    INFO ("bowed " << dB (bowed) << " dB, plucked " << dB (plucked) << " dB");
    CHECK (dB (plucked) < dB (bowed) - 10.0);
}

TEST_CASE ("Octavio 2's bridge stays stable at every Sympathetic, Wolf and Hold extreme", "[octavio2]")
{
    // M3 soak, short: the strings and the passive bridge alone, random bowing on all strings
    for (const double wolf : { 0.0, 1.0 })
        for (const double hold : { 0.0, 1.0 })
        {
            o2::Violin v;
            v.p.sympathetic = 1.0;
            v.p.wolf = wolf;
            v.p.hold = hold;
            v.init();
            o2::Rng rng;
            auto uniform = [&rng] { return 0.5 * (rng.uni() + 1.0); };
            double vb[4] = {}, fb[4] = {}, peak = 0, after = 0, tail = 0;
            bool finite = true;
            const int n = (int) (12 * v.fs);
            for (int i = 0; i < n; ++i)
            {
                if (i % 9600 == 0)
                    for (int s = 0; s < 4; ++s)
                    {
                        const bool bowing = i < n - (int) (4 * v.fs) && uniform() < 0.6;
                        vb[s] = bowing ? 0.05 + 0.4 * uniform() : 0.0;
                        fb[s] = bowing ? 0.2 + 1.8 * uniform() : 0.0;
                    }
                const double F = v.tick (vb, fb);
                finite = finite && std::isfinite (F);
                peak = std::max (peak, std::abs (F));
                if (i > n - (int) (4 * v.fs) + 4800 && i < n - (int) (3 * v.fs))
                    after = std::max (after, std::abs (F)); // just after the bows lift
                if (i > n - (int) v.fs)
                    tail = std::max (tail, std::abs (F));
            }
            INFO ("wolf " << wolf << ", hold " << hold << ": peak " << peak << " N, after the bows lift " << after
                          << " N, 3 s later " << tail << " N");
            CHECK (finite);
            CHECK (peak < 50.0);
            CHECK (tail < 0.3 * after); // the open strings ring on, but die away
        }
}

TEST_CASE ("Octavio 2 Studio mode plays its look-ahead later and reports it as latency", "[octavio2]")
{
    octavio2::Processor p;
    setParam (p, "octave", 2.0f);
    setParam (p, "room", 0.0f);
    setParam (p, "mode", 1.0f);
    const double rate = 48000.0;
    const auto out = play (p, rate, 69, 0.5, 2.2);
    CHECK (p.getLatencySamples() == o2::Engine::lookAheadSamples);
    const int la = o2::Engine::lookAheadSamples;
    CHECK (measure (out, 0, la - 100).peak < 1e-6);
    CHECK (dB (measure (out, la + 2400, la + 24000).rms) > -45.0);
}

TEST_CASE ("Octavio 2 renders the same performance identically", "[octavio2]")
{
    octavio2::Processor a, b;
    const auto x = play (a, 48000.0, 76, 0.6, 1.0);
    const auto y = play (b, 48000.0, 76, 0.6, 1.0);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < x.getNumSamples(); ++i)
            REQUIRE (x.getSample (c, i) == y.getSample (c, i));
}

TEST_CASE ("Octavio 2 saves and restores its parameters", "[octavio2]")
{
    octavio2::Processor a;
    setParam (a, "vibrato", 1.5f);
    setParam (a, "room", 3.0f);
    juce::MemoryBlock state;
    a.getStateInformation (state);
    octavio2::Processor b;
    b.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (std::abs (b.getParameters().getRawParameterValue ("vibrato")->load() - 1.5f) < 1e-4f);
    CHECK (juce::roundToInt (b.getParameters().getRawParameterValue ("room")->load()) == 3);
}

TEST_CASE ("Octavio 2's interface paints every tab while it plays", "[octavio2]")
{
    // OCTAVIO2_SNAPSHOTS=<folder> saves each tab as a PNG (Live, then Studio)
    const juce::ScopedJuceInitialiser_GUI gui;
    const auto folder = juce::SystemStats::getEnvironmentVariable ("OCTAVIO2_SNAPSHOTS", {});
    for (const bool studio : { false, true })
    {
        octavio2::Processor p;
        setParam (p, "mode", studio ? 1.0f : 0.0f);
        // a little run, the last note held
        const double rate = 48000.0;
        const int block = 480;
        p.prepareToPlay (rate, block);
        juce::AudioBuffer<float> buf (2, block);
        const int notes[] = { 62, 64, 66, 67, 69, 71, 73, 74, 76 };
        const int step = (int) (0.35 * rate / block);
        const int blocks = (int) (4.5 * rate / block);
        for (int b = 0; b < blocks; ++b)
        {
            juce::MidiBuffer midi;
            if (b % step == 0 && b / step < 9)
            {
                const int k = b / step;
                if (k > 0)
                    midi.addEvent (juce::MidiMessage::noteOff (1, notes[k - 1] - 12), 0);
                midi.addEvent (juce::MidiMessage::noteOn (1, notes[k] - 12, (juce::uint8) (80 + 4 * k)), 1);
            }
            p.processBlock (buf, midi);
        }
        for (int tab = 0; tab < 7; ++tab)
        {
            if (studio && tab != 0)
                break;
            p.editorTab = tab;
            octavio2::Editor editor (p);
            const auto image = editor.createComponentSnapshot (editor.getLocalBounds());
            CHECK (image.getWidth() == octavio2::ui::designWidth);
            if (folder.isNotEmpty())
            {
                juce::File file (folder + "/" + (studio ? "studio" : "live") + "-" + juce::String (tab) + ".png");
                file.deleteFile();
                juce::FileOutputStream out (file);
                juce::PNGImageFormat().writeImageToStream (image, out);
            }
        }
    }
}

// ---------------------------------------------------------------- M7 player: styles, intonation, MPE
namespace
{
// processes `blocks` blocks of 480 samples at 48 kHz, the MIDI at the start of the first block
void run (octavio2::Processor& p, int blocks, const std::vector<juce::MidiMessage>& first = {})
{
    juce::AudioBuffer<float> buf (2, 480);
    for (int b = 0; b < blocks; ++b)
    {
        juce::MidiBuffer midi;
        if (b == 0)
            for (const auto& m : first)
                midi.addEvent (m, 0);
        p.processBlock (buf, midi);
    }
}
} // namespace

TEST_CASE ("Octavio 2 M7: Modern soloist is the default player, and every style plays", "[octavio2][m7]")
{
    octavio2::Processor a, b;
    // b's engine goes through every style and back: Modern soloist restores the player exactly
    auto settings = b.getEngine().getSettings();
    for (int style = o2::styleCount - 1; style >= 0; --style)
    {
        settings.playerStyle = style;
        b.getEngine().setSettings (settings);
    }
    const auto x = play (a, 48000.0, 76, 0.6, 1.0);
    const auto y = play (b, 48000.0, 76, 0.6, 1.0);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < x.getNumSamples(); ++i)
            REQUIRE (x.getSample (c, i) == y.getSample (c, i));

    for (int style = 1; style < o2::styleCount; ++style)
    {
        octavio2::Processor p;
        setParam (p, "playerStyle", (float) style);
        setParam (p, "octave", 2.0f);
        const auto out = play (p, 48000.0, 74, 1.2, 1.6);
        const auto l = measure (out, 9600, 57600);
        INFO ("style " << style << ": " << dB (l.rms) << " dB RMS");
        CHECK (l.finite);
        CHECK (dB (l.rms) > -45.0);
        CHECK (l.peak < 1.0);
    }
}

TEST_CASE ("Octavio 2 M7: styles change the player's habits", "[octavio2][m7]")
{
    const o2::PlayerParams modern;
    for (int s = 0; s < o2::styleCount; ++s)
    {
        o2::PlayerParams p;
        o2::applyStyle (p, s);
        if (s == o2::styleModern)
            CHECK ((p.vibRate == modern.vibRate && p.vibWidthHi == modern.vibWidthHi && p.slideProb == 0.0
                    && p.scoop == 0.0 && p.pitchError == 0.0 && p.intonAmount == 0.0 && p.mdvPeak == 0.5
                    && p.openPenalty == modern.openPenalty && p.bite == modern.bite));
        if (s == o2::styleRomantic)
            CHECK ((p.vibWidthHi > modern.vibWidthHi && p.vibRate < modern.vibRate && p.slideProb > 0.0));
        if (s == o2::styleHungarian)
            CHECK ((p.vibRate > modern.vibRate && p.bite > modern.bite && p.vibDelay < modern.vibDelay));
        if (s == o2::styleBaroque)
            CHECK ((p.vibWidthHi < 0.5 * modern.vibWidthHi && p.mdvDepth > modern.mdvDepth));
        if (s == o2::styleMaqam)
            CHECK ((p.scoop > 0.0 && p.slideProb > 0.0));
        if (s == o2::styleFiddle)
            CHECK ((p.openPenalty < modern.openPenalty && p.vibWidthHi < modern.vibWidthHi));
        if (s == o2::styleStudent)
            CHECK ((p.pitchError > 0.0 && p.accel < modern.accel));
    }
    // portamento: a leap in reach on the string the finger is on slides when slideProb is 1
    o2::Violin vn;
    vn.init();
    o2::Player pl;
    pl.pp.slideProb = 1.0;
    pl.init (vn, 48000.0);
    double vb[4], fb[4];
    pl.noteOn (71, 90); // B4 on the A string
    for (int i = 0; i < 9600; ++i)
        pl.tick (vb, fb);
    pl.noteOn (74, 90); // D5 slurred on (the B is still held): 3 semitones up the A string, in reach
    CHECK (pl.lastString == 2);
    CHECK (pl.st[2].slideT0 >= 0.0);
}

TEST_CASE ("Octavio 2 M7: intonation systems and the A4 reference", "[octavio2][m7]")
{
    o2::Violin vn;
    vn.init();
    o2::Player pl;
    pl.init (vn, 48000.0);
    pl.setTuning (o2::intonExpressive, 0, 440.0, 0.0, nullptr, true);
    CHECK_FALSE (pl.tuneOn); // the default: exactly the 2.1 pitches
    pl.setTuning (o2::intonEqual, 0, 415.0, 0.0, nullptr, true);
    CHECK (pl.tuneOn);
    CHECK (std::abs (pl.tuneTable[69] - 1200.0 * std::log2 (415.0 / 440.0)) < 1e-9);
    CHECK (std::abs (pl.openCents[2] - pl.tuneTable[69]) < 1e-9);
    // Pythagorean in D: A at the reference, F# a Pythagorean third above D
    pl.setTuning (o2::intonPythagorean, 2, 440.0, 0.0, nullptr, true);
    CHECK (std::abs (pl.tuneTable[69]) < 1e-9);
    CHECK (std::abs ((pl.tuneTable[66] - pl.tuneTable[62]) - (1200.0 * std::log2 (81.0 / 64.0) - 400.0)) < 0.02);
    CHECK (std::abs (pl.openCents[0] - (-3.91)) < 1e-9); // G two pure fifths below A
    // Just in C: E a pure major third above C, G a pure fifth
    pl.setTuning (o2::intonJust, 0, 440.0, 0.0, nullptr, true);
    CHECK (std::abs ((pl.tuneTable[64] - pl.tuneTable[60]) - (1200.0 * std::log2 (5.0 / 4.0) - 400.0)) < 0.02);
    CHECK (std::abs ((pl.tuneTable[67] - pl.tuneTable[60]) - (1200.0 * std::log2 (3.0 / 2.0) - 700.0)) < 0.02);
    // the processor: A415 retunes the open strings and the notes
    octavio2::Processor p;
    setParam (p, "a4", 415.0f);
    setParam (p, "octave", 2.0f);
    p.prepareToPlay (48000.0, 480);
    run (p, 20, { juce::MidiMessage::noteOn (1, 71, (juce::uint8) 90) });
    auto& player = p.getEngine().getPlayer();
    CHECK (std::abs (p.getEngine().getViolin().s[2].d.f0 - 415.0) < 0.01);
    CHECK (std::abs (player.st[player.lastString].target - (71.0 + 1200.0 * std::log2 (415.0 / 440.0) / 100.0)) < 1e-3);
}

TEST_CASE ("Octavio 2 M7: Scala files load, play and are saved with the project", "[octavio2][m7]")
{
    // 12-TET: nothing moves; 24-EDO quarter tones; a keyboard map with A = 432 Hz
    const std::string et = "! et.scl\n12-TET\n12\n100.\n200.\n300.\n400.\n500.\n600.\n700.\n800.\n900.\n1000.\n"
                           "1100.\n2/1\n";
    auto t = o2::parseScala (et);
    REQUIRE (t.ok);
    for (int n = 0; n < 128; ++n)
        CHECK (std::abs (t.cents[n]) < 1e-9);
    // 24-EDO, every key the next quarter tone, key 69 at 440: key 61 is 8 quarter tones below A
    std::string q = "24-EDO\n24\n";
    for (int i = 1; i < 24; ++i)
        q += std::to_string (50 * i) + ".0\n";
    q += "2/1\n";
    t = o2::parseScala (q);
    REQUIRE (t.ok);
    CHECK (std::abs (t.cents[61] - 400.0) < 1e-9);
    CHECK (std::abs (t.cents[74] - (-250.0)) < 1e-9);
    const std::string kbm = "! a432.kbm\n0\n0\n127\n60\n69\n432.0\n0\n";
    t = o2::parseScala (et, kbm);
    REQUIRE (t.ok);
    CHECK (t.hasKeyboardMap);
    CHECK (std::abs (t.cents[69] - 1200.0 * std::log2 (432.0 / 440.0)) < 1e-9);
    CHECK_FALSE (o2::parseScala ("nonsense").ok);

    octavio2::Processor a;
    CHECK (a.loadScalaText ("quarter.scl", q, {}).isEmpty());
    setParam (a, "intonation", (float) o2::intonScala);
    setParam (a, "octave", 2.0f);
    a.prepareToPlay (48000.0, 480);
    run (a, 10, { juce::MidiMessage::noteOn (1, 74, (juce::uint8) 90) });
    auto& pl = a.getEngine().getPlayer();
    CHECK (pl.tuneOn);
    CHECK (std::abs (pl.st[pl.lastString].target - 71.5) < 1e-6); // 5 quarter tones above A4
    juce::MemoryBlock state;
    a.getStateInformation (state);
    octavio2::Processor b;
    b.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (b.getScalaName() == "quarter.scl");
    b.prepareToPlay (48000.0, 480);
    run (b, 2);
    CHECK (std::abs (b.getEngine().getPlayer().tuneTable[74] - (-250.0)) < 1e-6);
}

TEST_CASE ("Octavio 2 M7: MPE bends, presses and brightens each note; off it changes nothing", "[octavio2][m7]")
{
    for (const bool on : { false, true })
    {
        octavio2::Processor p;
        setParam (p, "octave", 2.0f);
        setParam (p, "mpe", on ? 1.0f : 0.0f);
        p.prepareToPlay (48000.0, 480);
        // a semitone up of the 48-semitone range, sent before the note as MPE controllers do
        const int semitone = 8192 + juce::roundToInt (8192.0 / 48.0);
        run (p,
             10,
             { juce::MidiMessage::pitchWheel (2, semitone),
               juce::MidiMessage::noteOn (2, 69, (juce::uint8) 90),
               juce::MidiMessage::channelPressureChange (2, 127),
               juce::MidiMessage::controllerEvent (2, 74, 127) });
        auto& pl = p.getEngine().getPlayer();
        const auto& S = pl.st[pl.lastString];
        if (on)
        {
            CHECK (std::abs (S.mpeBend - 100.0) < 0.5);
            CHECK (pl.mpeDyn == 1.0);
            CHECK (pl.mpeContact < 0.6);
        }
        else
        {
            CHECK (S.mpeBend == 0.0);
            CHECK (pl.mpeDyn < 0.0);
            CHECK (pl.mpeContact == 1.0);
        }
    }
}
