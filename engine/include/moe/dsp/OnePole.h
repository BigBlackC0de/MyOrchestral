#pragma once

#include <algorithm>
#include <cmath>

namespace moe::dsp
{

/** One-pole low-pass, used as a timbre control rather than as a filter proper.

    A real instrument does not simply get louder from pp to ff — its spectrum
    opens up. On a bank with several dynamic layers the samples carry that
    themselves; on a bank with one or two, tying a gentle low-pass to the
    dynamics control is what stops a crescendo sounding like a fader move.
    Deliberately mild: 6 dB/octave, never fully closed. */
class OnePole
{
public:
    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
        reset();
    }

    void reset() noexcept
    {
        state[0] = state[1] = 0.0f;
    }

    void setCutoff (float hertz) noexcept
    {
        const float nyquist = static_cast<float> (sampleRate) * 0.5f;
        const float clamped = std::clamp (hertz, 20.0f, nyquist * 0.99f);
        coefficient = 1.0f - std::exp (-2.0f * 3.14159265358979f * clamped / static_cast<float> (sampleRate));
    }

    float processChannel (int channel, float input) noexcept
    {
        float& z = state[channel & 1];
        z += coefficient * (input - z);
        return z;
    }

private:
    double sampleRate  = 44100.0;
    float  coefficient = 1.0f;
    float  state[2]    = { 0.0f, 0.0f };
};

} // namespace moe::dsp
