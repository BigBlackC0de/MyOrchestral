#pragma once

#include "moe/Types.h"
#include "moe/bank/Instrument.h"
#include "moe/bank/RegionSelector.h"
#include "moe/dsp/ConvolutionReverb.h"
#include "moe/humanize/Humanizer.h"
#include "moe/perf/KeyswitchRouter.h"
#include "moe/perf/LegatoDetector.h"
#include "moe/perf/MidiState.h"
#include "moe/space/StageProcessor.h"
#include "moe/stream/SampleStreamer.h"
#include "moe/voice/VoiceManager.h"

#include <array>
#include <atomic>
#include <string>
#include <vector>

namespace moe
{

/** One orchestral desk: an instrument, where it sits, and how it responds.

    A section owns no audio buffers — the engine renders it into a shared
    scratch bus — and no threads. It is the unit the mixer view maps onto. */
class Section
{
public:
    void prepare (double sampleRate, int maximumBlockSize);

    /** Installs a bank. The previous instrument stays alive until its voices
        finish, so this is safe while audio is running. */
    void setInstrument (bank::InstrumentPtr newInstrument);
    const bank::InstrumentPtr& getInstrument() const noexcept { return instrument; }

    /** Renames the section. Marks the name as the user's, so loading a bank no
        longer overwrites it. */
    void setName (std::string newName)
    {
        name         = std::move (newName);
        nameIsCustom = true;
    }

    /** Sets the name only if the user has not chosen one. Loading a bank uses
        this, which is what makes a freshly loaded desk show "Violins I" rather
        than "Section 3". */
    void setDefaultName (std::string newName)
    {
        if (! nameIsCustom)
            name = std::move (newName);
    }

    const std::string& getName() const noexcept { return name; }

    /** MIDI channel this section listens to, 0-based. -1 means omni. */
    void setMidiChannel (int channel) noexcept { midiChannel = channel; }
    int  getMidiChannel() const noexcept { return midiChannel; }

    bool listensTo (int channel) const noexcept
    {
        return midiChannel < 0 || midiChannel == channel;
    }

    void  setGainDb (float db) noexcept;
    float getGainDb() const noexcept { return gainDb; }
    float getLinearGain() const noexcept { return linearGain; }

    /** Multiplies every region's attack and release for this section. 1.0 leaves
        the bank as recorded; below 1 tightens it, above 1 softens it. Bounded so
        a slider cannot produce a note that never starts or never ends. */
    void setEnvelopeScales (float attack, float release) noexcept
    {
        attackScale  = std::clamp (attack, 0.25f, 8.0f);
        releaseScale = std::clamp (release, 0.1f, 8.0f);
    }

    float getAttackScale() const noexcept  { return attackScale; }
    float getReleaseScale() const noexcept { return releaseScale; }

    void setMuted (bool shouldMute) noexcept  { muted = shouldMute; }
    void setSoloed (bool shouldSolo) noexcept { soloed = shouldSolo; }
    bool isMuted() const noexcept  { return muted; }
    bool isSoloed() const noexcept { return soloed; }

    space::StageProcessor&       stage() noexcept       { return stageProcessor; }
    const space::StageProcessor& stage() const noexcept { return stageProcessor; }

    perf::KeyswitchRouter&       keyswitches() noexcept { return router; }
    perf::LegatoDetector&        legato() noexcept      { return legatoDetector; }
    humanize::Humanizer&         humanizer() noexcept   { return humanizerInstance; }
    bank::SelectionState&        selection() noexcept   { return selectionState; }

    // Const overloads: the UI reads this state every repaint and has no business
    // holding a mutable reference to it.
    const perf::KeyswitchRouter& keyswitches() const noexcept { return router; }
    const perf::LegatoDetector&  legato() const noexcept      { return legatoDetector; }
    const humanize::Humanizer&   humanizer() const noexcept   { return humanizerInstance; }

    /** Peak level of the last processed block, for metering. */
    float getPeakLevel() const noexcept { return peakLevel.load (std::memory_order_relaxed); }
    void  setPeakLevel (float value) noexcept { peakLevel.store (value, std::memory_order_relaxed); }

private:
    std::string          name         = "Section";
    bool                 nameIsCustom = false;
    bank::InstrumentPtr  instrument;
    int                  midiChannel  = -1;

    float gainDb       = 0.0f;
    float linearGain   = 1.0f;
    float attackScale  = 1.0f;
    float releaseScale = 1.0f;
    bool  muted        = false;
    bool  soloed       = false;

