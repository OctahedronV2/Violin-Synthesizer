// Octavio 2.3: drawn curves and who is in charge of each dimension (Auto / Guided / Manual).
//
// The player's expressive dimensions (dynamics, vibrato width and rate, contact point, bow
// pressure) each have a mode (spec §2):
//   Auto    the player decides (2.2 behaviour);
//   Guided  your curve or controller sets the level, the player's own micro-shaping (messa di
//           voce swells, vibrato bloom and taper, rate wander, the bow nearer the bridge in
//           quick passages) rides on top of it;
//   Manual  the value is exactly your curve or controller (or the knob, without either).
// An incoming controller on an Auto dimension makes it Guided until CC121 or a click on its badge.
//
// Drawn curves are breakpoints on the host's timeline in beats (PPQ), one lane per dimension
// except pressure. The message thread publishes them through CurveExchange (a triple buffer), so
// the audio thread reads them without locks or allocation. A lane's value is 0..1 on the scale
// of the controller that drives the same dimension (CC1, CC26, CC19, CC74 divided by 127).

#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <memory>
#include <sstream>
#include <string>

namespace o2
{
enum Dim : int
{
    dimDynamics,
    dimVibWidth,
    dimVibRate,
    dimContact,
    dimPressure,
    dimCount
};
inline constexpr int laneCount = 4; // the Curves tab's lanes are the first four dimensions
enum DimMode : int
{
    modeAuto,
    modeGuided,
    modeManual
};

// lane value (0..1, as CC/127) <-> the dimension's own unit (Player::controller's scales)
inline double laneToUnit (int dim, double x)
{
    switch (dim)
    {
        case dimDynamics:
            return x; // d, 0..1
        case dimVibWidth:
            return x * 127.0 * 0.5; // cents peak to peak (CC26: 0.5 per step)
        case dimVibRate:
            return 4.0 + 4.0 * x; // Hz (CC19)
        case dimContact:
            return 0.02 + 0.2 * (1.0 - x); // bow-bridge distance / string length (CC74: up = bridge)
        default:
            return x; // pressure: place in the playable window (CC22)
    }
}
inline double unitToLane (int dim, double u)
{
    switch (dim)
    {
        case dimDynamics:
            return std::clamp (u, 0.0, 1.0);
        case dimVibWidth:
            return std::clamp (u / 63.5, 0.0, 1.0);
        case dimVibRate:
            return std::clamp ((u - 4.0) / 4.0, 0.0, 1.0);
        case dimContact:
            return std::clamp (1.0 - (u - 0.02) / 0.2, 0.0, 1.0);
        default:
            return std::clamp (u, 0.0, 1.0);
    }
}
// the controller each lane exports as and listens to
inline constexpr int laneCc[laneCount] = { 1, 26, 19, 74 };

struct CurveLane
{
    static constexpr int maxPoints = 4096;
    int n = 0;
    double ppq[maxPoints]; // ascending
    float v[maxPoints]; // 0..1; < 0 a break (no curve between its neighbours and it)

    // The value at beat b, or -1 outside the first..last point or in a break. hint: a cursor the
    // caller keeps between calls (any value is safe; reading forward in time is O(1)).
    float at (double b, int& hint) const
    {
        if (n <= 0 || b < ppq[0] || b > ppq[n - 1])
            return -1.0f;
        int i = hint;
        if (i < 0 || i >= n || ppq[i] > b)
        {
            int lo = 0, hi = n - 1; // last i with ppq[i] <= b
            while (lo < hi)
            {
                const int mid = (lo + hi + 1) / 2;
                if (ppq[mid] <= b)
                    lo = mid;
                else
                    hi = mid - 1;
            }
            i = lo;
        }
        while (i + 1 < n && ppq[i + 1] <= b)
            ++i;
        hint = i;
        if (v[i] < 0.0f)
            return -1.0f;
        if (i + 1 >= n)
            return v[i];
        if (v[i + 1] < 0.0f)
            return -1.0f;
        const double span = ppq[i + 1] - ppq[i];
        const double u = span > 0.0 ? (b - ppq[i]) / span : 0.0;
        return (float) (v[i] + (v[i + 1] - v[i]) * u);
    }
};

struct CurveSet
{
    CurveLane lane[laneCount];
    bool any() const
    {
        for (const auto& l : lane)
            if (l.n > 0)
                return true;
        return false;
    }
};

// A triple buffer: the writer (message thread) fills write(), then publish(); the reader (audio
// thread) calls read() once per block and uses that set until its next call. Wait-free on both
// sides; allocation only in the constructor.
class CurveExchange
{
public:
    CurveExchange()
    {
        for (auto& b : buf)
            b = std::make_unique<CurveSet>();
    }
    CurveSet& write() { return *buf[(size_t) writeIdx]; }
    void publish() { writeIdx = middle.exchange (writeIdx | fresh, std::memory_order_acq_rel) & 3; }
    const CurveSet& read()
    {
        if (middle.load (std::memory_order_acquire) & fresh)
            readIdx = middle.exchange (readIdx, std::memory_order_acq_rel) & 3;
        return *buf[(size_t) readIdx];
    }

private:
    static constexpr int fresh = 4;
    std::unique_ptr<CurveSet> buf[3];
    int writeIdx = 0, readIdx = 1;
    std::atomic<int> middle { 2 };
};

// Text form (the renderer's curves= option and the tests): one point per line,
//   <lane> <beat> <value>     lane: dynamics | vibrato | rate | contact (or 0..3), value 0..1
//                             or "break"
//   bpm <n>                   the tempo the renderer maps beats with (default 120)
// '#' starts a comment. Points are sorted per lane. Returns false on a malformed line.
inline bool parseCurves (const std::string& text, CurveSet& out, double& bpm, std::string* error = nullptr)
{
    for (auto& l : out.lane)
        l.n = 0;
    std::istringstream in (text);
    std::string line;
    int lineNo = 0;
    while (std::getline (in, line))
    {
        ++lineNo;
        if (const auto hash = line.find ('#'); hash != std::string::npos)
            line.resize (hash);
        std::istringstream ls (line);
        std::string name;
        if (! (ls >> name))
            continue;
        if (name == "bpm")
        {
            ls >> bpm;
            continue;
        }
        int k = -1;
        if (name == "dynamics" || name == "0")
            k = dimDynamics;
        else if (name == "vibrato" || name == "width" || name == "1")
            k = dimVibWidth;
        else if (name == "rate" || name == "2")
            k = dimVibRate;
        else if (name == "contact" || name == "3")
            k = dimContact;
        double beat = 0.0;
        std::string val;
        if (k < 0 || ! (ls >> beat >> val))
        {
            if (error)
                *error = "line " + std::to_string (lineNo) + ": " + line;
            return false;
        }
        auto& L = out.lane[k];
        if (L.n >= CurveLane::maxPoints)
            continue;
        const float v = val == "break" ? -1.0f : (float) std::clamp (std::atof (val.c_str()), 0.0, 1.0);
        int i = L.n++;
        while (i > 0 && L.ppq[i - 1] > beat)
        {
            L.ppq[i] = L.ppq[i - 1];
            L.v[i] = L.v[i - 1];
            --i;
        }
        L.ppq[i] = beat;
        L.v[i] = v;
    }
    return true;
}
} // namespace o2
