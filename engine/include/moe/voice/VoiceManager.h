#pragma once

#include "moe/Types.h"
#include "moe/stream/SampleStreamer.h"
#include "moe/voice/Voice.h"

#include <cstdint>
#include <vector>

namespace moe::voice
{

/** Fixed pool of voices with a stealing policy.

    The pool never grows: `prepare()` allocates it and `startVoice()` is
    allocation-free. When every voice is busy one is stolen, in this order of
    preference:

      1. a voice already in its release tail — nobody misses it;
      2. the quietest voice, if it is below an audibility threshold;
      3. the oldest voice.

    Stealing always fades out over a few milliseconds rather than cutting, since
    a hard cut is a click and a click is more audible than any note it saves. */
class VoiceManager
{
public:
    void prepare (double sampleRate, int blockSize, int maxVoices = kDefaultMaxVoices);

    void setStreamManager (stream::StreamManager* manager) noexcept { streamManager = manager; }

    /** @returns the voice that was started, or nullptr when nothing was free
                 and nothing could be stolen */
    Voice* startVoice (const VoiceStartInfo& info);

    /** Releases every voice on `channel` playing `note`. When `sustainDown` is
        set the voices are flagged instead, and released by `releaseSustained`. */
    void noteOff (int channel, int note, bool sustainDown);

    /** Releases everything that was held by the sustain pedal. */
    void releaseSustained (int channel);

    /** Applies `off_by`: silences voices whose exclusive group matches. Used for
        hi-hat style mute groups and for articulations that cut each other. */
    void silenceGroup (int sectionId, int group, float fadeSeconds = 0.006f);

    /** Fades out every voice of a section — used when its bank is swapped. */
    void stopSection (int sectionId, bool immediately = false);

    void allNotesOff (bool immediately = false);

    /** Renders every active voice into `output`, additively. */
    void renderAdd (float* const* output, int numSamples);

    /** Renders only the voices belonging to `sectionId`. The engine calls this
        once per section so each one can be spatialised independently. */
    void renderSection (int sectionId, float* const* output, int numSamples);

    /** Fades one specific note of a section with a chosen release. This is how
        a legato transition retires the note it is leaving. */
    void releaseNote (int sectionId, int note, float releaseSeconds);

    // ---- live control, applied to every matching sounding voice ------------
    void setDynamics (int sectionId, float value);
    void setExpression (int sectionId, float value);
    void setSectionGain (int sectionId, float linearGain);
    void setPitchBend (int sectionId, float semitones);

    int getNumActiveVoices() const noexcept;
    int getNumVoices() const noexcept { return static_cast<int> (voices.size()); }

    /** How many voices had to be stolen since the last reset — a direct measure
        of whether the polyphony setting is high enough for the part being played. */
    std::uint32_t getStealCount() const noexcept { return stealCount; }
    void resetStealCount() noexcept { stealCount = 0; }

private:
    Voice* findFreeVoice();
    Voice* stealVoice();

    std::vector<Voice>     voices;
    stream::StreamManager* streamManager = nullptr;

    std::uint64_t startCounter = 0;
    std::uint32_t stealCount   = 0;
};

} // namespace moe::voice
