#include "TestUtilities.h"
#include "dsp/BowedString.h"
#include "dsp/FractionalDelay.h"
#include "dsp/FrictionJunction.h"
#include "dsp/LoopFilter.h"
#include "engine/StringData.h"

#include <juce_core/juce_core.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <complex>
#include <numbers>
#include <random>

using namespace violinsynth;
using Catch::Approx;

// --- friction junction ------------------------------------------------------

TEST_CASE ("Friction junction sticks inside the static limit", "[dsp][friction]")
{
    const dsp::FrictionParams p;
    const auto force = 1.0;
    const auto vH = 0.2 - 0.9 * p.muS * force / (2.0 * p.impedance);
    const auto r = dsp::solveJunction (0.2, vH, force, p, true);
    CHECK (r.sticking);
    CHECK (r.velocity == Approx (0.2));
}

TEST_CASE ("Friction junction slip satisfies force balance", "[dsp][friction]")
{
    const dsp::FrictionParams p;
    const auto vH = GENERATE (-3.0, -1.0, 1.5, 4.0);
    const auto vBow = 0.2, force = 0.5;
    const auto r = dsp::solveJunction (vBow, vH, force, p, false);
    REQUIRE_FALSE (r.sticking);
    const auto stringForce = 2.0 * p.impedance * (r.velocity - vH);
    const auto friction = force * dsp::frictionCoefficient (vBow - r.velocity, p);
    CHECK (stringForce == Approx (friction).margin (1e-12));
}

TEST_CASE ("Friction junction keeps its state in the hysteresis region", "[dsp][friction]")
{
    const dsp::FrictionParams p;
    const auto vBow = 0.2, force = 1.0;
    const auto vH = vBow - 0.95 * p.muS * force / (2.0 * p.impedance);
    CHECK (dsp::solveJunction (vBow, vH, force, p, true).sticking);
    CHECK_FALSE (dsp::solveJunction (vBow, vH, force, p, false).sticking);
}

// --- loop filter and delay ----------------------------------------------------

TEST_CASE ("Loss filter meets its decay-time targets", "[dsp][loss]")
{
    const auto fs = GENERATE (88200.0, 192000.0);
    const dsp::LossSpec spec;
    const auto f0 = 440.0;
    const auto c = dsp::lossCoefficients (f0, fs, spec);

    auto magnitude = [&] (double f)
    {
        const auto z = std::polar (1.0, -2.0 * std::numbers::pi * f / fs);
        return std::abs (c.g * (1.0 - c.a) / (1.0 - c.a * z));
    };

    CHECK (magnitude (0.0) == Approx (std::pow (10.0, -3.0 / (f0 * spec.t60))));
    CHECK (magnitude (spec.fHigh) == Approx (std::pow (10.0, -3.0 / (f0 * spec.t60High))).epsilon (1e-6));
}

TEST_CASE ("Fractional delay reproduces a delayed sinusoid", "[dsp][delay]")
{
    dsp::FractionalDelay line;
    line.prepare (100.0);
    const auto delay = 37.3;
    const auto w = 2.0 * std::numbers::pi * 0.01; // well below Nyquist
    double maxError = 0.0;

    for (int n = 0; n < 500; ++n)
    {
        if (n > 50)
            maxError = std::max (maxError, std::abs (line.read (delay) - std::sin (w * (n - delay))));
        line.write (std::sin (w * n));
    }

    CHECK (maxError < 1e-4);
}

// --- bowed string against the Python reference --------------------------------

namespace
{
juce::var loadGolden()
{
    const auto file = juce::File (VIOLINSYNTH_TEST_DATA_DIR).getChildFile ("golden/bowed_string.json");
    REQUIRE (file.existsAsFile());
    return juce::JSON::parse (file);
}

dsp::StringParams paramsFrom (const juce::var& c)
{
    dsp::StringParams p;
    p.friction.impedance = c["impedance"];
    p.friction.muS = c["mu_s"];
    p.friction.muD = c["mu_d"];
    p.friction.v0 = c["v0"];
    p.loss.t60 = c["t60"];
    p.loss.t60High = c["t60_high"];
    p.loss.fHigh = c["f_high"];
    p.tuning = static_cast<bool> (c["harmonic_tuning"]) ? dsp::Tuning::harmonic : dsp::Tuning::fundamental;
    return p;
}
} // namespace

