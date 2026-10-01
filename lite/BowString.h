// Octavio Lite, bare string: one bowed string and nothing else.
//
// While a key is held the bow moves at a constant speed and presses with a
// constant force at a fixed contact point. When the key is released the bow
// lifts off and the string rings down on its own. No envelope, vibrato, noise,
// body or room: only the stick-slip at the bow, to find Helmholtz motion.
//
// Freestanding like LiteCore.h (whose maths, delay line and friction it uses),
// so the browser build and the plugin run the same code.

#pragma once

#include "LiteCore.h"

namespace lite
{
struct BowString
{
    enum Param
    {
        pForce, // N
        pSpeed, // m/s
        pContact, // fraction of the string length from the bridge
        numParams
    };
    static constexpr ParamInfo kInfo[numParams] = {
        { "force", "Bow", "Bow pressure", "N", 0.02, 3.0, 0.6 },
        { "speed", "Bow", "Bow speed", "m/s", 0.02, 1.0, 0.2 },
        { "contact", "Bow", "Contact point", "of string", 0.03, 0.3, 0.12 },
    };

    // The string: an A string, the same for every note (the finger only shortens it).
    static constexpr double impedance = 0.2; // kg/s
    static constexpr double muS = 0.8, muD = 0.3, v0 = 0.1; // rosin friction curve
    static constexpr double ringSeconds = 2.5; // decay of the low harmonics
    static constexpr double darkness = 0.25; // loss pole: how fast upper harmonics die
    static constexpr double outputScale = 0.2;

    double params[numParams];
    double hostRate = 48000.0, fs = 96000.0;
    Delay bridge, nut;
    double lp = 0.0, N = 100.0, g = 0.99;
    bool stick = false, bowing = false;
    int heldNotes[16] = {}, numHeld = 0;

    // decimator 2x -> 1x and DC blocker
    static constexpr int decTaps = 63;
    double decH[decTaps], decBuf[decTaps] = {};
    int decW = 0;
    double dcX = 0.0, dcY = 0.0;

    // What the string is doing, for the display.
    static constexpr int scopeSize = 2048;
    float scope[scopeSize] = {}; // string velocity at the bow, host rate
    int scopePos = 0;
    double sinceSlip = 0.0, meanInterval = 0.0, varInterval = 0.0;
    int slipCount = 0;

    void prepare (double sampleRate)
    {
        hostRate = sampleRate;
        fs = 2.0 * sampleRate;
        for (int i = 0; i < numParams; ++i)
            params[i] = kInfo[i].def;
        for (int i = 0; i < decTaps; ++i)
        {
            const double k = i - (decTaps - 1) / 2.0, fc = 0.21;
            const double sinc = k == 0 ? 2 * fc : m::sin (2 * m::pi * fc * k) / (m::pi * k);
            decH[i] = sinc * (0.42 - 0.5 * m::cos (2 * m::pi * i / (decTaps - 1)) + 0.08 * m::cos (4 * m::pi * i / (decTaps - 1)));
        }
        bridge.clear();
        nut.clear();
        lp = 0.0;
        stick = bowing = false;
        numHeld = 0;
        setNote (69);
    }

    void setParam (int i, double v)
    {
        if (i >= 0 && i < numParams)
            params[i] = m::clamp (v, kInfo[i].min, kInfo[i].max);
    }

    void setNote (int note)
    {
        const double f0 = m::midiHz (note);
        g = m::pow (10.0, -3.0 / (ringSeconds * f0));
        const double w = 2 * m::pi * f0 / fs;
        const double pd = m::atan2 (darkness * m::sin (w), 1.0 - darkness * m::cos (w)) / w;
        N = fs / f0 - pd; // round trip, less the loss filter's own delay
        meanInterval = N;
        varInterval = 0.0;
        slipCount = 0;
    }

    // Monophonic: a new key moves the finger; releasing it returns to the last key still held.
    void noteOn (int note)
    {
        if (note < 55 || note > 103)
            return;
        noteOff (note);
        if (numHeld < 16)
            heldNotes[numHeld++] = note;
        setNote (note);
        bowing = true;
    }
    void noteOff (int note)
    {
        for (int i = 0; i < numHeld; ++i)
            if (heldNotes[i] == note)
            {
                for (int j = i; j < numHeld - 1; ++j)
                    heldNotes[j] = heldNotes[j + 1];
                --numHeld;
                if (numHeld > 0)
                    setNote (heldNotes[numHeld - 1]);
                else
                    bowing = false;
                return;
            }
    }
    void allNotesOff()
    {
        numHeld = 0;
        bowing = false;
    }

    // One sample at the internal rate. Returns the force on the bridge.
    double tick (double& bowPointVelocity)
    {
        const double beta = params[pContact];
        const double vBow = bowing ? params[pSpeed] : 0.0;
        const double force = bowing ? params[pForce] : 0.0;
        const double inB = bridge.read (beta * N), inN = nut.read ((1.0 - beta) * N);
        lp = (1 - darkness) * inB + darkness * lp;
        const double vinB = -g * lp, vinN = -inN;
        const double vh = vinB + vinN;
        const bool wasSticking = stick;
        const double v = friction (vBow, vh, force, impedance, muS, muD, v0, stick);
        const double dv = v - vh;
        bridge.push (vinN + dv);
        nut.push (vinB + dv);

        // Slip onsets: Helmholtz motion lets go of the bow once per period.
        sinceSlip += 1.0;
        if (bowing && wasSticking && ! stick)
        {
            if (slipCount > 0)
            {
                const double d = sinceSlip - meanInterval;
                meanInterval += 0.1 * d;
                varInterval = 0.9 * (varInterval + 0.1 * d * d);
            }
            ++slipCount;
            sinceSlip = 0.0;
        }
        bowPointVelocity = v;
        return inB - vinB;
    }

    void process (float* left, float* right, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            double y = 0.0, vel = 0.0;
            for (int s = 0; s < 2; ++s)
            {
                decBuf[decW] = tick (vel);
                decW = (decW + 1) % decTaps;
                if (s == 1)
                    for (int k = 0; k < decTaps; ++k)
                        y += decH[k] * decBuf[(decW + k) % decTaps];
            }
            dcY = y - dcX + 0.9995 * dcY;
            dcX = y;
            scope[scopePos] = (float) vel;
            scopePos = (scopePos + 1) % scopeSize;
            left[i] = right[i] = (float) (outputScale * dcY);
        }
    }

    // For the display: slips per period (1 = Helmholtz) and how irregular the slips are.
    double slipsPerPeriod() const
    {
        if (! bowing || slipCount < 4)
            return 0.0;
        if (sinceSlip > 3.0 * N)
            return 0.0; // stuck to the bow: no slips at all
        return N / m::max (meanInterval, 1.0);
    }
    double irregularity() const { return m::sqrt (varInterval) / m::max (meanInterval, 1.0); }
};
} // namespace lite
