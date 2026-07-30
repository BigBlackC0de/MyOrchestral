#include "doctest.h"

#include "TestHelpers.h"
#include "moe/dsp/ConvolutionReverb.h"
#include "moe/dsp/Fft.h"
#include "moe/dsp/Interpolation.h"
#include "moe/dsp/SmoothedValue.h"
#include "moe/voice/Envelope.h"

#include <random>

using namespace moe;
using namespace moe::test;

TEST_CASE ("FFT round-trips to the original signal")
{
    constexpr int order = 8;
    dsp::Fft fft (order);

    REQUIRE (fft.size() == 256);

    std::mt19937 rng (12345);
    std::uniform_real_distribution<float> distribution (-1.0f, 1.0f);

    std::vector<std::complex<float>> data (256);
    std::vector<std::complex<float>> original (256);

    for (std::size_t i = 0; i < data.size(); ++i)
    {
        data[i] = { distribution (rng), 0.0f };
        original[i] = data[i];
    }

    fft.forward (data.data());
    fft.inverse (data.data());

    for (std::size_t i = 0; i < data.size(); ++i)
        CHECK (data[i].real() == doctest::Approx (original[i].real()).epsilon (0.0001));
}

TEST_CASE ("FFT of a pure tone puts the energy in one bin")
{
    dsp::Fft fft (6);   // 64 points
    std::vector<std::complex<float>> data (64);

    constexpr int binIndex = 5;
    for (std::size_t i = 0; i < data.size(); ++i)
    {
        const double angle = 2.0 * 3.14159265358979 * binIndex * static_cast<double> (i) / 64.0;
        data[i] = { static_cast<float> (std::cos (angle)), 0.0f };
    }

    fft.forward (data.data());

    // A real cosine splits between bin k and bin N-k, each with magnitude N/2.
    CHECK (std::abs (data[binIndex]) == doctest::Approx (32.0f).epsilon (0.01));
    CHECK (std::abs (data[64 - binIndex]) == doctest::Approx (32.0f).epsilon (0.01));

    for (std::size_t i = 0; i < data.size(); ++i)
        if (i != binIndex && i != 64 - binIndex)
            CHECK (std::abs (data[i]) < 0.01f);
}

TEST_CASE ("partitioned convolution matches direct convolution")
{
    dsp::ConvolutionReverb reverb;
    reverb.prepare (48000.0, 512);

    // A short, sparse impulse response makes the expected output easy to state.
    std::vector<std::vector<float>> ir (2, std::vector<float> (600, 0.0f));
    ir[0][0]   = 1.0f;
    ir[0][100] = 0.5f;
    ir[0][417] = -0.25f;
    ir[1] = ir[0];

    REQUIRE (reverb.loadImpulseResponse (ir, 48000.0));

    constexpr int numSamples = 2048;
    std::vector<float> left (numSamples, 0.0f), right (numSamples, 0.0f);
    left[0] = right[0] = 1.0f;   // unit impulse in

    float* channels[2] = { left.data(), right.data() };
    reverb.process (channels, numSamples);

    // The IR is energy-normalised, so compare the *shape* rather than absolute
    // values: the taps must land at the right offsets with the right ratios.
    const int latency = reverb.getLatencySamples();

    const float direct = left[latency];
    REQUIRE (std::abs (direct) > 1.0e-4f);

    CHECK (left[latency + 100] / direct == doctest::Approx (0.5f).epsilon (0.01));
    CHECK (left[latency + 417] / direct == doctest::Approx (-0.25f).epsilon (0.01));

    // Everything between the taps must be silent.
    CHECK (std::abs (left[latency + 50])  < std::abs (direct) * 0.001f);
    CHECK (std::abs (left[latency + 300]) < std::abs (direct) * 0.001f);
}

