#include "doctest.h"

#include "TestHelpers.h"
#include "moe/stream/SampleStreamer.h"
#include "moe/stream/StreamRing.h"

#include <chrono>
#include <numeric>
#include <thread>

using namespace moe;
using namespace moe::test;

TEST_CASE ("ring buffer preserves order across wraparound")
{
    stream::StreamRing ring;
    ring.prepare (2, 64);

    CHECK (ring.framesReady() == 0);
    CHECK (ring.framesFree() == 64);

    int nextValue = 0;

    // Push and pop repeatedly with sizes that do not divide the capacity, so the
    // read and write positions land at every possible offset.
    for (int round = 0; round < 40; ++round)
    {
        const auto regions = ring.prepareWrite (23);
        REQUIRE (regions.total() > 0);

        std::size_t written = 0;
        for (int part = 0; part < 2; ++part)
        {
            const std::size_t start  = part == 0 ? regions.firstStart : regions.secondStart;
            const std::size_t length = part == 0 ? regions.firstLength : regions.secondLength;

            for (std::size_t i = 0; i < length; ++i)
            {
                ring.channelData (0)[start + i] = static_cast<float> (nextValue + static_cast<int> (written + i));
                ring.channelData (1)[start + i] = static_cast<float> (-(nextValue + static_cast<int> (written + i)));
            }

            written += length;
        }

        ring.finishWrite (written);
        nextValue += static_cast<int> (written);

        std::vector<float> left (written), right (written);
        float* destination[2] = { left.data(), right.data() };

        const std::size_t got = ring.read (destination, 2, written);
        REQUIRE (got == written);

        for (std::size_t i = 0; i < got; ++i)
            CHECK (left[i] == doctest::Approx (-right[i]));
    }
}

TEST_CASE ("ring buffer never reports more than it holds")
{
    stream::StreamRing ring;
    ring.prepare (1, 128);

    // The ring enforces a floor on its size, so ask it rather than assume.
    const std::size_t capacity = ring.getCapacity();
    REQUIRE (capacity >= 128);

    const auto regions = ring.prepareWrite (capacity * 10);
    CHECK (regions.total() == capacity);

    ring.finishWrite (capacity);
    CHECK (ring.framesReady() == capacity);
    CHECK (ring.framesFree() == 0);
    CHECK (ring.prepareWrite (1).total() == 0);

    std::vector<float> buffer (capacity * 2);
    float* destination[1] = { buffer.data() };
    CHECK (ring.read (destination, 1, capacity * 2) == capacity);
    CHECK (ring.framesReady() == 0);
}

TEST_CASE ("the streamer delivers a file's contents in order")
{
    const std::string path = scratchDirectory() + "stream-source.wav";

    // A ramp makes any gap, repeat or reorder immediately visible.
    constexpr int numFrames = 40000;
    std::vector<std::vector<float>> source (1, std::vector<float> (numFrames));
    for (int i = 0; i < numFrames; ++i)
        source[0][static_cast<std::size_t> (i)] =
            static_cast<float> (i) / static_cast<float> (numFrames) * 2.0f - 1.0f;

    REQUIRE (writeWav (path, source, 44100.0));

    stream::StreamManager manager;
    StreamingSettings settings;
    settings.ringFrames   = 8192;
    settings.refillFrames = 2048;
    manager.prepare (4, 2, settings);

    stream::LoopConfig loop;
    auto* slot = manager.acquireSlot (path, 0, numFrames, loop);
    REQUIRE (slot != nullptr);

    manager.notifyStreamThread();

    // Wait for the slot to open — the real engine covers this window with the
    // preloaded head of the sample.
    for (int i = 0; i < 200 && slot->isPending(); ++i)
        std::this_thread::sleep_for (std::chrono::milliseconds (5));

    REQUIRE_FALSE (slot->isPending());

    std::vector<float> collected;
    collected.reserve (static_cast<std::size_t> (numFrames));

    std::vector<float> left (512), right (512);
    float* destination[2] = { left.data(), right.data() };

    for (int attempt = 0; attempt < 4000 && static_cast<int> (collected.size()) < numFrames; ++attempt)
    {
        const auto got = slot->read (destination, 2, 512);

        if (got == 0)
        {
            if (slot->isExhausted())
                break;

            std::this_thread::sleep_for (std::chrono::milliseconds (1));
            continue;
        }

        collected.insert (collected.end(), left.begin(), left.begin() + static_cast<std::ptrdiff_t> (got));
    }

    REQUIRE (collected.size() == static_cast<std::size_t> (numFrames));

    for (std::size_t i = 0; i < collected.size(); ++i)
        CHECK (collected[i] == doctest::Approx (source[0][i]).epsilon (0.001));

    slot->release();
    manager.shutdown();
}

