#pragma once
#include <cstdlib>
// SCRATCH: picks one of the "more authentic" sound variations for the offline renderer.
namespace violinsynth::engine
{
inline int authVariant()
{
    static const int v = [] { const char* e = std::getenv ("AUTH_VARIANT"); return e ? std::atoi (e) : 0; }();
    return v;
}
} // namespace violinsynth::engine
