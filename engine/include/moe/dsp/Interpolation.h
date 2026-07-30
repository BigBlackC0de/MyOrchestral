#pragma once

namespace moe::dsp
{

/** 4-point, 3rd-order Hermite interpolation.

    The workhorse for pitched sample playback. Compared with linear
    interpolation it removes the dull high end and the aliasing sidebands you
    hear when a sample is transposed more than a couple of semitones — audible
    immediately on sustained strings, which is exactly where this engine spends
    its time.

    @param x   fractional position in 0..1 between `y1` and `y2`
    @param y0  sample before the interval
    @param y1  sample at the start of the interval
    @param y2  sample at the end of the interval
    @param y3  sample after the interval */
inline float hermite (float x, float y0, float y1, float y2, float y3) noexcept
{
    const float c0 = y1;
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);

    return ((c3 * x + c2) * x + c1) * x + c0;
}

inline float linear (float x, float y1, float y2) noexcept
{
    return y1 + x * (y2 - y1);
}

} // namespace moe::dsp
