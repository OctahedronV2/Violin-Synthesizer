#pragma once

// Real-time safety annotations, checked by RealtimeSanitizer (Clang 20's
// -fsanitize=realtime, enabled with -DVIOLINSYNTH_ENABLE_RTSAN=ON).
//
// VIOLINSYNTH_NONBLOCKING marks a function that runs on the audio thread. In
// a RealtimeSanitizer build, any allocation, lock, or system call reached
// from inside it aborts with a stack trace. In every other build it is empty.
//
// Usage: void process (float* samples, int numSamples) VIOLINSYNTH_NONBLOCKING;

#if defined(__has_feature)
#if __has_feature(realtime_sanitizer)
#define VIOLINSYNTH_NONBLOCKING [[clang::nonblocking]]
#endif
#endif

#ifndef VIOLINSYNTH_NONBLOCKING
#define VIOLINSYNTH_NONBLOCKING
#endif
