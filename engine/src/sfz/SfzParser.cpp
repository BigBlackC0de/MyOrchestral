#include "moe/sfz/SfzParser.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace moe::sfz
{

namespace
{

bool isSpace (char c) noexcept
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v';
}

std::string trim (const std::string& s)
{
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && isSpace (s[b]))     ++b;
    while (e > b && isSpace (s[e - 1])) --e;
    return s.substr (b, e - b);
}

std::string toLower (std::string s)
{
    std::transform (s.begin(), s.end(), s.begin(),
                    [] (unsigned char c) { return static_cast<char> (std::tolower (c)); });
    return s;
}

/** Returns the directory part of a path, including the trailing separator. */
std::string directoryOf (const std::string& path)
{
    const auto slash = path.find_last_of ("/\\");
    return slash == std::string::npos ? std::string{} : path.substr (0, slash + 1);
}

std::string joinPath (const std::string& dir, const std::string& relative)
{
    if (relative.empty())
        return dir;

    // Absolute paths win over the base directory.
    if (relative.front() == '/' || relative.front() == '\\'
        || (relative.size() > 1 && relative[1] == ':'))
        return relative;

    return dir + relative;
}

bool readWholeFile (const std::string& path, std::string& out)
{
    std::ifstream stream (path, std::ios::binary);
    if (! stream)
        return false;

    std::ostringstream buffer;
    buffer << stream.rdbuf();
    out = buffer.str();
    return true;
}

} // namespace

//==============================================================================
std::string ParsedRegion::get (const std::string& key, const std::string& fallback) const
{
    const auto it = opcodes.find (key);
    return it == opcodes.end() ? fallback : it->second;
}

bool ParsedRegion::has (const std::string& key) const
{
    return opcodes.find (key) != opcodes.end();
}

bool ParsedInstrument::ok() const
{
    return std::none_of (diagnostics.begin(), diagnostics.end(),
                         [] (const ParseDiagnostic& d)
                         { return d.severity == ParseDiagnostic::Severity::error; });
}

std::string ParsedInstrument::defaultPath() const
{
    const auto it = control.find ("default_path");
    if (it == control.end() || it->second.empty())
        return {};

    std::string p = it->second;
    std::replace (p.begin(), p.end(), '\\', '/');
    if (p.back() != '/')
        p += '/';
    return p;
}

//==============================================================================
int parseNoteName (const std::string& raw) noexcept
{
    const std::string text = trim (raw);
    if (text.empty())
        return -1;

    // Plain MIDI number.
    const bool numeric = std::all_of (text.begin(), text.end(),
                                      [] (char c)
                                      { return std::isdigit (static_cast<unsigned char> (c)) != 0
                                               || c == '-' || c == '+'; });
    if (numeric)
    {
        try
        {
            const int value = std::stoi (text);
            return (value >= 0 && value < 128) ? value : -1;
        }
        catch (...) { return -1; }
    }

    static constexpr int semitoneOfLetter[7] = { 9, 11, 0, 2, 4, 5, 7 }; // a b c d e f g

    const char letter = static_cast<char> (std::tolower (static_cast<unsigned char> (text[0])));
    if (letter < 'a' || letter > 'g')
        return -1;

    int semitone = semitoneOfLetter[letter - 'a'];
    std::size_t i = 1;

    // Accidentals. 'b' here is a flat, never the note B: the note letter was
    // already consumed above.
    for (; i < text.size(); ++i)
    {
        const char c = static_cast<char> (std::tolower (static_cast<unsigned char> (text[i])));
        if      (c == '#' || c == 's') ++semitone;
        else if (c == 'b' || c == 'f') --semitone;
        else if (c == 'x')             semitone += 2;
        else break;
    }

    if (i >= text.size())
        return -1;

    int octave = 0;
    try
    {
        std::size_t consumed = 0;
        octave = std::stoi (text.substr (i), &consumed);
        if (consumed != text.size() - i)
            return -1;
    }
    catch (...) { return -1; }

    const int note = semitone + (octave + 1) * 12;   // SFZ convention: c4 == 60
    return (note >= 0 && note < 128) ? note : -1;
}

