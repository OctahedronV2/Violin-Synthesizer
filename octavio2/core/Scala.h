// Octavio 2, M7: Scala tunings (.scl scale, optional .kbm keyboard map) -> cents per MIDI note.
//
// Format: https://www.huygens-fokker.org/scala/scl_format.html (and the .kbm help page). Parsing
// allocates: call it off the audio thread and hand the table over (Engine::setTuningTable).
// Without a keyboard map the scale starts on middle C (MIDI 60) and note 69 sounds at A4 (the
// A4 parameter moves it); a keyboard map sets its own middle note, reference note and frequency.

#pragma once

#include <cmath>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

namespace o2
{
struct ScalaTuning
{
    bool ok = false;
    std::string name, error;
    int notes = 0; // scale size
    bool hasKeyboardMap = false;
    double cents[128] = {}; // per MIDI note, cents on 12-TET at A440 (unmapped keys: 0)
};

namespace scala
{
inline std::vector<std::string> lines (const std::string& text)
{
    std::vector<std::string> out;
    std::istringstream in (text);
    std::string l;
    while (std::getline (in, l))
    {
        if (! l.empty() && l.back() == '\r')
            l.pop_back();
        if (! l.empty() && l[0] == '!')
            continue;
        out.push_back (l);
    }
    return out;
}

inline std::string trim (const std::string& s)
{
    const auto a = s.find_first_not_of (" \t");
    if (a == std::string::npos)
        return {};
    const auto b = s.find_last_not_of (" \t");
    return s.substr (a, b - a + 1);
}

// one pitch line: cents if it has a '.', else a ratio a/b or a whole number; false if unreadable
inline bool pitch (const std::string& line, double& cents)
{
    std::string t = trim (line);
    const auto sp = t.find_first_of (" \t");
    if (sp != std::string::npos)
        t = t.substr (0, sp); // anything after the value is a comment
    if (t.empty())
        return false;
    char* end = nullptr;
    if (t.find ('.') != std::string::npos)
    {
        cents = std::strtod (t.c_str(), &end);
        return end != t.c_str();
    }
    const auto slash = t.find ('/');
    const double a = std::strtod (t.substr (0, slash).c_str(), &end);
    const double b = slash == std::string::npos ? 1.0 : std::strtod (t.substr (slash + 1).c_str(), nullptr);
    if (! (a > 0.0) || ! (b > 0.0))
        return false;
    cents = 1200.0 * std::log2 (a / b);
    return true;
}
} // namespace scala

// scl: the .scl file's text; kbm: the .kbm file's text, or empty for the default map
inline ScalaTuning parseScala (const std::string& scl, const std::string& kbm = {})
{
    ScalaTuning r;
    const auto L = scala::lines (scl);
    if (L.size() < 2)
    {
        r.error = "not a Scala file";
        return r;
    }
    r.name = scala::trim (L[0]);
    const int n = std::atoi (scala::trim (L[1]).c_str());
    if (n < 1 || n > 1024 || (int) L.size() < 2 + n)
    {
        r.error = "the scale's note count does not match its notes";
        return r;
    }
    std::vector<double> deg ((size_t) n);
    for (int i = 0; i < n; ++i)
        if (! scala::pitch (L[(size_t) (2 + i)], deg[(size_t) i]))
        {
            r.error = "unreadable pitch: " + L[(size_t) (2 + i)];
            return r;
        }
    const double period = deg.back();
    // cents of scale step k (any integer): whole periods plus the degree
    auto stepCents = [&] (long k)
    {
        const long q = (long) std::floor ((double) k / n);
        const long rr = k - q * n;
        return q * period + (rr == 0 ? 0.0 : deg[(size_t) (rr - 1)]);
    };

    // keyboard map
    int mapSize = n, first = 0, last = 127, middle = 60, ref = 69, octaveDeg = n;
    double refHz = 440.0;
    std::vector<int> map;
    if (! scala::trim (kbm).empty())
    {
        std::vector<std::string> K;
        for (auto& l : scala::lines (kbm))
            if (! scala::trim (l).empty())
                K.push_back (scala::trim (l));
        if (K.size() < 7)
        {
            r.error = "keyboard map too short";
            return r;
        }
        mapSize = std::atoi (K[0].c_str());
        first = std::atoi (K[1].c_str());
        last = std::atoi (K[2].c_str());
        middle = std::atoi (K[3].c_str());
        ref = std::atoi (K[4].c_str());
        refHz = std::strtod (K[5].c_str(), nullptr);
        octaveDeg = std::atoi (K[6].c_str());
        if (mapSize < 0 || mapSize > 1024 || ! (refHz > 0.0))
        {
            r.error = "unreadable keyboard map";
            return r;
        }
        for (int i = 0; i < mapSize; ++i)
        {
            const std::string e = 7 + (size_t) i < K.size() ? K[7 + (size_t) i] : std::string ("x");
            map.push_back (e.empty() || e[0] == 'x' || e[0] == 'X' ? -1 : std::atoi (e.c_str()));
        }
        r.hasKeyboardMap = true;
    }
    if (mapSize == 0) // linear: every key the next step
    {
        mapSize = n;
        map.clear();
    }
    if (octaveDeg <= 0)
        octaveDeg = n;
    // scale step of a key, or false when the key is unmapped
    auto step = [&] (int key, long& k)
    {
        const long i = key - middle;
        const long q = (long) std::floor ((double) i / mapSize);
        const long rr = i - q * mapSize;
        const int m = map.empty() ? (int) rr : map[(size_t) rr];
        if (m < 0)
            return false;
        k = m + q * octaveDeg;
        return true;
    };
    long kRef = 0;
    if (! step (ref, kRef))
        kRef = ref - middle;
    const double refCents = stepCents (kRef);
    const double refEt = 1200.0 * std::log2 (refHz / 440.0);
    for (int key = 0; key < 128; ++key)
    {
        long k = 0;
        if (key < first || key > last || ! step (key, k))
            continue;
        const double abs = refEt + (stepCents (k) - refCents); // cents from A440
        r.cents[key] = abs - 100.0 * (key - 69);
    }
    r.notes = n;
    r.ok = true;
    return r;
}
} // namespace o2
