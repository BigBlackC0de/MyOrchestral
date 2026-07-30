#include "moe/stream/SampleStreamer.h"

#include <algorithm>
#include <chrono>

namespace moe::stream
{

//==============================================================================
void StreamSlot::prepare (int channels, std::size_t ringFrames, std::size_t refillFrames)
{
    numChannels = std::max (1, channels);
    refillChunk = std::max<std::size_t> (refillFrames, 256);

    ring.prepare (numChannels, ringFrames);
    channelPointers.assign (static_cast<std::size_t> (numChannels), nullptr);

    state.store (State::free, std::memory_order_release);
}

bool StreamSlot::acquire (const std::string& path,
                          SampleIndex        startFrame,
                          SampleIndex        endFrame,
                          const LoopConfig&  loopConfig) noexcept
{
    State expected = State::free;
    if (! state.compare_exchange_strong (expected, State::pending,
                                         std::memory_order_acq_rel,
                                         std::memory_order_relaxed))
        return false;

    requestedPath  = &path;
    requestedStart = startFrame;
    requestedEnd   = endFrame;
    requestedLoop  = loopConfig;

    ring.reset();
    endOfFile.store (false, std::memory_order_relaxed);
    loopEnabled.store (loopConfig.enabled, std::memory_order_relaxed);

    // Re-publish `pending` with release ordering so the streaming thread sees
    // the request fields written above.
    state.store (State::pending, std::memory_order_release);
    return true;
}

std::size_t StreamSlot::read (float* const* destination, int numDestChannels, std::size_t numFrames) noexcept
{
    if (state.load (std::memory_order_acquire) != State::running)
        return 0;

    const std::size_t got = ring.read (destination, numDestChannels, numFrames);

    if (got < numFrames && ! endOfFile.load (std::memory_order_acquire))
        underruns.fetch_add (1, std::memory_order_relaxed);

    return got;
}

void StreamSlot::release() noexcept
{
    State current = state.load (std::memory_order_acquire);

    // A slot that was never opened can go straight back to the pool.
    if (current == State::pending
        && state.compare_exchange_strong (current, State::free,
                                          std::memory_order_acq_rel,
                                          std::memory_order_relaxed))
        return;

    if (current == State::running)
        state.store (State::releasing, std::memory_order_release);
}

bool StreamSlot::needsService (std::size_t refillThreshold) const noexcept
{
    switch (state.load (std::memory_order_acquire))
    {
        case State::pending:
        case State::releasing:
            return true;

        case State::running:
            return ! endOfFile.load (std::memory_order_acquire)
                   && ring.framesFree() >= refillThreshold;

        case State::free:
        default:
            return false;
    }
}

bool StreamSlot::service (std::size_t refillFrames)
{
    const State current = state.load (std::memory_order_acquire);

    if (current == State::releasing)
    {
        // The file handle is kept open: the next note on the same sample — the
        // common case when repeating a phrase — then costs a seek, not an open.
        filePosition = 0;
        stopFrame    = -1;
        ring.reset();
        state.store (State::free, std::memory_order_release);
        return true;
    }

    if (current == State::pending)
    {
        const std::string* path = requestedPath;
        if (path == nullptr)
        {
            state.store (State::free, std::memory_order_release);
            return true;
        }

        if (reader == nullptr || openPath != *path)
        {
            reader = audio::openAudioFile (*path);
            openPath = reader != nullptr ? *path : std::string{};
        }

        if (reader == nullptr)
        {
            endOfFile.store (true, std::memory_order_release);
            state.store (State::running, std::memory_order_release);
            return true;
        }

        loop         = requestedLoop;
        filePosition = requestedStart;
        stopFrame    = requestedEnd >= 0 ? std::min (requestedEnd, reader->info().numFrames)
                                          : reader->info().numFrames;

        if (loop.enabled && (loop.end <= loop.start || loop.end > stopFrame))
            loop.enabled = false;

        fill (ring.getCapacity());
        state.store (State::running, std::memory_order_release);
        return true;
    }

    if (current == State::running)
        return fill (refillFrames) > 0;

    return false;
}

std::size_t StreamSlot::fill (std::size_t maxFrames)
{
    if (reader == nullptr)
    {
        endOfFile.store (true, std::memory_order_release);
        return 0;
    }

    std::size_t written = 0;

    while (written < maxFrames)
    {
        const auto regions = ring.prepareWrite (maxFrames - written);
        if (regions.total() == 0)
            break;

        std::size_t producedThisPass = 0;

        for (int part = 0; part < 2; ++part)
        {
            const std::size_t start  = part == 0 ? regions.firstStart  : regions.secondStart;
            const std::size_t length = part == 0 ? regions.firstLength : regions.secondLength;

            if (length == 0)
                continue;

            std::size_t producedInPart = 0;

            while (producedInPart < length)
            {
                const bool looping = loop.enabled && loopEnabled.load (std::memory_order_acquire);
                const SampleIndex boundary = looping ? loop.end : stopFrame;

                if (filePosition >= boundary)
                {
                    if (looping)
                    {
                        filePosition = loop.start;
                        continue;
                    }

                    endOfFile.store (true, std::memory_order_release);
                    break;
                }

                const auto wanted = static_cast<SampleIndex> (length - producedInPart);
                const auto toRead = std::min (wanted, boundary - filePosition);

                for (int ch = 0; ch < numChannels; ++ch)
                    channelPointers[static_cast<std::size_t> (ch)] =
                        ring.channelData (ch) + start + producedInPart;

                const SampleIndex got = reader->read (channelPointers.data(), numChannels,
                                                      filePosition, toRead);

                if (got <= 0)
                {
                    endOfFile.store (true, std::memory_order_release);
                    break;
                }

                filePosition   += got;
                producedInPart += static_cast<std::size_t> (got);
            }

            producedThisPass += producedInPart;

            if (endOfFile.load (std::memory_order_relaxed))
                break;
        }

        if (producedThisPass == 0)
            break;

        ring.finishWrite (producedThisPass);
        written += producedThisPass;

        if (endOfFile.load (std::memory_order_relaxed))
            break;
    }

    return written;
}

//==============================================================================
StreamManager::~StreamManager()
{
    shutdown();
}

void StreamManager::prepare (int numberOfSlots, int numChannels, const StreamingSettings& newSettings)
{
    shutdown();

    settings = newSettings;
    shouldExit.store (false, std::memory_order_relaxed);

    slots.clear();
    slots.reserve (static_cast<std::size_t> (std::max (1, numberOfSlots)));

    for (int i = 0; i < std::max (1, numberOfSlots); ++i)
    {
        auto slot = std::make_unique<StreamSlot>();
        slot->prepare (numChannels, settings.ringFrames, settings.refillFrames);
        slots.push_back (std::move (slot));
    }

    thread = std::thread ([this] { streamThreadLoop(); });
}

void StreamManager::shutdown()
{
    if (! thread.joinable())
    {
        slots.clear();
        return;
    }

    shouldExit.store (true, std::memory_order_release);
    notifyStreamThread();
    thread.join();
    slots.clear();
}

StreamSlot* StreamManager::acquireSlot (const std::string& path,
                                        SampleIndex        startFrame,
                                        SampleIndex        endFrame,
                                        const LoopConfig&  loop) noexcept
{
    for (auto& slot : slots)
        if (slot->acquire (path, startFrame, endFrame, loop))
            return slot.get();

    return nullptr;
}

void StreamManager::notifyStreamThread() noexcept
{
    workPending.store (true, std::memory_order_release);
    wakeCondition.notify_one();
}

int StreamManager::activeSlotCount() const noexcept
{
    int count = 0;
    for (const auto& slot : slots)
        if (slot->isRunning() || slot->isPending())
            ++count;

    return count;
}

std::uint32_t StreamManager::totalUnderruns() const noexcept
{
    std::uint32_t total = 0;
    for (const auto& slot : slots)
        total += slot->underrunCount();

    return total;
}

void StreamManager::streamThreadLoop()
{
    // Refill when the ring is at least a quarter empty: often enough that a
    // burst of note-ons cannot drain it, rare enough to keep syscalls down.
    const std::size_t refillThreshold = std::max<std::size_t> (settings.ringFrames / 4, 1024);

    while (! shouldExit.load (std::memory_order_acquire))
    {
        bool didWork = false;

        for (auto& slot : slots)
        {
            if (shouldExit.load (std::memory_order_acquire))
                break;

            if (slot->needsService (refillThreshold))
                didWork |= slot->service (settings.refillFrames);
        }

        if (didWork)
            continue;

        std::unique_lock<std::mutex> lock (wakeMutex);
        wakeCondition.wait_for (lock, std::chrono::milliseconds (5), [this]
        {
            return shouldExit.load (std::memory_order_acquire)
                   || workPending.exchange (false, std::memory_order_acq_rel);
        });
    }
}

} // namespace moe::stream