//==============================================================================
std::string stripComments (const std::string& text)
{
    std::string out;
    out.reserve (text.size());

    bool inLineComment  = false;
    bool inBlockComment = false;

    for (std::size_t i = 0; i < text.size(); ++i)
    {
        const char c    = text[i];
        const char next = (i + 1 < text.size()) ? text[i + 1] : '\0';

        if (inLineComment)
        {
            if (c == '\n') { inLineComment = false; out += c; }
            continue;
        }

        if (inBlockComment)
        {
            // Newlines are kept so reported line numbers stay correct.
            if (c == '\n')
                out += c;
            else if (c == '*' && next == '/')
            {
                inBlockComment = false;
                ++i;
            }
            continue;
        }

        if (c == '/' && next == '/') { inLineComment  = true; ++i; continue; }
        if (c == '/' && next == '*') { inBlockComment = true; ++i; continue; }

        out += c;
    }

    return out;
}

std::vector<std::pair<std::string, std::string>> splitOpcodes (const std::string& line)
{
    std::vector<std::pair<std::string, std::string>> result;

    // Collect the position of every '=' that separates an opcode from a value.
    std::vector<std::size_t> equalsPositions;
    for (std::size_t i = 0; i < line.size(); ++i)
        if (line[i] == '=')
            equalsPositions.push_back (i);

    for (std::size_t n = 0; n < equalsPositions.size(); ++n)
    {
        const std::size_t eq = equalsPositions[n];

        // The opcode name is the run of non-space characters ending at '='.
        std::size_t nameStart = eq;
        while (nameStart > 0 && ! isSpace (line[nameStart - 1]))
            --nameStart;

        const std::string name = trim (line.substr (nameStart, eq - nameStart));
        if (name.empty())
            continue;

        // The value runs to just before the *next* opcode name, which lets
        // values contain spaces (`sample=violin C4 rr1.wav`).
        std::size_t valueEnd = line.size();
        if (n + 1 < equalsPositions.size())
        {
            std::size_t nextNameStart = equalsPositions[n + 1];
            while (nextNameStart > 0 && ! isSpace (line[nextNameStart - 1]))
                --nextNameStart;
            valueEnd = nextNameStart;
        }

        if (valueEnd < eq + 1)
            valueEnd = eq + 1;

        result.emplace_back (toLower (name), trim (line.substr (eq + 1, valueEnd - eq - 1)));
    }

    return result;
}

//==============================================================================
struct Parser::State
{
    enum class Level { none, control, global, master, group, region, ignored };

    ParsedInstrument result;

    OpcodeMap global, master, group, region;
    Level     level         = Level::none;
    bool      regionPending = false;
    int       regionLine    = 0;

    std::unordered_map<std::string, std::string> macros;

    void diagnose (ParseDiagnostic::Severity severity,
                   const std::string& file,
                   int line,
                   std::string message)
    {
        result.diagnostics.push_back ({ severity, line, file, std::move (message) });
    }

    void flushRegion()
    {
        if (! regionPending)
            return;

        ParsedRegion merged;
        merged.sourceLine = regionLine;

        for (const auto* source : { &global, &master, &group, &region })
            for (const auto& [key, value] : *source)
                merged.opcodes[key] = value;

        result.regions.push_back (std::move (merged));
        regionPending = false;
        region.clear();
    }

    OpcodeMap* currentTarget()
    {
        switch (level)
        {
            case Level::control: return &result.control;
            case Level::global:  return &global;
            case Level::master:  return &master;
            case Level::group:   return &group;
            case Level::region:  return &region;
            default:             return nullptr;
        }
    }
};

ParsedInstrument Parser::parseFile (const std::string& path)
{
    std::string contents;
    const bool read = fileReader ? fileReader (path, contents)
                                 : readWholeFile (path, contents);

    if (! read)
    {
        ParsedInstrument failed;
        failed.diagnostics.push_back ({ ParseDiagnostic::Severity::error, 0, path,
                                        "cannot open file" });
        return failed;
    }

    return parseString (contents, path);
}

ParsedInstrument Parser::parseString (const std::string& text, const std::string& virtualPath)
{
    State state;
    parseInto (state, text, virtualPath, 0);
    state.flushRegion();
    return std::move (state.result);
}

