#pragma once

#include <cmath>

namespace violinsynth::dsp
{
// Bow-string friction junction. Mirrors research/violin_model/friction.py;
// the derivation is documented there.
//
// Hyperbolic friction curve:
//   mu(dv) = sign(dv) * (muD + (muS - muD) * v0 / (v0 + |dv|))
// With v_h the string velocity the bow would see without friction and
// dh = vBow - vH, the bow sticks if |2 Z dh| <= muS * F. Otherwise the slip
// speed is the largest positive root of
//   a d^2 + (a v0 + F muD - a |dh|) d + (F muS - a |dh|) v0 = 0,  a = 2 Z.
// Where sticking and slipping are both possible the previous state is kept.
struct FrictionParams
{
    double impedance = 0.197; // Z, kg/s
    double muS = 0.8;
    double muD = 0.3;
    double v0 = 0.1; // m/s
};

struct JunctionResult
{
    double velocity; // string velocity at the bow point
    bool sticking;
};

inline double frictionCoefficient (double dv, const FrictionParams& p)
{
    if (dv == 0.0)
        return 0.0;

    const auto mag = p.muD + (p.muS - p.muD) * p.v0 / (p.v0 + std::abs (dv));
    return dv > 0.0 ? mag : -mag;
}

// Largest positive slip speed, or -1 if there is none.
inline double largestSlipRoot (double absDh, double force, const FrictionParams& p)
{
    const auto a = 2.0 * p.impedance;
    const auto b = a * p.v0 + force * p.muD - a * absDh;
    const auto c = (force * p.muS - a * absDh) * p.v0;
    const auto disc = b * b - 4.0 * a * c;

    if (disc < 0.0)
        return -1.0;

    const auto root = (-b + std::sqrt (disc)) / (2.0 * a);
    return root > 0.0 ? root : -1.0;
}

inline JunctionResult solveJunction (double vBow, double vH, double force, const FrictionParams& p, bool wasSticking)
{
    const auto dh = vBow - vH;
    const auto absDh = std::abs (dh);

    if (force <= 0.0)
        return { vH, false };

    const bool canStick = 2.0 * p.impedance * absDh <= p.muS * force;
    double slip = 0.0;

    if (canStick)
    {
        if (wasSticking)
            return { vBow, true };

        slip = largestSlipRoot (absDh, force, p);

        if (slip < 0.0)
            return { vBow, true };
    }
    else
    {
        // There is always a positive root when sticking is impossible (c < 0).
        slip = largestSlipRoot (absDh, force, p);
    }

    const auto d = dh > 0.0 ? slip : -slip;
    return { vBow - d, false };
}
} // namespace violinsynth::dsp
