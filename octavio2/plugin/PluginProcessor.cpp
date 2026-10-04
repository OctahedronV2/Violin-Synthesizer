#include "PluginProcessor.h"

#include "../../third_party/mts-esp/libMTSClient.h"
#include "../core/Wav.h"
#include "Octavio2BinaryData.h"
#include "PluginEditor.h"

namespace octavio2
{
namespace
{
// The engine's sound data, from the files in octavio2/data built into the plugin.
o2::WavData resource (const char* filename)
{
    for (int i = 0; i < Octavio2BinaryData::namedResourceListSize; ++i)
    {
        const char* name = Octavio2BinaryData::namedResourceList[i];
        if (juce::String (Octavio2BinaryData::getNamedResourceOriginalFilename (name)) == filename)
        {
            int size = 0;
            const char* data = Octavio2BinaryData::getNamedResource (name, size);
            return o2::readWav (data, static_cast<size_t> (size));
        }
    }
    jassertfalse;
    return {};
}

o2::RadiationData loadRadiationData()
{
    o2::RadiationData data;
    auto mono = [] (const char* f)
    {
        auto w = resource (f);
        return w.channels.empty() ? std::vector<float> (1, 0.0f) : w.channels[0];
    };
    // the Violin choices, then the Mic position choices, in order (data/bodies/make_bodies.py)
    for (const char* b : { "stoppani-48k.wav", "klimke-48k.wav", "levaggi-48k.wav", "iowa-48k.wav" })
        data.bodies.push_back (mono (b));
    for (const char* m : { "front-48k.wav", "above-48k.wav", "ear-48k.wav", "side-48k.wav" })
    {
        auto w = resource (m);
        std::array<std::vector<float>, 6> irs;
        for (size_t k = 0; k < 6; ++k)
            irs[k] = k < w.channels.size() ? std::move (w.channels[k]) : std::vector<float> (1, 0.0f);
        data.mics.push_back (std::move (irs));
    }
    // the Room choices after "None", in order (params::roomNames)
    for (const char* hall : { "arvedi-near.wav",
                              "arvedi-far.wav",
                              "detmold.wav",
                              "church.wav",
                              "maida-vale-4.wav",
                              "maida-vale-5.wav",
                              "wdr-control-room.wav",
                              "wdr-studio.wav" })
    {
        auto w = resource (hall);
        if (w.channels.size() == 2)
            data.halls.push_back ({ std::move (w.channels[0]), std::move (w.channels[1]) });
        else
            data.halls.push_back ({ std::vector<float> (1, 0.0f), std::vector<float> (1, 0.0f) });
    }
    return data;
}
} // namespace

Processor::Processor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "Octavio2State", params::createLayout()),
      reader (parameters),
      engine (std::make_unique<o2::Engine>())
{
    engine->prepare (loadRadiationData());
    clickedKeys.fill (-1);
    for (auto& channel : sentNotes)
        channel.fill (-1);
    keyboardState.addListener (this);
    // M6: the parameters a mapped controller can move, by index (the audio thread's table)
    const auto& ps = AudioProcessor::getParameters();
    jassert (ps.size() <= maxMappedParameters);
    for (int i = 0; i < std::min (ps.size(), maxMappedParameters); ++i)
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (ps[i]))
            rawValue[(size_t) i] = parameters.getRawParameterValue (r->getParameterID());
    presets = std::make_unique<Presets> (parameters);
    mts = MTS_RegisterClient(); // M7: finds an MTS-ESP master if one is installed
    startTimerHz (10);
}

Processor::~Processor()
{
    stopTimer();
    if (mts != nullptr)
        MTS_DeregisterClient (mts);
    keyboardState.removeListener (this);
}

bool Processor::KeyQueue::push (const KeyEvent& e)
{
    const auto scope = fifo.write (1);
    if (scope.blockSize1 + scope.blockSize2 == 0)
        return false;
    events[static_cast<size_t> (scope.blockSize1 > 0 ? scope.startIndex1 : scope.startIndex2)] = e;
    return true;
}

template <typename Fn>
void Processor::KeyQueue::popAll (Fn&& fn)
{
    const auto scope = fifo.read (fifo.getNumReady());
    scope.forEach ([&] (int index) { fn (events[static_cast<size_t> (index)]); });
}

void Processor::handleNoteOn (juce::MidiKeyboardState*, int, int note, float velocity)
{
    const int sent = note - 12 * reader.octaveShift();
    if (sent < 0 || sent > 127)
        return;
    clickedKeys[static_cast<size_t> (note)] = static_cast<std::int8_t> (sent);
    keysToAudio.push ({ sent, std::max (velocity, 1.0f / 127.0f) });
}

