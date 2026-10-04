#include "Curves.h"

namespace octavio2
{
const juce::Identifier CurveModel::tag { "CURVES" };

CurveModel::Lane CurveModel::getLane (int k) const
{
    const juce::ScopedLock sl (lock);
    return lanes[(size_t) juce::jlimit (0, o2::laneCount - 1, k)];
}

void CurveModel::setLane (int k, Lane l)
{
    std::stable_sort (l.begin(), l.end(), [] (const Point& a, const Point& b) { return a.beat < b.beat; });
    // no break at either end, nor two in a row
    while (! l.empty() && l.front().v < 0.0f)
        l.erase (l.begin());
    while (! l.empty() && l.back().v < 0.0f)
        l.pop_back();
    for (size_t i = 1; i < l.size();)
        if (l[i].v < 0.0f && l[i - 1].v < 0.0f)
            l.erase (l.begin() + (std::ptrdiff_t) i);
        else
            ++i;
    const juce::ScopedLock sl (lock);
    auto& lane = lanes[(size_t) juce::jlimit (0, o2::laneCount - 1, k)];
    if (lane == l)
        return;
    lane = std::move (l);
    publish();
}

void CurveModel::clearAll()
{
    const juce::ScopedLock sl (lock);
    for (auto& l : lanes)
        l.clear();
    publish();
}

bool CurveModel::hasCurve (int k) const
{
    const juce::ScopedLock sl (lock);
    return ! lanes[(size_t) juce::jlimit (0, o2::laneCount - 1, k)].empty();
}

// under lock
void CurveModel::publish()
{
    auto& set = exchange.write();
    bool anyPoints = false;
    for (int k = 0; k < o2::laneCount; ++k)
    {
        const auto& src = lanes[(size_t) k];
        auto& dst = set.lane[k];
        // a lane longer than the audio thread's holds is thinned evenly (breaks always kept)
        const size_t step = src.size() / o2::CurveLane::maxPoints + 1;
        int n = 0;
        for (size_t i = 0; i < src.size() && n < o2::CurveLane::maxPoints; ++i)
            if (i % step == 0 || src[i].v < 0.0f || i + 1 == src.size())
            {
                dst.ppq[n] = src[i].beat;
                dst.v[n] = src[i].v;
                ++n;
            }
        dst.n = n;
        anyPoints = anyPoints || n > 0;
    }
    exchange.publish();
    any.store (anyPoints, std::memory_order_release);
    changes.fetch_add (1, std::memory_order_acq_rel);
}

juce::ValueTree CurveModel::toTree() const
{
    juce::ValueTree t (tag);
    t.setProperty ("version", 1, nullptr);
    const juce::ScopedLock sl (lock);
    for (int k = 0; k < o2::laneCount; ++k)
    {
        if (lanes[(size_t) k].empty())
            continue;
        // "beat:value" pairs, value "x" a break; beats to 1/10000, values to 1/10000
        juce::String text;
        for (const auto& p : lanes[(size_t) k])
            text << juce::String (p.beat, 4) << ':' << (p.v < 0.0f ? juce::String ("x") : juce::String (p.v, 4)) << ' ';
        juce::ValueTree lane ("LANE");
        lane.setProperty ("dim", k, nullptr);
        lane.setProperty ("points", text.trimEnd(), nullptr);
        t.appendChild (lane, nullptr);
    }
    return t;
}

void CurveModel::fromTree (const juce::ValueTree& t)
{
    std::array<Lane, o2::laneCount> l;
    if (t.isValid() && t.hasType (tag))
        for (const auto lane : t)
        {
            const int k = lane.getProperty ("dim", -1);
            if (k < 0 || k >= o2::laneCount)
                continue;
            for (const auto& token : juce::StringArray::fromTokens (lane["points"].toString(), " ", {}))
            {
                const auto beat = token.upToFirstOccurrenceOf (":", false, false);
                const auto value = token.fromFirstOccurrenceOf (":", false, false);
                if (beat.isEmpty() || value.isEmpty())
                    continue;
                l[(size_t) k].push_back (
                    { beat.getDoubleValue(), value == "x" ? -1.0f : juce::jlimit (0.0f, 1.0f, value.getFloatValue()) });
            }
        }
    for (int k = 0; k < o2::laneCount; ++k)
        setLane (k, std::move (l[(size_t) k]));
}
} // namespace octavio2
