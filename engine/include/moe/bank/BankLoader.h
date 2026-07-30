#pragma once

#include "moe/Types.h"
#include "moe/bank/Instrument.h"
#include "moe/sfz/SfzTypes.h"

#include <atomic>
#include <string>
#include <vector>

namespace moe::bank
{

struct BankOptions
{
    StreamingSettings streaming;

    /** Load every sample fully into RAM instead of preloading heads.
        Appropriate for small banks and for offline rendering. */
    bool preloadEntireSamples = false;

    /** Used when the SFZ gives no clue. */
    Family familyHint = Family::other;

    /** Stop after this many regions (0 = unlimited). A safety net against a
        malformed bank that would exhaust memory. */
    std::size_t maxRegions = 0;
};

struct BankLoadResult
{
    InstrumentPtr            instrument;
    std::vector<std::string> warnings;
    std::string              error;       ///< empty on success

    bool ok() const noexcept { return instrument != nullptr && error.empty(); }
};

/** Turns an SFZ file into a playable Instrument.

    Runs entirely off the audio thread: it opens files, allocates, and may take
    seconds on a large bank. The engine only ever receives the finished, immutable
    result. */
class BankLoader
{
public:
    // Declared at namespace scope above: a nested class with default member
    // initialisers cannot be brace-initialised in a default argument of its own
    // enclosing class, which is exactly what these are used for.
    using Options = BankOptions;
    using Result  = BankLoadResult;

    /** Parses and loads `sfzPath`. Sample paths resolve relative to the SFZ file
        (or to `default_path` when the bank declares one). */
    Result loadSfzFile (const std::string& sfzPath, const Options& options = {});

    /** Builds from an already-parsed instrument. `baseDirectory` must end with a
        separator. Exposed for tests, which parse from memory. */
    Result build (const sfz::ParsedInstrument& parsed,
                  const std::string&           baseDirectory,
                  const Options&               options = {});

    /** Progress in 0..1 while a load is running, for the UI. */
    float progress() const noexcept { return loadProgress.load (std::memory_order_relaxed); }

    /** Asks the current load to stop as soon as possible. */
    void cancel() noexcept { cancelled.store (true, std::memory_order_relaxed); }

private:
    std::atomic<float> loadProgress { 0.0f };
    std::atomic<bool>  cancelled    { false };
};

/** Guesses a family from an instrument or file name ("violins 1" -> strings).
    Exposed because the UI uses it to pick a default stage position. */
Family guessFamily (const std::string& name) noexcept;

/** Guesses an articulation from a sample path or group name when the bank
    declares no `sw_label`. Free banks encode it in filenames far more often than
    in opcodes. */
Articulation guessArticulation (const std::string& text) noexcept;

} // namespace moe::bank