    space::StageProcessor  stageProcessor;
    perf::KeyswitchRouter  router;
    perf::LegatoDetector   legatoDetector;
    humanize::Humanizer    humanizerInstance;
    bank::SelectionState   selectionState;

    std::atomic<float> peakLevel { 0.0f };
};

/** The whole instrument: sections, voices, streaming, space and reverb.

    Free of JUCE by design — the plugin layer is a thin adapter over this. All
    real-time entry points (`noteOn`, `process`, the controller handlers) are
    allocation-free. `prepare`, `setSectionInstrument` and reverb loading are
    not, and must be called off the audio thread. */
class OrchestraEngine
{
public:
    static constexpr int kMaxSections = 16;

    OrchestraEngine();
    ~OrchestraEngine();

    /** Allocates everything. Safe to call again on a sample-rate change. */
    void prepare (double sampleRate, int maximumBlockSize, int maxVoices = kDefaultMaxVoices);
    void releaseResources();

    // ---- configuration (not audio-thread safe) -----------------------------

    int  getNumSections() const noexcept { return static_cast<int> (sections.size()); }
    Section& getSection (int index) noexcept { return sections[static_cast<std::size_t> (index)]; }
    const Section& getSection (int index) const noexcept
    {
        return sections[static_cast<std::size_t> (index)];
    }

    /** Installs a bank and seats the section on the stage.

        Prefer this over `getSection(i).setInstrument(...)`: it works out which
        seat the desk should take from the families already loaded, so a second
        string section lands beside the first rather than on top of it.

        @returns the stage position chosen, so a caller that owns the position
                 elsewhere (the plugin, whose parameters are authoritative) can
                 mirror it. */
    space::StagePosition setSectionInstrument (int index, bank::InstrumentPtr instrument);

    void setStreamingProfile (StreamingProfile profile);
    StreamingProfile getStreamingProfile() const noexcept { return streamingProfile; }

    dsp::ConvolutionReverb& reverb() noexcept { return convolution; }

    void  setReverbMix (float wet) noexcept { reverbMix = std::clamp (wet, 0.0f, 1.0f); }
    float getReverbMix() const noexcept { return reverbMix; }

    void  setMasterGainDb (float db) noexcept;
    float getMasterGainDb() const noexcept { return masterGainDb; }

    // ---- MIDI (audio thread) -----------------------------------------------

    void noteOn (int channel, int note, int velocity);
    void noteOff (int channel, int note);
    void controller (int channel, int number, int value);
    void pitchBend (int channel, float semitones);
    void channelPressure (int channel, float value);
    void allNotesOff (bool immediately = false);

    // ---- audio (audio thread) ----------------------------------------------

    /** Renders into `output`, overwriting it. `output` must have two channels. */
    void process (float* const* output, int numSamples) noexcept;

    /** Total latency the engine introduces, in samples. */
    int getLatencySamples() const noexcept;

    // ---- diagnostics --------------------------------------------------------

    int getNumActiveVoices() const noexcept { return voices.getNumActiveVoices(); }
    int getNumActiveStreams() const noexcept { return streamer.activeSlotCount(); }
    std::uint32_t getStreamUnderruns() const noexcept { return streamer.totalUnderruns(); }
    std::uint32_t getVoiceStealCount() const noexcept { return voices.getStealCount(); }

private:
    void        processBlock (float* const* output, int numSamples) noexcept;
    bool        anySectionSoloed() const noexcept;
    static float decibelsToGain (float db) noexcept;

    // A fixed array rather than a vector: Section holds an atomic meter value
    // and is therefore neither copyable nor movable, and there is no reason for
    // the section count to be dynamic.
    std::array<Section, kMaxSections>                sections;
    std::array<perf::ChannelState, kNumMidiChannels> channels;

    voice::VoiceManager    voices;
    stream::StreamManager  streamer;
    dsp::ConvolutionReverb convolution;

    StreamingProfile streamingProfile = StreamingProfile::balanced;

    double sampleRate     = 44100.0;
    int    maxBlockSize   = 512;
    float  masterGainDb   = 0.0f;
    float  masterGain     = 1.0f;
    float  reverbMix      = 0.28f;

    // Scratch buses, allocated once in prepare().
    std::vector<float> sectionBusStorage;
    std::vector<float> reverbBusStorage;
    std::array<float*, 2> sectionBus {};
    std::array<float*, 2> reverbBus {};
};

} // namespace moe
