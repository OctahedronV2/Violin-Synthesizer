// String-physics lab driver.
//   lab note   G|D|A|E midi force(N) speed(m/s) beta dur(s) out.wav [k=v ...]
//   lab pizz   G|D|A|E out.wav [k=v ...]
//   lab score  score.txt out.wav [k=v ...]
//   lab schelleng G|D|A|E midi speed [k=v ...]     -> regime map (force x beta)
//   lab guettler  G|D|A|E midi beta [k=v ...]      -> attack map (force x acceleration)
//   lab flatten   G|D|A|E midi speed beta [k=v ...]  -> pitch and brightness vs force
//   lab bench  [k=v ...]                            -> CPU cost per string-second
// Options (k=v): friction=hyperbolic|thermal muS muD v0 tauFast tauSlow kFast kSlow
//   bowPoints bowWidth hairStiffness hairDamping grain grainHz dispersion maxAllpass
//   perString torsion torsionSpeed torsionImpedance torsionQ fingerLoss
//   admittance admScale modes=<file: f Q mass per line> seed
//   strings=<file: name f0 Z B t60lo t60hi fhi per line>
// Output wavs are the force on the bridge at 48 kHz, float, unnormalised.

#include "lab.h"
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>

using namespace lab;

static std::map<std::string, std::string> opts;
static double opt (const char* k, double def)
{
    auto it = opts.find (k);
    return it == opts.end() ? def : std::atof (it->second.c_str());
}

static int stringIndex (const char* s)
{
    switch (s[0])
    {
        case 'G': return 0;
        case 'D': return 1;
        case 'A': return 2;
        default: return 3;
    }
}

static void loadModes (Violin& v, const std::string& path, double scale)
{
    std::ifstream in (path);
    double f, Q, m;
    v.bridge.modes.clear();
    v.bridge.Yd = 0;
    while (in >> f >> Q >> m)
        if (f < 0.45 * v.fs)
            v.bridge.add (v.fs, f, Q, m / scale);
}

static void setup (Violin& v)
{
    Params& p = v.p;
    if (opts.count ("friction"))
        p.friction = opts["friction"] == "thermal" ? Friction::thermal : opts["friction"] == "thermalHyp" ? Friction::thermalHyp : Friction::hyperbolic;
#define O(name) p.name = opt (#name, p.name)
    O (muS);
    O (muD);
    O (v0);
    O (tauFast);
    O (tauSlow);
    O (kFast);
    O (kSlow);
    O (aT);
    O (bT);
    O (cT);
    O (tauG);
    O (ya);
    O (xi);
    p.bowPoints = (int) opt ("bowPoints", p.bowPoints);
    O (bowWidth);
    O (hairStiffness);
    O (hairDamping);
    O (grain);
    O (grainHz);
    p.grainMode = (int) opt ("grainMode", 0);
    O (grainLen);
    p.dispersion = opt ("dispersion", 0) != 0;
    p.maxAllpass = (int) opt ("maxAllpass", p.maxAllpass);
    p.perString = opt ("perString", 1) != 0;
    p.torsion = opt ("torsion", 0) != 0;
    O (torsionSpeed);
    O (torsionImpedance);
    O (torsionQ);
    O (fingerLoss);
    p.autoTune = opt ("autoTune", 0) != 0;
    p.admittance = opt ("admittance", 0) != 0;
    O (admScale);
#undef O
    v.init();
    if (opts.count ("strings"))
    {
        std::ifstream in (opts["strings"]);
        std::string name;
        int i = 0;
        StringData d;
        while (i < 4 && in >> name >> d.f0 >> d.Z >> d.B >> d.t60lo >> d.t60hi >> d.fhi)
        {
            static std::string names[4];
            names[i] = name;
            d.name = names[i].c_str();
            v.setString (i++, d);
        }
    }
    if (p.admittance)
        loadModes (v, opts.count ("modes") ? opts["modes"] : "modes.txt", p.admScale);
    const unsigned seed = (unsigned) opt ("seed", 1);
    for (int i = 0; i < 4; ++i)
        v.s[i].rng.s ^= 0x51ED2701ull * (seed + 7 * i);
}

