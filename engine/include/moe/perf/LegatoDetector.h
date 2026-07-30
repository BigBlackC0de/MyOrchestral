#pragma once

#include "moe/Types.h"
#include "moe/perf/MidiState.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace moe::perf
{

/** Decides whether a note-on continues a phrase or starts one.

    With a bank that has no sampled legato transitions — the normal case outside
    professional libraries — this is what separates "a section playing a line"
    from "a section playing separate notes". The result drives three things at
    once: a portamento glide from the previous pitch, a softened attack, and a
    shortened release on the note being left behind. */
class LegatoDetector
{
public:
    struct Settings
    {
        bool enabled = true;

        /** Glide time at one semitone. Wider intervals scale up, because a real
            player takes longer to cross a large interval. */
        float portamentoSeconds = 0.05f;

        /** Cap on the glide, however wide the interval. */
        float maxPortamentoSeconds = 0.18f;

        /** How much of the interval is actually glided. 1.0 is a full portamento
            (idiomatic on strings), 0.0 disables the glide while keeping the soft
            attack (better on brass and winds). */
        float portamentoAmount = 0.75f;

        /** Attack multiplier applied to a legato note. Values above 1 lengthen
            the attack, which is what makes a joined note sound bowed rather than
            re-articulated. */
        float attackScale = 2.5f;

        /** Release applied to the note being left behind, in seconds. Short, so
            the outgoing note does not smear into the new one. */
        float previousNoteRelease = 0.08f;

        /** Intervals wider than this are treated as a leap: no glide, normal
            attack. A player does not slide across two octaves. */
        int maxLegatoInterval = 12;
    };

    struct Result
    {
        bool  isLegato            = false;
        int   previousNote        = kNoNote;
        float portamentoSemitones = 0.0f;   ///< signed offset to glide from
        float portamentoSeconds   = 0.0f;
        float attackScale         = 1.0f;
    };

    void setSettings (const Settings& newSettings) noexcept { settings = newSettings; }
    const Settings& getSettings() const noexcept { return settings; }

    /** @param channel   channel state *before* the new note is registered
        @param newNote   the note being played */
    Result analyse (const ChannelState& channel, int newNote) const noexcept
    {
        Result result;

        if (! settings.enabled)
            return result;

        const int previous = channel.mostRecentHeldNote (newNote);
        if (previous == kNoNote)
            return result;

        const int interval = newNote - previous;
        if (std::abs (interval) > settings.maxLegatoInterval)
            return result;

        result.isLegato     = true;
        result.previousNote = previous;
        result.attackScale  = settings.attackScale;

        // The glide starts at the previous pitch and runs to the new one, so the
        // offset is expressed relative to the target.
        result.portamentoSemitones = -static_cast<float> (interval) * settings.portamentoAmount;

        const float scaled = settings.portamentoSeconds
                             * std::sqrt (static_cast<float> (std::abs (interval)));
        result.portamentoSeconds = std::min (scaled, settings.maxPortamentoSeconds);

        return result;
    }

private:
    Settings settings;
};

} // namespace moe::perf
