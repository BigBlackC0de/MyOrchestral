#pragma once

#include "moe/sfz/SfzTypes.h"

#include <functional>
#include <string>

namespace moe::sfz
{

/** SFZ v1 parser covering the subset this engine acts on, plus tolerant
    handling of everything else.

    Supported beyond plain opcodes:
      - headers `<control> <global> <master> <group> <region>` with inheritance
      - `<curve>` and `<effect>` are recognised and skipped
      - `#define $name value` macros, expanded anywhere in later lines
      - `#include "other.sfz"` with paths relative to the including file
      - line comments and block comments, in the C and C++ styles
      - multi-token values (`sample=my violin C4.wav`) — a value runs up to the
        last whitespace before the next `opcode=` on the line

    The parser never throws on malformed input. Problems land in
    `ParsedInstrument::diagnostics` so a partially broken bank still loads. */
class Parser
{
public:
    /** Reads a file from disk and everything it includes. */
    ParsedInstrument parseFile (const std::string& path);

    /** Parses text directly. `virtualPath` is used to resolve `#include` and to
        label diagnostics; it may be empty when there are no includes. */
    ParsedInstrument parseString (const std::string& text,
                                  const std::string& virtualPath = {});

    /** Overrides how included files are read. The default reads from disk;
        tests inject an in-memory table. Returning false marks the include as
        unresolved (a warning, not a failure). */
    using FileReader = std::function<bool (const std::string& path, std::string& contents)>;
    void setFileReader (FileReader reader) { fileReader = std::move (reader); }

    /** Guards against `#include` cycles and runaway nesting. */
    void setMaxIncludeDepth (int depth) noexcept { maxIncludeDepth = depth; }

private:
    struct State;

    void parseInto (State& state, const std::string& text, const std::string& path, int depth);

    FileReader fileReader;
    int        maxIncludeDepth = 16;
};

/** Strips line and block comments, preserving line count so diagnostics stay
    accurate. Exposed for testing. */
std::string stripComments (const std::string& text);

/** Splits one logical line into `opcode=value` pairs, handling values that
    contain spaces. Exposed for testing. */
std::vector<std::pair<std::string, std::string>> splitOpcodes (const std::string& line);

} // namespace moe::sfz
