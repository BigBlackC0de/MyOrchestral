/*
    moe_render — plays a bank offline and writes a WAV.

    Two reasons this exists rather than only the plugin:

      1. You can hear the engine without building or installing a plugin, and
         without a DAW. When something sounds wrong, this is the fastest way to
         find out whether it is the engine or the host.
      2. It is a smoke test that exercises the real path — bank loading,
         articulation selection, streaming, spatialisation, reverb — on whatever
         library you point it at, which is exactly what a unit test cannot do.

    Examples:
        moe_render --bank banks/placeholder-strings/placeholder-strings.sfz \
                   --out /tmp/chord.wav --notes 48,55,60,64 --seconds 4

        moe_render --bank strings.sfz --out /tmp/line.wav \
                   --sequence 60:0.5,62:0.5,64:1.0 --legato --cc1 90
*/

#include "moe/OrchestraEngine.h"
#include "moe/bank/BankLoader.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace
{

struct Options
{
    std::string bankPath;
    std::string outputPath = "moe-render.wav";
    std::string irPath;

    std::vector<int>   notes { 60 };
    std::vector<std::pair<int, double>> sequence;   // note, duration in seconds

    int    velocity   = 100;
    int    cc1        = 100;
    int    cc11       = 127;
    int    keyswitch  = -1;
    double seconds    = 3.0;
    double tailSeconds = 2.0;
    double sampleRate = 48000.0;
    int    blockSize  = 256;
    float  reverbMix  = 0.25f;
    bool   listOnly   = false;
};

void printUsage()
{
    std::cout << R"(moe_render — render a bank offline

  --bank <file.sfz>      bank to load                        (required)
  --out <file.wav>       output file                         (moe-render.wav)
  --notes 60,64,67       chord to play                       (60)
  --sequence 60:0.5,62:1 melodic line: note:seconds pairs
  --keyswitch <n>        press this keyswitch first
  --velocity <1-127>                                         (100)
  --cc1 <0-127>          dynamics                            (100)
  --cc11 <0-127>         expression                          (127)
  --seconds <s>          how long notes are held             (3.0)
  --tail <s>             extra time rendered after release   (2.0)
  --ir <file.wav>        impulse response for the reverb
  --mix <0-1>            reverb amount                       (0.25)
  --rate <hz>                                                (48000)
  --block <n>            processing block size               (256)
  --list                 print what the bank contains and exit
)";
}

std::vector<std::string> split (const std::string& text, char separator)
{
    std::vector<std::string> parts;
    std::stringstream stream (text);
    std::string       item;

    while (std::getline (stream, item, separator))
        if (! item.empty())
            parts.push_back (item);

    return parts;
}

bool parseArguments (int argc, char** argv, Options& options)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string argument = argv[i];
        auto next = [&] () -> std::string
        {
            return (i + 1 < argc) ? argv[++i] : std::string{};
        };

        if (argument == "--bank")            options.bankPath = next();
        else if (argument == "--out")        options.outputPath = next();
        else if (argument == "--ir")         options.irPath = next();
        else if (argument == "--velocity")   options.velocity = std::stoi (next());
        else if (argument == "--cc1")        options.cc1 = std::stoi (next());
        else if (argument == "--cc11")       options.cc11 = std::stoi (next());
        else if (argument == "--keyswitch")  options.keyswitch = std::stoi (next());
        else if (argument == "--seconds")    options.seconds = std::stod (next());
        else if (argument == "--tail")       options.tailSeconds = std::stod (next());
        else if (argument == "--rate")       options.sampleRate = std::stod (next());
        else if (argument == "--block")      options.blockSize = std::stoi (next());
        else if (argument == "--mix")        options.reverbMix = std::stof (next());
        else if (argument == "--list")       options.listOnly = true;
        else if (argument == "--help" || argument == "-h") { printUsage(); return false; }
        else if (argument == "--notes")
        {
            options.notes.clear();
            for (const auto& part : split (next(), ','))
                options.notes.push_back (std::stoi (part));
        }
        else if (argument == "--sequence")
        {
            for (const auto& part : split (next(), ','))
            {
                const auto fields = split (part, ':');
                if (fields.size() == 2)
                    options.sequence.emplace_back (std::stoi (fields[0]), std::stod (fields[1]));
            }
        }
        else
        {
            std::cerr << "unknown option: " << argument << "\n\n";
            printUsage();
            return false;
        }
    }

    if (options.bankPath.empty())
    {
        printUsage();
        return false;
    }

    return true;
}

