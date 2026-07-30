#include "doctest.h"

#include "TestHelpers.h"
#include "moe/audio/WavReader.h"

using namespace moe;
using namespace moe::test;

TEST_CASE ("stereo WAV round-trips through the reader")
{
    const std::string path = scratchDirectory() + "stereo.wav";

    std::vector<std::vector<float>> source (2);
    source[0] = sine (1000, 440.0, 48000.0, 0.8f);
    source[1] = sine (1000, 220.0, 48000.0, 0.4f);

    REQUIRE (writeWav (path, source, 48000.0));

    audio::WavReader reader (path);
    REQUIRE (reader.isOpen());

    CHECK (reader.info().numChannels == 2);
    CHECK (reader.info().sampleRate == doctest::Approx (48000.0));
    CHECK (reader.info().numFrames == 1000);
    CHECK (reader.info().bitsPerSample == 16);

    std::vector<float> left (1000), right (1000);
    float* destination[2] = { left.data(), right.data() };

    CHECK (reader.read (destination, 2, 0, 1000) == 1000);

    // 16-bit quantisation is the only expected difference.
    for (std::size_t i = 0; i < 1000; ++i)
    {
        CHECK (left[i]  == doctest::Approx (source[0][i]).epsilon (0.001));
        CHECK (right[i] == doctest::Approx (source[1][i]).epsilon (0.001));
    }
}

TEST_CASE ("reads from an offset and reports short reads at the end")
{
    const std::string path = scratchDirectory() + "offset.wav";

    std::vector<std::vector<float>> source (1);
    source[0] = sine (500, 100.0, 44100.0);
    REQUIRE (writeWav (path, source, 44100.0));

    audio::WavReader reader (path);
    REQUIRE (reader.isOpen());

    std::vector<float> buffer (200);
    float* destination[1] = { buffer.data() };

    CHECK (reader.read (destination, 1, 100, 200) == 200);
    CHECK (buffer[0] == doctest::Approx (source[0][100]).epsilon (0.001));

    // Asking past the end yields only what exists.
    CHECK (reader.read (destination, 1, 400, 200) == 100);
    CHECK (reader.read (destination, 1, 500, 200) == 0);
}

TEST_CASE ("mono files are duplicated into a stereo destination")
{
    const std::string path = scratchDirectory() + "mono.wav";

    std::vector<std::vector<float>> source (1);
    source[0] = sine (256, 330.0, 44100.0);
    REQUIRE (writeWav (path, source, 44100.0));

    audio::WavReader reader (path);
    REQUIRE (reader.isOpen());

    std::vector<float> left (256, 99.0f), right (256, 99.0f);
    float* destination[2] = { left.data(), right.data() };

    CHECK (reader.read (destination, 2, 0, 256) == 256);

    for (std::size_t i = 0; i < 256; ++i)
        CHECK (left[i] == doctest::Approx (right[i]));
}

TEST_CASE ("the smpl chunk supplies root note and loop points")
{
    const std::string path = scratchDirectory() + "looped.wav";

    std::vector<std::vector<float>> source (1);
    source[0] = sine (400, 220.0, 44100.0);
    REQUIRE (writeWav (path, source, 44100.0, /*rootNote*/ 57));

    audio::WavReader reader (path);
    REQUIRE (reader.isOpen());

    CHECK (reader.info().rootNote == 57);
    CHECK (reader.info().hasLoop);
    CHECK (reader.info().loopStart == 0);
    CHECK (reader.info().loopEnd == 399);
}

TEST_CASE ("a non-WAV file is rejected rather than misread")
{
    const std::string path = scratchDirectory() + "garbage.wav";

    {
        std::ofstream stream (path, std::ios::binary);
        stream << "this is definitely not a wave file, not even close";
    }

    audio::WavReader reader (path);
    CHECK_FALSE (reader.isOpen());
    CHECK_FALSE (reader.error().empty());
}

TEST_CASE ("a missing file fails cleanly")
{
    audio::WavReader reader (scratchDirectory() + "does-not-exist.wav");
    CHECK_FALSE (reader.isOpen());
}
