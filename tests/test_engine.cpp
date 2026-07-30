#include "doctest.h"

#include "TestHelpers.h"
#include "moe/OrchestraEngine.h"
#include "moe/bank/BankLoader.h"

#include <filesystem>
#include <fstream>

using namespace moe;
using namespace moe::test;

namespace
{

/** Builds a small but structurally complete bank on disk: two articulations,
    two velocity layers, two round-robins, a keyswitch each. It is the smallest
    fixture that still exercises every branch the real chain takes. */
std::string buildTestBank (const std::string& name)
{
    const std::string directory = scratchDirectory() + name + "/";
    std::filesystem::create_directories (directory);

    constexpr double sampleRate = 44100.0;

    auto writeSample = [&] (const std::string& file, int numFrames, double frequency, float amplitude)
    {
        std::vector<std::vector<float>> channels (2);
        channels[0] = sine (numFrames, frequency, sampleRate, amplitude);
        channels[1] = sine (numFrames, frequency * 1.003, sampleRate, amplitude);  // slight detune
        REQUIRE (writeWav (directory + file, channels, sampleRate));
    };

    // Long enough to require streaming past the default preload.
    writeSample ("sus_soft_rr1.wav", 60000, 220.0, 0.3f);
    writeSample ("sus_soft_rr2.wav", 60000, 221.0, 0.3f);
    writeSample ("sus_loud_rr1.wav", 60000, 220.0, 0.9f);
    writeSample ("sus_loud_rr2.wav", 60000, 221.0, 0.9f);
    writeSample ("stacc_rr1.wav",     4000, 220.0, 0.8f);
    writeSample ("stacc_rr2.wav",     4000, 221.0, 0.8f);

    const std::string sfz = R"(
// Test bank: sustains on keyswitch 24, staccato on keyswitch 25.
<control>
default_path=

<global>
ampeg_release=0.3
pitch_keycenter=57

<group>
sw_lokey=24 sw_hikey=25 sw_last=24 sw_default=24 sw_label=sustain
lokey=48 hikey=72 loop_mode=loop_continuous loop_start=1000 loop_end=50000
<region> sample=sus_soft_rr1.wav lovel=1  hivel=63  seq_length=2 seq_position=1
<region> sample=sus_soft_rr2.wav lovel=1  hivel=63  seq_length=2 seq_position=2
<region> sample=sus_loud_rr1.wav lovel=64 hivel=127 seq_length=2 seq_position=1
<region> sample=sus_loud_rr2.wav lovel=64 hivel=127 seq_length=2 seq_position=2

<group>
sw_lokey=24 sw_hikey=25 sw_last=25 sw_label=staccato
lokey=48 hikey=72 ampeg_release=0.05
<region> sample=stacc_rr1.wav seq_length=2 seq_position=1
<region> sample=stacc_rr2.wav seq_length=2 seq_position=2
)";

    const std::string sfzPath = directory + name + ".sfz";
    std::ofstream stream (sfzPath);
    stream << sfz;
    stream.close();

    return sfzPath;
}

bank::InstrumentPtr loadTestBank (const std::string& name)
{
    bank::BankLoader loader;
    bank::BankLoader::Options options;
    options.streaming = StreamingSettings::forProfile (StreamingProfile::balanced);

    const auto result = loader.loadSfzFile (buildTestBank (name), options);

    INFO ("bank load error: " << result.error);
    REQUIRE (result.ok());
    return result.instrument;
}

/** Renders `numBlocks` blocks and returns the peak of everything produced. */
float renderPeak (OrchestraEngine& engine, int numBlocks, int blockSize)
{
    std::vector<float> left (static_cast<std::size_t> (blockSize));
    std::vector<float> right (static_cast<std::size_t> (blockSize));
    float* channels[2] = { left.data(), right.data() };

    float peak = 0.0f;
    for (int block = 0; block < numBlocks; ++block)
    {
        engine.process (channels, blockSize);
        peak = std::fmax (peak, peakOf (left.data(), blockSize));
        peak = std::fmax (peak, peakOf (right.data(), blockSize));
    }

    return peak;
}

} // namespace

TEST_CASE ("a bank loads with its regions, keyswitches and articulations")
{
    const auto instrument = loadTestBank ("basic");

    CHECK (instrument->regions.size() == 6);
    CHECK (instrument->samples.size() == 6);
    CHECK (instrument->lowestKey == 48);
    CHECK (instrument->highestKey == 72);

    CHECK (instrument->keyswitches.size() == 2);
    CHECK (instrument->isKeyswitch (24));
    CHECK (instrument->isKeyswitch (25));
    CHECK_FALSE (instrument->isKeyswitch (60));

    CHECK (instrument->defaultKeyswitch == 24);

    // Root note, loop and release came from the <global> and <group> levels.
    CHECK (instrument->regions[0].rootKey == 57);
    CHECK (instrument->regions[0].loopMode == bank::LoopMode::loopContinuous);
    CHECK (instrument->regions[0].ampegRelease == doctest::Approx (0.3f));
    CHECK (instrument->regions[4].ampegRelease == doctest::Approx (0.05f));

    // The head of each sample is resident so notes start without disk access.
    CHECK (instrument->samples[0].preloadFrames > 0);
    CHECK (instrument->samples[0].preloadFrames < instrument->samples[0].info.numFrames);
}

