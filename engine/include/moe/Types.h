#pragma once

#include <cstdint>
#include <cstddef>
#include <limits>

namespace moe
{

using SampleIndex = std::int64_t;

inline constexpr int kNumMidiNotes   = 128;
inline constexpr int kNumMidiCcs     = 128;
inline constexpr int kNumMidiChannels = 16;

/** Sentinel for "no note". */
inline constexpr int kNoNote = -1;

/** Maximum simultaneous sounding voices per engine instance. */
inline constexpr int kDefaultMaxVoices = 256;

/** Channel layout we render internally. Sample sources may be mono; they are
    upmixed at the panner stage rather than duplicated in memory. */
inline constexpr int kNumOutputChannels = 2;

/** How the streaming layer trades disk safety against trigger latency. */
enum class StreamingProfile
{
    live,       ///< smallest buffers, lowest latency, most disk traffic
    balanced,   ///< default
    render      ///< largest buffers, safest for offline bounce
};

struct StreamingSettings
{
    std::size_t preloadFrames = 32768;   ///< frames kept resident in RAM per region
    std::size_t ringFrames    = 65536;   ///< per-voice streaming ring buffer size
    std::size_t refillFrames  = 16384;   ///< refill request granularity

    static StreamingSettings forProfile (StreamingProfile p) noexcept
    {
        switch (p)
        {
            case StreamingProfile::live:   return { 16384,  16384,  4096 };
            case StreamingProfile::render: return { 65536, 262144, 65536 };
            case StreamingProfile::balanced:
            default:                       return { 32768,  65536, 16384 };
        }
    }
};

/** Articulations the engine understands natively. Banks may declare more via
    keyswitch labels; unknown labels map to `custom` and are addressed by index. */
enum class Articulation
{
    sustain = 0,
    legato,
    staccato,
    spiccato,
    pizzicato,
    tremolo,
    trill,
    marcato,
    sulPonticello,
    sulTasto,
    colLegno,
    harmonics,
    muted,          ///< con sordino (strings) / stopped (horns)
    flutter,        ///< flutter tongue (winds/brass)
    swell,
    roll,           ///< percussion
    hit,            ///< percussion one-shot
    custom,
    count
};

const char* toName (Articulation) noexcept;

/** Parses an articulation from a keyswitch label such as "spicc" or "Legato".
    Returns Articulation::custom when nothing matches. */
Articulation articulationFromLabel (const char* label) noexcept;

/** Instrument families, used for defaults (stage position, humanisation depth). */
enum class Family
{
    strings = 0,
    brass,
    woodwinds,
    percussion,
    choir,
    other,
    count
};

const char* toName (Family) noexcept;

} // namespace moe
