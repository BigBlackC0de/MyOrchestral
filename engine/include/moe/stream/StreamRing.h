#pragma once

#include "moe/Types.h"

#include <algorithm>
#include <atomic>
#include <vector>

namespace moe::stream
{

/** Lock-free single-producer / single-consumer ring of deinterleaved audio.

    The producer is the streaming thread, the consumer is the audio thread.
    Indices are free-running `std::size_t` counters, wrapped only when indexing;
    the difference between them is the fill level, which stays correct across
    wraparound without a separate count or any modulo on the hot path.

    Not a general-purpose queue: exactly one reader and one writer, ever. */
class StreamRing
{
public:
    void prepare (int numChannels, std::size_t numFrames)
    {
        channels = std::max (1, numChannels);
        capacity = std::max<std::size_t> (numFrames, 64);
        data.assign (capacity * static_cast<std::size_t> (channels), 0.0f);
        reset();
    }

    void reset() noexcept
    {
        readIndex.store (0, std::memory_order_relaxed);
        writeIndex.store (0, std::memory_order_relaxed);
    }

    std::size_t getCapacity() const noexcept { return capacity; }
    int         getNumChannels() const noexcept { return channels; }

    /** Frames available to the consumer. */
    std::size_t framesReady() const noexcept
    {
        return writeIndex.load (std::memory_order_acquire)
               - readIndex.load (std::memory_order_relaxed);
    }

    /** Frames the producer may write. */
    std::size_t framesFree() const noexcept
    {
        return capacity - (writeIndex.load (std::memory_order_relaxed)
                           - readIndex.load (std::memory_order_acquire));
    }

    /** Consumer side. Copies up to `numFrames` into `destination` and advances.
        @returns frames actually copied */
    std::size_t read (float* const* destination, int numDestChannels, std::size_t numFrames) noexcept
    {
        const std::size_t ready = std::min (framesReady(), numFrames);
        if (ready == 0)
            return 0;

        const std::size_t start = readIndex.load (std::memory_order_relaxed) % capacity;
        const std::size_t first = std::min (ready, capacity - start);

        for (int ch = 0; ch < numDestChannels; ++ch)
        {
            const float* source = channelData (std::min (ch, channels - 1));
            float*       out    = destination[ch];

            std::copy (source + start, source + start + first, out);
            if (ready > first)
                std::copy (source, source + (ready - first), out + first);
        }

        readIndex.fetch_add (ready, std::memory_order_release);
        return ready;
    }

    /** Consumer side. Drops frames without copying (used when a voice skips). */
    void advanceRead (std::size_t numFrames) noexcept
    {
        readIndex.fetch_add (std::min (numFrames, framesReady()), std::memory_order_release);
    }

    /** Producer side. Hands out up to two contiguous write regions so the
        producer can fill the ring with a single decode pass per region. */
    struct WriteRegions
    {
        std::size_t firstStart = 0, firstLength = 0;
        std::size_t secondStart = 0, secondLength = 0;

        std::size_t total() const noexcept { return firstLength + secondLength; }
    };

    WriteRegions prepareWrite (std::size_t numFrames) const noexcept
    {
        WriteRegions regions;

        const std::size_t free = std::min (framesFree(), numFrames);
        if (free == 0)
            return regions;

        const std::size_t start = writeIndex.load (std::memory_order_relaxed) % capacity;

        regions.firstStart  = start;
        regions.firstLength = std::min (free, capacity - start);

        if (free > regions.firstLength)
        {
            regions.secondStart  = 0;
            regions.secondLength = free - regions.firstLength;
        }

        return regions;
    }

    void finishWrite (std::size_t numFrames) noexcept
    {
        writeIndex.fetch_add (numFrames, std::memory_order_release);
    }

    /** Writable pointer to one channel's storage. Producer side only. */
    float* channelData (int ch) noexcept
    {
        return data.data() + static_cast<std::size_t> (std::clamp (ch, 0, channels - 1)) * capacity;
    }

    const float* channelData (int ch) const noexcept
    {
        return data.data() + static_cast<std::size_t> (std::clamp (ch, 0, channels - 1)) * capacity;
    }

private:
    std::vector<float> data;
    std::size_t        capacity = 0;
    int                channels = 0;

    std::atomic<std::size_t> readIndex  { 0 };
    std::atomic<std::size_t> writeIndex { 0 };
};

} // namespace moe::stream