TEST_CASE ("silence in, silence out")
{
    OrchestraEngine engine;
    engine.prepare (44100.0, 256);
    engine.getSection (0).setInstrument (loadTestBank ("silence"));

    CHECK (renderPeak (engine, 20, 256) == doctest::Approx (0.0f));
    CHECK (engine.getNumActiveVoices() == 0);
}

TEST_CASE ("a note produces audio, and stops when released")
{
    OrchestraEngine engine;
    engine.prepare (44100.0, 256);
    engine.getSection (0).setInstrument (loadTestBank ("note"));

    engine.noteOn (0, 60, 100);
    CHECK (engine.getNumActiveVoices() >= 1);

    CHECK (renderPeak (engine, 20, 256) > 0.01f);

    engine.noteOff (0, 60);

    // The release tail runs, then the voice frees itself.
    renderPeak (engine, 200, 256);
    CHECK (engine.getNumActiveVoices() == 0);
}

TEST_CASE ("keyswitches change articulation and make no sound themselves")
{
    OrchestraEngine engine;
    engine.prepare (44100.0, 256);

    auto& section = engine.getSection (0);
    section.setInstrument (loadTestBank ("keyswitch"));

    // Pressing the keyswitch must not start a voice.
    engine.noteOn (0, 25, 100);
    CHECK (engine.getNumActiveVoices() == 0);
    CHECK (renderPeak (engine, 4, 256) == doctest::Approx (0.0f));
    CHECK (section.keyswitches().getArticulation() == Articulation::staccato);

    engine.noteOn (0, 60, 100);
    CHECK (engine.getNumActiveVoices() >= 1);

    engine.noteOff (0, 60);
    engine.allNotesOff (true);

    engine.noteOn (0, 24, 100);
    CHECK (section.keyswitches().getArticulation() == Articulation::sustain);
}

TEST_CASE ("velocity selects the dynamic layer")
{
    OrchestraEngine engine;
    engine.prepare (44100.0, 512);
    engine.getSection (0).setInstrument (loadTestBank ("velocity"));

    // The bank's soft samples are recorded at a third of the loud ones, so the
    // rendered peak has to follow.
    engine.noteOn (0, 60, 20);
    const float softPeak = renderPeak (engine, 30, 512);
    engine.allNotesOff (true);
    renderPeak (engine, 40, 512);

    engine.noteOn (0, 60, 120);
    const float loudPeak = renderPeak (engine, 30, 512);

    CHECK (softPeak > 0.0f);
    CHECK (loudPeak > softPeak * 2.0f);
}

TEST_CASE ("CC1 drives level without retriggering the note")
{
    OrchestraEngine engine;
    engine.prepare (44100.0, 512);
    engine.getSection (0).setInstrument (loadTestBank ("dynamics"));

    engine.controller (0, perf::cc::modulation, 127);
    engine.noteOn (0, 60, 100);

    const float loud = renderPeak (engine, 20, 512);
    const int voicesBefore = engine.getNumActiveVoices();

    engine.controller (0, perf::cc::modulation, 0);
    renderPeak (engine, 20, 512);             // let the smoother settle
    const float soft = renderPeak (engine, 20, 512);

    CHECK (engine.getNumActiveVoices() == voicesBefore);   // no retrigger
    CHECK (soft < loud * 0.5f);
    CHECK (soft > 0.0f);                                   // pp stays audible
}

TEST_CASE ("the sustain pedal holds notes past their release")
{
    OrchestraEngine engine;
    engine.prepare (44100.0, 256);
    engine.getSection (0).setInstrument (loadTestBank ("pedal"));

    engine.controller (0, perf::cc::sustainPedal, 127);
    engine.noteOn (0, 60, 100);
    engine.noteOff (0, 60);

    renderPeak (engine, 100, 256);
    CHECK (engine.getNumActiveVoices() >= 1);   // still held by the pedal

    engine.controller (0, perf::cc::sustainPedal, 0);
    renderPeak (engine, 200, 256);
    CHECK (engine.getNumActiveVoices() == 0);
}

TEST_CASE ("legato retires the previous note and glides into the new one")
{
    OrchestraEngine engine;
    engine.prepare (44100.0, 256);

    auto& section = engine.getSection (0);
    section.setInstrument (loadTestBank ("legato"));

    engine.noteOn (0, 60, 100);
    renderPeak (engine, 10, 256);
    const int afterFirst = engine.getNumActiveVoices();
    REQUIRE (afterFirst >= 1);

    // Second note while the first is still held.
    engine.noteOn (0, 62, 100);
    renderPeak (engine, 60, 256);

    // The outgoing note fades quickly, so polyphony must not keep climbing.
    CHECK (engine.getNumActiveVoices() <= afterFirst);
    CHECK (renderPeak (engine, 10, 256) > 0.0f);
}