TEST_CASE ("convolution handles any host block size")
{
    dsp::ConvolutionReverb reverb;
    reverb.prepare (44100.0, 1024);

    std::vector<std::vector<float>> ir (2, std::vector<float> (300, 0.0f));
    ir[0][0] = 1.0f;
    ir[1][0] = 1.0f;
    REQUIRE (reverb.loadImpulseResponse (ir, 44100.0));

    // Odd, prime-ish block sizes are exactly where a FIFO bug shows up.
    for (const int blockSize : { 1, 7, 63, 64, 127, 333, 1024 })
    {
        std::vector<float> left (blockSize, 0.25f), right (blockSize, 0.25f);
        float* channels[2] = { left.data(), right.data() };

        CHECK_NOTHROW (reverb.process (channels, blockSize));

        for (float value : left)
            CHECK (std::isfinite (value));
    }
}

TEST_CASE ("with no impulse response the reverb outputs silence, not dry signal")
{
    dsp::ConvolutionReverb reverb;
    reverb.prepare (48000.0, 256);

    std::vector<float> left (256, 0.7f), right (256, 0.7f);
    float* channels[2] = { left.data(), right.data() };

    reverb.process (channels, 256);

    CHECK (peakOf (left.data(), 256) == doctest::Approx (0.0f));
}

TEST_CASE ("Hermite interpolation passes through its control points")
{
    CHECK (dsp::hermite (0.0f, 1.0f, 2.0f, 3.0f, 4.0f) == doctest::Approx (2.0f));
    CHECK (dsp::hermite (1.0f, 1.0f, 2.0f, 3.0f, 4.0f) == doctest::Approx (3.0f));

    // On a straight line it must stay on the line.
    CHECK (dsp::hermite (0.5f, 0.0f, 1.0f, 2.0f, 3.0f) == doctest::Approx (1.5f));
}

TEST_CASE ("smoothed values approach their target without overshooting")
{
    dsp::SmoothedValue value;
    value.reset (48000.0, 0.01);
    value.setCurrentAndTarget (0.0f);
    value.setTarget (1.0f);

    float previous = 0.0f;
    for (int i = 0; i < 48000; ++i)
    {
        const float current = value.next();
        CHECK (current >= previous - 1.0e-6f);   // monotonic
        CHECK (current <= 1.0f + 1.0e-6f);       // never overshoots
        previous = current;
    }

    CHECK (previous == doctest::Approx (1.0f).epsilon (0.001));
    CHECK_FALSE (value.isSmoothing());
}

TEST_CASE ("envelope stages run in order and reach silence")
{
    voice::Envelope envelope;
    envelope.prepare (1000.0);   // 1 ms per sample keeps the arithmetic readable

    voice::Envelope::Parameters parameters;
    parameters.attack  = 0.010f;   // 10 samples
    parameters.decay   = 0.020f;   // 20 samples
    parameters.sustain = 0.5f;
    parameters.release = 0.010f;

    envelope.start (parameters);
    CHECK (envelope.isActive());

    float peak = 0.0f;
    for (int i = 0; i < 10; ++i)
        peak = std::fmax (peak, envelope.next());

    CHECK (peak == doctest::Approx (1.0f).epsilon (0.05));

    for (int i = 0; i < 20; ++i)
        envelope.next();

    CHECK (envelope.currentLevel() == doctest::Approx (0.5f).epsilon (0.05));
    CHECK (envelope.currentStage() == voice::Envelope::Stage::sustain);

    envelope.noteOff();
    CHECK (envelope.isReleasing());

    for (int i = 0; i < 200 && envelope.isActive(); ++i)
        envelope.next();

    CHECK_FALSE (envelope.isActive());
    CHECK (envelope.currentLevel() == doctest::Approx (0.0f));
}

TEST_CASE ("a one-shot envelope with zero sustain ends on its own")
{
    voice::Envelope envelope;
    envelope.prepare (1000.0);

    voice::Envelope::Parameters parameters;
    parameters.attack  = 0.001f;
    parameters.decay   = 0.010f;
    parameters.sustain = 0.0f;

    envelope.start (parameters);

    for (int i = 0; i < 500 && envelope.isActive(); ++i)
        envelope.next();

    CHECK_FALSE (envelope.isActive());
}
