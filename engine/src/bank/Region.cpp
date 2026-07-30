#include "moe/bank/Region.h"

#include <algorithm>
#include <cmath>

namespace moe::bank
{

namespace
{

/** Equal-power ramp across [lo, hi]. Using sqrt rather than a linear ramp keeps
    the summed loudness of two crossfading layers roughly constant, which is what
    matters when several dynamic layers sound at once. */
float fadeIn (int value, int lo, int hi) noexcept
{
    if (lo < 0 || hi < 0 || hi <= lo)
        return value >= std::max (lo, 0) ? 1.0f : 0.0f;

    if (value <= lo) return 0.0f;
    if (value >= hi) return 1.0f;

    const float t = static_cast<float> (value - lo) / static_cast<float> (hi - lo);
    return std::sqrt (t);
}

float fadeOut (int value, int lo, int hi) noexcept
{
    if (lo < 0 || hi < 0 || hi <= lo)
        return value <= (hi >= 0 ? hi : 127) ? 1.0f : 0.0f;

    if (value <= lo) return 1.0f;
    if (value >= hi) return 0.0f;

    const float t = static_cast<float> (value - lo) / static_cast<float> (hi - lo);
    return std::sqrt (1.0f - t);
}

} // namespace

float Region::velocityCrossfadeGain (int velocity) const noexcept
{
    float gain = 1.0f;

    if (xfInLoVel >= 0 && xfInHiVel >= 0)
        gain *= fadeIn (velocity, xfInLoVel, xfInHiVel);

    if (xfOutLoVel >= 0 && xfOutHiVel >= 0)
        gain *= fadeOut (velocity, xfOutLoVel, xfOutHiVel);

    return gain;
}

float Region::ccCrossfadeGain (int ccValue) const noexcept
{
    if (xfCcNumber < 0)
        return 1.0f;

    float gain = 1.0f;

    if (xfInLoCc >= 0 && xfInHiCc >= 0)
        gain *= fadeIn (ccValue, xfInLoCc, xfInHiCc);

    if (xfOutLoCc >= 0 && xfOutHiCc >= 0)
        gain *= fadeOut (ccValue, xfOutLoCc, xfOutHiCc);

    return gain;
}

float Region::velocityGain (int velocity) const noexcept
{
    const float normalised = std::clamp (static_cast<float> (velocity) / 127.0f, 0.0f, 1.0f);
    const float track      = std::clamp (ampVelTrack / 100.0f, -1.0f, 1.0f);

    // SFZ semantics: amp_veltrack=100 means velocity 127 is full level and
    // velocity 1 is silent, following a square law that matches how the ear
    // reads dynamics better than a straight line does.
    const float curved = normalised * normalised;

    if (track >= 0.0f)
        return (1.0f - track) + track * curved;

    return (1.0f + track) - track * curved;
}

} // namespace moe::bank