// ---------------------------------------------------------------- rendering
struct Renderer
{
    Violin v;
    Decim dec;
    std::vector<float> out;
    std::vector<double> slipTimes[4];
    double t = 0;
    void sample (const double* vb, const double* fb)
    {
        for (int s = 0; s < 2; ++s)
        {
            dec.push (v.tick (vb, fb));
            for (int i = 0; i < 4; ++i)
                if (v.s[i].slipped)
                    slipTimes[i].push_back (t);
            t += 1.0 / v.fs;
        }
        out.push_back ((float) dec.out());
        if (t > ringFrom)
            for (int i = 0; i < 4; ++i)
            {
                const double Z = v.p.perString ? v.s[i].d.Z : 0.2;
                ringEnergy[i] += (Z * v.s[i].aBr) * (Z * v.s[i].aBr);
            }
    }
    double ringFrom = 1e9, ringEnergy[4] = {};
};

// Regime from slip times in [t0, t1]: 1 = Helmholtz, 2+ = multiple slip, 0 = none, -1 = irregular
struct Regime
{
    int kind;
    double slipsPerPeriod, cv, freq;
};
static Regime classify (const std::vector<double>& st, double t0, double t1, double period)
{
    std::vector<double> iv;
    double prev = -1;
    for (double x : st)
        if (x >= t0 && x <= t1)
        {
            if (prev >= 0)
                iv.push_back (x - prev);
            prev = x;
        }
    if (iv.size() < 5)
        return { 0, 0, 0, 0 };
    double m = 0;
    for (double x : iv)
        m += x;
    m /= iv.size();
    double var = 0;
    for (double x : iv)
        var += (x - m) * (x - m);
    const double cv = std::sqrt (var / iv.size()) / m;
    const double spp = period / m;
    // fraction of intervals that are one clean period, and the typical interval
    std::vector<double> sorted = iv;
    std::sort (sorted.begin(), sorted.end());
    const double med = sorted[sorted.size() / 2];
    int clean = 0;
    double sumClean = 0;
    int nClean = 0;
    for (double x : iv)
        if (std::abs (x / med - 1.0) < 0.05)
        {
            sumClean += x;
            ++nClean;
            clean += std::abs (x / period - 1.0) < 0.07;
        }
    const double fracClean = clean / (double) iv.size();
    Regime r { -1, spp, cv, sumClean > 0 ? 1.0 / (sumClean / std::max (1, nClean)) : 1.0 / med };
    if (std::abs (med / period - 1.0) < 0.07 && fracClean > 0.9)
        r.kind = 1;
    else if (spp > 1.5 && std::abs (spp - std::round (spp)) < 0.07)
    {
        // periodic multiple slipping: every n slips add up to one period
        const int nn = (int) std::round (spp);
        int good = 0, tot = 0;
        for (size_t i = 0; i + nn <= iv.size(); i += nn)
        {
            double sum = 0;
            for (int k = 0; k < nn; ++k)
                sum += iv[i + k];
            good += std::abs (sum / period - 1.0) < 0.05;
            ++tot;
        }
        if (tot > 0 && good > 0.8 * tot)
            r.kind = nn;
    }
    r.cv = 1.0 - fracClean; // reported: share of intervals that are not a clean period
    return r;
}

static Renderer runNote (int si, double midi, double force, double speed, double beta, double dur, double attackAccel = 0, double ring = 1.5)
{
    Renderer R;
    setup (R.v);
    R.ringFrom = opt ("ringFrom", 1e9);
    R.v.s[si].setNote (midi);
    R.v.s[si].setBeta (beta);
    const int n = (int) ((dur + ring) * 48000);
    double vb[4] = {}, fb[4] = {};
    for (int i = 0; i < n; ++i)
    {
        const double tt = i / 48000.0;
        const bool on = tt < dur;
        double sp = speed;
        if (attackAccel > 0)
            sp = std::min (speed, attackAccel * tt);
        else
            sp = speed * std::min (1.0, tt / 0.03); // 30 ms bow start
        vb[si] = on ? sp : 0.0;
        const double fend = opt ("fend", 0);
        fb[si] = on ? (fend > 0 ? force * std::pow (fend / force, tt / dur) : force) : 0.0;
        R.sample (vb, fb);
    }
    return R;
}

