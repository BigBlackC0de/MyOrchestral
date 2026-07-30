#pragma once

#include "moe/Types.h"

#include <memory>
#include <string>
#include <vector>

namespace moe::audio
{

/** Everything the engine needs to know about a sample file before playing it. */
struct AudioFileInfo
{
    int         numChannels   = 0;
    double      sampleRate    = 0.0;
    SampleIndex numFrames     = 0;
    int         bitsPerSample = 0;

    /** Loop points from the `smpl` chunk, when present. SFZ `loop_start` /
        `loop_end` override these. */
    bool        hasLoop   = false;
    SampleIndex loopStart = 0;
    SampleIndex loopEnd   = 0;

    /** Root note from the `smpl` chunk (-1 when absent). SFZ `pitch_keycenter`
        overrides it. */
    int rootNote = -1;

    bool isValid() const noexcept
    {
        return numChannels > 0 && sampleRate > 0.0 && numFrames > 0;
    }
};

/** Random-access decoder for one sample file.

    A reader owns its own file handle and is **not** thread safe: the streaming
    layer gives each concurrent reading slot its own instance, which keeps the
    audio path free of any lock around file access. */
class AudioFileReader
{
public:
    virtual ~AudioFileReader() = default;

    virtual const AudioFileInfo& info() const noexcept = 0;

    /** Reads deinterleaved float frames.

        @param destination  array of `numDestChannels` pointers, each with room
                            for `numFrames` floats
        @param startFrame   frame offset in the file
        @returns            frames actually written; short reads mean end of file

        When the file has fewer channels than requested, the last available
        channel is copied to the remaining destinations (mono -> stereo).
        Destination channels beyond the file's are never left uninitialised. */
    virtual SampleIndex read (float* const* destination,
                              int           numDestChannels,
                              SampleIndex   startFrame,
                              SampleIndex   numFrames) = 0;
};

/** Opens `path`, dispatching on file extension and magic bytes.
    Returns nullptr when the format is unsupported or the file is unreadable. */
std::unique_ptr<AudioFileReader> openAudioFile (const std::string& path);

} // namespace moe::audio