void Processor::handleNoteOff (juce::MidiKeyboardState*, int, int note, float)
{
    auto& sent = clickedKeys[static_cast<size_t> (note)];
    if (sent >= 0)
        keysToAudio.push ({ sent, 0.0f });
    sent = -1;
}

void Processor::timerCallback()
{
    updateLatency();
    notifyMappedParameters();
    // the keyboard's own copy of its notes is not needed: they reached the audio thread already
    juce::MidiBuffer none;
    keyboardState.processNextMidiBuffer (none, 0, 1, false);
}

void Processor::updateLatency()
{
    // Studio mode's look-ahead is latency the host compensates. Set from the message thread:
    // hosts are told through a callback that is not safe on the audio thread.
    const double interp
        = resampling ? static_cast<double> (juce::WindowedSincInterpolator::getBaseLatency()) / ratio : 0.0;
    const int samples = juce::roundToInt ((reader.studio() ? o2::Engine::lookAheadSamples : 0) / ratio + interp);
    if (samples != latencyShown.exchange (samples))
        setLatencySamples (samples);
}

void Processor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    hostRate = sampleRate;
    ratio = o2::Engine::rate / sampleRate;
    resampling = std::abs (ratio - 1.0) > 1e-9;
    hostClock = 0;
    engine->setSettings (reader.read());
    engine->reset();
    for (auto& channel : sentNotes)
        channel.fill (-1);
    pedal = false;
    pedalShown = false;
    deferredOff.fill (false);
    const auto chunk = static_cast<size_t> (std::ceil (512.0 * std::max (1.0, ratio))) + 256;
    for (auto& f : fifo)
        f.assign (chunk, 0.0f);
    fifoFill = 0;
    for (auto& i : interpolators)
        i.reset();
    scratchL.assign (static_cast<size_t> (std::max (samplesPerBlock, 512)), 0.0f);
    scratchR.assign (scratchL.size(), 0.0f);
    latencyShown = -1;
    nextHistoryT = 0.0;
    updateLatency();
}

void Processor::releaseResources()
{
}

bool Processor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

int64_t Processor::engineTime (int hostOffset) const
{
    const auto host = hostClock + hostOffset;
    return resampling ? static_cast<int64_t> (std::llround (static_cast<double> (host) * ratio)) : host;
}

void Processor::handleMidi (const juce::MidiMessage& m, int64_t when)
{
    // M7: MPE expression on the member channels (off: everything is ordinary MIDI)
    if (reader.mpe() && mpe.handle (m, when, *engine, reader.octaveShift(), reader.mpeBendRange(), sentNotes))
        return;
    const int channel = juce::jlimit (1, 16, m.getChannel()) - 1;
    auto& sent = sentNotes[static_cast<size_t> (channel)];
    if (m.isNoteOn())
    {
        // keyswitches (M5: MIDI 24-28 pick Arco, Pizzicato, Bartok, Left-hand pizz, Harmonic) are
        // fixed keys, whatever the Octave setting
        if (m.getNoteNumber() >= 24 && m.getNoteNumber() <= 28)
        {
            engine->noteOn (when, m.getNoteNumber(), m.getVelocity());
            return;
        }
        // notes outside the violin (G3 to E7, after Octave) stay silent, as in Octavio 1
        const int pitch = m.getNoteNumber() + 12 * reader.octaveShift();
        if (pitch < 55 || pitch > 104)
            return;
        if (sent[static_cast<size_t> (m.getNoteNumber())] >= 0)
            engineNoteOff (when, sent[static_cast<size_t> (m.getNoteNumber())]);
        sent[static_cast<size_t> (m.getNoteNumber())] = static_cast<std::int8_t> (pitch);
        engineNoteOn (when, pitch, m.getVelocity());
        lastVelocity = m.getVelocity();
    }
    else if (m.isNoteOff())
    {
        // the pitch it started on, even if Octave changed while it was held
        auto& s = sent[static_cast<size_t> (m.getNoteNumber())];
        if (s >= 0)
            engineNoteOff (when, s);
        s = -1;
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        engine->allNotesOff (when);
        sent.fill (-1);
        deferredOff.fill (false);
    }
    else if (m.isPitchWheel())
    {
        // M6: the wheel's full throw is Bend range semitones; the player bends the bowed notes
        engine->controller (when, 128, (m.getPitchWheelValue() - 8192) / 8192.0 * reader.bendRange() * 100.0);
    }
    else if (m.isController())
    {
        const int cc = m.getControllerNumber(), value = m.getControllerValue();
        midiMap.noteIncoming (cc, value);
        if (cc == 121)
        {
            // reset all controllers: the curves go back to the player, the pedal and bend centre
            engine->controller (when, 121, value);
            setPedal (false, when);
        }
        else
            mappedController (cc, value, when);
    }
}

