#include "moe/bank/BankLoader.h"

#include "moe/audio/WavReader.h"
#include "moe/sfz/SfzParser.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_map>

namespace moe::bank
{

namespace
{

std::string toLower (std::string s)
{
    std::transform (s.begin(), s.end(), s.begin(),
                    [] (unsigned char c) { return static_cast<char> (std::tolower (c)); });
    return s;
}

std::string directoryOf (const std::string& path)
{
    const auto slash = path.find_last_of ("/\\");
    return slash == std::string::npos ? std::string{} : path.substr (0, slash + 1);
}

std::string fileStem (const std::string& path)
{
    const auto slash = path.find_last_of ("/\\");
    const auto start = slash == std::string::npos ? 0 : slash + 1;
    const auto dot   = path.find_last_of ('.');
    const auto end   = (dot == std::string::npos || dot < start) ? path.size() : dot;
    return path.substr (start, end - start);
}

int getInt (const sfz::ParsedRegion& region, const char* key, int fallback)
{
    if (! region.has (key))
        return fallback;

    try { return std::stoi (region.get (key)); }
    catch (...) { return fallback; }
}

float getFloat (const sfz::ParsedRegion& region, const char* key, float fallback)
{
    if (! region.has (key))
        return fallback;

    try { return std::stof (region.get (key)); }
    catch (...) { return fallback; }
}

/** Note-valued opcodes accept either a number or a note name. */
int getNote (const sfz::ParsedRegion& region, const char* key, int fallback)
{
    if (! region.has (key))
        return fallback;

    const int note = sfz::parseNoteName (region.get (key));
    return note >= 0 ? note : fallback;
}

LoopMode parseLoopMode (const std::string& text, LoopMode fallback)
{
    const std::string value = toLower (text);
    if (value == "no_loop")         return LoopMode::noLoop;
    if (value == "one_shot")        return LoopMode::oneShot;
    if (value == "loop_continuous") return LoopMode::loopContinuous;
    if (value == "loop_sustain")    return LoopMode::loopSustain;
    return fallback;
}

TriggerMode parseTrigger (const std::string& text)
{
    const std::string value = toLower (text);
    if (value == "release" || value == "release_key") return TriggerMode::release;
    if (value == "first")                             return TriggerMode::first;
    if (value == "legato")                            return TriggerMode::legato;
    return TriggerMode::attack;
}

/** Finds `locc<n>` / `hicc<n>` style opcodes and returns the CC number used. */
int findCcGate (const sfz::ParsedRegion& region, int& loValue, int& hiValue)
{
    for (const auto& [key, value] : region.opcodes)
    {
        if (key.rfind ("locc", 0) == 0 && key.size() > 4)
        {
            try
            {
                const int cc = std::stoi (key.substr (4));
                loValue = std::stoi (value);
                hiValue = getInt (region, ("hicc" + std::to_string (cc)).c_str(), 127);
                return cc;
            }
            catch (...) { }
        }
    }

    for (const auto& [key, value] : region.opcodes)
    {
        if (key.rfind ("hicc", 0) == 0 && key.size() > 4)
        {
            try
            {
                const int cc = std::stoi (key.substr (4));
                loValue = 0;
                hiValue = std::stoi (value);
                return cc;
            }
            catch (...) { }
        }
    }

    return -1;
}

/** Finds `xfin_locc<n>` and friends, which is how banks express CC1 dynamics. */
int findCcCrossfade (const sfz::ParsedRegion& region, Region& out)
{
    int ccNumber = -1;

    static constexpr const char* prefixes[] = { "xfin_locc", "xfin_hicc", "xfout_locc", "xfout_hicc" };

    for (const auto& [key, value] : region.opcodes)
    {
        for (int p = 0; p < 4; ++p)
        {
            const std::string prefix = prefixes[p];
            if (key.rfind (prefix, 0) != 0 || key.size() <= prefix.size())
                continue;

            try
            {
                const int cc     = std::stoi (key.substr (prefix.size()));
                const int amount = std::stoi (value);

                if (ccNumber >= 0 && ccNumber != cc)
                    continue;   // only one crossfade CC is modelled

                ccNumber = cc;

                switch (p)
                {
                    case 0: out.xfInLoCc  = amount; break;
                    case 1: out.xfInHiCc  = amount; break;
                    case 2: out.xfOutLoCc = amount; break;
                    case 3: out.xfOutHiCc = amount; break;
                    default: break;
                }
            }
            catch (...) { }
        }
    }

    return ccNumber;
}

} // namespace

//==============================================================================
Family guessFamily (const std::string& rawName) noexcept
{
    const std::string name = toLower (rawName);
    auto has = [&name] (const char* needle) { return name.find (needle) != std::string::npos; };

    if (has ("violin") || has ("viola") || has ("cello") || has ("violoncell")
        || has ("bass") || has ("contrabass") || has ("string") || has ("cordes")
        || has ("harp") || has ("vln") || has ("vla") || has ("vlc") || has ("cb"))
        return Family::strings;

    if (has ("horn") || has ("trumpet") || has ("trombone") || has ("tuba")
        || has ("brass") || has ("cuivre") || has ("cor ") || has ("trp"))
        return Family::brass;

    if (has ("flute") || has ("oboe") || has ("clarinet") || has ("bassoon")
        || has ("piccolo") || has ("wind") || has ("bois") || has ("hautbois")
        || has ("basson") || has ("clarinette"))
        return Family::woodwinds;

    if (has ("timpani") || has ("timbale") || has ("drum") || has ("cymbal")
        || has ("perc") || has ("taiko") || has ("gong") || has ("snare")
        || has ("hit") || has ("impact") || has ("riser"))
        return Family::percussion;

    if (has ("choir") || has ("choeur") || has ("voice") || has ("vocal"))
        return Family::choir;

    return Family::other;
}

Articulation guessArticulation (const std::string& text) noexcept
{
    const Articulation fromLabel = articulationFromLabel (text.c_str());
    return fromLabel;
}

//==============================================================================
BankLoader::Result BankLoader::loadSfzFile (const std::string& sfzPath, const Options& options)
{
    sfz::Parser parser;
    const auto  parsed = parser.parseFile (sfzPath);

    if (! parsed.ok())
    {
        Result failed;
        failed.error = "cannot parse " + sfzPath;
        for (const auto& diagnostic : parsed.diagnostics)
            failed.warnings.push_back (diagnostic.file + ":" + std::to_string (diagnostic.line)
                                       + " " + diagnostic.message);
        return failed;
    }

    auto result = build (parsed, directoryOf (sfzPath), options);

    if (result.instrument != nullptr && result.instrument->name.empty())
    {
        // `name` is const inside the shared_ptr, so set it before publishing:
        // build() left it empty only when the bank declared nothing.
        auto named = std::make_shared<Instrument> (*result.instrument);
        named->name = fileStem (sfzPath);
        named->family = options.familyHint != Family::other ? options.familyHint
                                                            : guessFamily (named->name);
        named->buildIndex();
        result.instrument = named;
    }

    for (const auto& diagnostic : parsed.diagnostics)
        result.warnings.push_back (diagnostic.file + ":" + std::to_string (diagnostic.line)
                                   + " " + diagnostic.message);

    return result;
}

BankLoader::Result BankLoader::build (const sfz::ParsedInstrument& parsed,
                                       const std::string&           baseDirectory,
                                       const Options&               options)
{
    cancelled.store (false, std::memory_order_relaxed);
    loadProgress.store (0.0f, std::memory_order_relaxed);

    Result result;
    auto   instrument = std::make_shared<Instrument>();

    instrument->family = options.familyHint;

    const std::string defaultPath = parsed.defaultPath();
    const std::string sampleRoot  = defaultPath.empty()
                                        ? baseDirectory
                                        : (defaultPath.front() == '/' ? defaultPath
                                                                      : baseDirectory + defaultPath);

    std::unordered_map<std::string, int> sampleIds;
    std::unordered_map<int, Keyswitch>   keyswitchesByNote;

    const std::size_t regionLimit = options.maxRegions == 0
                                        ? parsed.regions.size()
                                        : std::min (options.maxRegions, parsed.regions.size());

    for (std::size_t i = 0; i < regionLimit; ++i)
    {
        if (cancelled.load (std::memory_order_relaxed))
        {
            result.error = "load cancelled";
            return result;
        }

        loadProgress.store (static_cast<float> (i) / static_cast<float> (std::max<std::size_t> (regionLimit, 1)),
                            std::memory_order_relaxed);

        const auto& parsedRegion = parsed.regions[i];

        std::string samplePath = parsedRegion.get ("sample");
        if (samplePath.empty())
        {
            result.warnings.push_back ("region at line " + std::to_string (parsedRegion.sourceLine)
                                       + " has no sample, skipped");
            continue;
        }

        std::replace (samplePath.begin(), samplePath.end(), '\\', '/');
        const std::string resolved = (samplePath.front() == '/') ? samplePath
                                                                 : sampleRoot + samplePath;

        // ---- resolve (and preload) the sample file, once per unique path ----
        int sampleId = -1;
        if (const auto existing = sampleIds.find (resolved); existing != sampleIds.end())
        {
            sampleId = existing->second;
        }
        else
        {
            audio::WavReader reader (resolved);
            if (! reader.isOpen())
            {
                result.warnings.push_back ("cannot read sample: " + resolved
                                           + " (" + reader.error() + ")");
                continue;
            }

            SampleFile file;
            file.path = resolved;
            file.info = reader.info();

            const SampleIndex wanted = options.preloadEntireSamples
                                           ? file.info.numFrames
                                           : std::min<SampleIndex> (
                                               file.info.numFrames,
                                               static_cast<SampleIndex> (options.streaming.preloadFrames));

            file.preloadFrames = wanted;
            file.preload.assign (static_cast<std::size_t> (wanted)
                                     * static_cast<std::size_t> (file.info.numChannels),
                                 0.0f);

            std::vector<float*> channelPointers (static_cast<std::size_t> (file.info.numChannels));
            for (int ch = 0; ch < file.info.numChannels; ++ch)
                channelPointers[static_cast<std::size_t> (ch)] =
                    file.preload.data() + static_cast<std::size_t> (ch) * static_cast<std::size_t> (wanted);

            reader.read (channelPointers.data(), file.info.numChannels, 0, wanted);

            sampleId = static_cast<int> (instrument->samples.size());
            instrument->samples.push_back (std::move (file));
            sampleIds.emplace (resolved, sampleId);
        }

        const SampleFile& file = instrument->samples[static_cast<std::size_t> (sampleId)];

        // ---- interpret the opcodes ----------------------------------------
        Region region;
        region.sampleId = sampleId;

        const int key = getNote (parsedRegion, "key", -1);
        if (key >= 0)
        {
            region.loKey = region.hiKey = region.rootKey = key;
        }
        else
        {
            region.loKey = getNote (parsedRegion, "lokey", 0);
            region.hiKey = getNote (parsedRegion, "hikey", 127);
            region.rootKey = getNote (parsedRegion, "pitch_keycenter",
                                      file.info.rootNote >= 0 ? file.info.rootNote : 60);
        }

        region.loVel = getInt (parsedRegion, "lovel", 1);
        region.hiVel = getInt (parsedRegion, "hivel", 127);

        region.volumeDb    = getFloat (parsedRegion, "volume", 0.0f);
        region.pan         = getFloat (parsedRegion, "pan", 0.0f);
        region.width       = getFloat (parsedRegion, "width", 100.0f);
        region.ampVelTrack = getFloat (parsedRegion, "amp_veltrack", 100.0f);

        region.transpose     = getInt (parsedRegion, "transpose", 0);
        region.tuneCents     = getFloat (parsedRegion, "tune",
                                          getFloat (parsedRegion, "pitch", 0.0f));
        region.pitchKeytrack = getFloat (parsedRegion, "pitch_keytrack", 100.0f);

        region.offset   = static_cast<SampleIndex> (getInt (parsedRegion, "offset", 0));
        region.endFrame = static_cast<SampleIndex> (getInt (parsedRegion, "end", -1));

        region.loopMode = parseLoopMode (parsedRegion.get ("loop_mode",
                                                            parsedRegion.get ("loopmode")),
                                          file.info.hasLoop ? LoopMode::loopContinuous
                                                            : LoopMode::noLoop);
        region.loopStart = static_cast<SampleIndex> (
            getInt (parsedRegion, "loop_start", static_cast<int> (file.info.loopStart)));
        region.loopEnd = static_cast<SampleIndex> (
            getInt (parsedRegion, "loop_end", static_cast<int> (file.info.loopEnd)));

        region.ampegDelay   = getFloat (parsedRegion, "ampeg_delay", 0.0f);
        region.ampegAttack  = getFloat (parsedRegion, "ampeg_attack", 0.001f);
        region.ampegHold    = getFloat (parsedRegion, "ampeg_hold", 0.0f);
        region.ampegDecay   = getFloat (parsedRegion, "ampeg_decay", 0.0f);
        region.ampegSustain = getFloat (parsedRegion, "ampeg_sustain", 100.0f) / 100.0f;
        region.ampegRelease = getFloat (parsedRegion, "ampeg_release", 0.15f);

        region.seqLength   = std::max (1, getInt (parsedRegion, "seq_length", 1));
        region.seqPosition = std::clamp (getInt (parsedRegion, "seq_position", 1),
                                          1, region.seqLength);

        region.group   = getInt (parsedRegion, "group", 0);
        region.offBy   = getInt (parsedRegion, "off_by", 0);
        region.offMode = toLower (parsedRegion.get ("off_mode", "fast")) == "normal"
                             ? OffMode::normal : OffMode::fast;

        region.trigger = parseTrigger (parsedRegion.get ("trigger", "attack"));

        region.xfInLoVel  = getInt (parsedRegion, "xfin_lovel", -1);
        region.xfInHiVel  = getInt (parsedRegion, "xfin_hivel", -1);
        region.xfOutLoVel = getInt (parsedRegion, "xfout_lovel", -1);
        region.xfOutHiVel = getInt (parsedRegion, "xfout_hivel", -1);

        region.xfCcNumber = findCcCrossfade (parsedRegion, region);
        region.ccNumber   = findCcGate (parsedRegion, region.loCc, region.hiCc);

        region.pitchRandomCents = getFloat (parsedRegion, "pitch_random", 0.0f);
        region.ampRandom        = getFloat (parsedRegion, "amp_random", 0.0f);
        region.delaySeconds     = getFloat (parsedRegion, "delay", 0.0f);

        // ---- keyswitch and articulation ------------------------------------
        region.swLoKey   = getNote (parsedRegion, "sw_lokey", -1);
        region.swHiKey   = getNote (parsedRegion, "sw_hikey", -1);
        region.swLast    = getNote (parsedRegion, "sw_last", -1);
        region.swDefault = getNote (parsedRegion, "sw_default", -1);

        const std::string label = parsedRegion.get ("sw_label");

        if (! label.empty())
            region.articulation = articulationFromLabel (label.c_str());
        else
            region.articulation = guessArticulation (samplePath);

        if (region.swLast >= 0)
        {
            Keyswitch ks;
            ks.note         = region.swLast;
            ks.label        = label.empty() ? std::string (toName (region.articulation)) : label;
            ks.articulation = region.articulation;
            keyswitchesByNote.emplace (ks.note, ks);
        }

        if (region.swDefault >= 0 && instrument->defaultKeyswitch < 0)
            instrument->defaultKeyswitch = region.swDefault;

        if (region.loKey > region.hiKey || region.loVel > region.hiVel)
        {
            result.warnings.push_back ("region at line " + std::to_string (parsedRegion.sourceLine)
                                       + " has an empty key or velocity range, skipped");
            continue;
        }

        instrument->regions.push_back (region);
    }

    if (instrument->regions.empty())
    {
        result.error = "no playable region found";
        return result;
    }

    for (auto& [note, ks] : keyswitchesByNote)
        instrument->keyswitches.push_back (ks);

    std::sort (instrument->keyswitches.begin(), instrument->keyswitches.end(),
               [] (const Keyswitch& a, const Keyswitch& b) { return a.note < b.note; });

    if (const auto it = parsed.control.find ("moe_name"); it != parsed.control.end())
        instrument->name = it->second;

    if (instrument->family == Family::other && ! instrument->name.empty())
        instrument->family = guessFamily (instrument->name);

    instrument->buildIndex();

    loadProgress.store (1.0f, std::memory_order_relaxed);
    result.instrument = instrument;
    return result;
}

} // namespace moe::bank
