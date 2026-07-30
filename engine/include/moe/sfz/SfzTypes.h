#pragma once

#include <map>
#include <string>
#include <vector>

namespace moe::sfz
{

/** Opcodes as they appear in the file, before interpretation. Keeping the raw
    text around means an unknown opcode is preserved rather than silently lost —
    useful when debugging a third-party bank. */
using OpcodeMap = std::map<std::string, std::string>;

/** One `<region>` with every inherited opcode already merged in.

    SFZ inheritance runs `<global>` -> `<master>` -> `<group>` -> `<region>`,
    each level overriding the previous. Flattening at parse time keeps the
    runtime free of lookup chains. */
struct ParsedRegion
{
    OpcodeMap opcodes;
    int       sourceLine = 0;   ///< line of the `<region>` header, for diagnostics

    /** Returns the opcode value, or `fallback` when absent. */
    std::string get (const std::string& key, const std::string& fallback = {}) const;
    bool        has (const std::string& key) const;
};

struct ParseDiagnostic
{
    enum class Severity { warning, error };

    Severity    severity = Severity::warning;
    int         line     = 0;
    std::string file;
    std::string message;
};

/** Result of parsing one .sfz file (plus everything it `#include`s). */
struct ParsedInstrument
{
    OpcodeMap                    control;    ///< merged `<control>` opcodes
    std::vector<ParsedRegion>    regions;
    std::vector<ParseDiagnostic> diagnostics;

    /** True when no diagnostic has error severity. Warnings are tolerated:
        a bank with a few unreadable regions should still load. */
    bool ok() const;

    /** `default_path` from `<control>`, normalised to use '/' and to end with
        a separator (empty when unset). */
    std::string defaultPath() const;
};

/** Converts an SFZ note reference to a MIDI note number.

    Accepts a plain integer ("60") or a note name ("c4", "C#4", "Db-1", "as3").
    Follows the SFZ convention where c4 == 60. Returns -1 when unparseable. */
int parseNoteName (const std::string& text) noexcept;

} // namespace moe::sfz