bool writeWav (const std::string& path,
               const std::vector<float>& left,
               const std::vector<float>& right,
               double sampleRate)
{
    std::ofstream stream (path, std::ios::binary);
    if (! stream)
        return false;

    const auto frames     = static_cast<std::uint32_t> (left.size());
    const std::uint32_t dataBytes = frames * 2 * 3;   // stereo, 24-bit
    const std::uint32_t riffSize  = 4 + 24 + 8 + dataBytes;

    auto u32 = [&stream] (std::uint32_t v)
    {
        char b[4] = { char (v & 0xFF), char ((v >> 8) & 0xFF),
                      char ((v >> 16) & 0xFF), char ((v >> 24) & 0xFF) };
        stream.write (b, 4);
    };
    auto u16 = [&stream] (std::uint16_t v)
    {
        char b[2] = { char (v & 0xFF), char ((v >> 8) & 0xFF) };
        stream.write (b, 2);
    };

    stream.write ("RIFF", 4); u32 (riffSize); stream.write ("WAVE", 4);
    stream.write ("fmt ", 4); u32 (16);
    u16 (1); u16 (2);
    u32 (static_cast<std::uint32_t> (sampleRate));
    u32 (static_cast<std::uint32_t> (sampleRate) * 2 * 3);
    u16 (6); u16 (24);
    stream.write ("data", 4); u32 (dataBytes);

    for (std::size_t i = 0; i < left.size(); ++i)
        for (const float value : { left[i], right[i] })
        {
            const float clamped = std::clamp (value, -1.0f, 1.0f);
            const auto  scaled  = static_cast<std::int32_t> (clamped * 8388607.0f);

            char bytes[3] = { char (scaled & 0xFF),
                              char ((scaled >> 8) & 0xFF),
                              char ((scaled >> 16) & 0xFF) };
            stream.write (bytes, 3);
        }

    return stream.good();
}

} // namespace

