// Octavio Lite, bare string: one bowed string and nothing else.
//
// While a key is held the bow moves at a constant speed and presses with a
// constant force at a fixed contact point. When the key is released the bow
// lifts off and the string rings down on its own. No envelope, vibrato, added
// noise or room: only the stick-slip at the bow, heard through a violin body.
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
        pBody, // 0: the bare string's force on the bridge, 1: heard through a violin body
        pWideBow, // 0: the bow touches at one point, 1: along a centimetre of springy hair
        numParams
    };
    static constexpr ParamInfo kInfo[numParams] = {
        { "force", "Bow", "Bow pressure", "N", 0.02, 3.0, 0.5 },
        { "speed", "Bow", "Bow speed", "m/s", 0.02, 1.0, 0.2 },
        { "contact", "Bow", "Contact point", "of string", 0.03, 0.3, 0.12 },
        { "body", "Body", "Violin body", "", 0.0, 1.0, 1.0 },
        { "widebow", "Bow", "Real bow", "", 0.0, 1.0, 1.0 }, // width, hair give and rosin grain
    };

    // The string: an A string, the same for every note (the finger only shortens it).
    static constexpr double impedance = 0.2; // kg/s
    static constexpr double muS = 0.8, muD = 0.3, v0 = 0.1; // rosin friction curve
    // String losses, as decay times: the low harmonics ring ringSeconds, and at
    // ringHighHz they die in ringHighSeconds (measured violin strings: about 3 s
    // and 0.25 s at 4 kHz). Set in Hz, so the tone is the same at any sample rate.
    static constexpr double ringSeconds = 2.5;
    static constexpr double ringHighSeconds = 0.25, ringHighHz = 4000.0;
    static constexpr double outputScale = 0.2;

    double params[numParams];
    double hostRate = 48000.0, fs = 96000.0;
    Delay bridge, nut;
    Body body; // impulse response loaded by the host into body.irBuf, then loadBody()
    double bodyGain = 1.0;
    // Twisting (torsional) waves: the bow drags the string's surface, which both
    // bends and twists the string. The twist travels 5x faster, carries a share
    // of the motion and dies within a couple of its own periods, which damps the
    // ripples that a point bow otherwise traps between itself and the bridge.
    static constexpr double torsionSpeed = 5.0, torsionImpedance = 3.0, torsionQ = 2.0;
    bool torsion = false; // tried 2026-10-01: no clearer Helmholtz motion here, so off
    Delay tBridge, tNut;
    // Bow width: the hair ribbon touches the string along about a centimetre, not at
    // one point. Modelled as bowPoints contact points spread over bowWidth metres,
    // each with its own stick-slip and a share of the force. 1 point = the old point bow.
    static constexpr int maxBowPoints = 4;
    static constexpr double waveSpeed = 2 * 0.325 * 440.0; // A string, m/s
    int bowPoints = 3; // when pWideBow is on
    double bowWidth = 0.01;
    Delay gapR[maxBowPoints - 1], gapL[maxBowPoints - 1]; // between neighbouring points, towards nut / towards bridge
    bool stickK[maxBowPoints] = {};
    // Rosin grain: the hairs and rosin give a force that is never quite steady.
    // Each point's force wobbles by `grain` (relative, rms) with noise below grainHz.
    double grain = 0.03, grainHz = 3000.0; // only with pWideBow
    Rng rng;
    double grainLp[maxBowPoints] = {};
    // Bow hair give: the hair at each contact point is a spring (hairStiffness N/m for the
    // whole ribbon) with damping hairDamping kg/s, so it can stretch a little with the string.
    // 0 = rigid hair.
    // Tuned 2026-10-01 so open A4 matches a real violin's cycle-to-cycle variation and
    // noise between harmonics (Iowa recording) while keeping Helmholtz motion as easy to find.
    double hairStiffness = 2000.0, hairDamping = 5.0;
    double hairY[maxBowPoints] = {};
    double lp = 0.0, N = 100.0, g = 0.99, darkness = 0.25;
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
        tBridge.clear();
        tNut.clear();
        for (auto& d : gapR)
            d.clear();
        for (auto& d : gapL)
            d.clear();
        lp = 0.0;
        stick = bowing = false;
        numHeld = 0;
        setNote (69);
    }

    // The body response is in body.irBuf[0..length) at the host rate. Its level is
    // set so a note sounds about as loud with the body as without.
    void loadBody (int length)
    {
        body.initTables();
        body.load (length);
        double e = 0.0;
        for (int i = 0; i < body.irLength; ++i)
            e += (double) body.irBuf[i] * body.irBuf[i];
        bodyGain = e > 0.0 ? 0.35 / m::sqrt (e) : 1.0;
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
        {
            // one-pole loss filter: gain g at DC, and the ringHigh decay at ringHighHz
            const double r = m::pow (10.0, -3.0 / f0 * (1.0 / ringHighSeconds - 1.0 / ringSeconds));
            const double cw = m::cos (2 * m::pi * ringHighHz / fs), r2 = r * r;
            const double A = 1.0 - r2, B = 1.0 - r2 * cw;
            darkness = A <= 1.0e-12 ? 0.0 : (B - m::sqrt (m::max (B * B - A * A, 0.0))) / A;
        }
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
        const int K = torsion || params[pWideBow] < 0.5 ? 1 : m::clamp (bowPoints, 1, maxBowPoints);
        // Spacing between contact points, one-way samples (at least 2 for the delay read).
        const double gap = K > 1 ? m::max (2.0, bowWidth * fs / waveSpeed / (K - 1)) : 0.0;
        // With the twist, the bow moves 4/3 as fast so the string bends as far as without it.
        const double makeup = torsion ? (1.0 + torsionImpedance) / torsionImpedance : 1.0;
        const double vBow = bowing ? params[pSpeed] * makeup : 0.0;
        const double force = bowing ? params[pForce] / K : 0.0;
        const double inB = bridge.read (beta * N), inN = nut.read ((1.0 - beta) * N - 2.0 * (K - 1) * gap);
        lp = (1 - darkness) * inB + darkness * lp;
        const double vinB = -g * lp, vinN = -inN;
        const bool wasSticking = stick;
        if (K == 1 || torsion)
        {
            double vh = vinB + vinN, z = impedance, tinB = 0.0, tinN = 0.0;
            const double zt = impedance * torsionImpedance;
            if (torsion)
            {
                // Both waves meet the bow: the surface moves with their sum, and the
                // bow sees the two impedances in series.
                const double nt = N / torsionSpeed, r = m::exp (-m::pi / torsionQ);
                tinB = -r * tBridge.read (beta * nt);
                tinN = -r * tNut.read ((1.0 - beta) * nt);
                vh += tinB + tinN;
                z = impedance * zt / (impedance + zt);
            }
            const double v = friction (vBow, vh, force * grainFactor (0), z, muS, muD, v0, stick);
            const double f = 2.0 * z * (v - vh); // friction force on the string
            const double dv = f / (2.0 * impedance);
            bridge.push (vinN + dv);
            nut.push (vinB + dv);
            if (torsion)
            {
                const double dt = f / (2.0 * zt);
                tBridge.push (tinN + dt);
                tNut.push (tinB + dt);
            }
            bowPointVelocity = v;
        }
        else
        {
            // Waves arriving at each point: from the bridge side and from the nut side.
            double fromB[maxBowPoints], fromN[maxBowPoints];
            for (int k = 0; k < K; ++k)
            {
                fromB[k] = k == 0 ? vinB : gapR[k - 1].read (gap);
                fromN[k] = k == K - 1 ? vinN : gapL[k].read (gap);
            }
            double vSum = 0.0;
            for (int k = 0; k < K; ++k)
            {
                const double vh = fromB[k] + fromN[k];
                double dv;
                if (hairStiffness > 0.0)
                {
                    // The hair's damper sits in series with the string: the junction sees both.
                    const double c = hairDamping / K, kk = hairStiffness / K;
                    const double a = 1.0 / (1.0 / (2.0 * impedance) + 1.0 / c);
                    const double vb = vBow - kk * hairY[k] / c;
                    const double vv = friction (vb, vh, force * grainFactor (k), 0.5 * a, muS, muD, v0, stickK[k]);
                    const double f = a * (vv - vh);
                    dv = f / (2.0 * impedance);
                    hairY[k] += -(f + kk * hairY[k]) / c / fs;
                    if (! bowing)
                        hairY[k] = 0.0;
                }
                else
                    dv = friction (vBow, vh, force * grainFactor (k), impedance, muS, muD, v0, stickK[k]) - vh; // f / (2Z)
                const double v = vh + dv;
                const double toB = fromN[k] + dv, toN = fromB[k] + dv;
                if (k == 0)
                    bridge.push (toB);
                else
                    gapL[k - 1].push (toB);
                if (k == K - 1)
                    nut.push (toN);
                else
                    gapR[k].push (toN);
                vSum += v;
            }
            // Across the bow width the points slip at slightly different moments; count the
            // string as slipping while it moves backwards on average, and sticking again once
            // it is back up near the bow's speed.
            bowPointVelocity = vSum / K;
            if (bowPointVelocity < 0.0)
                stick = false;
            else if (bowPointVelocity > 0.5 * vBow)
                stick = true;
        }

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
        return inB - vinB;
    }

    double grainFactor (int k)
    {
        if (grain <= 0.0 || params[pWideBow] < 0.5)
            return 1.0;
        // one-pole low-passed white noise, scaled to unit rms
        const double a = m::exp (-2 * m::pi * grainHz / fs);
        grainLp[k] = a * grainLp[k] + (1 - a) * rng.gauss();
        return m::max (0.0, 1.0 + grain * grainLp[k] * m::sqrt ((1 + a) / (1 - a)));
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
            const float bodied = (float) (bodyGain * body.process ((float) dcY));
            left[i] = right[i] = (float) (outputScale * (params[pBody] > 0.5 ? bodied : dcY));
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
