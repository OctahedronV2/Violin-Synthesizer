#include "plugin/Parameters.h"

namespace violinsynth::params
{
namespace
{
using Range = juce::NormalisableRange<float>;

enum class Format
{
    plain, // two decimals
    percent,
    seconds, // shown in ms below one second
    hertz,
    cents,
    decibels,
};

juce::AudioParameterFloatAttributes attributesFor (Format format)
{
    juce::AudioParameterFloatAttributes a;

    switch (format)
    {
        case Format::plain:
            return a.withStringFromValueFunction ([] (float v, int) { return juce::String (v, 2); });
        case Format::percent:
            return a
                .withStringFromValueFunction ([] (float v, int)
                                              { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; })
                .withValueFromStringFunction ([] (const juce::String& t) { return t.getFloatValue() / 100.0f; });
        case Format::seconds:
            return a
                .withStringFromValueFunction (
                    [] (float v, int) {
                        return v < 1.0f ? juce::String (juce::roundToInt (v * 1000.0f)) + " ms"
                                        : juce::String (v, 2) + " s";
                    })
                .withValueFromStringFunction (
                    [] (const juce::String& t)
                    { return t.containsIgnoreCase ("ms") ? t.getFloatValue() / 1000.0f : t.getFloatValue(); });
        case Format::hertz:
            return a.withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " Hz"; });
        case Format::cents:
            return a.withStringFromValueFunction ([] (float v, int)
                                                  { return juce::String (juce::roundToInt (v)) + " ct"; });
        case Format::decibels:
            return a.withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " dB"; });
    }
    return a;
}

std::unique_ptr<juce::AudioParameterFloat>
floatParam (const juce::ParameterID& id, const juce::String& name, Range range, float defaultValue, Format format)
{
    return std::make_unique<juce::AudioParameterFloat> (id, name, range, defaultValue, attributesFor (format));
}

Range skewed (float min, float max, float centre, float interval = 0.0f)
{
    Range r { min, max, interval };
    r.setSkewForCentre (centre);
    return r;
}

juce::StringArray bodyNames()
{
    juce::StringArray names;
    for (const auto* name : engine::Body::names)
        names.add (name);
    return names;
}

