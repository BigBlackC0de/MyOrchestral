#include "moe/audio/WavReader.h"

#include <algorithm>
#include <cstring>

namespace moe::audio
{

namespace
{

std::uint16_t readU16 (const char* p) noexcept
{
    return static_cast<std::uint16_t> (static_cast<unsigned char> (p[0])
                                       | (static_cast<unsigned char> (p[1]) << 8));
}

std::uint32_t readU32 (const char* p) noexcept
{
    return static_cast<std::uint32_t> (static_cast<unsigned char> (p[0])
                                       | (static_cast<unsigned char> (p[1]) << 8)
                                       | (static_cast<unsigned char> (p[2]) << 16)
                                       | (static_cast<std::uint32_t> (static_cast<unsigned char> (p[3])) << 24));
}

std::uint64_t readU64 (const char* p) noexcept
{
    return static_cast<std::uint64_t> (readU32 (p))
           | (static_cast<std::uint64_t> (readU32 (p + 4)) << 32);
}

constexpr std::uint16_t kFormatPcm        = 0x0001;
constexpr std::uint16_t kFormatIeeeFloat  = 0x0003;
constexpr std::uint16_t kFormatExtensible = 0xFFFE;

} // namespace

WavReader::WavReader (const std::string& path)
{
    open (path);
}

bool WavReader::open (const std::string& path)
{
    fileIsOpen   = false;
    errorMessage.clear();
    fileInfo     = {};

    stream.close();
    stream.clear();
    stream.open (path, std::ios::binary);

    if (! stream)
    {
        errorMessage = "cannot open file";
        return false;
    }

    if (! parseHeader())
        return false;

    fileIsOpen = true;
    return true;
}

bool WavReader::parseHeader()
{
    char header[12] = {};
    stream.read (header, 12);
    if (stream.gcount() != 12)
    {
        errorMessage = "file too short";
        return false;
    }

    const bool isRiff = std::memcmp (header, "RIFF", 4) == 0;
    const bool isRf64 = std::memcmp (header, "RF64", 4) == 0;

    if ((! isRiff && ! isRf64) || std::memcmp (header + 8, "WAVE", 4) != 0)
    {
        errorMessage = "not a RIFF/WAVE file";
        return false;
    }

    std::uint64_t dataSizeBytes = 0;
    bool          haveFormat    = false;
    bool          haveData      = false;

    // RF64 stores real sizes in `ds64`; the RIFF fields are then 0xFFFFFFFF.
    std::uint64_t rf64DataSize = 0;

    // Scan every chunk to the end of the file rather than stopping at `data`:
    // `smpl` (loop points, root note) is usually written after it.
    while (stream)
    {
        char chunkHeader[8];
        stream.read (chunkHeader, 8);
        if (stream.gcount() != 8)
            break;

        const std::uint32_t declaredSize = readU32 (chunkHeader + 4);
        std::uint64_t       chunkSize    = declaredSize;
        const std::streamoff chunkStart  = stream.tellg();

        if (std::memcmp (chunkHeader, "ds64", 4) == 0)
        {
            std::vector<char> body (static_cast<std::size_t> (chunkSize));
            stream.read (body.data(), static_cast<std::streamsize> (body.size()));
            if (body.size() >= 16)
                rf64DataSize = readU64 (body.data() + 8);
        }
        else if (std::memcmp (chunkHeader, "fmt ", 4) == 0)
        {
            if (chunkSize < 16)
            {
                errorMessage = "truncated fmt chunk";
                return false;
            }

            std::vector<char> body (static_cast<std::size_t> (chunkSize));
            stream.read (body.data(), static_cast<std::streamsize> (chunkSize));

            std::uint16_t formatTag = readU16 (body.data());
            fileInfo.numChannels    = readU16 (body.data() + 2);
            fileInfo.sampleRate     = static_cast<double> (readU32 (body.data() + 4));
            fileInfo.bitsPerSample  = readU16 (body.data() + 14);

            if (formatTag == kFormatExtensible && chunkSize >= 40)
            {
                // The real format is the first two bytes of the sub-format GUID.
                formatTag = readU16 (body.data() + 24);

                // `validBitsPerSample` is more trustworthy than the container size
                // only when it is a sane multiple of 8; some encoders leave it at 0.
                const std::uint16_t validBits = readU16 (body.data() + 18);
                if (validBits > 0 && validBits <= fileInfo.bitsPerSample && validBits % 8 == 0)
                    fileInfo.bitsPerSample = validBits;
            }

            isFloatFormat = (formatTag == kFormatIeeeFloat);

            if (formatTag != kFormatPcm && formatTag != kFormatIeeeFloat)
            {
                errorMessage = "unsupported WAV encoding (compressed formats are not supported)";
                return false;
            }

            if (fileInfo.numChannels <= 0 || fileInfo.numChannels > 64)
            {
                errorMessage = "implausible channel count";
                return false;
            }

            if (fileInfo.bitsPerSample != 8 && fileInfo.bitsPerSample != 16
                && fileInfo.bitsPerSample != 24 && fileInfo.bitsPerSample != 32
                && fileInfo.bitsPerSample != 64)
            {
                errorMessage = "unsupported bit depth";
                return false;
            }

            bytesPerSample = fileInfo.bitsPerSample / 8;
            bytesPerFrame  = bytesPerSample * fileInfo.numChannels;
            haveFormat     = true;
        }
        else if (std::memcmp (chunkHeader, "data", 4) == 0)
        {
            dataOffset    = chunkStart;
            dataSizeBytes = (declaredSize == 0xFFFFFFFFu && rf64DataSize > 0) ? rf64DataSize
                                                                              : chunkSize;
            haveData      = true;

            // Do not read the payload; just skip past it to keep scanning for
            // `smpl`, which usually sits after `data`.
            stream.seekg (chunkStart + static_cast<std::streamoff> (dataSizeBytes), std::ios::beg);
            chunkSize = dataSizeBytes;
        }
        else if (std::memcmp (chunkHeader, "smpl", 4) == 0)
        {
            std::vector<char> body (static_cast<std::size_t> (chunkSize));
            stream.read (body.data(), static_cast<std::streamsize> (chunkSize));

            if (body.size() >= 36)
            {
                fileInfo.rootNote = static_cast<int> (readU32 (body.data() + 12));
                if (fileInfo.rootNote < 0 || fileInfo.rootNote > 127)
                    fileInfo.rootNote = -1;

                const std::uint32_t numLoops = readU32 (body.data() + 28);
                if (numLoops > 0 && body.size() >= 36 + 24)
                {
                    fileInfo.hasLoop   = true;
                    fileInfo.loopStart = readU32 (body.data() + 36 + 8);
                    fileInfo.loopEnd   = readU32 (body.data() + 36 + 12);
                }
            }
        }
        else
        {
            stream.seekg (chunkStart + static_cast<std::streamoff> (chunkSize), std::ios::beg);
        }

        // RIFF chunks are word aligned.
        if (chunkSize % 2 != 0)
            stream.seekg (1, std::ios::cur);
    }

    if (! haveFormat)
    {
        errorMessage = "missing fmt chunk";
        return false;
    }

    if (! haveData)
    {
        errorMessage = "missing data chunk";
        return false;
    }

    fileInfo.numFrames = bytesPerFrame > 0
                             ? static_cast<SampleIndex> (dataSizeBytes / static_cast<std::uint64_t> (bytesPerFrame))
                             : 0;

    if (fileInfo.hasLoop
        && (fileInfo.loopEnd <= fileInfo.loopStart || fileInfo.loopEnd > fileInfo.numFrames))
        fileInfo.hasLoop = false;

    if (! fileInfo.isValid())
    {
        errorMessage = "no audio frames";
        return false;
    }

    stream.clear();   // the scan above ran to EOF on most files
    return true;
}

SampleIndex WavReader::read (float* const* destination,
                             int           numDestChannels,
                             SampleIndex   startFrame,
                             SampleIndex   numFrames)
{
    if (! fileIsOpen || destination == nullptr || numDestChannels <= 0 || numFrames <= 0)
        return 0;

    if (startFrame >= fileInfo.numFrames || startFrame < 0)
        return 0;

    const SampleIndex available = std::min (numFrames, fileInfo.numFrames - startFrame);
    const std::size_t byteCount = static_cast<std::size_t> (available)
                                  * static_cast<std::size_t> (bytesPerFrame);

    if (scratch.size() < byteCount)
        scratch.resize (byteCount);

    stream.clear();
    stream.seekg (dataOffset + startFrame * bytesPerFrame, std::ios::beg);
    stream.read (scratch.data(), static_cast<std::streamsize> (byteCount));

    const auto bytesRead   = static_cast<std::size_t> (stream.gcount());
    const SampleIndex read = static_cast<SampleIndex> (bytesRead / static_cast<std::size_t> (bytesPerFrame));

    const int srcChannels  = fileInfo.numChannels;
    const int copyChannels = std::min (numDestChannels, srcChannels);

    for (int ch = 0; ch < copyChannels; ++ch)
    {
        float*      out    = destination[ch];
        const char* base   = scratch.data() + ch * bytesPerSample;

        for (SampleIndex i = 0; i < read; ++i)
        {
            const char* p = base + i * bytesPerFrame;

            if (isFloatFormat)
            {
                if (bytesPerSample == 4)
                {
                    float value;
                    std::memcpy (&value, p, sizeof (value));
                    out[i] = value;
                }
                else
                {
                    double value;
                    std::memcpy (&value, p, sizeof (value));
                    out[i] = static_cast<float> (value);
                }
            }
            else
            {
                switch (bytesPerSample)
                {
                    case 1:   // 8-bit PCM is unsigned by definition
                        out[i] = (static_cast<float> (static_cast<unsigned char> (*p)) - 128.0f) / 128.0f;
                        break;

                    case 2:
                    {
                        const auto value = static_cast<std::int16_t> (readU16 (p));
                        out[i] = static_cast<float> (value) / 32768.0f;
                        break;
                    }

                    case 3:
                    {
                        std::int32_t value = (static_cast<unsigned char> (p[0]))
                                             | (static_cast<unsigned char> (p[1]) << 8)
                                             | (static_cast<std::int32_t> (static_cast<signed char> (p[2])) << 16);
                        out[i] = static_cast<float> (value) / 8388608.0f;
                        break;
                    }

                    case 4:
                    {
                        const auto value = static_cast<std::int32_t> (readU32 (p));
                        out[i] = static_cast<float> (value) / 2147483648.0f;
                        break;
                    }

                    default:
                        out[i] = 0.0f;
                        break;
                }
            }
        }
    }

    // Mono source into a stereo voice: duplicate rather than store twice on disk.
    for (int ch = copyChannels; ch < numDestChannels; ++ch)
    {
        const float* source = destination[copyChannels - 1];
        std::copy (source, source + read, destination[ch]);
    }

    return read;
}

//==============================================================================
std::unique_ptr<AudioFileReader> openAudioFile (const std::string& path)
{
    auto reader = std::make_unique<WavReader> (path);
    if (reader->isOpen())
        return reader;

    return nullptr;
}

} // namespace moe::audio
