// Octavio 2 radiation: bridge force -> stereo sound, the C++ port of render/finish.py.
//
//   force -> 60 Hz high-pass -> brightness shelf (1.5 kHz) -> full-band all-direction body ->
//            hall tail (convolution)                                    \
//         -> left / right directional bodies (the two mics) -> direct sound at the hall's own
//            delay and gain, as finish.py's split() measures it          -> left / right
//
// Bodies and halls come in as sample data (octavio2/data), so the plugin can load them from
// binary data and the renderer from files. Runs at 48 kHz. prepare() allocates; process()
// doesn't. Hall changes crossfade over 50 ms.

#pragma once
#include "dsp/PartitionedConvolution.h"

#include <algorithm>
#include <array>
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
    std::vector<float> body, bodyLeft, bodyRight; // all-direction body, left and right mic bodies
    // stereo hall impulse responses (left, right), in the Hall choice order after "none"
    std::vector<std::array<std::vector<float>, 2>> halls;
};

class Radiation
{
public:
    static constexpr double fs = 48000.0;
    static constexpr int maxBlock = 64;
    static constexpr int maxHalls = 16;

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
        body.prepare (128, { data.body }, 0, 1);
        left.prepare (128, { data.bodyLeft }, 0, 1);
        right.prepare (128, { data.bodyRight }, 0, 1);
        numHalls = std::min ((int) data.halls.size(), maxHalls - 1);
        for (int h = 0; h < maxHalls; ++h)
            levels[h] = (float) std::pow (10.0, hallGainDb[h] / 20.0);
        for (int h = 0; h < numHalls; ++h)
        {
            Hall& H = halls[(size_t) h];
            std::array<std::vector<float>, 2> tail;
            split (data.halls[(size_t) h], H, tail);
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
        body.reset();
        left.reset();
        right.reset();
        for (int h = 0; h < numHalls; ++h)
            for (auto& c : halls[(size_t) h].conv)
                c->reset();
        std::fill (std::begin (dline[0]), std::end (dline[0]), 0.0f);
        std::fill (std::begin (dline[1]), std::end (dline[1]), 0.0f);
        fadeFrom = -1;
        current = std::clamp (hallWanted, 0, numHalls);
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

    // 0: no hall (the two mics only), 1..: data.halls[hall - 1]
    void setHall (int hall) { hallWanted = std::clamp (hall, 0, numHalls); }
    void setReverbGain (double g) { reverbGain = (float) g; }
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
        float x[maxBlock], diffuse[maxBlock], dl[maxBlock], dr[maxBlock];
        for (int i = 0; i < n; ++i)
            x[i] = (float) shelf.tick (hp2.tick (hp1.tick (force[i])));
        std::copy (x, x + n, diffuse);
        std::copy (x, x + n, dl);
        std::copy (x, x + n, dr);
        body.process (diffuse, n);
        left.process (dl, n);
        right.process (dr, n);
        for (int i = 0; i < n; ++i)
        {
            dline[0][dpos] = dl[i];
            dline[1][dpos] = dr[i];
            dpos = (dpos + 1) & dmask;
        }
        mix (current, diffuse, n, outL, outR);
        if (fadeFrom >= 0)
        {
            float oL[maxBlock], oR[maxBlock];
            mix (fadeFrom, diffuse, n, oL, oR);
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
        const float g = (float) outGain;
        for (int i = 0; i < n; ++i)
        {
            outL[i] *= g;
            outR[i] *= g;
        }
    }

private:
    struct Hall
    {
        int delay[2] = { 0, 0 };
        float gain[2] = { 1, 1 };
        std::unique_ptr<violinsynth::dsp::PartitionedConvolution> conv[2];
    };

    // finish.py split(): the direct sound is a 3.5 ms window around the first arrival in each
    // channel (its energy is the gain the mic signal gets, its position the delay); the rest is
    // the tail, which starts 1 ms before the earlier arrival
    static void split (const std::array<std::vector<float>, 2>& ir, Hall& H, std::array<std::vector<float>, 2>& tail)
    {
        int p[2];
        std::vector<float> T[2];
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
            for (int i = a; i < n && i < b + 48; ++i)
            {
                const double w = i < b ? 1.0 : 1.0 - (i - b) / 47.0;
                e += (x[(size_t) i] * w) * (x[(size_t) i] * w);
                T[c][(size_t) i] = (float) (x[(size_t) i] * (1.0 - w));
            }
            p[c] = pk;
            H.gain[c] = (float) std::sqrt (e);
        }
        const int p0 = std::min (p[0], p[1]);
        const int cut = std::max (p0 - 48, 0);
        for (int c = 0; c < 2; ++c)
        {
            tail[(size_t) c].assign (T[c].begin() + std::min ((size_t) cut, T[c].size()), T[c].end());
            H.delay[c] = std::min (p[c] - p0 + std::min (p0, 48), dmask - maxBlock);
        }
    }

    // the selected hall's tail from the diffuse body plus the mic signals at its direct delay
    void mix (int hall, const float* diffuse, int n, float* outL, float* outR)
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
            std::copy (diffuse, diffuse + n, out[c]);
            H.conv[c]->process (out[c], n);
            for (int i = 0; i < n; ++i)
                out[c][i]
                    = level * (reverbGain * out[c][i] + H.gain[c] * dline[c][(dpos - n + i - H.delay[c]) & dmask]);
        }
    }

    static constexpr int dmask = 1023;
    Biquad hp1, hp2, shelf;
    double brightDb = 5.0;
    violinsynth::dsp::PartitionedConvolution body, left, right;
    std::array<Hall, maxHalls> halls;
    int numHalls = 0;
    int hallWanted = 1, current = 1, fadeFrom = -1, fadePos = 0;
    float reverbGain = 1.0f;
    float levels[maxHalls] = {};
    double outGain = 1.0;
    float dline[2][dmask + 1] = {};
    int dpos = 0;
};
} // namespace o2
