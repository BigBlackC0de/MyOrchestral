#pragma once

#include "moe/Types.h"
#include "moe/bank/Instrument.h"
#include "moe/dsp/OnePole.h"
#include "moe/dsp/SmoothedValue.h"
#include "moe/stream/SampleStreamer.h"
#include "moe/voice/Envelope.h"

#include <array>

namespace moe::voice
{

/** Frames staged per pull from the preload buffer or the stream ring. Sized to
    amortise the ring's atomics over a block without bloating a 256-voice pool. */
inline constexpr int kStagingFrames = 512;

struct VoiceStartInfo
{
    const bank::Instrument* instrument  = nullptr;
    int                     regionIndex = -1;

    int   midiNote     = 60;
    int   midiChannel  = 0;
    int   velocity     = 100;
    int   sectionId    = 0;
    float selectionGain = 1.0f;     ///< from region selection (velocity + crossfades)

    /** Legato: where the glide starts, in semitones relative to the target note,
        and how long it takes. Zero disables portamento. */
    float portamentoSemitones = 0.0f;
    float portamentoSeconds   = 0.0f;

    /** Attack scaling for a legato re-trigger: a bowed string joining a phrase
        does not re-attack at full strength. */
    float attackScale = 1.0f;

    // Humanisation, resolved by the caller so the voice stays deterministic.
    float humanisePitchCents   = 0.0f;
    float humaniseGainDb       = 0.0f;
    float humaniseDelaySeconds = 0.0f;
};

/** One sounding sample.

    Voices are pre-allocated by VoiceManager and reused; `start()` performs no
    allocation and is safe on the audio thread. */
class Voice
{
public:
    void prepare (double sampleRate, int blockSize);

    /** Begins playback. `streamManager` may be null for fully resident banks. */
    void start (const VoiceStartInfo& info, stream::StreamManager* streamManager);

    void noteOff();

    /** Click-free forced stop, for voice stealing and exclusive groups. */
    void fadeOutAndStop (float seconds = 0.006f);

    /** Frees the streaming slot and marks the voice reusable. */
    void reset();

    /** Adds this voice into a stereo buffer. Never allocates.
        @returns false once the voice has finished and can be recycled */
    bool renderAdd (float* const* output, int numSamples);

    // ---- live control ------------------------------------------------------

    /** 0..1 expression control, normally CC1. Drives level and brightness, and
        the CC crossfade between dynamic layers. */
    void setDynamics (float value) noexcept;

    /** 0..1, normally CC11. A pure level trim, independent of timbre. */
    void setExpression (float value) noexcept;

    void setSectionGain (float linearGain) noexcept;
    void setPitchBendSemitones (float semitones) noexcept;

    // ---- queries -----------------------------------------------------------

    bool isActive() const noexcept   { return active; }
    bool isReleasing() const noexcept { return envelope.isReleasing(); }
    int  getMidiNote() const noexcept { return midiNote; }
    int  getMidiChannel() const noexcept { return midiChannel; }
    int  getSectionId() const noexcept { return sectionId; }
    int  getExclusiveGroup() const noexcept { return exclusiveGroup; }
    int  getOffBy() const noexcept { return offByGroup; }
    float getCurrentLevel() const noexcept { return envelope.currentLevel(); }
    std::uint64_t getStartOrder() const noexcept { return startOrder; }
    void setStartOrder (std::uint64_t order) noexcept { startOrder = order; }

    /** True when the voice keeps sounding after note-off because the sustain
        pedal is down; VoiceManager uses this to release it later. */
    bool isSustained() const noexcept { return sustained; }
    void setSustained (bool shouldSustain) noexcept { sustained = shouldSustain; }

private:
    bool  refillStaging();
    float pullFrame (int channel);
    void  advanceSourceWindow();
    void  updatePitchRatio();

    // ---- identity ----------------------------------------------------------
    const bank::Instrument* instrument = nullptr;
    const bank::Region*     region     = nullptr;
    const bank::SampleFile* sample     = nullptr;

    int  midiNote    = 60;
    int  midiChannel = 0;
    int  velocity    = 100;
    int  sectionId   = 0;
    int  exclusiveGroup = 0;
    int  offByGroup     = 0;

    std::uint64_t startOrder = 0;

    bool active    = false;
    bool sustained = false;

    // ---- source reading ----------------------------------------------------
    stream::StreamManager* streamManager = nullptr;
    stream::StreamSlot*    streamSlot    = nullptr;

    SampleIndex sourcePosition = 0;     ///< next frame to stage, in file frames
    SampleIndex endFrame       = 0;
    bool        useStream      = false;
    bool        sourceEnded    = false;

    std::array<std::array<float, kStagingFrames>, 2> staging {};
    int stagingFilled = 0;
    int stagingRead   = 0;

    /** 4-sample interpolation window per channel, plus the fractional phase. */
    std::array<std::array<float, 4>, 2> window {};
    double phase = 0.0;

    // ---- pitch -------------------------------------------------------------
    double baseRatio    = 1.0;   ///< key transposition and sample-rate conversion
    double currentRatio = 1.0;
    float  pitchBendSemitones = 0.0f;
    dsp::SmoothedValue portamento;      ///< in semitones, glides to 0

    // ---- amplitude ---------------------------------------------------------
    Envelope           envelope;
    dsp::SmoothedValue dynamicsGain;
    dsp::SmoothedValue expressionGain;
    dsp::SmoothedValue sectionGain;
    dsp::OnePole       toneFilter;

    float staticGain = 1.0f;    ///< region volume, velocity, selection, humanisation
    float gainLeft   = 1.0f;
    float gainRight  = 1.0f;

    int   delayCounter = 0;     ///< `delay=` plus humanised timing offset

    double sampleRate = 44100.0;
};

} // namespace moe::voice
