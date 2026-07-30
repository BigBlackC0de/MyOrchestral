#include "moe/dsp/Fft.h"

#include <cmath>
#include <utility>

namespace moe::dsp
{

namespace
{
constexpr double kTwoPi = 6.283185307179586476925286766559;
}

int nextPowerOfTwoOrder (std::size_t n) noexcept
{
    int order = 0;
    while ((static_cast<std::size_t> (1) << order) < n)
        ++order;

    return order;
}

Fft::Fft (int order)
{
    bits   = order < 0 ? 0 : order;
    length = static_cast<std::size_t> (1) << bits;

    bitReversal.resize (length);
    for (std::size_t i = 0; i < length; ++i)
    {
        std::size_t reversed = 0;
        for (int bit = 0; bit < bits; ++bit)
            if ((i & (static_cast<std::size_t> (1) << bit)) != 0)
                reversed |= static_cast<std::size_t> (1) << (bits - 1 - bit);

        bitReversal[i] = reversed;
    }

    // One twiddle table shared by every stage: entry k is exp(-2*pi*i*k/N).
    twiddles.resize (length / 2 == 0 ? 1 : length / 2);
    for (std::size_t k = 0; k < twiddles.size(); ++k)
    {
        const double angle = -kTwoPi * static_cast<double> (k) / static_cast<double> (length);
        twiddles[k] = { static_cast<float> (std::cos (angle)),
                        static_cast<float> (std::sin (angle)) };
    }
}

void Fft::transform (std::complex<float>* data, bool inverseTransform) const noexcept
{
    if (data == nullptr || length < 2)
        return;

    for (std::size_t i = 0; i < length; ++i)
    {
        const std::size_t j = bitReversal[i];
        if (i < j)
            std::swap (data[i], data[j]);
    }

    for (std::size_t half = 1; half < length; half *= 2)
    {
        const std::size_t step = length / (half * 2);

        for (std::size_t start = 0; start < length; start += half * 2)
        {
            for (std::size_t k = 0; k < half; ++k)
            {
                std::complex<float> twiddle = twiddles[k * step];
                if (inverseTransform)
                    twiddle = std::conj (twiddle);

                const std::complex<float> even = data[start + k];
                const std::complex<float> odd  = data[start + k + half] * twiddle;

                data[start + k]        = even + odd;
                data[start + k + half] = even - odd;
            }
        }
    }

    if (inverseTransform)
    {
        const float scale = 1.0f / static_cast<float> (length);
        for (std::size_t i = 0; i < length; ++i)
            data[i] *= scale;
    }
}

void Fft::forward (std::complex<float>* data) const noexcept
{
    transform (data, false);
}

void Fft::inverse (std::complex<float>* data) const noexcept
{
    transform (data, true);
}

} // namespace moe::dsp
