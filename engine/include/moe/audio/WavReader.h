#pragma once

#include "moe/audio/AudioFileReader.h"

#include <fstream>

namespace moe::audio
{

/** RIFF/WAVE reader.

    Handles the formats orchestral libraries actually ship in:
      - PCM 8/16/24/32 bit integer
      - IEEE float 32/64 bit
      - WAVE_FORMAT_EXTENSIBLE (resolves the real format from the GUID)
      - RF64 / `ds64` for files above 4 GB
      - `smpl` chunk for loop points and root note

    Deliberately not handled: ADPCM, mu-law and other compressed variants. A
    sample library that ships those is not a library we want to stream anyway. */
class WavReader final : public AudioFileReader
{
public:
    WavReader() = default;

    /** Opens and parses the header. Check `isOpen()` afterwards. */
    explicit WavReader (const std::string& path);

    bool open (const std::string& path);
    bool isOpen() const noexcept { return fileIsOpen; }

    const AudioFileInfo& info() const noexcept override { return fileInfo; }

    SampleIndex read (float* const* destination,
                      int           numDestChannels,
                      SampleIndex   startFrame,
                      SampleIndex   numFrames) override;

    /** Reason the file was rejected, for diagnostics. Empty on success. */
    const std::string& error() const noexcept { return errorMessage; }

private:
    bool parseHeader();

    std::ifstream   stream;
    std::string     errorMessage;
    AudioFileInfo   fileInfo;

    bool            fileIsOpen      = false;
    bool            isFloatFormat   = false;
    int             bytesPerSample  = 0;
    int             bytesPerFrame   = 0;
    std::streamoff  dataOffset      = 0;

    std::vector<char> scratch;   ///< interleaved raw bytes, reused across reads
};

} // namespace moe::audio
