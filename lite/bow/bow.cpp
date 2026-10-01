// WebAssembly entry points for the bare bowed string (BowString.h).
#include "../BowString.h"

extern "C"
{
void* memset (void* d, int c, unsigned long n)
{
    auto* p = static_cast<unsigned char*> (d);
    while (n--)
        *p++ = static_cast<unsigned char> (c);
    return d;
}
void* memcpy (void* d, const void* s, unsigned long n)
{
    auto* p = static_cast<unsigned char*> (d);
    auto* q = static_cast<const unsigned char*> (s);
    while (n--)
        *p++ = *q++;
    return d;
}
}

namespace
{
lite::BowString bs;
constexpr int maxBlock = 1024;
float outL[maxBlock], outR[maxBlock];
float scopeCopy[lite::BowString::scopeSize];
} // namespace

#define EXPORT(name) extern "C" __attribute__ ((export_name (#name)))

EXPORT (bs_prepare) void bs_prepare (double sr) { bs.prepare (sr); }
EXPORT (bs_set) void bs_set (int i, double v) { bs.setParam (i, v); }
EXPORT (bs_get) double bs_get (int i) { return bs.params[i]; }
EXPORT (bs_note_on) void bs_note_on (int n) { bs.noteOn (n); }
EXPORT (bs_note_off) void bs_note_off (int n) { bs.noteOff (n); }
EXPORT (bs_all_off) void bs_all_off() { bs.allNotesOff(); }
EXPORT (bs_process) void bs_process (int n) { bs.process (outL, outR, n < maxBlock ? n : maxBlock); }
EXPORT (bs_left) float* bs_left() { return outL; }
EXPORT (bs_right) float* bs_right() { return outR; }
EXPORT (bs_slips) double bs_slips() { return bs.slipsPerPeriod(); }
EXPORT (bs_irregularity) double bs_irregularity() { return bs.irregularity(); }
EXPORT (bs_bowing) int bs_bowing() { return bs.bowing ? 1 : 0; }
EXPORT (bs_period) double bs_period() { return bs.N / 2.0; } // host samples
EXPORT (bs_fmax) double bs_fmax()
{
    // Schelleng's upper limit for the current speed and contact point
    return 2.0 * lite::BowString::impedance * bs.params[1] / (bs.params[2] * (lite::BowString::muS - lite::BowString::muD));
}
// The last scopeSize samples of string velocity at the bow, oldest first.
EXPORT (bs_scope) float* bs_scope()
{
    for (int i = 0; i < lite::BowString::scopeSize; ++i)
        scopeCopy[i] = bs.scope[(bs.scopePos + i) % lite::BowString::scopeSize];
    return scopeCopy;
}
EXPORT (bs_scope_size) int bs_scope_size() { return lite::BowString::scopeSize; }