// M6 ------------------------------------------------------------------------------------------
o2::EngineSettings Processor::currentSettings() const
{
    return reader.read();
}

// The slur pedal (Slur everything, CC64 by default): while it is down a released note keeps
// sounding until the next note starts, so every note overlaps the next and the player slurs it.
// Lifting the pedal releases what is still sounding.
void Processor::engineNoteOn (int64_t when, int pitch, double vel127)
{
    const auto p = static_cast<size_t> (pitch & 127);
    if (deferredOff[p]) // the same note again: it ends first (a repeated note)
    {
        engine->noteOff (when, pitch);
        deferredOff[p] = false;
    }
    engine->noteOn (when, pitch, vel127);
    for (size_t k = 0; k < deferredOff.size(); ++k)
        if (deferredOff[k])
        {
            engine->noteOff (when, static_cast<int> (k));
            deferredOff[k] = false;
        }
}

void Processor::engineNoteOff (int64_t when, int pitch)
{
    if (pedal)
        deferredOff[static_cast<size_t> (pitch & 127)] = true;
    else
        engine->noteOff (when, pitch);
}

void Processor::setPedal (bool down, int64_t when)
{
    if (down == pedal)
        return;
    pedal = down;
    pedalShown.store (down, std::memory_order_relaxed);
    if (! down)
        for (size_t k = 0; k < deferredOff.size(); ++k)
            if (deferredOff[k])
            {
                engine->noteOff (when, static_cast<int> (k));
                deferredOff[k] = false;
            }
}

// A controller through the map: to the player (its own controller numbers), the slur pedal, or a
// host parameter. A parameter's value is written where the engine reads it at once (this block)
// and handed to the message thread, which tells the host and the editor (notifyMappedParameters).
void Processor::mappedController (int cc, int value, int64_t when)
{
    midiMap.forEach (cc,
                     value,
                     [this, when] (int target, float x)
                     {
                         if (target == MidiMap::slurPedal)
                             setPedal (x >= 0.5f, when);
                         else if (target >= 0 && target < 128)
                             engine->controller (when, target, x * 127.0);
                         else if (target >= MidiMap::parameterBase)
                         {
                             const auto i = static_cast<size_t> (target - MidiMap::parameterBase);
                             const auto& ps = AudioProcessor::getParameters();
                             if (i >= rawValue.size() || rawValue[i] == nullptr || (int) i >= ps.size())
                                 return;
                             auto* r = static_cast<juce::RangedAudioParameter*> (ps[(int) i]);
                             rawValue[i]->store (r->convertFrom0to1 (x));
                             mappedValue[i].store (x, std::memory_order_relaxed);
                             mappedDirty[i].store (true, std::memory_order_release);
                             mappedChanged = true;
                         }
                     });
}

void Processor::notifyMappedParameters()
{
    const auto& ps = AudioProcessor::getParameters();
    for (size_t i = 0; i < mappedDirty.size() && (int) i < ps.size(); ++i)
        if (mappedDirty[i].exchange (false, std::memory_order_acquire))
        {
            auto* p = ps[(int) i];
            const float v = mappedValue[i].load (std::memory_order_relaxed);
            if (std::abs (p->getValue() - v) > 1e-7f)
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (v);
                p->endChangeGesture();
            }
        }
}

void Processor::renderEngine (float* left, float* right, int numSamples)
{
    if (! resampling)
    {
        engine->render (left, right, numSamples);
        return;
    }
    float* out[2] = { left, right };
    while (numSamples > 0)
    {
        const int n = std::min (numSamples, 512);
        const int need = static_cast<int> (std::ceil (n * ratio)) + 4;
        while (fifoFill < need)
        {
            const int m = std::min (64, static_cast<int> (fifo[0].size()) - fifoFill);
            engine->render (fifo[0].data() + fifoFill, fifo[1].data() + fifoFill, m);
            fifoFill += m;
        }
        int used = 0;
        for (size_t c = 0; c < 2; ++c)
            used = interpolators[c].process (ratio, fifo[c].data(), out[c], n);
        for (auto& f : fifo)
            std::copy (f.begin() + used, f.begin() + fifoFill, f.begin());
        fifoFill -= used;
        out[0] += n;
        out[1] += n;
        numSamples -= n;
    }
}

void Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const auto startTicks = juce::Time::getHighResolutionTicks();
    const int n = buffer.getNumSamples();
    if (auto* head = getPlayHead())
        if (const auto pos = head->getPosition())
            if (const auto bpm = pos->getBpm())
            {
                hostBpm = *bpm;
                telemetry.bpm.store (static_cast<float> (*bpm));
            }
    engine->setSettings (currentSettings());
    updateTuning();

    keysToAudio.popAll (
        [this] (const KeyEvent& e)
        {
            const auto when = engineTime (0);
            if (e.velocity > 0.0f)
            {
                engineNoteOn (when, e.note, e.velocity * 127.0f);
                lastVelocity = juce::roundToInt (e.velocity * 127.0f);
            }
            else
                engineNoteOff (when, e.note);
        });
    mappedChanged = false;
    for (const auto metadata : midi)
    {
        if (metadata.numBytes > 3)
            continue; // SysEx: copying it into a MidiMessage would allocate
        handleMidi (metadata.getMessage(), engineTime (metadata.samplePosition));
    }
    midi.clear();
    if (mappedChanged) // M6: a mapped controller moved a parameter; it plays from this block
        engine->setSettings (currentSettings());

    // in pieces, in case the host sends a block larger than it announced
    const bool stereo = buffer.getNumChannels() > 1;
    for (int done = 0; done < n;)
    {
        const int m = std::min (n - done, static_cast<int> (scratchL.size()));
        renderEngine (scratchL.data(), scratchR.data(), m);
        if (stereo)
        {
            buffer.copyFrom (0, done, scratchL.data(), m);
            buffer.copyFrom (1, done, scratchR.data(), m);
        }
        else
        {
            buffer.copyFrom (0, done, scratchL.data(), m, 0.5f);
            buffer.addFrom (0, done, scratchR.data(), m, 0.5f);
        }
        done += m;
    }
    for (int c = 2; c < buffer.getNumChannels(); ++c)
        buffer.clear (c, 0, n);
    hostClock += n;
    updateTelemetry (n / hostRate, startTicks);
}

void Processor::updateTelemetry (double blockSeconds, juce::int64 startTicks)
{
    const double used = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - startTicks);
    telemetry.cpu.store (static_cast<float> (0.9 * telemetry.cpu.load() + 0.1 * used / std::max (1e-6, blockSeconds)));

    auto& pl = engine->getPlayer();
    auto& vn = engine->getViolin();
    auto& T = telemetry;
    const int s = juce::jlimit (0, 3, pl.lastString);
    const auto& S = pl.st[static_cast<size_t> (s)];
    const bool sounding = pl.nHeld > 0 && ! pl.releasing;
    T.note.store (sounding ? static_cast<int> (std::lround (pl.noteNow)) : -1);
    T.string.store (s);
    T.velocity.store (lastVelocity);
    T.slur.store (pl.slurNotes > 0);
    T.slurNotes.store (pl.slurNotes);
    T.releasing.store (pl.releasing);
    T.pitch.store (static_cast<float> (S.pitch));
    T.dynamics.store (static_cast<float> (pl.dEff()));
    T.handPos.store (static_cast<float> (pl.handPos));
    T.bowDir.store (static_cast<float> (pl.dir));
    T.hair.store (static_cast<float> (pl.hair / pl.pp.bowLength));
    T.speed.store (static_cast<float> (std::abs (pl.v)));
    T.force.store (static_cast<float> (S.force));
    T.contact.store (static_cast<float> (pl.betaFor (s)));
    T.vibWidth.store (static_cast<float> (S.vibWidth));
    T.vibRate.store (static_cast<float> (S.vibRate));
    T.slips.store (static_cast<float> (S.spp));
    for (size_t k = 0; k < 4; ++k)
        T.stringForce[k].store (static_cast<float> (pl.st[k].force));
    juce::ignoreUnused (vn);
    T.sliding.store (S.slideT0 >= 0.0);
    T.slideFrom.store (static_cast<float> (S.slideFrom));
    T.target.store (static_cast<float> (S.target));
    T.changing.store (pl.changing);
    T.dynMode.store (pl.manDyn ? 2 : 0);
    T.vibMode.store (pl.ccVib >= 0.0 ? 2 : 0);
    T.rateMode.store (pl.ccRate > 0.0 ? 2 : 0);
    T.contactMode.store (pl.ccContact > 0.0 ? 2 : 0);

    const double now = engine->seconds();
    if (now >= nextHistoryT)
    {
        nextHistoryT = now + 0.01;
        const auto i = T.historyCount.load (std::memory_order_relaxed);
        T.history[static_cast<size_t> (i % Telemetry::historySize)]
            = { static_cast<float> (now),
                static_cast<float> (pl.dEff()),
                sounding ? static_cast<float> (S.vibWidth) : 0.0f,
                static_cast<float> (pl.betaFor (s)),
                static_cast<float> (S.vibRate),
                sounding };
        T.historyCount.store (i + 1, std::memory_order_release);
    }
}

