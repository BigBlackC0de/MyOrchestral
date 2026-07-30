#pragma once

#include "moe/Types.h"

#include <string>

namespace moe::bank
{

enum class LoopMode
{
    noLoop = 0,     ///< play to the end
    oneShot,        ///< ignore note-off, play to the end (percussion)
    loopContinuous, ///< loop until the envelope finishes
    loopSustain     ///< loop while held, play the tail after note-off
};

enum class TriggerMode
{
    attack = 0,     ///< normal note-on
    release,        ///< fires on note-off (release samples)
    first,          ///< only when no other note of the group is held
    legato          ///< only when another note of the group is already held
};

enum class OffMode
{
    fast = 0,       ///< short fade when muted by another group
    normal          ///< follow the release envelope
};

/** One playable mapping: a sample plus the conditions under which it sounds and
    the shaping applied to it.

    Interpreted form of an SFZ `<region>`; the raw opcode text is not kept here.
    Layout is deliberately flat and trivially copyable — `RegionIndex` walks
    arrays of these on every note-on. */
struct Region
{
    int sampleId = -1;              ///< index into Instrument::samples

    // ---- selection ---------------------------------------------------------
    int loKey = 0,  hiKey = 127;
    int loVel = 1,  hiVel = 127;
    int rootKey = 60;

    int swLoKey   = -1, swHiKey  = -1;  ///< keyswitch range this region listens to
    int swLast    = -1;                 ///< required last-pressed keyswitch
    int swDefault = -1;

    Articulation articulation = Articulation::sustain;

    int seqLength   = 1;            ///< round-robin cycle length
    int seqPosition = 1;            ///< 1-based position in that cycle

    int ccNumber = -1;              ///< `locc<n>`/`hicc<n>` gate (-1 = none)
    int loCc = 0, hiCc = 127;

    TriggerMode trigger = TriggerMode::attack;

    // ---- amplitude ---------------------------------------------------------
    float volumeDb    = 0.0f;
    float pan         = 0.0f;       ///< -100 (left) .. +100 (right)
    float width       = 100.0f;     ///< stereo width, SFZ units
    float ampVelTrack = 100.0f;     ///< how much velocity drives level, in %

    /** Velocity crossfade ranges. `xfIn` fades the region in as velocity rises,
        `xfOut` fades it out. Both inactive by default. */
    int xfInLoVel = -1, xfInHiVel = -1;
    int xfOutLoVel = -1, xfOutHiVel = -1;

    /** CC crossfade — the mechanism behind CC1-driven dynamics. */
    int xfCcNumber = -1;
    int xfInLoCc = -1, xfInHiCc = -1;
    int xfOutLoCc = -1, xfOutHiCc = -1;

    // ---- pitch -------------------------------------------------------------
    int   transpose     = 0;        ///< semitones
    float tuneCents     = 0.0f;
    float pitchKeytrack = 100.0f;   ///< % of a semitone per key; 0 = unpitched

    // ---- playback window ---------------------------------------------------
    SampleIndex offset    = 0;
    SampleIndex endFrame  = -1;     ///< -1 = end of file
    LoopMode    loopMode  = LoopMode::noLoop;
    SampleIndex loopStart = 0;
    SampleIndex loopEnd   = 0;

    // ---- envelope (seconds, sustain in 0..1) -------------------------------
    float ampegDelay   = 0.0f;
    float ampegAttack  = 0.001f;
    float ampegHold    = 0.0f;
    float ampegDecay   = 0.0f;
    float ampegSustain = 1.0f;
    float ampegRelease = 0.15f;

    // ---- voice grouping ----------------------------------------------------
    int     group   = 0;            ///< exclusive group (`group=`)
    int     offBy   = 0;            ///< silences voices of that group (`off_by=`)
    OffMode offMode = OffMode::fast;

    // ---- humanisation ------------------------------------------------------
    float pitchRandomCents = 0.0f;
    float ampRandom        = 0.0f;  ///< dB of random level variation
    float delaySeconds     = 0.0f;

    bool matchesKey (int note) const noexcept  { return note >= loKey && note <= hiKey; }
    bool matchesVel (int vel)  const noexcept  { return vel  >= loVel && vel  <= hiVel; }

    /** Gain contributed by the velocity crossfade ranges, in 0..1.
        Returns 1 when no crossfade is configured. */
    float velocityCrossfadeGain (int velocity) const noexcept;

    /** Gain contributed by the CC crossfade ranges, in 0..1.
        `ccValue` is the current value of `xfCcNumber`. */
    float ccCrossfadeGain (int ccValue) const noexcept;

    /** Linear gain from velocity given `ampVelTrack`, in 0..1. */
    float velocityGain (int velocity) const noexcept;
};

} // namespace moe::bank
