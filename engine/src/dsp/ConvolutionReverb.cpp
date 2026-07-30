#include "moe/dsp/ConvolutionReverb.h"

#include "moe/audio/WavReader.h"
#include "moe/dsp/Interpolation.h"

#include <algorithm>
#include <cmath>

namespace moe::dsp
{

ConvolutionReverb::ConvolutionReverb()
    : fft (std::make_unique<Fft> (nextPowerOfTwoOrder (fftSize)))
{
}

ConvolutionReverb::~ConvolutionReverb() = default;

void ConvolutionReverb::prepare (double newSampleRate, int /*maximumBlockSize*/)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;

    fftScratch.assign (fftSize, {});
    accumulator.assign (fftSize, {});

    for (int ch = 0; ch < 2; ++ch)
    {
        inputFifo[ch].assign (partitionSize, 0.0f);
        outputFifo[ch].assign (partitionSize, 0.0f);
        overlap[ch].assign (partitionSize, 0.0f);
    }

    fifoFill      = 0;
    spectrumWrite = 0;

    if (! irTimeDomain.empty())
        rebuildPartitions();
}

void ConvolutionReverb::clear()
{
    irSpectra.clear();
    irTimeDomain.clear();
    inputSpectra.clear();
    numPartitions = 0;
    spectrumWrite = 0;
    fifoFill      = 0;

    for (int ch = 0; ch < 2; ++ch)
    {
        std::fill (overlap[ch].begin(), overlap[ch].end(), 0.0f);
        std::fill (outputFifo[ch].begin(), outputFifo[ch].end(), 0.0f);
        std::fill (inputFifo[ch].begin(), inputFifo[ch].end(), 0.0f);
    }
}

void ConvolutionReverb::setTailSeconds (float seconds) noexcept
{
    if (std::abs (seconds - tailSeconds) < 1.0e-3f)
        return;

    tailSeconds = std::max (0.0f, seconds);

    if (! irTimeDomain.empty())
        rebuildPartitions();
}

bool ConvolutionReverb::loadImpulseResponse (const std::vector<std::vector<float>>& channels,
                                             double                                irSampleRate)
{
    if (channels.empty() || channels[0].empty() || irSampleRate <= 0.0)
    {
        clear();
        return false;
    }

    irTimeDomain.clear();
    irTimeDomain.resize (2);

    const double ratio = irSampleRate / sampleRate;

    for (int ch = 0; ch < 2; ++ch)
    {
        const auto& source = channels[static_cast<std::size_t> (
            std::min<std::size_t> (static_cast<std::size_t> (ch), channels.size() - 1))];

        if (std::abs (ratio - 1.0) < 1.0e-6)
        {
            irTimeDomain[static_cast<std::size_t> (ch)] = source;
            continue;
        }

        // Resampling an IR is a one-off cost, so interpolate properly rather
        // than linearly: a badly resampled IR audibly dulls the tail.
        const auto outputLength = static_cast<std::size_t> (
            static_cast<double> (source.size()) / ratio);

        auto& destination = irTimeDomain[static_cast<std::size_t> (ch)];
        destination.resize (outputLength);

        for (std::size_t i = 0; i < outputLength; ++i)
        {
            const double position = static_cast<double> (i) * ratio;
            const auto   index    = static_cast<std::ptrdiff_t> (position);
            const auto   fraction = static_cast<float> (position - static_cast<double> (index));

            auto at = [&source] (std::ptrdiff_t n) -> float
            {
                if (n < 0 || n >= static_cast<std::ptrdiff_t> (source.size()))
                    return 0.0f;
                return source[static_cast<std::size_t> (n)];
            };

            destination[i] = hermite (fraction, at (index - 1), at (index),
                                       at (index + 1), at (index + 2));
        }
    }

    // Normalise to unit energy so swapping IRs does not change the send level.
    double energy = 0.0;
    for (const auto& channel : irTimeDomain)
        for (const float value : channel)
            energy += static_cast<double> (value) * static_cast<double> (value);

    if (energy > 0.0)
    {
        const auto scale = static_cast<float> (1.0 / std::sqrt (energy));
        for (auto& channel : irTimeDomain)
            for (float& value : channel)
                value *= scale;
    }

    rebuildPartitions();
    return numPartitions > 0;
}

bool ConvolutionReverb::loadImpulseResponseFile (const std::string& path)
{
    audio::WavReader reader (path);
    if (! reader.isOpen())
    {
        clear();
        return false;
    }

    const auto& info = reader.info();
    const auto  frames = static_cast<std::size_t> (info.numFrames);

    std::vector<std::vector<float>> channels (2, std::vector<float> (frames, 0.0f));
    float* pointers[2] = { channels[0].data(), channels[1].data() };

    reader.read (pointers, 2, 0, info.numFrames);

    return loadImpulseResponse (channels, info.sampleRate);
}

