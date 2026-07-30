#pragma once

#include <complex>
#include <cstddef>
#include <vector>

namespace moe::dsp
{

/** Iterative radix-2 complex FFT.

    Self-contained on purpose: the engine must build and be testable without
    pulling in FFTW, Accelerate or JUCE's DSP module. It is used only by the
    convolution reverb, on a background-friendly block size, so clarity is worth
    more here than the last 20 % of speed.

    Sizes must be powers of two. */
class Fft
{
public:
    /** @param order  transform size is 1 << order */
    explicit Fft (int order);

    std::size_t size() const noexcept { return length; }

    /** In-place forward transform. `data` must hold `size()` complex values. */
    void forward (std::complex<float>* data) const noexcept;

    /** In-place inverse transform, scaled by 1/N. */
    void inverse (std::complex<float>* data) const noexcept;

private:
    void transform (std::complex<float>* data, bool inverseTransform) const noexcept;

    std::size_t                      length = 0;
    int                              bits   = 0;
    std::vector<std::size_t>         bitReversal;
    std::vector<std::complex<float>> twiddles;      ///< forward twiddles, one per stage
};

/** Smallest power-of-two order such that (1 << order) >= n. */
int nextPowerOfTwoOrder (std::size_t n) noexcept;

} // namespace moe::dsp
