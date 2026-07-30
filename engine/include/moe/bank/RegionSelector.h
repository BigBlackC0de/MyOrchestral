#pragma once

#include "moe/bank/Instrument.h"

#include <array>

namespace moe::bank
{

/** Per-instrument mutable state that selection needs but the (immutable)
    Instrument cannot hold: round-robin counters.

    Counters are kept **per key**, not globally. Repeating the same note is the
    case where a missing round-robin is most audible — the "machine-gun" effect —
    so cycling per key guarantees consecutive repeats never reuse the same
    sample, which a shared counter does not. */
struct SelectionState
{
    std::array<std::uint32_t, 128> roundRobinCounter {};

    void reset() noexcept { roundRobinCounter.fill (0); }
};

struct SelectionContext
{
    int          note        = 60;
    int          velocity    = 100;
    Articulation articulation = Articulation::sustain;
    TriggerMode  trigger     = TriggerMode::attack;

    int lastKeyswitch = -1;             ///< most recently pressed keyswitch note

    const int* ccValues = nullptr;      ///< 128 entries, or nullptr for defaults

    /** When true, regions whose articulation differs from `articulation` are
        accepted as a fallback if the requested articulation yields nothing.
        Keeps a bank with sparse articulation coverage playable. */
    bool allowArticulationFallback = true;
};

struct SelectedRegion
{
    int   regionIndex = -1;
    float gain        = 1.0f;   ///< combined velocity / crossfade gain, linear
};

/** Picks the regions that should sound for one note-on.

    Several regions may be returned: overlapping dynamic layers are the mechanism
    behind CC1-driven expression, and a bank may deliberately stack a body sample
    with a noise/attack layer.

    Allocation-free — writes into the caller's buffer. Safe on the audio thread.

    @returns number of entries written to `out` (never more than `maxOut`) */
int selectRegions (const Instrument&       instrument,
                   const SelectionContext& context,
                   SelectionState&         state,
                   SelectedRegion*         out,
                   int                     maxOut) noexcept;

} // namespace moe::bank