int main (int argc, char** argv)
{
    Options options;
    if (! parseArguments (argc, argv, options))
        return 1;

    // ---- load the bank ----------------------------------------------------
    moe::bank::BankLoader loader;
    moe::bank::BankLoader::Options loadOptions;
    loadOptions.streaming = moe::StreamingSettings::forProfile (moe::StreamingProfile::render);

    std::cout << "Loading " << options.bankPath << " ...\n";
    const auto loaded = loader.loadSfzFile (options.bankPath, loadOptions);

    for (const auto& warning : loaded.warnings)
        std::cerr << "  warning: " << warning << "\n";

    if (! loaded.ok())
    {
        std::cerr << "failed: " << loaded.error << "\n";
        return 2;
    }

    const auto& instrument = *loaded.instrument;

    std::cout << "  name .......... " << instrument.name << "\n"
              << "  family ........ " << moe::toName (instrument.family) << "\n"
              << "  regions ....... " << instrument.regions.size() << "\n"
              << "  samples ....... " << instrument.samples.size() << "\n"
              << "  range ......... " << instrument.lowestKey << ".." << instrument.highestKey << "\n"
              << "  articulations . ";

    for (const auto articulation : instrument.availableArticulations())
        std::cout << moe::toName (articulation) << " ";
    std::cout << "\n";

    if (! instrument.keyswitches.empty())
    {
        std::cout << "  keyswitches ... ";
        for (const auto& ks : instrument.keyswitches)
            std::cout << ks.note << ":" << ks.label << " ";
        std::cout << "\n";
    }

    if (options.listOnly)
        return 0;

    // ---- set up the engine ------------------------------------------------
    moe::OrchestraEngine engine;
    engine.prepare (options.sampleRate, options.blockSize);
    engine.getSection (0).setMidiChannel (0);
    engine.setSectionInstrument (0, loaded.instrument);
    engine.setReverbMix (options.reverbMix);

    if (! options.irPath.empty())
    {
        if (engine.reverb().loadImpulseResponseFile (options.irPath))
            std::cout << "  reverb ........ " << options.irPath << "\n";
        else
            std::cerr << "  warning: could not load IR " << options.irPath << "\n";
    }

    // ---- build the performance --------------------------------------------
    struct Event { double time; bool on; int note; };
    std::vector<Event> events;

    double totalSeconds = 0.0;

    if (! options.sequence.empty())
    {
        double cursor = 0.0;
        for (const auto& [note, duration] : options.sequence)
        {
            events.push_back ({ cursor, true, note });
            // Overlap slightly so the engine's legato detection fires, which is
            // what --sequence is for.
            events.push_back ({ cursor + duration * 1.08, false, note });
            cursor += duration;
        }
        totalSeconds = cursor + options.tailSeconds;
    }
    else
    {
        for (const int note : options.notes)
        {
            events.push_back ({ 0.0, true, note });
            events.push_back ({ options.seconds, false, note });
        }
        totalSeconds = options.seconds + options.tailSeconds;
    }

    std::sort (events.begin(), events.end(),
               [] (const Event& a, const Event& b) { return a.time < b.time; });

    // ---- render ------------------------------------------------------------
    engine.controller (0, moe::perf::cc::modulation, options.cc1);
    engine.controller (0, moe::perf::cc::expression, options.cc11);

    if (options.keyswitch >= 0)
        engine.noteOn (0, options.keyswitch, 100);

    const auto totalFrames = static_cast<std::size_t> (totalSeconds * options.sampleRate);

    std::vector<float> outputLeft (totalFrames, 0.0f);
    std::vector<float> outputRight (totalFrames, 0.0f);

    std::vector<float> blockLeft (static_cast<std::size_t> (options.blockSize));
    std::vector<float> blockRight (static_cast<std::size_t> (options.blockSize));
    float* channels[2] = { blockLeft.data(), blockRight.data() };

    std::size_t nextEvent = 0;
    std::size_t position  = 0;

    while (position < totalFrames)
    {
        const auto blockFrames = std::min (static_cast<std::size_t> (options.blockSize),
                                            totalFrames - position);

        // Events are applied at block boundaries. Sample-accurate placement is
        // the plugin's job; here it would only obscure what is being tested.
        const double blockTime = static_cast<double> (position) / options.sampleRate;
        while (nextEvent < events.size() && events[nextEvent].time <= blockTime)
        {
            const auto& event = events[nextEvent];
            if (event.on)
                engine.noteOn (0, event.note, options.velocity);
            else
                engine.noteOff (0, event.note);
            ++nextEvent;
        }

        engine.process (channels, static_cast<int> (blockFrames));

        std::copy_n (blockLeft.begin(), blockFrames, outputLeft.begin() + static_cast<std::ptrdiff_t> (position));
        std::copy_n (blockRight.begin(), blockFrames, outputRight.begin() + static_cast<std::ptrdiff_t> (position));

        position += blockFrames;
    }

    float peak = 0.0f;
    for (std::size_t i = 0; i < totalFrames; ++i)
        peak = std::max ({ peak, std::abs (outputLeft[i]), std::abs (outputRight[i]) });

    std::cout << "\nRendered " << totalSeconds << " s"
              << "  peak " << (peak > 0.0f ? 20.0f * std::log10 (peak) : -144.0f) << " dBFS"
              << "  streams " << engine.getNumActiveStreams()
              << "  underruns " << engine.getStreamUnderruns() << "\n";

    if (peak <= 0.0f)
        std::cerr << "warning: the output is silent — check the note range and keyswitch\n";

    if (! writeWav (options.outputPath, outputLeft, outputRight, options.sampleRate))
    {
        std::cerr << "could not write " << options.outputPath << "\n";
        return 3;
    }

    std::cout << "Wrote " << options.outputPath << "\n";
    return 0;
}
