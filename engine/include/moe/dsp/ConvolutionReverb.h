#pragma once

#include "moe/dsp/Fft.h"

#include <complex>
#include <memory>
#include <string>
#include <vector>

namespace moe::dsp
{

/** Uniformly-partitioned FFT convolution, stereo in / stereo out.

    Partition size is fixed at 256 frames, which costs 256 samples of latency
    (~5 ms at 48 kHz — reported to the host so it compensates on playback, and
    small enough to stay playable live). A non-uniform partitioning scheme would
    remove that latency and cut the cost of long tails; it is the obvious next
    optimisation, not a V1 requirement.

    `loadImpulseResponse` allocates and must not be called from the audio
    thread. `process` allocates nothing. */
class ConvolutionReverb
{
public:
    ConvolutionReverb();
    ~ConvolutionReverb();

    void prepare (double sampleRate, int maximumBlockSize);

    /** Installs an impulse response given as deinterleaved channels.
        A mono IR is applied to both output channels.

        @param irSampleRate  sample rate the IR was recorded at; it is resampled
                             when it does not match the engine's
        @returns false when the IR is empty or unusable */
    bool loadImpulseResponse (const std::vector<std::vector<float>>& channels,
                              double                                irSampleRate);

    /** Loads an IR from a WAV file. Not audio-thread safe. */
    bool loadImpulseResponseFile (const std::string& path);

    /** Replaces the IR with silence — the reverb then passes dry only. */
    void clear();

    bool hasImpulseResponse() const noexcept { return numPartitions > 0; }

    /** Latency introduced, in samples. Report this to the host. */
    int getLatencySamples() const noexcept { return partitionSize; }

    /** Processes in place. `channels[0..1]` must each hold `numSamples` floats.
        The output is 100 % wet; dry/wet balance belongs to the caller. */
    void process (float* const* channels, int numSamples) noexcept;

    /** Truncates the tail, in seconds. 0 keeps the whole IR. Shorter tails cost
        proportionally less CPU, which is the main lever when the plugin is
        instantiated on a dozen tracks. */
    void setTailSeconds (float seconds) noexcept;

    float getTailSeconds() const noexcept { return tailSeconds; }

private:
    void processPartitionBlock() noexcept;
    void rebuildPartitions();

    static constexpr int partitionSize = 256;
    static constexpr int fftSize       = partitionSize * 2;

    std::unique_ptr<Fft> fft;

    double sampleRate = 44100.0;
    float  tailSeconds = 0.0f;

    /** Frequency-domain IR: [channel][partition][bin]. */
    std::vector<std::vector<std::vector<std::complex<float>>>> irSpectra;

    /** Raw IR kept so the tail length can change without reloading the file. */
    std::vector<std::vector<float>> irTimeDomain;

    /** Ring of input spectra, newest first when indexed through `spectrumWrite`. */
    std::vector<std::vector<std::complex<float>>> inputSpectra;
    int spectrumWrite = 0;
    int numPartitions = 0;

    std::vector<std::complex<float>> fftScratch;
    std::vector<std::complex<float>> accumulator;

    /** Input/output FIFOs that decouple the host block size from the partition
        size, so any buffer size works. */
    std::vector<float> inputFifo[2];
    std::vector<float> outputFifo[2];
    std::vector<float> overlap[2];
    int                fifoFill = 0;
};

} // namespace moe::dsp
