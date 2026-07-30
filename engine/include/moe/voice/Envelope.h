#pragma once

#include <algorithm>
#include <cmath>

namespace moe::voice
{

/** DAHDSR amplitude envelope with exponential segments.

    Exponential rather than linear because a linear decay on a bowed string
    sounds like a fader being pulled, not like an instrument stopping. Segment
    rates are precomputed so `next()` is two operations.

    The envelope never reaches its target exactly; each segment ends on a count,
    which keeps timings exact regardless of the curve. */
class Envelope
{
public:
    struct Parameters
    {
        float delay   = 0.0f;   ///< seconds before the attack starts
        float attack  = 0.001f;
        float hold    = 0.0f;
        float decay   = 0.0f;
        float sustain = 1.0f;   ///< 0..1
        float release = 0.15f;
    };

    enum class Stage { idle, delay, attack, hold, decay, sustain, release };

    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    }

    void start (const Parameters& p) noexcept
    {
        parameters = p;
        level      = 0.0f;
        stage      = parameters.delay > 0.0f ? Stage::delay : Stage::attack;
        counter    = samplesFor (parameters.delay > 0.0f ? parameters.delay : parameters.attack);

        if (stage == Stage::attack)
            beginAttack();
    }

    void noteOff() noexcept
    {
        if (stage == Stage::idle || stage == Stage::release)
            return;

        stage       = Stage::release;
        counter     = samplesFor (parameters.release);
        releaseCoef = coefficientFor (parameters.release);
    }

    /** Immediate but click-free stop, used for voice stealing and `off_by`. */
    void fastRelease (float seconds = 0.005f) noexcept
    {
        stage       = Stage::release;
        counter     = samplesFor (seconds);
        releaseCoef = coefficientFor (seconds);
    }

    bool isActive() const noexcept { return stage != Stage::idle; }
    bool isReleasing() const noexcept { return stage == Stage::release; }
    Stage currentStage() const noexcept { return stage; }
    float currentLevel() const noexcept { return level; }

    float next() noexcept
    {
        switch (stage)
        {
            case Stage::idle:
                return 0.0f;

            case Stage::delay:
                if (--counter <= 0)
                {
                    stage = Stage::attack;
                    beginAttack();
                }
                return 0.0f;

            case Stage::attack:
                // Aims past 1.0 so the curve through the audible range stays
                // brisk instead of crawling asymptotically toward the target.
                level = attackTarget + (level - attackTarget) * attackCoef;
                if (--counter <= 0)
                {
                    level = 1.0f;
                    stage = parameters.hold > 0.0f ? Stage::hold : Stage::decay;
                    counter = samplesFor (stage == Stage::hold ? parameters.hold : parameters.decay);
                    if (stage == Stage::decay)
                        decayCoef = coefficientFor (parameters.decay);
                }
                return std::min (level, 1.0f);

            case Stage::hold:
                if (--counter <= 0)
                {
                    stage     = Stage::decay;
                    counter   = samplesFor (parameters.decay);
                    decayCoef = coefficientFor (parameters.decay);
                }
                return level;

            case Stage::decay:
                level = parameters.sustain + (level - parameters.sustain) * decayCoef;
                if (--counter <= 0)
                {
                    level = parameters.sustain;
                    stage = parameters.sustain > 0.0f ? Stage::sustain : Stage::idle;
                }
                return level;

            case Stage::sustain:
                return level;

            case Stage::release:
                level *= releaseCoef;
                if (--counter <= 0 || level < 1.0e-5f)
                {
                    level = 0.0f;
                    stage = Stage::idle;
                }
                return level;
        }

        return 0.0f;
    }

private:
    int samplesFor (float seconds) const noexcept
    {
        return std::max (1, static_cast<int> (seconds * static_cast<float> (sampleRate)));
    }

    /** One-pole coefficient reaching ~99.3 % of the target over the segment. */
    float coefficientFor (float seconds) const noexcept
    {
        const float samples = std::max (1.0f, seconds * static_cast<float> (sampleRate));
        return std::exp (-5.0f / samples);
    }

    void beginAttack() noexcept
    {
        counter    = samplesFor (parameters.attack);
        attackCoef = coefficientFor (parameters.attack);
    }

    double     sampleRate  = 44100.0;
    Parameters parameters;

    Stage stage   = Stage::idle;
    int   counter = 0;
    float level   = 0.0f;

    static constexpr float attackTarget = 1.2f;

    float attackCoef  = 0.0f;
    float decayCoef   = 0.0f;
    float releaseCoef = 0.0f;
};

} // namespace moe::voice
