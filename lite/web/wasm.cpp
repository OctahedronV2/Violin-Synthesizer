// WebAssembly entry points for the Octavio Lite engine (LiteCore.h).
// Built freestanding with clang; see lite/web/build.sh.

#include "../LiteCore.h"

extern "C"
{
    // The compiler may emit calls to these for struct copies and clears.
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
    void* memmove (void* d, const void* s, unsigned long n)
    {
        auto* p = static_cast<unsigned char*> (d);
        auto* q = static_cast<const unsigned char*> (s);
        if (p < q)
            while (n--)
                *p++ = *q++;
        else
            for (p += n, q += n; n--;)
                *--p = *--q;
        return d;
    }
}

namespace
{
lite::Engine engine;
constexpr int maxBlock = 1024;
float outL[maxBlock], outR[maxBlock];
} // namespace

#define EXPORT(name) extern "C" __attribute__ ((export_name (#name)))

EXPORT (lite_prepare) void lite_prepare (double sampleRate)
{
    engine.prepare (sampleRate);
}
EXPORT (lite_param_count) int lite_param_count()
{
    return lite::numParams;
}
EXPORT (lite_param_id) const char* lite_param_id (int i)
{
    return lite::kParams[i].id;
}
EXPORT (lite_param_group) const char* lite_param_group (int i)
{
    return lite::kParams[i].group;
}
EXPORT (lite_param_label) const char* lite_param_label (int i)
{
    return lite::kParams[i].label;
}
EXPORT (lite_param_unit) const char* lite_param_unit (int i)
{
    return lite::kParams[i].unit;
}
EXPORT (lite_param_min) double lite_param_min (int i)
{
    return lite::kParams[i].min;
}
EXPORT (lite_param_max) double lite_param_max (int i)
{
    return lite::kParams[i].max;
}
EXPORT (lite_param_default) double lite_param_default (int i)
{
    return lite::kParams[i].def;
}
EXPORT (lite_set_param) void lite_set_param (int i, double v)
{
    engine.setParam (i, v);
}
EXPORT (lite_get_param) double lite_get_param (int i)
{
    return engine.params[i];
}
EXPORT (lite_note_on) void lite_note_on (int note, double velocity)
{
    engine.noteOn (note, velocity);
}
EXPORT (lite_note_off) void lite_note_off (int note)
{
    engine.noteOff (note);
}
EXPORT (lite_controller) void lite_controller (int cc, double value)
{
    engine.controller (cc, value);
}
EXPORT (lite_all_off) void lite_all_off()
{
    engine.allNotesOff();
}
EXPORT (lite_body_buffer) float* lite_body_buffer()
{
    return engine.body.irBuf;
}
EXPORT (lite_body_capacity) int lite_body_capacity()
{
    return lite::Body::maxParts * lite::Body::B;
}
EXPORT (lite_body_load) void lite_body_load (int length)
{
    engine.body.load (length);
}
EXPORT (lite_out_left) float* lite_out_left()
{
    return outL;
}
EXPORT (lite_out_right) float* lite_out_right()
{
    return outR;
}
EXPORT (lite_process) void lite_process (int n)
{
    engine.process (outL, outR, n < maxBlock ? n : maxBlock);
}
