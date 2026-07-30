#include "moe/bank/RegionSelector.h"

#include <algorithm>
#include <cmath>

namespace moe::bank
{

namespace
{

constexpr float kInaudible = 1.0e-4f;   ///< -80 dB; below this a layer costs a voice for nothing

bool passesKeyswitch (const Region& region, int lastKeyswitch) noexcept
{
    if (region.swLast < 0)
        return true;

    // A region gated on a keyswitch that has never been pressed still plays if
    // the bank named it as the default.
    const int effective = lastKeyswitch >= 0 ? lastKeyswitch : region.swDefault;
    return effective == region.swLast;
}

bool passesCcGate (const Region& region, const int* ccValues) noexcept
{
    if (region.ccNumber < 0 || region.ccNumber >= kNumMidiCcs)
        return true;

    const int value = ccValues != nullptr ? ccValues[region.ccNumber] : 0;
    return value >= region.loCc && value <= region.hiCc;
}

bool passesRoundRobin (const Region& region, std::uint32_t counter) noexcept
{
    if (region.seqLength <= 1)
        return true;

    const auto position = static_cast<int> (counter % static_cast<std::uint32_t> (region.seqLength));
    return position == (region.seqPosition - 1);
}

} // namespace

int selectRegions (const Instrument&       instrument,
                   const SelectionContext& context,
                   SelectionState&         state,
                   SelectedRegion*         out,
                   int                     maxOut) noexcept
{
    if (out == nullptr || maxOut <= 0)
        return 0;

    const auto candidates = instrument.candidatesForKey (context.note);
    if (candidates.empty())
        return 0;

    const auto key     = static_cast<std::size_t> (std::clamp (context.note, 0, 127));
    const auto counter = state.roundRobinCounter[key];

    // Two passes: the requested articulation first, then — only if nothing was
    // found — anything else. A bank that lacks `spiccato` should still speak
    // when the user selects it rather than going silent.
    int count = 0;

    for (int pass = 0; pass < 2 && count == 0; ++pass)
    {
        const bool strictArticulation = (pass == 0);

        if (! strictArticulation && ! context.allowArticulationFallback)
            break;

        for (const int regionIndex : candidates)
        {
            const Region& region = instrument.regions[static_cast<std::size_t> (regionIndex)];

            if (region.trigger != context.trigger)
                continue;

            if (strictArticulation && region.articulation != context.articulation)
                continue;

            if (! region.matchesVel (context.velocity))
                continue;

            if (! passesKeyswitch (region, context.lastKeyswitch))
                continue;

            if (! passesCcGate (region, context.ccValues))
                continue;

            if (! passesRoundRobin (region, counter))
                continue;

            float gain = region.velocityGain (context.velocity)
                         * region.velocityCrossfadeGain (context.velocity);

            if (region.xfCcNumber >= 0 && region.xfCcNumber < kNumMidiCcs)
            {
                const int ccValue = context.ccValues != nullptr
                                        ? context.ccValues[region.xfCcNumber]
                                        : 0;
                gain *= region.ccCrossfadeGain (ccValue);
            }

            if (gain <= kInaudible)
                continue;

            out[count].regionIndex = regionIndex;
            out[count].gain        = gain;

            if (++count >= maxOut)
                break;
        }
    }

    // Advance the round-robin only when the note actually produced something,
    // so a silent selection does not desynchronise the cycle.
    if (count > 0 && context.trigger == TriggerMode::attack)
        state.roundRobinCounter[key] = counter + 1;

    return count;
}

} // namespace moe::bank