juce::StringArray articulationNames()
{
    juce::StringArray names;
    for (const auto* name : engine::articulationNames)
        names.add (name);
    return names;
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Bow
    layout.add (floatParam (id::bowPosition, "Bow Position", skewed (0.03f, 0.3f, 0.11f), 0.11f, Format::plain));
    layout.add (floatParam (id::bowPressure, "Bow Pressure", Range { 0.0f, 1.0f }, 0.5f, Format::percent));
    layout.add (floatParam (id::attack, "Attack", skewed (0.005f, 0.5f, 0.08f), 0.08f, Format::seconds));
    layout.add (floatParam (id::release, "Release", skewed (0.02f, 1.0f, 0.15f), 0.15f, Format::seconds));

    // Pitch
    layout.add (floatParam (id::vibratoRate, "Vibrato Rate", Range { 3.0f, 8.0f }, 5.5f, Format::hertz));
    layout.add (floatParam (id::vibratoDepth, "Vibrato Depth", Range { 0.0f, 60.0f }, 25.0f, Format::cents));
    layout.add (floatParam (id::vibratoDelay, "Vibrato Delay", Range { 0.0f, 1.0f }, 0.3f, Format::seconds));
    layout.add (floatParam (id::portamento, "Portamento", skewed (0.0f, 0.3f, 0.05f), 0.05f, Format::seconds));
    layout.add (std::make_unique<juce::AudioParameterInt> (id::bendRange, "Pitch Bend Range", 1, 12, 2));

    // Body and output
    layout.add (std::make_unique<juce::AudioParameterChoice> (id::body, "Body", bodyNames(), 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (id::bodyQuality,
                                                              "Body Quality",
                                                              juce::StringArray { "Measured", "Light" },
                                                              0));
    layout.add (floatParam (id::sordino, "Sordino", Range { 0.0f, 1.0f }, 0.0f, Format::percent));
    layout.add (floatParam (id::width, "Stereo Width", Range { 0.0f, 1.0f }, 0.5f, Format::percent));
    layout.add (floatParam (id::room, "Room", Range { 0.0f, 1.0f }, 0.15f, Format::percent));
    layout.add (floatParam (id::outputGain, "Output Gain", Range { -60.0f, 12.0f, 0.1f }, 0.0f, Format::decibels));

    // Playing (Phase 4)
    layout.add (std::make_unique<juce::AudioParameterChoice> (id::playMode,
                                                              "Play Mode",
                                                              juce::StringArray { "Auto", "Mono legato", "Poly" },
                                                              0));
    layout.add (floatParam (id::resonance, "Resonance", Range { 0.0f, 1.0f }, 0.3f, Format::percent));
    layout.add (floatParam (id::humanise, "Humanise", Range { 0.0f, 1.0f }, 0.5f, Format::percent));
    layout.add (std::make_unique<juce::AudioParameterBool> (id::autoBowChange, "Auto Bow Change", true));
    layout.add (std::make_unique<juce::AudioParameterBool> (id::mpe, "MPE", false));
    layout.add (std::make_unique<juce::AudioParameterInt> (id::mpeBendRange, "MPE Bend Range", 1, 96, 48));

    // Articulations (Phase 5); keyswitches override this until it changes.
    layout.add (
        std::make_unique<juce::AudioParameterChoice> (id::articulation, "Articulation", articulationNames(), 0));

    // Keyboard response (Phase 7): how loud velocity 127 plays, and an octave
    // shift for typing keyboards that start below the violin's range.
    layout.add (floatParam (id::velocityRange, "Velocity Range", Range { 0.6f, 1.0f }, 0.7f, Format::percent));
    layout.add (std::make_unique<juce::AudioParameterChoice> (id::octave,
                                                              "Octave",
                                                              juce::StringArray { "-2", "-1", "0", "+1", "+2" },
                                                              octaveChoiceOffset + defaultOctaveShift));

    // Clean bowing: 0 plays like a professional, keeping the string free of
    // scratch; higher values let the unassisted model's errors back in
    // (docs/CLEAN_BOWING.md).
    layout.add (floatParam (id::imperfection, "Imperfection", Range { 0.0f, 1.0f }, 0.0f, Format::percent));

    // Bow noise: the grain of rosin and hair in the friction. 50% matches the
    // noise of real held notes; 0 is the clean model (docs/NATURAL_PLAYING.md).
    layout.add (floatParam (id::bowNoise, "Bow Noise", Range { 0.0f, 1.0f }, 0.5f, Format::percent));

    return layout;
}

Reader::Reader (juce::AudioProcessorValueTreeState& state)
    : bowPosition (state.getRawParameterValue (id::bowPosition.getParamID())),
      bowPressure (state.getRawParameterValue (id::bowPressure.getParamID())),
      attack (state.getRawParameterValue (id::attack.getParamID())),
      release (state.getRawParameterValue (id::release.getParamID())),
      vibratoRate (state.getRawParameterValue (id::vibratoRate.getParamID())),
      vibratoDepth (state.getRawParameterValue (id::vibratoDepth.getParamID())),
      vibratoDelay (state.getRawParameterValue (id::vibratoDelay.getParamID())),
      portamento (state.getRawParameterValue (id::portamento.getParamID())),
      bendRange (state.getRawParameterValue (id::bendRange.getParamID())),
      body (state.getRawParameterValue (id::body.getParamID())),
      bodyQuality (state.getRawParameterValue (id::bodyQuality.getParamID())),
      sordino (state.getRawParameterValue (id::sordino.getParamID())),
      width (state.getRawParameterValue (id::width.getParamID())),
      room (state.getRawParameterValue (id::room.getParamID())),
      outputGain (state.getRawParameterValue (id::outputGain.getParamID())),
      playMode (state.getRawParameterValue (id::playMode.getParamID())),
      resonance (state.getRawParameterValue (id::resonance.getParamID())),
      humanise (state.getRawParameterValue (id::humanise.getParamID())),
      autoBowChange (state.getRawParameterValue (id::autoBowChange.getParamID())),
      mpe (state.getRawParameterValue (id::mpe.getParamID())),
      mpeBendRange (state.getRawParameterValue (id::mpeBendRange.getParamID())),
      articulation (state.getRawParameterValue (id::articulation.getParamID())),
      velocityRange (state.getRawParameterValue (id::velocityRange.getParamID())),
      octave (state.getRawParameterValue (id::octave.getParamID())),
      imperfection (state.getRawParameterValue (id::imperfection.getParamID())),
      bowNoise (state.getRawParameterValue (id::bowNoise.getParamID()))
{
}

int Reader::octaveShift() const
{
    return juce::jlimit (-2, 2, juce::roundToInt (octave->load()) - octaveChoiceOffset);
}

engine::EngineSettings Reader::read() const
{
    engine::EngineSettings s;
    s.performance.voice.bowPosition = bowPosition->load();
    s.performance.voice.bowPressure = bowPressure->load();
    s.performance.voice.attackSeconds = attack->load();
    s.performance.voice.releaseSeconds = release->load();
    s.performance.voice.vibratoRateHz = vibratoRate->load();
    s.performance.voice.vibratoDepthCents = vibratoDepth->load();
    s.performance.voice.vibratoDelaySeconds = vibratoDelay->load();
    s.performance.voice.portamentoSeconds = portamento->load();
    s.performance.voice.pitchBendRangeSemitones = bendRange->load();

    s.performance.voice.resonance = resonance->load();
    s.performance.voice.humanise = humanise->load();
    s.performance.voice.imperfection = imperfection->load();
    s.performance.voice.bowNoise = bowNoise->load();
    s.performance.voice.autoBowChange = autoBowChange->load() > 0.5f;
    s.performance.playMode = static_cast<engine::PlayMode> (juce::jlimit (0, 2, static_cast<int> (playMode->load())));
    s.performance.mpe = mpe->load() > 0.5f;
    s.performance.mpeBendRangeSemitones = mpeBendRange->load();
    s.performance.articulation = static_cast<engine::Articulation> (
        juce::jlimit (0, engine::numArticulations - 1, static_cast<int> (articulation->load())));
    s.performance.velocityTop = velocityRange->load();
    s.performance.octaveShift = octaveShift();

    s.body = static_cast<int> (body->load());
    s.bodyQuality = bodyQuality->load() < 0.5f ? engine::Body::Quality::convolution : engine::Body::Quality::modal;

    s.output.sordino = sordino->load();
    s.output.width = width->load();
    s.output.room = room->load();
    s.output.gainDb = outputGain->load();
    return s;
}
} // namespace violinsynth::params
