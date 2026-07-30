#pragma once

#include "moe/Types.h"
#include "moe/bank/Region.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <ostream>
#include <string>
#include <vector>

namespace moe
{

// Without these, doctest cannot stringify our enums and a failing comparison
// between two of them will not even compile, let alone report a useful message.
inline std::ostream& operator<< (std::ostream& stream, Articulation value)
{
    return stream << toName (value);
}

inline std::ostream& operator<< (std::ostream& stream, Family value)
{
    return stream << toName (value);
}

namespace bank
{
    inline std::ostream& operator<< (std::ostream& stream, LoopMode value)
    {
        switch (value)
        {
            case LoopMode::noLoop:         return stream << "no_loop";
            case LoopMode::oneShot:        return stream << "one_shot";
            case LoopMode::loopContinuous: return stream << "loop_continuous";
            case LoopMode::loopSustain:    return stream << "loop_sustain";
        }
        return stream << "?";
    }
} // namespace bank

} // namespace moe

namespace moe::test
{

/** Minimal 16-bit PCM WAV writer, used to build fixtures on the fly.

    Deliberately independent of the engine's own reader: a test that wrote and
    read with the same code would pass even if both agreed on a wrong format. */
inline bool writeWav (const std::string&                     path,
                      const std::vector<std::vector<float>>& channels,
                      double                                 sampleRate,
                      int                                    rootNote = -1)
{
    if (channels.empty() || channels[0].empty())
        return false;

    const auto numChannels = static_cast<std::uint16_t> (channels.size());
    const auto numFrames   = static_cast<std::uint32_t> (channels[0].size());
    const std::uint16_t bitsPerSample = 16;
    const std::uint32_t byteRate   = static_cast<std::uint32_t> (sampleRate) * numChannels * 2;
    const std::uint16_t blockAlign = static_cast<std::uint16_t> (numChannels * 2);
    const std::uint32_t dataBytes  = numFrames * numChannels * 2;

    const bool writeSmpl = rootNote >= 0;
    const std::uint32_t smplBytes = 36 + 24;
    const std::uint32_t riffSize  = 4 + (8 + 16) + (8 + dataBytes)
                                    + (writeSmpl ? (8 + smplBytes) : 0);

    std::ofstream stream (path, std::ios::binary);
    if (! stream)
        return false;

    auto u32 = [&stream] (std::uint32_t value)
    {
        char bytes[4] = { static_cast<char> (value & 0xFF),
                          static_cast<char> ((value >> 8) & 0xFF),
                          static_cast<char> ((value >> 16) & 0xFF),
                          static_cast<char> ((value >> 24) & 0xFF) };
        stream.write (bytes, 4);
    };

    auto u16 = [&stream] (std::uint16_t value)
    {
        char bytes[2] = { static_cast<char> (value & 0xFF),
                          static_cast<char> ((value >> 8) & 0xFF) };
        stream.write (bytes, 2);
    };

    stream.write ("RIFF", 4);
    u32 (riffSize);
    stream.write ("WAVE", 4);

    stream.write ("fmt ", 4);
    u32 (16);
    u16 (1);                                            // PCM
    u16 (numChannels);
    u32 (static_cast<std::uint32_t> (sampleRate));
    u32 (byteRate);
    u16 (blockAlign);
    u16 (bitsPerSample);

    stream.write ("data", 4);
    u32 (dataBytes);

    for (std::uint32_t frame = 0; frame < numFrames; ++frame)
        for (std::uint16_t ch = 0; ch < numChannels; ++ch)
        {
            const float clamped = std::fmax (-1.0f, std::fmin (1.0f, channels[ch][frame]));
            u16 (static_cast<std::uint16_t> (static_cast<std::int16_t> (clamped * 32767.0f)));
        }

    if (writeSmpl)
    {
        stream.write ("smpl", 4);
        u32 (smplBytes);
        u32 (0); u32 (0); u32 (0);                      // manufacturer, product, period
        u32 (static_cast<std::uint32_t> (rootNote));    // MIDI unity note
        u32 (0); u32 (0); u32 (0);                      // pitch fraction, SMPTE
        u32 (1);                                        // one loop
        u32 (0);                                        // sampler data
        u32 (0); u32 (0);                               // cue id, loop type
        u32 (0);                                        // loop start
        u32 (numFrames > 1 ? numFrames - 1 : 0);        // loop end
        u32 (0); u32 (0);                               // fraction, play count
    }

    return stream.good();
}

/** A steady sine, the easiest signal to make assertions about. */
inline std::vector<float> sine (int numFrames, double frequency, double sampleRate, float amplitude = 0.5f)
{
    std::vector<float> output (static_cast<std::size_t> (numFrames));

    for (int i = 0; i < numFrames; ++i)
        output[static_cast<std::size_t> (i)] = amplitude
            * static_cast<float> (std::sin (2.0 * 3.14159265358979 * frequency
                                             * static_cast<double> (i) / sampleRate));

    return output;
}

inline float peakOf (const float* data, int numSamples)
{
    float peak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
        peak = std::fmax (peak, std::fabs (data[i]));

    return peak;
}

inline float rmsOf (const float* data, int numSamples)
{
    double sum = 0.0;
    for (int i = 0; i < numSamples; ++i)
        sum += static_cast<double> (data[i]) * static_cast<double> (data[i]);

    return numSamples > 0 ? static_cast<float> (std::sqrt (sum / numSamples)) : 0.0f;
}

/** Scratch directory for fixtures, unique per test binary run. */
const std::string& scratchDirectory();

} // namespace moe::test
