#pragma once

#include "moe/Types.h"
#include "moe/dsp/OnePole.h"
#include "moe/dsp/SmoothedValue.h"

#include <algorithm>
#include <array>
#include <vector>

namespace moe::space
{

/** Where a section sits on the stage, in metres, relative to the listener at
    the origin looking down the +Y axis. */
struct StagePosition
{
    float distance = 8.0f;      ///< 3 (front desk) .. 25 (back of the hall)
    float lateral  = 0.0f;      ///< -1 (hard left) .. +1 (hard right)
    float width    = 0.6f;      ///< how wide the section spreads, 0..1
    float height   = 0.0f;      ///< riser height in metres; affects early reflections only

    /** Seating that matches a standard symphonic layout, viewed from the
        audience. Used as the default when a bank is loaded and its family is
        known — it is what makes a freshly loaded orchestra sit correctly without
        the user touching a single pan control.

        `seat` distinguishes desks of the same family: seat 0 of the strings is
        where first violins sit, seat 1 the seconds, and so on. Without it every
        string section would land on the same spot, which is both wrong and
        useless on the stage view. Seats beyond the table wrap around and step
        further back. */
    static StagePosition defaultFor (Family family, int seat = 0) noexcept;
};

/** Places a section in a virtual hall.

    Three cues, in order of how much they contribute:

      1. **Pre-delay** — sound from the back of the stage arrives later. This is
         the cue that actually reads as depth; level alone reads as "quieter",
         not "further away".
      2. **Air absorption** — distance rolls off the top end. Subtle, but it is
         what stops distant instruments sounding like near ones turned down.
      3. **Early reflections** — a handful of taps whose spacing follows the
         source position. They give the section its own place in the room before
         the reverb tail arrives.

    Deliberately not a full room simulation: the convolution reverb supplies the
    hall, this supplies the seating within it. */
class StageProcessor
{
public:
    void prepare (double sampleRate, int maximumBlockSize);
    void reset();

    void setPosition (const StagePosition& newPosition) noexcept;
    const StagePosition& getPosition() const noexcept { return position; }

    /** Overall dry level for the section, linear. */
    void setGain (float linearGain) noexcept { gain.setTarget (linearGain); }

    /** How much of the section is sent to the shared reverb, 0..1. */
    void setReverbSend (float amount) noexcept { reverbSend = amount; }
    float getReverbSend() const noexcept { return reverbSend; }

    /** Processes the section's stereo bus in place, and adds the send signal
        into `reverbBus` (which may be null). */
    void process (float* const* channels, float* const* reverbBus, int numSamples) noexcept;

private:
    void updateDerivedValues();

    static constexpr int   kNumEarlyReflections = 6;
    static constexpr float kSpeedOfSound        = 343.0f;   ///< m/s
    static constexpr float kMaxDelaySeconds     = 0.25f;

    struct DelayLine
    {
        std::vector<float> buffer;
        int                writeIndex = 0;

        void prepare (int numSamples)
        {
            buffer.assign (static_cast<std::size_t> (std::max (numSamples, 2)), 0.0f);
            writeIndex = 0;
        }

        void reset() { std::fill (buffer.begin(), buffer.end(), 0.0f); writeIndex = 0; }

        void write (float value) noexcept
        {
            buffer[static_cast<std::size_t> (writeIndex)] = value;
            writeIndex = (writeIndex + 1) % static_cast<int> (buffer.size());
        }

        float readDelayed (int samples) const noexcept
        {
            const auto size = static_cast<int> (buffer.size());
            int index = writeIndex - 1 - std::min (samples, size - 1);
            if (index < 0)
                index += size;

            return buffer[static_cast<std::size_t> (index)];
        }
    };

    StagePosition position;
    double        sampleRate = 44100.0;

    std::array<DelayLine, 2> delays;
    dsp::OnePole             airFilter;
    dsp::SmoothedValue       gain;

    int   directDelaySamples = 0;
    float distanceGain       = 1.0f;
    float panLeft            = 0.70710678f;
    float panRight           = 0.70710678f;
    float reverbSend         = 0.25f;

    std::array<int, kNumEarlyReflections>   reflectionDelays {};
    std::array<float, kNumEarlyReflections> reflectionGains {};
    std::array<int, kNumEarlyReflections>   reflectionChannel {};
};

} // namespace moe::space
