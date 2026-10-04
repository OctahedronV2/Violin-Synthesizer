// M5 research spike: a second (vertical) string polarisation coupled at the bridge.
// Not in the core. The horizontal plane is the normal Violin (four strings on the modal bridge).
// The vertical plane is a second set of four strings on their own bridge admittance (admV x the
// horizontal one: the bridge top is stiffer up-down), terminated slightly differently (detune
// cents) and coupled to the horizontal plane through the bridge (cross term kappa: each plane's
// bridge velocity picks up kappa x the other's, one sample late). The pluck leaves the string at
// an angle theta from the bridge plane; the bow drives only the horizontal plane. The output is
// the horizontal bridge force plus wv x the vertical one.
//   pol mode(pizz|bow) string dual theta kappa admV detune wv out.wav
#include "../../octavio2/core/Strings.h"
#include <chrono>
using namespace o2;

static double tickPlane (Violin& V, double vExtra, const double* vBow, const double* force, double& vOut)
{
    double sumZA = 0, sumZ = 0;
    for (int i = 0; i < 4; ++i)
    {
        V.s[i].readBridge();
        sumZA += V.s[i].d.Z * V.s[i].aBr;
        sumZ += V.s[i].d.Z;
    }
    const double Yd = V.bridge.Yd;
    const double v = (2 * Yd * sumZA + V.bridge.past()) / (1 + Yd * sumZ) + vExtra;
    double F = 0, hiss = 0;
    for (int i = 0; i < 4; ++i)
    {
        const double Fi = V.s[i].d.Z * (2 * V.s[i].aBr - v);
        F += Fi;
        V.s[i].tick (v - V.s[i].aBr, vBow[i], force[i]);
        hiss += V.s[i].hiss;
    }
    V.bridge.update (F);
    vOut = v;
    return F + hiss;
}

int main (int argc, char** argv)
{
    if (argc < 10)
    {
        fprintf (stderr, "pol mode string dual theta kappa admV detune wv out.wav\n");
        return 1;
    }
    const bool bow = std::string (argv[1]) == "bow";
    const int si = atoi (argv[2]);
    const bool dual = atoi (argv[3]) != 0;
    const double theta = atof (argv[4]) * pi / 180, kappa = atof (argv[5]), admV = atof (argv[6]),
                 detune = atof (argv[7]), wv = atof (argv[8]);
    Violin H, V;
    H.init();
    V.p.admScale = H.p.admScale * admV;
    V.p.tuneCents = H.p.tuneCents + detune;
    V.init();
    const double fs = H.fs;
    std::vector<float> out;
    double vb[4] = {}, fb[4] = {}, zero[4] = {};
    double vh = 0, vv = 0;
    H.s[si].setBeta (bow ? 0.1 : 0.22);
    V.s[si].setBeta (0.22);
    if (! bow)
    {
        H.s[si].pluck (2e-3 * std::cos (theta), 1e-4, 0, 0, 0.015);
        if (dual)
            V.s[si].pluck (2e-3 * std::sin (theta), 1e-4, 0, 0, 0.015);
    }
    const auto t0 = std::chrono::steady_clock::now();
    const int n = (int) (fs * 4.0);
    for (int i = 0; i < n; ++i)
    {
        if (bow)
        {
            const double t = i / fs;
            vb[si] = t < 1.5 ? std::min (0.2, t * 4.0) : 0.0;
            fb[si] = t < 1.5 ? 0.5 : 0.0;
        }
        double vhNew, vvNew;
        double F = tickPlane (H, dual ? kappa * vv : 0.0, vb, fb, vhNew);
        if (dual)
            F += wv * tickPlane (V, kappa * vh, zero, zero, vvNew);
        else
            vvNew = 0;
        vh = vhNew;
        vv = vvNew;
        if (i % 2 == 0)
            out.push_back ((float) F);
    }
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    fprintf (stderr, "%s %.3f s cpu for 4 s (%s)\n", argv[9], secs, dual ? "dual" : "single");
    float pk = 0;
    for (float x : out)
        pk = std::max (pk, std::abs (x));
    for (float& x : out)
        x *= 0.5f / pk;
    writeWav (argv[9], out, 48000);
}
