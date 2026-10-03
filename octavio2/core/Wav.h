// Octavio 2: reads the WAV files the engine ships (bodies, halls) from memory or disk.
// PCM 16/24/32-bit and 32-bit float, any channel count. Message thread only (allocates).

#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace o2
{
struct WavData
{
    int sampleRate = 0;
    std::vector<std::vector<float>> channels; // empty if the data could not be read
};

inline WavData readWav (const void* bytes, size_t size)
{
    WavData out;
    const auto* d = static_cast<const unsigned char*> (bytes);
    auto u16 = [&] (size_t i) { return (uint32_t) d[i] | ((uint32_t) d[i + 1] << 8); };
    auto u32 = [&] (size_t i) { return u16 (i) | (u16 (i + 2) << 16); };
    if (size < 12 || std::memcmp (d, "RIFF", 4) != 0 || std::memcmp (d + 8, "WAVE", 4) != 0)
        return out;
    int format = 0, numChannels = 0, bits = 0;
    for (size_t i = 12; i + 8 <= size;)
    {
        const uint32_t len = u32 (i + 4);
        const size_t body = i + 8;
        if (std::memcmp (d + i, "fmt ", 4) == 0 && len >= 16 && body + 16 <= size)
        {
            format = (int) u16 (body);
            numChannels = (int) u16 (body + 2);
            out.sampleRate = (int) u32 (body + 4);
            bits = (int) u16 (body + 14);
            if (format == 0xFFFE && len >= 26) // WAVE_FORMAT_EXTENSIBLE: the sub-format's first two bytes
                format = (int) u16 (body + 24);
        }
        else if (std::memcmp (d + i, "data", 4) == 0 && numChannels > 0)
        {
            const size_t bytesPer = (size_t) bits / 8;
            const size_t avail = std::min ((size_t) len, size - body);
            const size_t frames = avail / (bytesPer * (size_t) numChannels);
            out.channels.assign ((size_t) numChannels, std::vector<float> (frames));
            for (size_t f = 0; f < frames; ++f)
                for (size_t c = 0; c < (size_t) numChannels; ++c)
                {
                    const unsigned char* s = d + body + (f * (size_t) numChannels + c) * bytesPer;
                    float v = 0.0f;
                    if (format == 3 && bits == 32)
                        std::memcpy (&v, s, 4);
                    else if (format == 1 && bits == 16)
                        v = (float) (int16_t) (s[0] | (s[1] << 8)) / 32768.0f;
                    else if (format == 1 && bits == 24)
                        v = (float) ((int32_t) (((uint32_t) s[0] << 8) | ((uint32_t) s[1] << 16)
                                                | ((uint32_t) s[2] << 24))
                                     >> 8)
                            / 8388608.0f;
                    else if (format == 1 && bits == 32)
                    {
                        int32_t k;
                        std::memcpy (&k, s, 4);
                        v = (float) k / 2147483648.0f;
                    }
                    else
                    {
                        out.channels.clear();
                        return out;
                    }
                    out.channels[c][f] = v;
                }
            return out;
        }
        i = body + len + (len & 1);
    }
    return out;
}

inline WavData readWavFile (const std::string& path)
{
    std::ifstream f (path, std::ios::binary);
    std::vector<char> bytes ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char>());
    return readWav (bytes.data(), bytes.size());
}
} // namespace o2
