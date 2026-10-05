#pragma once

#include "../../RenderEngine/Assets/AudioClipSourceMetadata.h"

#include <array>
#include <fstream>
#include <limits>
#include <span>

// Shared container validation for Editor import and AssetCooker. Decoder
// implementations stay inside their existing, separate miniaudio TUs.
namespace wave
{
    namespace SourceValidation
    {
        inline bool ReadAt(std::ifstream& stream, std::uint64_t offset,
            std::span<std::uint8_t> bytes)
        {
            if (offset > static_cast<std::uint64_t>(
                (std::numeric_limits<std::streamoff>::max)()))
            {
                return false;
            }
            stream.clear();
            stream.seekg(static_cast<std::streamoff>(offset));
            if (!stream)
            {
                return false;
            }
            stream.read(reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
            return stream.gcount() == static_cast<std::streamsize>(bytes.size());
        }

        inline std::uint32_t Big32(std::span<const std::uint8_t, 4> bytes) noexcept
        {
            return (static_cast<std::uint32_t>(bytes[0]) << 24u)
                | (static_cast<std::uint32_t>(bytes[1]) << 16u)
                | (static_cast<std::uint32_t>(bytes[2]) << 8u)
                | static_cast<std::uint32_t>(bytes[3]);
        }

        inline bool ValidateMp3Frames(std::ifstream& stream, std::uint64_t size,
            std::string& failure)
        {
            std::uint64_t begin = 0u;
            std::array<std::uint8_t, 10> tag{};
            if (!ReadAt(stream, 0u, tag))
            {
                failure = "MP3 header cannot be read";
                return false;
            }
            if (tag[0] == 'I' && tag[1] == 'D' && tag[2] == '3')
            {
                if (tag[3] < 2u || tag[3] > 4u
                    || tag[6] >= 128u || tag[7] >= 128u
                    || tag[8] >= 128u || tag[9] >= 128u)
                {
                    failure = "MP3 ID3v2 header is invalid";
                    return false;
                }
                const std::uint32_t tagBytes =
                    (static_cast<std::uint32_t>(tag[6]) << 21u)
                    | (static_cast<std::uint32_t>(tag[7]) << 14u)
                    | (static_cast<std::uint32_t>(tag[8]) << 7u)
                    | static_cast<std::uint32_t>(tag[9]);
                begin = 10u + tagBytes + ((tag[3] == 4u && (tag[5] & 0x10u)) ? 10u : 0u);
            }

            std::uint64_t end = size;
            if (size >= 128u)
            {
                std::array<std::uint8_t, 3> trailer{};
                if (!ReadAt(stream, size - 128u, trailer))
                {
                    failure = "MP3 trailer cannot be read";
                    return false;
                }
                if (trailer[0] == 'T' && trailer[1] == 'A' && trailer[2] == 'G')
                {
                    end -= 128u;
                }
            }
            if (begin >= end)
            {
                failure = "MP3 has no audio frames";
                return false;
            }

            constexpr std::array<std::uint16_t, 16> mpeg1Kbps{
                0,32,40,48,56,64,80,96,112,128,160,192,224,256,320,0 };
            constexpr std::array<std::uint16_t, 16> mpeg2Kbps{
                0,8,16,24,32,40,48,56,64,80,96,112,128,144,160,0 };
            constexpr std::array<std::uint32_t, 3> baseRates{ 44100u, 48000u, 32000u };
            std::uint64_t cursor = begin;
            std::uint32_t frameCount = 0u;
            std::uint32_t firstRate = 0u;
            std::uint32_t firstVersion = 0u;
            while (cursor < end)
            {
                std::array<std::uint8_t, 4> raw{};
                if (end - cursor < raw.size() || !ReadAt(stream, cursor, raw))
                {
                    failure = "MP3 ends inside a frame header";
                    return false;
                }
                const std::uint32_t header = Big32(raw);
                const std::uint32_t version = (header >> 19u) & 3u;
                const std::uint32_t layer = (header >> 17u) & 3u;
                const std::uint32_t bitrateIndex = (header >> 12u) & 15u;
                const std::uint32_t rateIndex = (header >> 10u) & 3u;
                const std::uint32_t padding = (header >> 9u) & 1u;
                if ((header & 0xFFE00000u) != 0xFFE00000u
                    || version == 1u || layer != 1u
                    || bitrateIndex == 0u || bitrateIndex == 15u || rateIndex == 3u)
                {
                    failure = "MP3 frame header is invalid or unsupported";
                    return false;
                }
                const std::uint32_t rate = baseRates[rateIndex]
                    / (version == 3u ? 1u : version == 2u ? 2u : 4u);
                if (frameCount != 0u && (rate != firstRate || version != firstVersion))
                {
                    failure = "MP3 changes sample rate or MPEG version midstream";
                    return false;
                }
                firstRate = rate;
                firstVersion = version;
                const std::uint32_t kbps = version == 3u
                    ? mpeg1Kbps[bitrateIndex] : mpeg2Kbps[bitrateIndex];
                const std::uint32_t frameBytes =
                    (version == 3u ? 144000u : 72000u) * kbps / rate + padding;
                if (frameBytes < 4u || frameBytes > end - cursor)
                {
                    failure = "MP3 ends inside an encoded frame";
                    return false;
                }
                cursor += frameBytes;
                ++frameCount;
            }
            if (frameCount == 0u || cursor != end)
            {
                failure = "MP3 frame chain does not fill the payload";
                return false;
            }
            return true;
        }

        inline bool ValidateContainer(std::ifstream& stream, std::uint64_t size,
            assets::AudioCodec codec, std::uint64_t& declaredFrames,
            std::string& failure)
        {
            declaredFrames = 0u;
            if (codec == assets::AudioCodec::Mp3)
            {
                return ValidateMp3Frames(stream, size, failure);
            }

            if (codec == assets::AudioCodec::Wav)
            {
                std::array<std::uint8_t, 12> header{};
                if (!ReadAt(stream, 0u, header))
                {
                    failure = "WAV RIFF header cannot be read";
                    return false;
                }
                const std::uint64_t declaredSize = 8u
                    + static_cast<std::uint32_t>(header[4])
                    + (static_cast<std::uint32_t>(header[5]) << 8u)
                    + (static_cast<std::uint32_t>(header[6]) << 16u)
                    + (static_cast<std::uint32_t>(header[7]) << 24u);
                if (declaredSize != size)
                {
                    failure = "WAV RIFF size does not match file extent";
                    return false;
                }
                return true;
            }

            std::array<std::uint8_t, 42> header{};
            if (!ReadAt(stream, 0u, header))
            {
                failure = "FLAC STREAMINFO cannot be read";
                return false;
            }
            std::uint64_t packed = 0u;
            for (std::size_t index = 18u; index < 26u; ++index)
            {
                packed = (packed << 8u) | header[index];
            }
            declaredFrames = packed & ((1ull << 36u) - 1u);
            if (declaredFrames == 0u)
            {
                failure = "FLAC must declare a nonzero total sample count";
                return false;
            }
            return true;
        }
    }

    inline bool ValidateAudioSourceContainer(const std::filesystem::path& source,
        const assets::AudioClipSourceMetadata& stamp, std::uint64_t& declaredFrames, std::string& error)
    {
        std::ifstream stream(source, std::ios::binary);
        if (!stream)
        {
            error = "Audio source cannot be opened for container validation";
            return false;
        }
        return SourceValidation::ValidateContainer(stream, stamp.payloadSize, stamp.codec, declaredFrames, error);
    }

    inline bool ValidateAudioSourceContainer(const std::filesystem::path& source,
        std::uint64_t& declaredFrames, std::string& error)
    {
        assets::AudioClipSourceMetadata stamp;
        return assets::InspectAudioClipSource(source, stamp, error)
            && ValidateAudioSourceContainer(source, stamp, declaredFrames, error);
    }
}