TEST_CASE ("a looping stream repeats its loop region indefinitely")
{
    const std::string path = scratchDirectory() + "stream-loop.wav";

    constexpr int numFrames = 8000;
    std::vector<std::vector<float>> source (1, std::vector<float> (numFrames, 0.0f));
    for (int i = 0; i < numFrames; ++i)
        source[0][static_cast<std::size_t> (i)] = static_cast<float> (i % 100) / 100.0f;

    REQUIRE (writeWav (path, source, 44100.0));

    stream::StreamManager manager;
    StreamingSettings settings;
    settings.ringFrames   = 4096;
    settings.refillFrames = 1024;
    manager.prepare (2, 2, settings);

    stream::LoopConfig loop;
    loop.enabled = true;
    loop.start   = 1000;
    loop.end     = 2000;

    auto* slot = manager.acquireSlot (path, 1000, numFrames, loop);
    REQUIRE (slot != nullptr);
    manager.notifyStreamThread();

    for (int i = 0; i < 200 && slot->isPending(); ++i)
        std::this_thread::sleep_for (std::chrono::milliseconds (5));

    std::size_t totalRead = 0;
    std::vector<float> left (256), right (256);
    float* destination[2] = { left.data(), right.data() };

    // Far more than the loop length: a non-looping stream would run dry.
    for (int attempt = 0; attempt < 5000 && totalRead < 20000; ++attempt)
    {
        const auto got = slot->read (destination, 2, 256);
        if (got == 0)
        {
            CHECK_FALSE (slot->isExhausted());
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
            continue;
        }

        totalRead += got;
    }

    CHECK (totalRead >= 20000);

    slot->release();
    manager.shutdown();
}

TEST_CASE ("slots are recycled once released")
{
    const std::string path = scratchDirectory() + "stream-recycle.wav";

    std::vector<std::vector<float>> source (1, sine (5000, 200.0, 44100.0));
    REQUIRE (writeWav (path, source, 44100.0));

    stream::StreamManager manager;
    manager.prepare (2, 2, StreamingSettings::forProfile (StreamingProfile::live));

    stream::LoopConfig loop;

    auto* first  = manager.acquireSlot (path, 0, 5000, loop);
    auto* second = manager.acquireSlot (path, 0, 5000, loop);
    REQUIRE (first != nullptr);
    REQUIRE (second != nullptr);

    // The pool is exhausted; the engine must cope with a null slot rather than
    // waiting for one.
    CHECK (manager.acquireSlot (path, 0, 5000, loop) == nullptr);

    manager.notifyStreamThread();
    for (int i = 0; i < 200 && (first->isPending() || second->isPending()); ++i)
        std::this_thread::sleep_for (std::chrono::milliseconds (5));

    first->release();
    manager.notifyStreamThread();

    stream::StreamSlot* third = nullptr;
    for (int i = 0; i < 200 && third == nullptr; ++i)
    {
        third = manager.acquireSlot (path, 0, 5000, loop);
        if (third == nullptr)
            std::this_thread::sleep_for (std::chrono::milliseconds (5));
    }

    CHECK (third != nullptr);
    manager.shutdown();
}
