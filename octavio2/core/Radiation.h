// Octavio 2 radiation: bridge force -> stereo sound (body-radiation FINDINGS §4).
//
//   force -> 60 Hz high-pass -> brightness shelf (1.5 kHz) -> bridge (rocking resonance, mutes)
//         -> full-band all-direction body (the chosen violin, at its size)
//            -> hall tail (convolution), after the distance's pre-delay
//            -> direction filters of the mic position: left and right, each a sway between
//               three directions -> stereo width -> air -> direct sound at the hall's own delay
//               and gain (finish.py split()), scaled by distance              -> left / right
//
// Bodies, mic positions and halls come in as sample data (octavio2/data), so the plugin can
// load them from binary data and the renderer from files. Runs at 48 kHz. prepare() and
// setBodySize() allocate; process() doesn't. Violin, mic position and hall changes crossfade
// over 50 ms; the continuous controls glide.

#pragma once
#include "dsp/PartitionedConvolution.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <memory>
#include <vector>

namespace o2
{
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    double tick (double x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() { z1 = z2 = 0; }
    void set (double nb0, double nb1, double nb2, double a0, double na1, double na2)
    {
        b0 = nb0 / a0;
        b1 = nb1 / a0;
        b2 = nb2 / a0;
        a1 = na1 / a0;
        a2 = na2 / a0;
    }
    // RBJ high-pass; two of them at Q 0.5412 and 1.3066 make scipy's butter(4, f, 'hp')
    void highPass (double fs, double f, double q)
    {
        const double w = 2 * 3.14159265358979323846 * f / fs, c = std::cos (w), al = std::sin (w) / (2 * q);
        set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
    // RBJ band-pass, 0 dB peak
    void bandPass (double fs, double f, double q)
    {
        const double w = 2 * 3.14159265358979323846 * f / fs, c = std::cos (w), al = std::sin (w) / (2 * q);
        set (al, 0, -al, 1 + al, -2 * c, 1 - al);
    }
    // the ratio of two resonant low-passes 1 / (1 + s/(Q w) + (s/w)^2), at fTo (qTo) over fFrom (q):
    // moves a resonance hill from fFrom to fTo (chain.py bridge_filter), bilinear with each prewarped
    void moveResonance (double fs, double fFrom, double fTo, double q, double qTo)
    {
        const double K = 2 * fs, pi = 3.14159265358979323846;
        const double wa = K * std::tan (pi * fFrom / fs), wb = K * std::tan (pi * fTo / fs);
        // numerator (s/wa)^2 + s/(q wa) + 1, denominator the same at wb; s = K (1 - z^-1) / (1 + z^-1)
        auto coeffs = [K] (double w, double q, double& c0, double& c1, double& c2)
        {
            const double A = K * K / (w * w), B = K / (q * w);
            c0 = A + B + 1;
            c1 = 2 - 2 * A;
            c2 = A - B + 1;
        };
        double n0, n1, n2, d0, d1, d2;
        coeffs (wa, q, n0, n1, n2);
        coeffs (wb, qTo, d0, d1, d2);
        set (n0, n1, n2, d0, d1, d2);
    }
    // RBJ high shelf, slope 1 (finish.py high_shelf)
    void highShelf (double fs, double f0, double db)
    {
        const double A = std::pow (10.0, db / 40), w = 2 * 3.14159265358979323846 * f0 / fs;
        const double al = std::sin (w) / 2 * std::sqrt (2.0), c = std::cos (w), sA = std::sqrt (A);
        set (A * ((A + 1) + (A - 1) * c + 2 * sA * al),
             -2 * A * ((A - 1) + (A + 1) * c),
             A * ((A + 1) + (A - 1) * c - 2 * sA * al),
             (A + 1) - (A - 1) * c + 2 * sA * al,
             2 * ((A - 1) - (A + 1) * c),
             (A + 1) - (A - 1) * c - 2 * sA * al);
    }
};

struct RadiationData
{
    // all-direction bodies, in the Violin choice order (data/bodies/make_bodies.py)
    std::vector<std::vector<float>> bodies;
    // per mic position, in the Mic choice order: six direction filters applied after the body,
    // left, right, then the two more directions each side sways towards (left 2, left 3, right 2,
    // right 3)
    std::vector<std::array<std::vector<float>, 6>> mics;
    // stereo hall impulse responses (left, right), in the Hall choice order after "none"
    std::vector<std::array<std::vector<float>, 2>> halls;
};

class Radiation
{
public:
    static constexpr double fs = 48000.0;
    static constexpr int maxBlock = 64;
    static constexpr int maxHalls = 16;
    static constexpr double refDistance = 2.0; // m: the balance Octavio 2 had before M1
    static constexpr double bridgeRef = 2900.0; // Hz: the measured bodies' own bridge (FINDINGS §4)
    // what each Mute choice multiplies the bridge's rocking frequency by: off, con sordino
    // (1.4 kHz on the default bridge), practice mute (about 0.6 kHz)
    static constexpr double muteRatio[3] = { 1.0, 1400.0 / 2900.0, 600.0 / 2900.0 };
    // and the moved resonance's Q: a practice mute's rubber damps the bridge, so its 0.6 kHz hill
    // doesn't ring up into a honk
    static constexpr double muteQ[3] = { 2.6, 2.6, 1.2 };
    // and an overall loss: the moved hill alone leaves the level below 1 kHz, where most of the
    // energy is. Judged from players' descriptions (a practice mute is "hotel" quiet), not measured.
    static constexpr double muteLossDb[3] = { 0.0, -2.0, -10.0 };

    // Output gain per hall in dB (index 0: no hall, then RadiationData::halls in order), so every
    // room plays the tuned Haydn Finale (fitnotes.py, 2026-10-02) at -23 dB RMS: finish.py's -20 dB
    // less 3 dB of headroom for the loudest peaks, since the plugin has no normaliser. Measured with
    // render.cpp sound= hall=0..8 length=30 on the fitted Finale.
    static constexpr double hallGainDb[maxHalls] = { 4.9, 35.7, 38.1, 0.0, -12.8, -4.8, 5.1, -1.8, -2.4 };

    void prepare (const RadiationData& data)
    {
        hp1.highPass (fs, 60.0, 0.54119610);
        hp2.highPass (fs, 60.0, 1.30656296);
        setBrightness (brightDb);
        setBridge (bridgeHz, mute);
        bodies = data.bodies;
        if (bodies.empty())
            bodies.push_back ({ 1.0f });
        numViolins = (int) bodies.size();
        setBodySize (bodySize);
        numMics = std::max (1, (int) data.mics.size());
        for (int k = 0; k < 6; ++k)
        {
            std::vector<std::vector<float>> irs;
            for (const auto& m : data.mics)
                irs.push_back (m[(size_t) k].empty() ? std::vector<float> { 1.0f } : m[(size_t) k]);
            if (irs.empty())
                irs.push_back ({ 1.0f });
            dirs[k].prepare (128, irs, std::clamp (micWanted, 0, numMics - 1), (int) (0.05 * fs));
        }
        numHalls = std::min ((int) data.halls.size(), maxHalls - 1);
        for (int h = 0; h < maxHalls; ++h)
            levels[h] = (float) std::pow (10.0, hallGainDb[h] / 20.0);
        for (int h = 0; h < numHalls; ++h)
        {
            Hall& H = halls[(size_t) h];
            std::array<std::vector<float>, 2> tail;
            split (data.halls[(size_t) h], bodies[0], H, tail);
            for (int c = 0; c < 2; ++c)
            {
                H.conv[c] = std::make_unique<violinsynth::dsp::PartitionedConvolution>();
                H.conv[c]->prepare (512, { tail[(size_t) c] }, 0, 1);
            }
        }
        reset();
    }

    void reset()
    {
        hp1.reset();
        hp2.reset();
        shelf.reset();
        bridge.reset();
        air[0].reset();
        air[1].reset();
        body.reset();
        for (auto& d : dirs)
            d.reset();
        for (int h = 0; h < numHalls; ++h)
            for (auto& c : halls[(size_t) h].conv)
                c->reset();
        std::fill (std::begin (dline[0]), std::end (dline[0]), 0.0f);
        std::fill (std::begin (dline[1]), std::end (dline[1]), 0.0f);
        std::fill (std::begin (tline), std::end (tline), 0.0f);
        fadeFrom = -1;
        current = std::clamp (hallWanted, 0, numHalls);
        clock = 0;
        updateDistance();
        directDelay = directDelayWanted;
        tailDelay = tailDelayWanted;
        directGain = directGainWanted;
        tailGain = tailGainWanted;
    }

    void setBrightness (double db)
    {
        brightDb = db;
        if (db != 0.0)
            shelf.highShelf (fs, 1500.0, db);
        else
            shelf.set (1, 0, 0, 1, 0, 0);
    }
    double brightness() const { return brightDb; }

    // Bridge: the bridge's rocking resonance, 2.4 (dark) to 3.6 kHz (bright); mute: 0 off,
    // 1 con sordino, 2 practice mute. Both move the bridge hill (FINDINGS §4.4).
    void setBridge (double hz, int muteChoice)
    {
        bridgeHz = std::clamp (hz, 1500.0, 5000.0);
        mute = std::clamp (muteChoice, 0, 2);
        const double to = bridgeHz * muteRatio[mute];
        if (std::abs (to - bridgeRef) < 1.0)
            bridge.set (1, 0, 0, 1, 0, 0);
        else
            bridge.moveResonance (fs, bridgeRef, to, 2.6, muteQ[mute]);
        const double loss = std::pow (10.0, muteLossDb[mute] / 20);
        bridge.b0 *= loss;
        bridge.b1 *= loss;
        bridge.b2 *= loss;
    }

    // Violin: data.bodies[index]; changes crossfade
    void setViolin (int index) { body.selectFilter (std::clamp (index, 0, numViolins - 1)); }
    int violinCount() const { return numViolins; }

    // Message thread; allocates. Scales the body's modes by 1 / size (FINDINGS §4.6): 1 is the
    // violin as measured, about 1.15-1.2 a viola.
    void setBodySize (double size)
    {
        bodySize = std::clamp (size, 0.8, 2.5);
        std::vector<std::vector<float>> scaled;
        for (const auto& h : bodies)
        {
            const int n = (int) std::ceil ((double) h.size() * bodySize);
            std::vector<float> y ((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                const double t = i / bodySize;
                const int k = (int) t;
                const double fr = t - k;
                const double a = k < (int) h.size() ? h[(size_t) k] : 0.0;
                const double b = k + 1 < (int) h.size() ? h[(size_t) k + 1] : 0.0;
                y[(size_t) i] = (float) ((a + fr * (b - a)) / bodySize);
            }
            scaled.push_back (std::move (y));
        }
        body.prepare (128, scaled, body.selectedFilter() < numViolins ? body.selectedFilter() : 0, (int) (0.05 * fs));
    }

    // Mic position: data.mics[index]; changes crossfade
    void setMic (int index)
    {
        micWanted = std::clamp (index, 0, numMics - 1);
        for (auto& d : dirs)
            d.selectFilter (micWanted);
    }
    int micCount() const { return numMics; }

    // 0: both channels hear one (the middle) direction, 1: the two mics as placed, up to 2: wider
    void setWidth (double w) { widthWanted = (float) std::clamp (w, 0.0, 2.0); }

    // Movement 0..1: how far the player's sway turns the violin between directions (0.05-0.2 Hz)
    void setMovement (double m) { movement = std::clamp (m, 0.0, 1.0); }

    // Distance in metres (0.5..10): direct-to-reverberant ratio, the gap before the room answers,
    // and air absorption. The level stays about the same, so this changes the perspective only.
    void setDistance (double m)
    {
        distance = std::clamp (m, 0.5, 10.0);
        updateDistance();
    }

    // 0: no hall (the two mics only), 1..: data.halls[hall - 1]
    void setHall (int hall)
    {
        hallWanted = std::clamp (hall, 0, numHalls);
        updateDistance();
    }
    void setReverbGain (double g)
    {
        reverbGain = (float) g;
        updateDistance();
    }
    void setOutputGain (double g) { outGain = g; }
    int hallCount() const { return numHalls; }

    // n <= maxBlock samples of bridge force -> left, right (overwritten)
    void process (const double* force, float* outL, float* outR, int n)
    {
        if (fadeFrom < 0 && hallWanted != current)
        {
            fadeFrom = current;
            current = hallWanted;
            fadePos = 0;
            if (current > 0)
                for (auto& c : halls[(size_t) current - 1].conv)
                    c->reset();
        }
        float diffuse[maxBlock], d[6][maxBlock];
        for (int i = 0; i < n; ++i)
            diffuse[i] = (float) bridge.tick (shelf.tick (hp2.tick (hp1.tick (force[i]))));
        body.process (diffuse, n);
        for (int k = 0; k < 6; ++k)
        {
            std::copy (diffuse, diffuse + n, d[k]);
            dirs[k].process (d[k], n);
        }

        // sway: each side turns from its main direction towards one of its two others
        float w0[2][2], w1[2][2], w2[2][2]; // [channel][block start, end]
        for (int e = 0; e < 2; ++e)
        {
            const double t = (double) (clock + e * n) / fs, tau = 2 * 3.14159265358979323846;
            const double u[2] = { movement * (0.6 * std::sin (tau * 0.07 * t) + 0.4 * std::sin (tau * 0.13 * t + 1.0)),
                                  movement * (0.6 * std::sin (tau * 0.083 * t + 2.0) + 0.4 * std::sin (tau * 0.17 * t + 4.0)) };
            for (int c = 0; c < 2; ++c)
            {
                const double a = std::min (1.0, std::abs (u[c]));
                // between a power and an amplitude crossfade: the directions agree at low
                // frequencies and differ at high ones, so neither end swells or dips by more than 1.5 dB
                const double norm = std::pow ((1 - a) * (1 - a) + a * a, 0.25);
                w0[c][e] = (float) ((1 - a) / norm);
                w1[c][e] = (float) (u[c] < 0 ? a / norm : 0.0);
                w2[c][e] = (float) (u[c] > 0 ? a / norm : 0.0);
            }
        }
        clock += n;
        const float* main[2] = { d[0], d[1] };
        const float* other[2][2] = { { d[2], d[3] }, { d[4], d[5] } };
        const float wStart = width, wEnd = width + std::clamp (widthWanted - width, -0.02f, 0.02f);
        width = wEnd;
        for (int i = 0; i < n; ++i)
        {
            const float a = (float) (i + 1) / (float) n;
            float s[2];
            for (int c = 0; c < 2; ++c)
                s[c] = (w0[c][0] + a * (w0[c][1] - w0[c][0])) * main[c][i]
                       + (w1[c][0] + a * (w1[c][1] - w1[c][0])) * other[c][0][i]
                       + (w2[c][0] + a * (w2[c][1] - w2[c][0])) * other[c][1][i];
            const float wd = wStart + a * (wEnd - wStart);
            const float mid = 0.5f * (s[0] + s[1]), side = 0.5f * wd * (s[0] - s[1]);
            dline[0][dpos] = (float) air[0].tick (mid + side);
            dline[1][dpos] = (float) air[1].tick (mid - side);
            tline[dpos & tmask] = diffuse[i];
            dpos = (dpos + 1) & dmask;
        }

        // glide the distance: delays at most 2 samples per block, gains over about 20 ms
        const double dd0 = directDelay, td0 = tailDelay, dg0 = directGain, tg0 = tailGain;
        directDelay += std::clamp (directDelayWanted - directDelay, -2.0, 2.0);
        tailDelay += std::clamp (tailDelayWanted - tailDelay, -2.0, 2.0);
        const double k = 1 - std::exp (-n / (0.02 * fs));
        directGain += k * (directGainWanted - directGain);
        tailGain += k * (tailGainWanted - tailGain);
        Glide g { dd0, directDelay, td0, tailDelay, dg0, directGain, tg0, tailGain };

        mix (current, g, n, outL, outR);
        if (fadeFrom >= 0)
        {
            float oL[maxBlock], oR[maxBlock];
            mix (fadeFrom, g, n, oL, oR);
            const int len = (int) (0.05 * fs);
            for (int i = 0; i < n; ++i)
            {
                const float a = std::min (1.0f, (float) (fadePos + i) / (float) len);
                outL[i] = a * outL[i] + (1 - a) * oL[i];
                outR[i] = a * outR[i] + (1 - a) * oR[i];
            }
            fadePos += n;
            if (fadePos >= len)
                fadeFrom = -1;
        }
        const float og = (float) outGain;
        for (int i = 0; i < n; ++i)
        {
            outL[i] *= og;
            outR[i] *= og;
        }
    }

private:
    struct Hall
    {
        int delay[2] = { 0, 0 };
        float gain[2] = { 1, 1 };
        double directBand = 0, tailBand = 0;
        std::unique_ptr<violinsynth::dsp::PartitionedConvolution> conv[2];
    };
    struct Glide
    {
        double direct0, direct1, tail0, tail1, gain0, gain1, tailGain0, tailGain1;
    };

    // finish.py split(): the direct sound is a 3.5 ms window around the first arrival in each
    // channel (its energy is the gain the mic signal gets, its position the delay); the rest is
    // the tail, which starts 1 ms before the earlier arrival. Also measures the direct and tail
    // energies as the violin excites them (the halls' tails are strongest where the body is, below
    // 600 Hz, so the tail plays 5-8 dB above the direct sound), which the Distance make-up uses.
    static void split (const std::array<std::vector<float>, 2>& ir,
                       const std::vector<float>& violin,
                       Hall& H,
                       std::array<std::vector<float>, 2>& tail)
    {
        violinsynth::dsp::PartitionedConvolution colour;
        colour.prepare (512, { violin }, 0, 1);
        int p[2];
        std::vector<float> T[2];
        H.directBand = H.tailBand = 0;
        for (int c = 0; c < 2; ++c)
        {
            const auto& x = ir[(size_t) c];
            const int n = (int) x.size();
            int pk = 0;
            for (int i = 1; i < n; ++i)
                if (std::abs (x[(size_t) i]) > std::abs (x[(size_t) pk]))
                    pk = i;
            const int a = std::max (pk - 48, 0), b = pk + 120;
            double e = 0;
            T[c] = x;
            for (int i = 0; i < n; ++i)
            {
                const double w = i < a ? 0.0 : i < b ? 1.0 : i < b + 48 ? 1.0 - (i - b) / 47.0 : 0.0;
                e += (x[(size_t) i] * w) * (x[(size_t) i] * w);
                T[c][(size_t) i] = (float) (x[(size_t) i] * (1.0 - w));
            }
            {
                // the tail through the violin: its energy
                colour.reset();
                const int len = n + (int) violin.size();
                std::vector<float> y ((size_t) len, 0.0f);
                std::copy (T[c].begin(), T[c].end(), y.begin());
                for (int i = 0; i < len; i += maxBlock)
                    colour.process (y.data() + i, std::min (maxBlock, len - i));
                for (float v : y)
                    H.tailBand += 0.5 * v * v;
            }
            // the direct sound is the mic signal at a flat gain, so the violin's energy at that gain
            double body = 0;
            for (float v : violin)
                body += v * v;
            H.directBand += 0.5 * e * body;
            p[c] = pk;
            H.gain[c] = (float) std::sqrt (e);
        }
        const int p0 = std::min (p[0], p[1]);
        const int cut = std::max (p0 - 48, 0);
        for (int c = 0; c < 2; ++c)
        {
            tail[(size_t) c].assign (T[c].begin() + std::min ((size_t) cut, T[c].size()), T[c].end());
            H.delay[c] = std::min (p[c] - p0 + std::min (p0, 48), 1023 - maxBlock);
        }
    }

    // Distance r against the 2 m the halls were balanced at: the direct sound falls as 1/r; the room
    // answers later when the mic is close (up to 24 ms at 0.5 m) and the direct sound arrives later
    // into the room when it is far (up to 15 ms); air takes 0.6 dB per metre above 4 kHz beyond 2 m.
    // Gains are made up so the mix's power stays where it was at 2 m.
    void updateDistance()
    {
        const double r = distance;
        const double g = refDistance / r;
        directDelayWanted = r > refDistance ? std::min ((r - refDistance) / 343.0, 0.015) * fs : 0.0;
        tailDelayWanted = std::min (std::max (0.0, (refDistance / r - 1) * 0.008) * fs, (double) tmask - maxBlock - 2);
        if (r > refDistance)
            for (auto& a : air)
                a.highShelf (fs, 4000.0, -0.6 * (r - refDistance));
        else
            for (auto& a : air)
                a.set (1, 0, 0, 1, 0, 0);
        const int h = hallWanted;
        if (h == 0)
        {
            directGainWanted = 1;
            tailGainWanted = 1;
            return;
        }
        const Hall& H = halls[(size_t) h - 1];
        const double D = H.directBand, T = H.tailBand * reverbGain * reverbGain;
        const double makeup = std::sqrt ((D + T) / std::max (1e-12, D * g * g + T));
        directGainWanted = g * makeup;
        tailGainWanted = makeup;
    }

    // a fractional read from a delay line, `back` samples before write position `pos`
    static float tap (const float* line, int mask, int pos, double back)
    {
        const int k = (int) back;
        const float fr = (float) (back - k);
        const float a = line[(pos - k) & mask], b = line[(pos - k - 1) & mask];
        return a + fr * (b - a);
    }

    // the selected hall's tail from the diffuse body plus the mic signals at its direct delay
    void mix (int hall, const Glide& g, int n, float* outL, float* outR)
    {
        float* out[2] = { outL, outR };
        const float level = levels[hall];
        for (int c = 0; c < 2; ++c)
        {
            if (hall == 0)
            {
                for (int i = 0; i < n; ++i)
                    out[c][i] = level * dline[c][(dpos - n + i) & dmask];
                continue;
            }
            Hall& H = halls[(size_t) hall - 1];
            for (int i = 0; i < n; ++i)
            {
                const double a = (double) (i + 1) / n;
                out[c][i] = tap (tline, tmask, dpos - n + i, g.tail0 + a * (g.tail1 - g.tail0));
            }
            H.conv[c]->process (out[c], n);
            for (int i = 0; i < n; ++i)
            {
                const double a = (double) (i + 1) / n;
                const double back = H.delay[c] + g.direct0 + a * (g.direct1 - g.direct0);
                const float direct = tap (dline[c], dmask, dpos - n + i, back);
                const float tg = (float) (g.tailGain0 + a * (g.tailGain1 - g.tailGain0));
                const float dg = (float) (g.gain0 + a * (g.gain1 - g.gain0));
                out[c][i] = level * (reverbGain * tg * out[c][i] + H.gain[c] * dg * direct);
            }
        }
    }

    static constexpr int dmask = 2047, tmask = 2047;
    Biquad hp1, hp2, shelf, bridge, air[2];
    double brightDb = 5.0, bridgeHz = bridgeRef, bodySize = 1.0, movement = 0.0, distance = refDistance;
    int mute = 0;
    std::vector<std::vector<float>> bodies;
    int numViolins = 1, numMics = 1, micWanted = 0;
    violinsynth::dsp::PartitionedConvolution body, dirs[6];
    float width = 1.0f, widthWanted = 1.0f;
    std::array<Hall, maxHalls> halls;
    int numHalls = 0;
    int hallWanted = 1, current = 1, fadeFrom = -1, fadePos = 0;
    float reverbGain = 1.0f;
    float levels[maxHalls] = {};
    double outGain = 1.0;
    double directDelay = 0, directDelayWanted = 0, tailDelay = 0, tailDelayWanted = 0;
    double directGain = 1, directGainWanted = 1, tailGain = 1, tailGainWanted = 1;
    int64_t clock = 0;
    float dline[2][dmask + 1] = {};
    float tline[tmask + 1] = {};
    int dpos = 0;
};
} // namespace o2
