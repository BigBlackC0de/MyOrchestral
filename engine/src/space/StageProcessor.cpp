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

StagePosition StagePosition::defaultFor (Family family, int seat) noexcept
{
    // A conventional European seating plan, seen from the audience: first violins
    // front left, seconds beside them, violas centre, cellos right, basses behind
    // them; winds in front of the brass, percussion at the rear.
    struct Seat { float lateral, distance, width, height; };

    static constexpr Seat strings[]    = { { -0.55f,  6.5f, 0.75f, 0.0f },   // violins I
                                           { -0.22f,  7.5f, 0.75f, 0.0f },   // violins II
                                           {  0.12f,  8.0f, 0.70f, 0.0f },   // violas
                                           {  0.45f,  8.5f, 0.65f, 0.0f },   // cellos
                                           {  0.62f, 10.5f, 0.60f, 0.5f } }; // basses

    static constexpr Seat woodwinds[]  = { { -0.18f, 12.0f, 0.45f, 0.5f },
                                           {  0.10f, 12.5f, 0.45f, 0.5f },
                                           { -0.32f, 13.0f, 0.40f, 0.8f },
                                           {  0.26f, 13.5f, 0.40f, 0.8f } };

    static constexpr Seat brass[]      = { {  0.28f, 15.5f, 0.55f, 1.0f },
                                           { -0.30f, 16.0f, 0.55f, 1.0f },
                                           {  0.50f, 16.5f, 0.50f, 1.2f },
                                           {  0.05f, 17.0f, 0.50f, 1.2f } };

    static constexpr Seat percussion[] = { { -0.10f, 19.0f, 0.70f, 1.5f },
                                           {  0.35f, 20.0f, 0.65f, 1.5f },
                                           { -0.45f, 20.5f, 0.65f, 1.8f } };

    static constexpr Seat choir[]      = { {  0.0f,  21.0f, 0.90f, 2.0f },
                                           { -0.35f, 22.0f, 0.80f, 2.2f },
                                           {  0.35f, 22.0f, 0.80f, 2.2f } };

    static constexpr Seat other[]      = { {  0.0f,  10.0f, 0.60f, 0.0f } };

    const Seat* table = other;
    int         count = 1;

    switch (family)
    {
        case Family::strings:    table = strings;    count = 5; break;
        case Family::woodwinds:  table = woodwinds;  count = 4; break;
        case Family::brass:      table = brass;      count = 4; break;
        case Family::percussion: table = percussion; count = 3; break;
        case Family::choir:      table = choir;      count = 3; break;
        default: break;
    }

    const int index = seat < 0 ? 0 : seat % count;
    const int wraps = seat < 0 ? 0 : seat / count;

    StagePosition p;
    p.lateral  = table[index].lateral;
    p.distance = table[index].distance + static_cast<float> (wraps) * 1.5f;
    p.width    = table[index].width;
    p.height   = table[index].height;

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
