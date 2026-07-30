#pragma once

#include "moe/Types.h"

#include <cstdint>

namespace moe::humanize
{

/** Per-note micro-variation.

    Sixty players never attack together, never play exactly in tune with each
    other, and never play a note twice at the same level. A sampler that
    reproduces the same file at the same pitch at the same instant sounds
    synthetic for precisely that reason, and the effect is worst where it hurts
    most: repeated notes and unison lines.

    Amounts are deliberately small. Past roughly 8 cents and 12 ms it stops
    sounding like an ensemble and starts sounding like a sloppy one.

    The generator is a xorshift PRNG rather than `std::mt19937`: it is
    allocation-free, has no global state, and costs a handful of cycles, so it
    can be called from the audio thread on every note-on. */
class Humanizer
{
public:
    struct Settings
    {
        bool enabled = true;

        float pitchCents   = 3.0f;   ///< +/- detune per note
        float timingMs     = 6.0f;   ///< 0..timingMs of delay, never negative
        float gainDb       = 1.2f;   ///< +/- level variation

        /** Scales everything. Sections are more variable than soloists, and
            percussion should keep its timing far tighter than strings. */
        float depth = 1.0f;
    };

    struct Variation
    {
        float pitchCents   = 0.0f;
        float delaySeconds = 0.0f;
        float gainDb       = 0.0f;
    };

    void setSettings (const Settings& newSettings) noexcept { settings = newSettings; }
    const Settings& getSettings() const noexcept { return settings; }

    void seed (std::uint32_t newSeed) noexcept
    {
        state = newSeed != 0 ? newSeed : 0x9E3779B9u;
    }

    /** Sensible per-family defaults. Percussion barely moves in time — a late
        timpani stroke is a mistake, a late violin entry is an ensemble. */
    static Settings defaultsFor (Family family) noexcept
    {
        Settings s;

        switch (family)
        {
            case Family::strings:    s.pitchCents = 4.0f; s.timingMs = 8.0f;  s.gainDb = 1.5f; break;
            case Family::brass:      s.pitchCents = 3.0f; s.timingMs = 6.0f;  s.gainDb = 1.2f; break;
            case Family::woodwinds:  s.pitchCents = 3.5f; s.timingMs = 5.0f;  s.gainDb = 1.0f; break;
            case Family::percussion: s.pitchCents = 1.0f; s.timingMs = 1.5f;  s.gainDb = 2.0f; break;
            case Family::choir:      s.pitchCents = 5.0f; s.timingMs = 10.0f; s.gainDb = 1.5f; break;
            default: break;
        }

        return s;
    }

    Variation next() noexcept
    {
        Variation variation;

        if (! settings.enabled || settings.depth <= 0.0f)
            return variation;

        variation.pitchCents   = bipolar() * settings.pitchCents * settings.depth;
        variation.gainDb       = bipolar() * settings.gainDb * settings.depth;
        variation.delaySeconds = unipolar() * settings.timingMs * settings.depth * 0.001f;

        return variation;
    }

private:
    float unipolar() noexcept
    {
        // xorshift32
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;

        return static_cast<float> (state >> 8) * (1.0f / 16777216.0f);   // 0..1
    }

    float bipolar() noexcept { return unipolar() * 2.0f - 1.0f; }

    Settings      settings;
    std::uint32_t state = 0x9E3779B9u;
};

} // namespace moe::humanize