void Parser::parseInto (State& state, const std::string& text, const std::string& path, int depth)
{
    const std::string cleaned = stripComments (text);
    const std::string baseDir = directoryOf (path);

    std::istringstream stream (cleaned);
    std::string        rawLine;
    int                lineNumber = 0;

    while (std::getline (stream, rawLine))
    {
        ++lineNumber;
        std::string line = trim (rawLine);
        if (line.empty())
            continue;

        // Macro expansion, longest name first so `$vel` never eats `$velocity`.
        if (! state.macros.empty() && line.find('$') != std::string::npos)
        {
            std::vector<const std::string*> names;
            names.reserve (state.macros.size());
            for (const auto& entry : state.macros)
                names.push_back (&entry.first);
            std::sort (names.begin(), names.end(),
                       [] (const std::string* a, const std::string* b)
                       { return a->size() > b->size(); });

            for (const auto* name : names)
            {
                const std::string& value = state.macros[*name];
                for (std::size_t at = line.find (*name);
                     at != std::string::npos;
                     at = line.find (*name, at + value.size()))
                    line.replace (at, name->size(), value);
            }
        }

        if (line[0] == '#')
        {
            const auto spaceAt   = line.find_first_of (" \t");
            const std::string kw = toLower (line.substr (0, spaceAt));
            const std::string arg = spaceAt == std::string::npos ? std::string{}
                                                                 : trim (line.substr (spaceAt + 1));

            if (kw == "#define")
            {
                const auto sep = arg.find_first_of (" \t");
                if (sep == std::string::npos || arg.empty() || arg[0] != '$')
                {
                    state.diagnose (ParseDiagnostic::Severity::warning, path, lineNumber,
                                    "malformed #define, expected `#define $name value`");
                }
                else
                {
                    state.macros[arg.substr (0, sep)] = trim (arg.substr (sep + 1));
                }
            }
            else if (kw == "#include")
            {
                std::string included = arg;
                if (included.size() >= 2 && included.front() == '"' && included.back() == '"')
                    included = included.substr (1, included.size() - 2);
                std::replace (included.begin(), included.end(), '\\', '/');

                if (depth >= maxIncludeDepth)
                {
                    state.diagnose (ParseDiagnostic::Severity::warning, path, lineNumber,
                                    "#include nesting too deep, skipped: " + included);
                }
                else
                {
                    const std::string resolved = joinPath (baseDir, included);
                    std::string       contents;
                    const bool        read = fileReader ? fileReader (resolved, contents)
                                                        : readWholeFile (resolved, contents);

                    if (read)
                        parseInto (state, contents, resolved, depth + 1);
                    else
                        state.diagnose (ParseDiagnostic::Severity::warning, path, lineNumber,
                                        "cannot resolve #include: " + resolved);
                }
            }
            else
            {
                state.diagnose (ParseDiagnostic::Severity::warning, path, lineNumber,
                                "unknown directive: " + kw);
            }
            continue;
        }

        // A line may mix headers and opcodes: `<region> sample=a.wav key=60`.
        std::size_t pos = 0;
        while (pos < line.size())
        {
            const std::size_t lt = line.find ('<', pos);
            const std::string chunk = line.substr (pos, lt == std::string::npos
                                                            ? std::string::npos
                                                            : lt - pos);

            if (! trim (chunk).empty())
            {
                if (auto* target = state.currentTarget())
                {
                    for (auto& [key, value] : splitOpcodes (chunk))
                        (*target)[key] = value;
                }
                else if (state.level == State::Level::none)
                {
                    state.diagnose (ParseDiagnostic::Severity::warning, path, lineNumber,
                                    "opcodes before any header, ignored");
                }
            }

            if (lt == std::string::npos)
                break;

            const std::size_t gt = line.find ('>', lt);
            if (gt == std::string::npos)
            {
                state.diagnose (ParseDiagnostic::Severity::warning, path, lineNumber,
                                "unterminated header");
                break;
            }

            const std::string header = toLower (trim (line.substr (lt + 1, gt - lt - 1)));

            state.flushRegion();

            if (header == "control")     { state.level = State::Level::control; }
            else if (header == "global") { state.global.clear(); state.master.clear();
                                           state.group.clear();  state.level = State::Level::global; }
            else if (header == "master") { state.master.clear(); state.group.clear();
                                           state.level = State::Level::master; }
            else if (header == "group")  { state.group.clear();  state.level = State::Level::group; }
            else if (header == "region")
            {
                state.region.clear();
                state.regionPending = true;
                state.regionLine    = lineNumber;
                state.level         = State::Level::region;
            }
            else if (header == "curve" || header == "effect" || header == "midi"
                     || header == "sample")
            {
                state.level = State::Level::ignored;
            }
            else
            {
                state.diagnose (ParseDiagnostic::Severity::warning, path, lineNumber,
                                "unknown header <" + header + ">, contents ignored");
                state.level = State::Level::ignored;
            }

            pos = gt + 1;
        }
    }
}

} // namespace moe::sfz
