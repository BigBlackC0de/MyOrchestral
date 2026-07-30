#include "moe/space/StageProcessor.h"

#include <algorithm>
#include <cmath>

namespace moe::space
{

namespace
{
constexpr float kPi = 3.14159265358979f;

/** Reference distance at which a section plays at unit gain. */
constexpr float kReferenceDistance = 6.0f;
}

StagePosition StagePosition::defaultFor (Family family) noexcept
{
    StagePosition p;

    // A conventional European seating plan, seen from the audience: first violins
    // front left, seconds and violas centre-left through centre, cellos right,
    // basses behind them, winds and brass further back, percussion at the rear.
    switch (family)
    {
        case Family::strings:    p.distance = 7.0f;  p.lateral = -0.35f; p.width = 0.75f; break;
        case Family::woodwinds:  p.distance = 12.0f; p.lateral =  0.05f; p.width = 0.45f; p.height = 0.5f; break;
        case Family::brass:      p.distance = 15.0f; p.lateral =  0.30f; p.width = 0.55f; p.height = 1.0f; break;
        case Family::percussion: p.distance = 19.0f; p.lateral = -0.15f; p.width = 0.70f; p.height = 1.5f; break;
        case Family::choir:      p.distance = 21.0f; p.lateral =  0.0f;  p.width = 0.90f; p.height = 2.0f; break;
        default:                 p.distance = 10.0f; p.lateral =  0.0f;  p.width = 0.60f; break;
    }

    return p;
}

void StageProcessor::prepare (double newSampleRate, int /*maximumBlockSize*/)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;

    const int maxDelay = static_cast<int> (kMaxDelaySeconds * static_cast<float> (sampleRate)) + 4;

    for (auto& delay : delays)
        delay.prepare (maxDelay);

    airFilter.prepare (sampleRate);
    gain.reset (sampleRate, 0.02);
    gain.setCurrentAndTarget (1.0f);

    updateDerivedValues();
}

void StageProcessor::reset()
{
    for (auto& delay : delays)
        delay.reset();

    airFilter.reset();
}

void StageProcessor::setPosition (const StagePosition& newPosition) noexcept
{
    position = newPosition;
    position.distance = std::clamp (position.distance, 1.0f, 30.0f);
    position.lateral  = std::clamp (position.lateral, -1.0f, 1.0f);
    position.width    = std::clamp (position.width, 0.0f, 1.0f);

    updateDerivedValues();
}

void StageProcessor::updateDerivedValues()
{
    const float distance = position.distance;

    directDelaySamples = static_cast<int> (distance / kSpeedOfSound * static_cast<float> (sampleRate));
    directDelaySamples = std::clamp (directDelaySamples, 0,
                                     static_cast<int> (kMaxDelaySeconds * static_cast<float> (sampleRate)) - 1);

    // Inverse-distance law, softened: a strict 1/d makes the back of the stage
    // disappear, because a real hall's reverberant field props it back up.
    distanceGain = kReferenceDistance / (kReferenceDistance + distance * 0.6f);

    // Air absorption: gentle, and never below 4 kHz — beyond that it stops
    // reading as distance and starts reading as a blanket over the speakers.
    airFilter.setCutoff (std::max (4000.0f, 20000.0f - distance * 520.0f));

    const float angle = (std::clamp (position.lateral, -1.0f, 1.0f) + 1.0f) * 0.25f * kPi;
    panLeft  = std::cos (angle);
    panRight = std::sin (angle);

    // Early reflections: prime-ish spacings so the taps never comb-filter into a
    // pitched resonance, spreading with distance and alternating across channels.
    static constexpr float baseTimesMs[kNumEarlyReflections] = { 11.0f, 17.0f, 23.0f, 31.0f, 41.0f, 53.0f };

    for (int i = 0; i < kNumEarlyReflections; ++i)
    {
        const float spread = 1.0f + distance * 0.04f + position.height * 0.05f;
        const float timeMs = baseTimesMs[i] * spread;

        reflectionDelays[static_cast<std::size_t> (i)] =
            directDelaySamples
            + static_cast<int> (timeMs * 0.001f * static_cast<float> (sampleRate));

        reflectionDelays[static_cast<std::size_t> (i)] = std::min (
            reflectionDelays[static_cast<std::size_t> (i)],
            static_cast<int> (kMaxDelaySeconds * static_cast<float> (sampleRate)) - 1);

        // Later reflections are quieter; a wider section throws more of them.
        const float decay = std::pow (0.72f, static_cast<float> (i));
        reflectionGains[static_cast<std::size_t> (i)] = 0.22f * decay * (0.4f + position.width);

        reflectionChannel[static_cast<std::size_t> (i)] = i % 2;
    }
}

void StageProcessor::process (float* const* channels, float* const* reverbBus, int numSamples) noexcept
{
    if (channels == nullptr)
        return;

    // Mid/side width control. Narrowing a section is what lets a solo instrument
    // sit at a point in the image while a section spreads across it.
    const float sideAmount = position.width;

    for (int i = 0; i < numSamples; ++i)
    {
        const float inLeft  = channels[0][i];
        const float inRight = channels[1][i];

        const float mid  = 0.5f * (inLeft + inRight);
        const float side = 0.5f * (inLeft - inRight) * sideAmount;

        delays[0].write (mid + side);
        delays[1].write (mid - side);

        float left  = delays[0].readDelayed (directDelaySamples);
        float right = delays[1].readDelayed (directDelaySamples);

        for (int r = 0; r < kNumEarlyReflections; ++r)
        {
            const int   tap     = reflectionDelays[static_cast<std::size_t> (r)];
            const float tapGain = reflectionGains[static_cast<std::size_t> (r)];
            const int   channel = reflectionChannel[static_cast<std::size_t> (r)];

            // Cross-feeding the taps is what widens the section rather than
            // simply thickening one side of it.
            const float tapValue = delays[static_cast<std::size_t> (channel)].readDelayed (tap);

            if (channel == 0) { left += tapValue * tapGain; right += tapValue * tapGain * 0.6f; }
            else              { right += tapValue * tapGain; left  += tapValue * tapGain * 0.6f; }
        }

        left  = airFilter.processChannel (0, left);
        right = airFilter.processChannel (1, right);

        const float sectionGain = gain.next() * distanceGain;

        left  *= sectionGain * panLeft  * 1.41421356f;
        right *= sectionGain * panRight * 1.41421356f;

        channels[0][i] = left;
        channels[1][i] = right;

        if (reverbBus != nullptr)
        {
            // Distant sections are wetter: that relationship between distance and
            // wet/dry ratio is a strong depth cue on its own.
            const float sendAmount = reverbSend * (0.6f + position.distance * 0.03f);
            reverbBus[0][i] += left  * sendAmount;
            reverbBus[1][i] += right * sendAmount;
        }
    }
}

} // namespace moe::space