TEST_CASE ("sections are independent and respect mute and solo")
{
    OrchestraEngine engine;
    engine.prepare (44100.0, 256);

    engine.getSection (0).setInstrument (loadTestBank ("mix-a"));
    engine.getSection (1).setInstrument (loadTestBank ("mix-b"));

    engine.getSection (0).setMidiChannel (0);
    engine.getSection (1).setMidiChannel (1);

    engine.noteOn (0, 60, 100);
    engine.noteOn (1, 64, 100);
    CHECK (engine.getNumActiveVoices() >= 2);

    CHECK (renderPeak (engine, 10, 256) > 0.0f);

    engine.getSection (0).setMuted (true);
    engine.getSection (1).setMuted (true);
    CHECK (renderPeak (engine, 10, 256) == doctest::Approx (0.0f));

    engine.getSection (0).setMuted (false);
    CHECK (renderPeak (engine, 10, 256) > 0.0f);

    // Solo on the muted section overrides everything else.
    engine.getSection (1).setMuted (false);
    engine.getSection (1).setSoloed (true);
    const float soloed = renderPeak (engine, 10, 256);
    CHECK (soloed > 0.0f);
    CHECK (engine.getSection (0).getPeakLevel() == doctest::Approx (0.0f));
}

TEST_CASE ("the output stays finite and bounded under a dense chord")
{
    OrchestraEngine engine;
    engine.prepare (48000.0, 128, 64);
    engine.getSection (0).setInstrument (loadTestBank ("dense"));
    engine.setMasterGainDb (-12.0f);

    for (int note = 48; note <= 72; ++note)
        engine.noteOn (0, note, 110);

    std::vector<float> left (128), right (128);
    float* channels[2] = { left.data(), right.data() };

    for (int block = 0; block < 200; ++block)
    {
        engine.process (channels, 128);

        for (int i = 0; i < 128; ++i)
        {
            REQUIRE (std::isfinite (left[i]));
            REQUIRE (std::isfinite (right[i]));
        }
    }
}

TEST_CASE ("voice stealing keeps polyphony inside its limit")
{
    OrchestraEngine engine;
    engine.prepare (44100.0, 256, /*maxVoices*/ 8);
    engine.getSection (0).setInstrument (loadTestBank ("stealing"));

    for (int note = 48; note <= 72; ++note)
    {
        engine.noteOn (0, note, 100);
        CHECK (engine.getNumActiveVoices() <= 8);
    }

    renderPeak (engine, 20, 256);
    CHECK (engine.getNumActiveVoices() <= 8);
    CHECK (engine.getVoiceStealCount() > 0);
}

TEST_CASE ("a block larger than the prepared size is still rendered correctly")
{
    OrchestraEngine engine;
    engine.prepare (44100.0, 128);
    engine.getSection (0).setInstrument (loadTestBank ("blocksize"));

    engine.noteOn (0, 60, 100);

    // Hosts are allowed to exceed the block size they announced.
    std::vector<float> left (2048), right (2048);
    float* channels[2] = { left.data(), right.data() };

    engine.process (channels, 2048);

    CHECK (peakOf (left.data(), 2048) > 0.0f);
    for (int i = 0; i < 2048; ++i)
        REQUIRE (std::isfinite (left[i]));
}

TEST_CASE ("a section with no instrument is silently skipped")
{
    OrchestraEngine engine;
    engine.prepare (44100.0, 256);

    engine.noteOn (0, 60, 100);
    CHECK (engine.getNumActiveVoices() == 0);
    CHECK (renderPeak (engine, 10, 256) == doctest::Approx (0.0f));
}

TEST_CASE ("streaming past the preloaded head produces continuous audio")
{
    OrchestraEngine engine;
    engine.prepare (44100.0, 256);
    engine.getSection (0).setInstrument (loadTestBank ("streaming"));

    engine.noteOn (0, 57, 120);   // root note, so it plays at its recorded pitch

    std::vector<float> left (256), right (256);
    float* channels[2] = { left.data(), right.data() };

    // Well past the 32k-frame preload, into streamed territory.
    int silentBlocks = 0;
    for (int block = 0; block < 400; ++block)
    {
        engine.process (channels, 256);

        if (block > 20 && rmsOf (left.data(), 256) < 1.0e-5f)
            ++silentBlocks;
    }

    // A couple of starved blocks would be tolerable; a stream that never opened
    // would leave hundreds.
    CHECK (silentBlocks < 10);
    CHECK (engine.getNumActiveVoices() >= 1);
}
