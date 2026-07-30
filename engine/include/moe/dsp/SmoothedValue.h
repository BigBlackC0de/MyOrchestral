#pragma once

#include <cmath>

namespace moe::dsp
{

/** One-pole smoother for control-rate values reaching the audio path.

    Any gain, pan or pitch offset that a user or a MIDI CC can jump is routed
    through one of these. Without it, moving CC1 during a held chord produces a
    step in the waveform, which is heard as a click. */
class SmoothedValue
{
public:
    void reset (double sampleRate, double timeConstantSeconds) noexcept
    {
        coefficient = (sampleRate > 0.0 && timeConstantSeconds > 0.0)
                          ? static_cast<float> (std::exp (-1.0 / (sampleRate * timeConstantSeconds)))
                          : 0.0f;
    }

    void setCurrentAndTarget (float value) noexcept
    {
        current = target = value;
    }

    void setTarget (float value) noexcept { target = value; }

    float getTarget()  const noexcept { return target; }
    float getCurrent() const noexcept { return current; }

    bool isSmoothing() const noexcept { return current != target; }

    float next() noexcept
    {
        if (current == target)
            return current;

        // A one-pole approaches its target asymptotically, and in single
        // precision it eventually stalls short of it: the increment rounds away
        // to nothing and the value stops moving. Detecting that the update made
        // no progress — rather than guessing a threshold, which depends on the
        // coefficient — is what lets the smoother actually settle, and therefore
        // what lets `isSmoothing` ever return false. The final jump is at worst
        // a fraction of a dB below -90 dBFS.
        const float updated = target - (target - current) * coefficient;
        current = (updated == current) ? target : updated;
        return current;
    }

    /** Skips ahead by `numSamples` without producing them. Used when a voice is
        silent but its controls must stay coherent. */
    void skip (int numSamples) noexcept
    {
        if (numSamples <= 0 || current == target)
            return;

        const float updated = target - (target - current)
                                       * std::pow (coefficient, static_cast<float> (numSamples));
        current = (updated == current) ? target : updated;
    }

private:

    float current     = 0.0f;
    float target      = 0.0f;
    float coefficient = 0.0f;
};

} // namespace moe::dsp