// Pre-Helmholtz transient length (s) from bow start, or -1 if Helmholtz never settles.
static double transient (const std::vector<double>& st, double period, double tEnd)
{
    // find the first slip from which every interval up to tEnd is within 10% of the period
    const int n = (int) st.size();
    int last = -1;
    for (int i = n - 1; i > 0; --i)
    {
        if (st[i] > tEnd)
            continue;
        const double iv = st[i] - st[i - 1];
        if (std::abs (iv / period - 1.0) > 0.1)
        {
            last = i;
            break;
        }
    }
    // need Helmholtz to be established and to last at least 20 periods before tEnd
    const int start = last < 0 ? 0 : last;
    if (start >= n || st[std::min (n - 1, start)] > tEnd - 20 * period)
        return -1;
    return st[start];
}

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf (stderr, "see lab.cpp header\n");
        return 1;
    }
    const std::string mode = argv[1];
    std::vector<std::string> pos;
    for (int i = 2; i < argc; ++i)
    {
        const char* eq = std::strchr (argv[i], '=');
        if (eq)
            opts[std::string (argv[i], eq - argv[i])] = eq + 1;
        else
            pos.push_back (argv[i]);
    }

    if (mode == "note")
    {
        const int si = stringIndex (pos[0].c_str());
        auto R = runNote (si, std::atof (pos[1].c_str()), std::atof (pos[2].c_str()), std::atof (pos[3].c_str()),
                          std::atof (pos[4].c_str()), std::atof (pos[5].c_str()), opt ("accel", 0), opt ("ring", 1.5));
        writeWav (pos[6], R.out, 48000);
        if (std::getenv ("LAB_TRACE"))
        {
            writeWav ("/tmp/trace.wav", R.v.s[si].trace, 96000);
            writeWav ("/tmp/traceT.wav", R.v.s[si].traceT, 96000);
        }
        const double dur = std::atof (pos[5].c_str());
        const auto r = classify (R.slipTimes[si], dur * 0.5, dur, R.v.s[si].period / R.v.fs);
        if (opt ("dumpSlips", 0) != 0)
        {
            FILE* f = std::fopen ("/tmp/slips.txt", "w");
            for (double x : R.slipTimes[si])
                std::fprintf (f, "%.7f\n", x);
            std::fclose (f);
        }
        if (opt ("ringFrom", 0) > 0)
            std::printf ("ring energy G %.3g D %.3g A %.3g E %.3g\n", R.ringEnergy[0], R.ringEnergy[1], R.ringEnergy[2], R.ringEnergy[3]);
        std::printf ("regime %d slips/period %.3f cv %.4f f %.2f (string f1 %.2f)\n", r.kind, r.slipsPerPeriod, r.cv, r.freq, R.v.s[si].f1);
        return 0;
    }
    if (mode == "pizz")
    {
        Renderer R;
        setup (R.v);
        const int si = stringIndex (pos[0].c_str());
        if (pos.size() > 2)
            R.v.s[si].setNote (std::atof (pos[2].c_str()));
        R.v.s[si].pluck (0.3, 0.1);
        double vb[4] = {}, fb[4] = {};
        for (int i = 0; i < 48000 * 4; ++i)
            R.sample (vb, fb);
        writeWav (pos[1], R.out, 48000);
        return 0;
    }
    if (mode == "score")
    {
        // lines: start dur string midi force speed beta [dir]   (times in s; dir +1/-1)
        // A note's bow ramps in over 'att' seconds and out over 'rel' seconds.
        struct Ev
        {
            double t, d;
            int s;
            double midi, f, v, b, dir;
        };
        std::vector<Ev> ev;
        std::ifstream in (pos[0]);
        std::string line;
        double tEnd = 0;
        while (std::getline (in, line))
        {
            if (line.empty() || line[0] == '#')
                continue;
            std::istringstream ls (line);
            Ev e;
            std::string sname;
            e.dir = 1;
            ls >> e.t >> e.d >> sname >> e.midi >> e.f >> e.v >> e.b >> e.dir;
            e.s = stringIndex (sname.c_str());
            ev.push_back (e);
            tEnd = std::max (tEnd, e.t + e.d);
        }
        Renderer R;
        setup (R.v);
        const double att = opt ("att", 0.04), rel = opt ("rel", 0.03);
        const int n = (int) ((tEnd + 2.0) * 48000);
        int cur[4] = { -1, -1, -1, -1 };
        for (int i = 0; i < n; ++i)
        {
            const double tt = i / 48000.0;
            double vb[4] = {}, fb[4] = {};
            for (size_t k = 0; k < ev.size(); ++k)
            {
                const Ev& e = ev[k];
                if (tt < e.t || tt >= e.t + e.d)
                    continue;
                if (cur[e.s] != (int) k)
                {
                    cur[e.s] = (int) k;
                    R.v.s[e.s].setNote (e.midi);
                    R.v.s[e.s].setBeta (e.b);
                }
                const double u = tt - e.t, left = e.t + e.d - tt;
                const double env = std::min ({ 1.0, u / att, left / rel });
                vb[e.s] = e.dir * e.v * env;
                fb[e.s] = e.f * std::min (1.0, 0.3 + 0.7 * env);
            }
            // a string not bowed and not in its last note's ring keeps its finger (stopped)
            R.sample (vb, fb);
        }
        writeWav (pos[1], R.out, 48000);
        return 0;
    }
    if (mode == "schelleng")
    {
        const int si = stringIndex (pos[0].c_str());
        const double midi = std::atof (pos[1].c_str()), speed = std::atof (pos[2].c_str());
        const double betas[] = { 0.03, 0.045, 0.06, 0.08, 0.1, 0.13, 0.17, 0.22 };
        const int nf = (int) opt ("nf", 12);
        const double fmin = opt ("fmin", 0.02), fmax = opt ("fmax", 4.0);
        const double dur = opt ("dur", 0.6);
        int helm = 0, total = 0;
        std::printf ("beta \\ force:");
        for (int j = 0; j < nf; ++j)
            std::printf (" %5.3f", fmin * std::pow (fmax / fmin, j / (nf - 1.0)));
        std::printf ("\n");
        for (double b : betas)
        {
            std::printf ("%6.3f      :", b);
            for (int j = 0; j < nf; ++j)
            {
                const double F = fmin * std::pow (fmax / fmin, j / (nf - 1.0));
                auto R = runNote (si, midi, F, speed, b, dur, 0, 0.0);
                const auto r = classify (R.slipTimes[si], dur * 0.5, dur, R.v.s[si].period / R.v.fs);
                const char* c = r.kind == 1 ? "H" : r.kind == 0 ? "." : r.kind == -1 ? "x" : r.kind == 2 ? "2" : r.kind == 3 ? "3" : "m";
                std::printf ("     %s", c);
                helm += r.kind == 1;
                ++total;
            }
            std::printf ("\n");
        }
        std::printf ("helmholtz %d / %d\n", helm, total);
        return 0;
    }
    if (mode == "limits")
    {
        // Steady-state force limits, measured like SGA08's second method: establish Helmholtz
        // motion at a comfortable force, then sweep the force slowly down (or up) and note
        // where the motion breaks down.
        const int si = stringIndex (pos[0].c_str());
        const double midi = std::atof (pos[1].c_str()), speed = std::atof (pos[2].c_str());
        const double betas[] = { 0.04, 0.06, 0.08, 0.1, 0.13, 0.17 };
        double sumLo = 0, sumUp = 0;
        int nLo = 0, nUp = 0;
        for (double b : betas)
        {
            double lim[2] = { -1, -1 };
            for (int dirn = 0; dirn < 2; ++dirn)
            {
                // start force: geometric mean of Schelleng's ideal limits for this string
                Renderer R;
                setup (R.v);
                R.v.s[si].setNote (midi);
                R.v.s[si].setBeta (b);
                const double Z = R.v.s[si].d.Z;
                const double F0 = opt ("start", 0.35) * 2 * Z * speed / (b * 0.5); // ~35% of Fmax
                const double tHold = 0.4, tSweep = 3.0;
                const double Fend = dirn == 0 ? F0 / 100 : F0 * 30;
                const int n = (int) ((tHold + tSweep) * 48000);
                double vb[4] = {}, fb[4] = {};
                std::vector<double> forceAt;
                for (int i = 0; i < n; ++i)
                {
                    const double tt = i / 48000.0;
                    vb[si] = speed * std::min (1.0, tt / 0.03);
                    fb[si] = tt < tHold ? F0 : F0 * std::pow (Fend / F0, (tt - tHold) / tSweep);
                    R.sample (vb, fb);
                    forceAt.push_back (fb[si]);
                }
                const double P = R.v.s[si].period / R.v.fs;
                const auto& st = R.slipTimes[si];
                // Helmholtz must hold over the last 0.15 s of the hold
                const auto r0 = classify (st, tHold - 0.15, tHold, P);
                if (r0.kind != 1)
                    continue;
                // breakdown: two intervals out of the 10% band within 5 periods after tHold
                double tBreak = -1, lastBad = -1;
                for (size_t i = 1; i < st.size(); ++i)
                {
                    if (st[i] < tHold)
                        continue;
                    const double iv = st[i] - st[i - 1];
                    if (std::abs (iv / P - 1.0) > 0.1)
                    {
                        if (lastBad > 0 && st[i] - lastBad < 5 * P)
                        {
                            tBreak = lastBad;
                            break;
                        }
                        lastBad = st[i];
                    }
                }
                // no slips at all for a while also counts (stuck or silent)
                if (tBreak < 0 && ! st.empty() && st.back() < tHold + tSweep - 10 * P)
                    tBreak = st.back();
                if (tBreak > 0)
                    lim[dirn] = forceAt[std::min (forceAt.size() - 1, (size_t) (tBreak * 48000))];
            }
            std::printf ("beta %.3f  Fmin %7.4f N  Fmax %7.3f N\n", b, lim[0], lim[1]);
            if (lim[0] > 0)
            {
                sumLo += std::log (lim[0] * b * b);
                ++nLo;
            }
            if (lim[1] > 0)
            {
                sumUp += std::log (lim[1] * b);
                ++nUp;
            }
        }
        std::printf ("c_lower %.2f g/s  c_upper %.3f kg/s  (beta points %d / %d)\n", nLo ? std::exp (sumLo / nLo) / speed * 1000 : -1.0,
                     nUp ? std::exp (sumUp / nUp) / speed : -1.0, nLo, nUp);
        return 0;
    }
    if (mode == "guettler")
    {
        const int si = stringIndex (pos[0].c_str());
        const double midi = std::atof (pos[1].c_str()), beta = std::atof (pos[2].c_str());
        const int nf = (int) opt ("nf", 10), na = (int) opt ("na", 10);
        const double fmin = opt ("fmin", 0.05), fmax = opt ("fmax", 2.0);
        const double amin = opt ("amin", 0.5), amax = opt ("amax", 20.0);
        const double dur = opt ("dur", 0.35);
        int perfect = 0, ok = 0, total = 0;
        std::printf ("accel \\ force:");
        for (int j = 0; j < nf; ++j)
            std::printf (" %5.3f", fmin * std::pow (fmax / fmin, j / (nf - 1.0)));
        std::printf ("\n");
        for (int ia = na - 1; ia >= 0; --ia)
        {
            const double a = amin * std::pow (amax / amin, ia / (na - 1.0));
            std::printf ("%7.2f      :", a);
            for (int j = 0; j < nf; ++j)
            {
                const double F = fmin * std::pow (fmax / fmin, j / (nf - 1.0));
                auto R = runNote (si, midi, F, opt ("vmax", 0.3), beta, dur, a, 0.0); // constant acceleration up to vmax
                const double P = R.v.s[si].period / R.v.fs;
                const auto& st = R.slipTimes[si];
                const double tr = transient (st, P, dur);
                // perfect: Helmholtz from the first or second slip
                const bool perf = tr >= 0 && ! st.empty() && tr <= st[0] + 1.5 * P;
                const double ms = tr < 0 ? -1 : (tr - (st.empty() ? 0 : st[0])) * 1000;
                if (perf)
                    std::printf ("     P");
                else if (tr >= 0 && ms < 50)
                    std::printf ("  %4.0f", ms);
                else
                    std::printf ("     -");
                perfect += perf;
                ok += tr >= 0 && ms < 50;
                ++total;
            }
            std::printf ("\n");
        }
        std::printf ("perfect %d  under-50ms %d  of %d\n", perfect, ok, total);
        return 0;
    }
    if (mode == "flatten")
    {
        const int si = stringIndex (pos[0].c_str());
        const double midi = std::atof (pos[1].c_str()), speed = std::atof (pos[2].c_str()), beta = std::atof (pos[3].c_str());
        const double dur = 1.0;
        for (int j = 0; j < 14; ++j)
        {
            const double F = 0.05 * std::pow (60.0, j / 13.0);
            auto R = runNote (si, midi, F, speed, beta, dur, 0, 0.0);
            const auto r = classify (R.slipTimes[si], 0.5, dur, R.v.s[si].period / R.v.fs);
            // brightness: spectral centroid of the bridge force over the last 0.4 s (by DFT on harmonics)
            const double f1 = r.freq > 0 ? r.freq : R.v.s[si].f1;
            double num = 0, den = 0;
            const int i0 = (int) (0.6 * 48000), i1 = (int) (1.0 * 48000);
            for (int k = 1; k <= 40 && k * f1 < 20000; ++k)
            {
                double re = 0, im = 0;
                for (int i = i0; i < i1; ++i)
                {
                    const double w = 0.5 - 0.5 * std::cos (2 * pi * (i - i0) / (i1 - i0));
                    re += w * R.out[i] * std::cos (2 * pi * k * f1 * i / 48000.0);
                    im += w * R.out[i] * std::sin (2 * pi * k * f1 * i / 48000.0);
                }
                const double a = std::sqrt (re * re + im * im);
                num += k * f1 * a;
                den += a;
            }
            std::printf ("force %6.3f regime %2d cents %+7.2f centroid %6.0f Hz\n", F, r.kind,
                         r.freq > 0 ? 1200 * std::log2 (r.freq / R.v.s[si].f1) : 0.0, den > 0 ? num / den : 0.0);
        }
        return 0;
    }
    if (mode == "bench")
    {
        Violin v;
        setup (v);
        double vb[4] = { 0.2, 0, 0, 0 }, fb[4] = { 0.5, 0, 0, 0 };
        const int n = (int) v.fs * 10;
        auto t0 = std::chrono::steady_clock::now();
        double acc = 0;
        for (int i = 0; i < n; ++i)
            acc += v.tick (vb, fb);
        auto t1 = std::chrono::steady_clock::now();
        const double sec = std::chrono::duration<double> (t1 - t0).count();
        std::printf ("%.3f ms per second of audio (4 strings, 1 bowed, %zu bridge modes) [%g]\n", sec / 10 * 1000, v.bridge.modes.size(), acc * 0);
        return 0;
    }
    std::fprintf (stderr, "unknown mode\n");
    return 1;
}
