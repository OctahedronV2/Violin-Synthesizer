#pragma once

#include "dsp/FractionalDelay.h"
#include "dsp/FrictionJunction.h"
#include "dsp/LoopFilter.h"

#include <array>
#include <vector>

namespace violinsynth::dsp
{
enum class Tuning
{
    harmonic, // bowed (sawtooth) motion
    fundamental, // free vibration, e.g. pizzicato
};

// Torsional (twisting) waves. The bow drags the string's surface, which both
// moves the string sideways and twists it; the twist travels much faster than
// the transverse wave and dies away much sooner, which steadies the stick-slip
// at the bow (docs/CLEAN_BOWING.md). speedRatio 0 turns it off.
struct TorsionParams
{
    double speedRatio = 0.0; // torsional / transverse wave speed
    double impedanceRatio = 3.0; // torsional / transverse impedance, at the string's surface
    double q = 45.0; // quality factor of the torsional modes
};

struct StringParams
{
    FrictionParams friction;
    TorsionParams torsion;
    LossSpec loss;
    Tuning tuning = Tuning::harmonic;
};

// Digital-waveguide bowed string, run at the internal (oversampled) rate.
// Mirrors _simulate in research/violin_model/waveguide.py; see there and
// docs/PHASE1_FINDINGS.md for the model.
//
//   bridge <-- line (round trip beta*P) -- BOW -- line (round trip (1-beta)*P) --> nut
//   -H(z)                              junction                                    -1
class BowedString
{
public:
    // Allocates the delay lines for notes down to lowestF0. Not real-time safe.
    void prepare (double internalSampleRate, double lowestF0);
    void reset();
    // The lowest note this string will play, at or above prepare()'s: the
    // phase delay table starts there. Real-time safe.
    void setLowestF0 (double f0);

    // A magnetic pickup: coils at up to four points, in metres from the bridge,
    // on a string whose open length is openLength (m) at openF0 (Hz). The
    // pickup hears the string's transverse velocity averaged over the coils
    // (docs/BOWED_GUITAR.md). No coils: no pickup, and no cost.
    static constexpr int maxCoils = 4;
    void setPickup (const std::array<double, maxCoils>& coilMetres, int numCoils, double openLength, double openF0);
    double pickupVelocity() const { return lastPickup; }

    void setParams (const StringParams& newParams);
    const StringParams& getParams() const { return params; }

    // One sample. f0 in Hz, beta = bow distance from the bridge as a fraction
    // of the string length, vBow in m/s, force in N, excitation = velocity
    // injected at the bow point (m/s). Returns the transverse bridge force (N).
    double process (double f0, double beta, double vBow, double force, double excitation = 0.0);

    // A finger holding the string at the bow point (pizzicato). The string
    // there takes `hold` (0..1) of the way from its free velocity to the
    // finger's: a fingertip pressed on the string holds it almost rigidly and
    // lets go as it rolls off (docs/PIZZICATO.md). hold = R / (R + 2Z) for a
    // fingertip of mechanical resistance R. 0 lets go.
    void setFinger (double velocity, double hold)
    {
        fingerVelocity = velocity;
        fingerHold = hold;
    }

    // HG experiments. A bow of finite width: two contact points `fraction`
    // of the string apart (0: a point bow). A soft stopping finger at the far
    // end: it keeps `gain` of each reflection and low-passes it with pole `a`
    // (0: a rigid stop).
    void setBowWidth (double fraction) { widthFraction = fraction; }
    void setFingerStop (double gain, double a)
    {
        fingerGain = gain;
        fingerPole = a;
    }
    double stringVelocity() const { return lastVelocity; }
    bool isSticking() const { return sticking; }
    // Samples between the last two slip onsets (the string letting go of the
    // bow), and whether this sample started a slip. Helmholtz motion lets go
    // once per period.
    bool slipStarted() const { return slipOnset; }
    double slipInterval() const { return lastSlipInterval; }
    double samplesSinceSlip() const { return sinceSlip; }

    // Smallest beta the delay lines support at f0 (at least 2 samples bow-to-bridge).
    double minBeta (double f0) const;

    double sampleRate() const { return fs; }

private:
    void updateCoefficients (double f0);
    double tabulatedPhaseDelay (double f0, double a);

    StringParams params;
    double fs = 192000.0;
    FractionalDelay bridgeLine;
    FractionalDelay nutLine;
    FractionalDelay torsionBridgeLine, torsionNutLine;
    LoopFilter loopFilter;

    // Harmonic phase delay on a log-frequency grid, filled as pitches are
    // played and cleared when the loss settings change: under vibrato this
    // replaces 40 atan2 calls per coefficient update with an interpolation.
    std::vector<double> tauTable;
    double tauLogLowest = 0.0;

    double lastF0 = -1.0;
    double tau = 0.0;
    bool sticking = false;
    double lastVelocity = 0.0;
    bool slipOnset = false;
    double sinceSlip = 0.0, lastSlipInterval = 0.0;
    double fingerVelocity = 0.0, fingerHold = 0.0;
    double torsionReflection = -1.0, contactImpedance = 0.0, transverseShare = 1.0;

    std::array<double, maxCoils> coils {}; // fraction of the open string, from the bridge
    int numCoils = 0;
    double pickupScale = 0.0; // coil fraction at f0 = coil fraction of the open string * f0 * pickupScale
    double lastPickup = 0.0;

    // HG experiment: the bridge moves. The body's admittance at the bridge
    // (a bank of resonances) turns the string's force into bridge velocity,
    // which feeds back into the string.
    static constexpr int bodyModes = 10;
    struct Mode { double b0 = 0.0, a1 = 0.0, a2 = 0.0, x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0; };
    std::array<Mode, bodyModes> modes {};
    bool bodyFeedback = false;
    FractionalDelay midAB, midBA; // between the two contact points of a wide bow
    double widthFraction = 0.0, fingerGain = 1.0, fingerPole = 0.0, fingerState = 0.0;
    bool stickingB = false;
    double fingerDelayFor (double f0);
    double cachedFingerPole = -1.0, cachedFingerF0 = -1.0, cachedFingerDelay = 0.0;
    double process2 (double f0, double beta, double vBow, double force, double excitation);
    double lastForce = 0.0;
};
} // namespace violinsynth::dsp