juce::AudioProcessorEditor* Processor::createEditor()
{
    return new Editor (*this);
}

// ---------------------------------------------------------------- M7 tuning
void Processor::updateTuning()
{
    // a new Scala table from the message thread (never waits: tried again next block)
    if (scalaChanged.load (std::memory_order_acquire))
    {
        const juce::SpinLock::ScopedTryLockType lock (tuningLock);
        if (lock.isLocked())
        {
            scalaNow = pendingScala;
            scalaChanged.store (false, std::memory_order_relaxed);
        }
    }
    const int system = reader.intonation();
    if (system == o2::intonScala && scalaNow.ok)
        engine->setTuningTable (scalaNow.cents, ! scalaNow.hasKeyboardMap);
    else if (system == o2::intonMts && mts != nullptr && MTS_HasMaster (mts))
    {
        // per sounding pitch, the master's retuning of the key that plays it
        const int shift = 12 * reader.octaveShift();
        for (int p = 0; p < 128; ++p)
        {
            const int key = p - shift;
            mtsTable[p] = key >= 0 && key < 128 ? 100.0 * MTS_RetuningInSemitones (mts, (char) key, -1) : 0.0;
        }
        engine->setTuningTable (mtsTable, false);
    }
    else
        engine->clearTuningTable();
}

juce::String Processor::loadScala (const juce::File& scl, const juce::File& kbm)
{
    if (! scl.existsAsFile())
        return "file not found";
    return loadScalaText (scl.getFileName(),
                          scl.loadFileAsString(),
                          kbm.existsAsFile() ? kbm.loadFileAsString() : juce::String());
}

juce::String Processor::loadScalaText (const juce::String& name, const juce::String& scl, const juce::String& kbm)
{
    auto tuning = o2::parseScala (scl.toStdString(), kbm.toStdString());
    if (! tuning.ok)
        return juce::String (tuning.error);
    {
        const juce::SpinLock::ScopedLockType lock (tuningLock);
        pendingScala.ok = true;
        pendingScala.hasKeyboardMap = tuning.hasKeyboardMap;
        std::copy (std::begin (tuning.cents), std::end (tuning.cents), pendingScala.cents);
        scalaChanged.store (true, std::memory_order_release);
    }
    // saved with the project (parameters.state is written by getStateInformation)
    parameters.state.setProperty ("scalaName", name, nullptr);
    parameters.state.setProperty ("scalaText", scl, nullptr);
    parameters.state.setProperty ("kbmText", kbm, nullptr);
    return {};
}

void Processor::clearScala()
{
    {
        const juce::SpinLock::ScopedLockType lock (tuningLock);
        pendingScala = TuningTable();
        scalaChanged.store (true, std::memory_order_release);
    }
    for (auto* key : { "scalaName", "scalaText", "kbmText" })
        parameters.state.removeProperty (key, nullptr);
}

juce::String Processor::getScalaName() const
{
    return parameters.state.getProperty ("scalaName").toString();
}

bool Processor::mtsHasMaster() const
{
    return mts != nullptr && MTS_HasMaster (mts);
}

juce::String Processor::mtsScaleName() const
{
    return mtsHasMaster() ? juce::String (MTS_GetScaleName (mts)) : juce::String();
}

// The state: the parameters (APVTS), the name of the preset they came from (property "preset")
// and, from M6, the MIDI map as a child MIDIMAP (MidiMap::toTree).
void Processor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.removeChild (state.getChildWithName (MidiMap::tag), nullptr);
    state.appendChild (midiMap.toTree(), nullptr);
    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void Processor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml != nullptr && xml->hasTagName (parameters.state.getType()))
    {
        auto state = juce::ValueTree::fromXml (*xml);
        const auto map = state.getChildWithName (MidiMap::tag);
        midiMap.fromTree (map); // none (saved before M6): the Octavio 2 map
        state.removeChild (map, nullptr);
        parameters.replaceState (state);
        presets->restoreFromState();
        // M7: the project's Scala tuning
        const auto scl = parameters.state.getProperty ("scalaText").toString();
        if (scl.isNotEmpty())
            loadScalaText (getScalaName(), scl, parameters.state.getProperty ("kbmText").toString());
        else
            clearScala();
    }
}
} // namespace octavio2

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new octavio2::Processor();
}