void ConvolutionReverb::rebuildPartitions()
{
    irSpectra.clear();
    inputSpectra.clear();
    numPartitions = 0;
    spectrumWrite = 0;

    if (irTimeDomain.empty() || irTimeDomain[0].empty())
        return;

    std::size_t irLength = 0;
    for (const auto& channel : irTimeDomain)
        irLength = std::max (irLength, channel.size());

    if (tailSeconds > 0.0f)
        irLength = std::min (irLength,
                             static_cast<std::size_t> (tailSeconds * static_cast<float> (sampleRate)));

    if (irLength == 0)
        return;

    numPartitions = static_cast<int> ((irLength + partitionSize - 1) / partitionSize);

    irSpectra.resize (2);

    std::vector<std::complex<float>> block (fftSize);

    for (int ch = 0; ch < 2; ++ch)
    {
        const auto& source = irTimeDomain[static_cast<std::size_t> (
            std::min<std::size_t> (static_cast<std::size_t> (ch), irTimeDomain.size() - 1))];

        irSpectra[static_cast<std::size_t> (ch)].resize (static_cast<std::size_t> (numPartitions));

        for (int p = 0; p < numPartitions; ++p)
        {
            std::fill (block.begin(), block.end(), std::complex<float> {});

            const std::size_t offset = static_cast<std::size_t> (p) * partitionSize;
            for (int i = 0; i < partitionSize; ++i)
            {
                const std::size_t index = offset + static_cast<std::size_t> (i);
                if (index < source.size() && index < irLength)
                    block[static_cast<std::size_t> (i)] = { source[index], 0.0f };
            }

            fft->forward (block.data());
            irSpectra[static_cast<std::size_t> (ch)][static_cast<std::size_t> (p)] = block;
        }
    }

    inputSpectra.assign (static_cast<std::size_t> (numPartitions),
                         std::vector<std::complex<float>> (fftSize));
}

void ConvolutionReverb::processPartitionBlock() noexcept
{
    // The input FIFO holds one fresh partition; the FFT block is the previous
    // partition followed by it, which is what makes the overlap-save valid.
    // Both channels share one input spectrum: the reverb is fed a mono sum,
    // which halves the FFT work and is inaudible for a room simulation.
    auto& spectrum = inputSpectra[static_cast<std::size_t> (spectrumWrite)];

    for (int i = 0; i < partitionSize; ++i)
    {
        const float monoInput = 0.5f * (inputFifo[0][static_cast<std::size_t> (i)]
                                        + inputFifo[1][static_cast<std::size_t> (i)]);
        spectrum[static_cast<std::size_t> (i)] = { monoInput, 0.0f };
        spectrum[static_cast<std::size_t> (i + partitionSize)] = {};
    }

    fft->forward (spectrum.data());

    for (int ch = 0; ch < 2; ++ch)
    {
        std::fill (accumulator.begin(), accumulator.end(), std::complex<float> {});

        for (int p = 0; p < numPartitions; ++p)
        {
            // Partition p of the IR convolves with the input from p blocks ago.
            const int index = (spectrumWrite - p + numPartitions * 2) % numPartitions;

            const auto& in = inputSpectra[static_cast<std::size_t> (index)];
            const auto& ir = irSpectra[static_cast<std::size_t> (ch)][static_cast<std::size_t> (p)];

            for (int bin = 0; bin < fftSize; ++bin)
                accumulator[static_cast<std::size_t> (bin)] +=
                    in[static_cast<std::size_t> (bin)] * ir[static_cast<std::size_t> (bin)];
        }

        fft->inverse (accumulator.data());

        for (int i = 0; i < partitionSize; ++i)
        {
            outputFifo[ch][static_cast<std::size_t> (i)] =
                accumulator[static_cast<std::size_t> (i)].real()
                + overlap[ch][static_cast<std::size_t> (i)];

            overlap[ch][static_cast<std::size_t> (i)] =
                accumulator[static_cast<std::size_t> (i + partitionSize)].real();
        }
    }

    spectrumWrite = (spectrumWrite + 1) % numPartitions;
}

void ConvolutionReverb::process (float* const* channels, int numSamples) noexcept
{
    if (numPartitions == 0 || channels == nullptr)
    {
        // No IR: output silence rather than dry signal, since the caller mixes
        // this as a send.
        for (int ch = 0; ch < 2; ++ch)
            std::fill_n (channels[ch], numSamples, 0.0f);
        return;
    }

    int position = 0;

    while (position < numSamples)
    {
        const int chunk = std::min (partitionSize - fifoFill, numSamples - position);

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < chunk; ++i)
            {
                const float input = channels[ch][position + i];
                // Read the processed sample *before* overwriting the slot, which
                // is what gives the block its one-partition latency.
                channels[ch][position + i] = outputFifo[ch][static_cast<std::size_t> (fifoFill + i)];
                inputFifo[ch][static_cast<std::size_t> (fifoFill + i)] = input;
            }

        fifoFill += chunk;
        position += chunk;

        if (fifoFill == partitionSize)
        {
            processPartitionBlock();
            fifoFill = 0;
        }
    }
}

} // namespace moe::dsp
