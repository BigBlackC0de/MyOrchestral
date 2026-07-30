#pragma once

#include "moe/Types.h"
#include "moe/audio/AudioFileReader.h"
#include "moe/bank/Region.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace moe::bank
{

/** A sample file referenced by one or more regions.

    `preload` holds the first `preloadFrames` frames deinterleaved and resident,
    so a note always starts without touching the disk. The rest is streamed. */
struct SampleFile
{
    std::string           path;
    audio::AudioFileInfo  info;

    std::vector<float>    preload;         ///< channel-major, numChannels blocks
    SampleIndex           preloadFrames = 0;

    const float* channel (int ch) const noexcept
    {
        if (ch < 0 || ch >= info.numChannels || preload.empty())
            return nullptr;
        return preload.data() + static_cast<std::size_t> (ch) * static_cast<std::size_t> (preloadFrames);
    }

    /** True when the whole file fits in the preload buffer — no streaming needed. */
    bool isFullyResident() const noexcept { return preloadFrames >= info.numFrames; }
};

/** A keyswitch declared by the bank. */
struct Keyswitch
{
    int          note = -1;
    std::string  label;
    Articulation articulation = Articulation::custom;
};

/** Everything needed to play one instrument, immutable once loaded.

    Instances are built off the audio thread and published by pointer swap, so
    every accessor here is const and allocation-free. */
class Instrument
{
public:
    std::string name;
    Family      family = Family::other;

    std::vector<SampleFile> samples;
    std::vector<Region>     regions;
    std::vector<Keyswitch>  keyswitches;

    int defaultKeyswitch = -1;      ///< `sw_default`, -1 when the bank declares none

    int lowestKey  = 127;
    int highestKey = 0;

    /** Builds the per-key lookup table. Must be called after filling `regions`
        and before any call to `candidatesForKey`. */
    void buildIndex();

    /** Region indices whose key range covers `note`, as a contiguous view.
        Empty when the note is outside the instrument's range. */
    struct View
    {
        const int*  first = nullptr;
        std::size_t count = 0;

        const int*  begin() const noexcept { return first; }
        const int*  end()   const noexcept { return first + count; }
        std::size_t size()  const noexcept { return count; }
        bool        empty() const noexcept { return count == 0; }
    };

    View candidatesForKey (int note) const noexcept;

    /** Articulations this instrument actually provides, derived from its regions. */
    const std::vector<Articulation>& availableArticulations() const noexcept
    {
        return articulations;
    }

    bool isKeyswitch (int note) const noexcept;

    /** Keyswitch mapped to `note`, or nullptr. */
    const Keyswitch* keyswitchForNote (int note) const noexcept;

private:
    std::vector<int>                        indexPool;
    std::array<std::pair<int, int>, 128>    indexRanges {};   ///< {offset, count}
    std::vector<Articulation>               articulations;
    std::array<bool, 128>                   keyswitchFlags {};
};

using InstrumentPtr = std::shared_ptr<const Instrument>;

} // namespace moe::bank
