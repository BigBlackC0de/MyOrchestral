#include "moe/bank/Instrument.h"

#include <algorithm>

namespace moe::bank
{

void Instrument::buildIndex()
{
    indexPool.clear();
    indexRanges.fill ({ 0, 0 });
    keyswitchFlags.fill (false);
    articulations.clear();

    lowestKey  = 127;
    highestKey = 0;

    // Counting sort by key: one pass to size the buckets, one to fill them.
    std::array<int, 128> counts {};
    counts.fill (0);

    for (const auto& region : regions)
    {
        const int lo = std::clamp (region.loKey, 0, 127);
        const int hi = std::clamp (region.hiKey, 0, 127);

        for (int key = lo; key <= hi; ++key)
            ++counts[static_cast<std::size_t> (key)];

        lowestKey  = std::min (lowestKey, lo);
        highestKey = std::max (highestKey, hi);

        if (std::find (articulations.begin(), articulations.end(), region.articulation)
            == articulations.end())
            articulations.push_back (region.articulation);
    }

    int offset = 0;
    for (int key = 0; key < 128; ++key)
    {
        indexRanges[static_cast<std::size_t> (key)] = { offset, 0 };
        offset += counts[static_cast<std::size_t> (key)];
    }

    indexPool.resize (static_cast<std::size_t> (offset));

    for (std::size_t r = 0; r < regions.size(); ++r)
    {
        const auto& region = regions[r];
        const int lo = std::clamp (region.loKey, 0, 127);
        const int hi = std::clamp (region.hiKey, 0, 127);

        for (int key = lo; key <= hi; ++key)
        {
            auto& range = indexRanges[static_cast<std::size_t> (key)];
            indexPool[static_cast<std::size_t> (range.first + range.second)] = static_cast<int> (r);
            ++range.second;
        }
    }

    for (const auto& ks : keyswitches)
        if (ks.note >= 0 && ks.note < 128)
            keyswitchFlags[static_cast<std::size_t> (ks.note)] = true;

    std::sort (articulations.begin(), articulations.end());

    if (regions.empty())
    {
        lowestKey  = 0;
        highestKey = 0;
    }
}

Instrument::View Instrument::candidatesForKey (int note) const noexcept
{
    if (note < 0 || note > 127 || indexPool.empty())
        return {};

    const auto& range = indexRanges[static_cast<std::size_t> (note)];
    if (range.second <= 0)
        return {};

    return { indexPool.data() + range.first, static_cast<std::size_t> (range.second) };
}

bool Instrument::isKeyswitch (int note) const noexcept
{
    return note >= 0 && note < 128 && keyswitchFlags[static_cast<std::size_t> (note)];
}

const Keyswitch* Instrument::keyswitchForNote (int note) const noexcept
{
    for (const auto& ks : keyswitches)
        if (ks.note == note)
            return &ks;

    return nullptr;
}

} // namespace moe::bank
