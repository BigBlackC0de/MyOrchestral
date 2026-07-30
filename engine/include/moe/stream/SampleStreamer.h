#pragma once

#include "moe/Types.h"
#include "moe/audio/AudioFileReader.h"
#include "moe/stream/StreamRing.h"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace moe::stream
{

/** Loop applied by the streaming side, so the consumer sees one continuous
    stream and never has to think about file boundaries. */
struct LoopConfig
{
    bool        enabled = false;
    SampleIndex start   = 0;
    SampleIndex end     = 0;
};

/** One concurrent disk stream: its own file handle, its own ring buffer.

    Lifecycle, and which thread drives each transition:

        free ──acquire (audio)──► pending ──open+fill (stream)──► running
          ▲                                                          │
          └──────────── close (stream) ◄── releasing ◄── release (audio)

    The audio thread never opens, seeks or reads a file; it only flips atomics
    and drains the ring. */
class StreamSlot
{
public:
    enum class State : int { free = 0, pending, running, releasing };

    void prepare (int numChannels, std::size_t ringFrames, std::size_t refillFrames);

    // ---- audio thread ------------------------------------------------------

    /** Reserves this slot. `path` must outlive the stream — it points into the
        loaded Instrument, which the engine keeps alive while voices reference it.
        @returns false when the slot was not free */
    bool acquire (const std::string& path,
                  SampleIndex        startFrame,
                  SampleIndex        endFrame,
                  const LoopConfig&  loop) noexcept;

    /** Pulls audio. Returns frames written; a short result means the stream is
        starved or finished — the caller must fade rather than hold the value. */
    std::size_t read (float* const* destination, int numDestChannels, std::size_t numFrames) noexcept;

    /** True once the file is exhausted and the ring is drained. */
    bool isExhausted() const noexcept
    {
        return endOfFile.load (std::memory_order_acquire) && ring.framesReady() == 0;
    }

    /** True while the stream is still being opened; the voice should keep
        playing from its preload buffer until this clears. */
    bool isPending() const noexcept
    {
        return state.load (std::memory_order_acquire) == State::pending;
    }

    bool isRunning() const noexcept
    {
        return state.load (std::memory_order_acquire) == State::running;
    }

    /** Stops looping — the note-off half of `loop_sustain`. */
    void releaseLoop() noexcept { loopEnabled.store (false, std::memory_order_release); }

    /** Hands the slot back. Safe to call from the audio thread; the actual file
        close happens on the streaming thread. */
    void release() noexcept;

    // ---- streaming thread --------------------------------------------------

    /** Performs whatever work the slot needs: opening, refilling, closing.
        @returns true when the slot did something (used to decide whether to sleep) */
    bool service (std::size_t refillFrames);

    bool needsService (std::size_t refillThreshold) const noexcept;

    /** Diagnostics: number of times the consumer found the ring empty. */
    std::uint32_t underrunCount() const noexcept
    {
        return underruns.load (std::memory_order_relaxed);
    }

private:
    std::size_t fill (std::size_t maxFrames);

    StreamRing ring;

    std::atomic<State>         state       { State::free };
    std::atomic<bool>          endOfFile   { false };
    std::atomic<bool>          loopEnabled { false };
    std::atomic<std::uint32_t> underruns   { 0 };

    // Written by the audio thread before publishing `pending`, read by the
    // streaming thread after observing it — the release/acquire pair on `state`
    // is what makes that safe.
    const std::string* requestedPath  = nullptr;
    SampleIndex        requestedStart = 0;
    SampleIndex        requestedEnd   = -1;
    LoopConfig         requestedLoop;

    // Streaming-thread-only state.
    std::unique_ptr<audio::AudioFileReader> reader;
    std::string                             openPath;
    SampleIndex                             filePosition = 0;
    SampleIndex                             stopFrame    = -1;
    LoopConfig                              loop;

    std::vector<float*> channelPointers;
    int                 numChannels  = 2;
    std::size_t         refillChunk  = 16384;
};

/** Owns the streaming thread and a fixed pool of slots.

    The pool is fixed because allocating a slot must be possible from the audio
    thread. When every slot is busy the engine degrades gracefully: the voice
    plays its preloaded head and fades out, which is far better than blocking
    the audio callback. */
class StreamManager
{
public:
    StreamManager() = default;
    ~StreamManager();

    /** Allocates the pool and starts the thread. Call before any audio runs. */
    void prepare (int numSlots, int numChannels, const StreamingSettings& settings);

    /** Stops the thread and frees everything. */
    void shutdown();

    /** Audio thread. Returns nullptr when no slot is free. */
    StreamSlot* acquireSlot (const std::string& path,
                             SampleIndex        startFrame,
                             SampleIndex        endFrame,
                             const LoopConfig&  loop) noexcept;

    /** Audio thread. Wakes the streaming thread so pending work starts promptly. */
    void notifyStreamThread() noexcept;

    int numSlots() const noexcept { return static_cast<int> (slots.size()); }

    /** Diagnostics for the UI: how many slots are currently in use. */
    int activeSlotCount() const noexcept;

    std::uint32_t totalUnderruns() const noexcept;

private:
    void streamThreadLoop();

    std::vector<std::unique_ptr<StreamSlot>> slots;
    StreamingSettings                        settings;

    std::thread             thread;
    std::mutex              wakeMutex;
    std::condition_variable wakeCondition;
    std::atomic<bool>       shouldExit { false };
    std::atomic<bool>       workPending { false };
};

} // namespace moe::stream
