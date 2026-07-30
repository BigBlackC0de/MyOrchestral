#include "doctest.h"

#include "moe/sfz/SfzParser.h"

#include <algorithm>

using namespace moe::sfz;

TEST_CASE ("note names follow the SFZ convention where c4 is 60")
{
    CHECK (parseNoteName ("c4")   == 60);
    CHECK (parseNoteName ("C4")   == 60);
    CHECK (parseNoteName ("60")   == 60);
    CHECK (parseNoteName ("a4")   == 69);
    CHECK (parseNoteName ("c#4")  == 61);
    CHECK (parseNoteName ("db4")  == 61);
    CHECK (parseNoteName ("c-1")  == 0);
    CHECK (parseNoteName ("g9")   == 127);

    // 'b' after a letter is a flat, never the note B.
    CHECK (parseNoteName ("b3")   == 59);
    CHECK (parseNoteName ("bb3")  == 58);

    CHECK (parseNoteName ("")     == -1);
    CHECK (parseNoteName ("h4")   == -1);
    CHECK (parseNoteName ("c99")  == -1);
    CHECK (parseNoteName ("c4x")  == -1);
}

TEST_CASE ("comments are stripped without shifting line numbers")
{
    const std::string source = "one // trailing\n"
                               "/* block\n"
                               "   spanning */two\n"
                               "three\n";

    const std::string stripped = stripComments (source);

    const auto countNewlines = [] (const std::string& s)
    {
        return std::count (s.begin(), s.end(), '\n');
    };

    CHECK (countNewlines (stripped) == countNewlines (source));
    CHECK (stripped.find ("trailing") == std::string::npos);
    CHECK (stripped.find ("spanning") == std::string::npos);
    CHECK (stripped.find ("two") != std::string::npos);
}

TEST_CASE ("opcode values may contain spaces")
{
    const auto pairs = splitOpcodes ("sample=violin C4 rr1.wav lokey=60 hikey=62");

    REQUIRE (pairs.size() == 3);
    CHECK (pairs[0].first  == "sample");
    CHECK (pairs[0].second == "violin C4 rr1.wav");
    CHECK (pairs[1].first  == "lokey");
    CHECK (pairs[1].second == "60");
    CHECK (pairs[2].second == "62");
}

TEST_CASE ("opcodes inherit from global, master and group")
{
    Parser parser;

    const auto instrument = parser.parseString (R"(
        <global> ampeg_release=0.5 volume=-3
        <master> pan=-20
        <group>  lokey=60 hikey=72 volume=-6
        <region> sample=a.wav key=60
        <region> sample=b.wav key=61 pan=10
        <group>  lokey=73 hikey=80
        <region> sample=c.wav key=73
    )");

    REQUIRE (instrument.ok());
    REQUIRE (instrument.regions.size() == 3);

    // Inherited from <global> and <group>, overridden where the region says so.
    CHECK (instrument.regions[0].get ("ampeg_release") == "0.5");
    CHECK (instrument.regions[0].get ("volume") == "-6");
    CHECK (instrument.regions[0].get ("pan") == "-20");

    CHECK (instrument.regions[1].get ("pan") == "10");

    // A new <group> replaces the previous one but keeps <master> and <global>.
    CHECK (instrument.regions[2].get ("lokey") == "73");
    CHECK (instrument.regions[2].get ("volume") == "-3");
    CHECK (instrument.regions[2].get ("pan") == "-20");
}

TEST_CASE ("headers and opcodes may share a line")
{
    Parser parser;
    const auto instrument = parser.parseString (
        "<group> volume=-2 <region> sample=a.wav key=60 <region> sample=b.wav key=62");

    REQUIRE (instrument.regions.size() == 2);
    CHECK (instrument.regions[0].get ("sample") == "a.wav");
    CHECK (instrument.regions[1].get ("sample") == "b.wav");
    CHECK (instrument.regions[1].get ("volume") == "-2");
}

TEST_CASE ("#define macros expand in later lines")
{
    Parser parser;
    const auto instrument = parser.parseString (R"(
        #define $DIR strings/violins
        #define $REL 0.4
        <region> sample=$DIR/c4.wav key=60 ampeg_release=$REL
    )");

    REQUIRE (instrument.regions.size() == 1);
    CHECK (instrument.regions[0].get ("sample") == "strings/violins/c4.wav");
    CHECK (instrument.regions[0].get ("ampeg_release") == "0.4");
}

TEST_CASE ("#include pulls in another file and keeps the hierarchy")
{
    Parser parser;
    parser.setFileReader ([] (const std::string& path, std::string& contents)
    {
        if (path == "shared.sfz")
        {
            contents = "<region> sample=included.wav key=64";
            return true;
        }
        return false;
    });

    const auto instrument = parser.parseString (R"(
        <group> volume=-4
        #include "shared.sfz"
        <region> sample=local.wav key=65
    )");

    REQUIRE (instrument.regions.size() == 2);
    CHECK (instrument.regions[0].get ("sample") == "included.wav");
    CHECK (instrument.regions[0].get ("volume") == "-4");
    CHECK (instrument.regions[1].get ("sample") == "local.wav");
}

TEST_CASE ("an unresolved include warns instead of failing the whole bank")
{
    Parser parser;
    parser.setFileReader ([] (const std::string&, std::string&) { return false; });

    const auto instrument = parser.parseString (R"(
        #include "missing.sfz"
        <region> sample=a.wav key=60
    )");

    CHECK (instrument.ok());
    CHECK (instrument.regions.size() == 1);
    CHECK (instrument.diagnostics.size() >= 1);
}

TEST_CASE ("malformed input degrades rather than throwing")
{
    Parser parser;

    const auto instrument = parser.parseString (R"(
        <unknown_header> foo=bar
        <region sample=broken.wav
        <region> sample=fine.wav key=60
        #nonsense
    )");

    CHECK (instrument.regions.size() == 1);
    CHECK (instrument.regions[0].get ("sample") == "fine.wav");
    CHECK_FALSE (instrument.diagnostics.empty());
}

TEST_CASE ("control block exposes default_path normalised")
{
    Parser parser;
    const auto instrument = parser.parseString (R"(
        <control> default_path=samples\strings
        <region> sample=a.wav key=60
    )");

    CHECK (instrument.defaultPath() == "samples/strings/");
}