TEST_CASE ("BowedString matches the Python reference implementation", "[dsp][string][golden]")
{
    const auto golden = loadGolden();
    const auto* cases = golden["cases"].getArray();
    REQUIRE (cases != nullptr);

    for (const auto& c : *cases)
    {
        const juce::String name = c["name"];
        INFO ("case " << name);

        const double f0 = c["f0"], beta = c["beta"], vBow = c["v_bow"], force = c["f_bow"], fs = c["internal_fs"];
        dsp::BowedString string;
        string.prepare (fs, f0);
        string.setParams (paramsFrom (c));

        const auto* reference = c["bridge_force"].getArray();
        REQUIRE (reference != nullptr);

        double peak = 0.0;
        for (const auto& v : *reference)
            peak = std::max (peak, std::abs (static_cast<double> (v)));

        const auto total = static_cast<int> (c["total_samples"]);
        double maxError = 0.0;
        int stuck = 0, onsets = 0, steady = 0;
        bool previous = false;

        for (int i = 0; i < total; ++i)
        {
            const auto out = string.process (f0, beta, vBow, force);

            if (i < reference->size())
                maxError = std::max (maxError, std::abs (out - static_cast<double> ((*reference)[i])));

            if (i >= total / 2)
            {
                ++steady;
                stuck += string.isSticking() ? 1 : 0;
                onsets += (previous && ! string.isSticking()) ? 1 : 0;
            }
            previous = string.isSticking();
        }

        CHECK (maxError < 1e-6 * peak);
        CHECK (static_cast<double> (stuck) / steady
               == Approx (static_cast<double> (c["steady_stick_fraction"])).margin (0.01));
        CHECK (onsets / (steady / (fs / f0))
               == Approx (static_cast<double> (c["steady_slips_per_period"])).margin (0.03));
    }
}

TEST_CASE ("Bowed notes are in tune across the range", "[dsp][string][tuning]")
{
    const auto internalRate = GENERATE (176400.0, 192000.0);
    const auto note = GENERATE (55, 62, 69, 76, 81, 88, 93, 100);
    CAPTURE (internalRate, note);

    const auto f0 = test::midiToHz (note);
    dsp::BowedString string;
    string.prepare (internalRate, f0);
    auto params = string.getParams();
    params.friction.impedance = engine::stringForNote (note).impedance;
    string.setParams (params);

    const auto beta = std::max (0.1, 1.2 * string.minBeta (f0));
    const auto vBow = 0.2;
    const auto force
        = 0.3 * 2.0 * params.friction.impedance * vBow / (beta * (params.friction.muS - params.friction.muD));

    const auto n = static_cast<int> (1.2 * internalRate);
    std::vector<double> out (static_cast<std::size_t> (n));
    for (auto& s : out)
        s = string.process (f0, beta, vBow, force);

    // Phase 1 found +/-2 cents up to D7 and a residual of up to 3 cents at
    // the very top of the range (docs/PHASE1_FINDINGS.md, section 2.5).
    const auto tolerance = note >= 99 ? 3.5 : 2.0;
    const std::span<const double> steady (out.data() + n / 2, static_cast<std::size_t> (n / 2));
    CHECK (std::abs (test::cents (test::estimateF0 (steady, internalRate, f0), f0)) < tolerance);
}

TEST_CASE ("BowedString stays finite and bounded under random control changes", "[dsp][string]")
{
    std::mt19937 rng (7);
    std::uniform_real_distribution<double> u (0.0, 1.0);
    dsp::BowedString string;
    string.prepare (192000.0, 150.0);

    double f0 = 196.0, beta = 0.1, vBow = 0.2, force = 0.5;
    double peakVelocity = 0.0;

    for (int i = 0; i < 192000; ++i)
    {
        if (i % 960 == 0)
        {
            f0 = 196.0 * std::pow (2.0, 3.0 * u (rng));
            beta = std::max (0.05 + 0.2 * u (rng), 1.2 * string.minBeta (f0));
            vBow = (u (rng) - 0.5) * 1.0;
            force = 3.0 * u (rng);
        }
        const auto out = string.process (f0, beta, vBow, force);
        REQUIRE (std::isfinite (out));
        peakVelocity = std::max (peakVelocity, std::abs (string.stringVelocity()));
    }

    CHECK (peakVelocity < 50.0);
}
