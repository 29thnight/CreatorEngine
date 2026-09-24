#include "AudioDecodeValidation.h"

#include "Assets/AudioClipSourceMetadata.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <span>
#include <vector>

// AssetCooker is a separate executable. This is its sole miniaudio
// implementation TU; Editor/Player still link their existing FMOD path.
#define MA_NO_DEVICE_IO
#define MA_NO_ENGINE
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_THREADING
#define MA_NO_ENCODING
#define MINIAUDIO_IMPLEMENTATION
#pragma warning(push, 0)
#include "../../ThirdParty/miniaudio/miniaudio.h"
#pragma warning(pop)

namespace audio_cook
{
    namespace
    {
        bool ReadAt(std::ifstream& stream, std::uint64_t offset,
            std::span<std::uint8_t> bytes)
        {
            if (offset > static_cast<std::uint64_t>(
                (std::numeric_limits<std::streamoff>::max)())) return false;
            stream.clear();
            stream.seekg(static_cast<std::streamoff>(offset));
            if (!stream) return false;
            stream.read(reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
            return stream.gcount() == static_cast<std::streamsize>(bytes.size());
        }

        std::uint32_t Big32(std::span<const std::uint8_t, 4> bytes) noexcept
        {
            return (static_cast<std::uint32_t>(bytes[0]) << 24u)
                | (static_cast<std::uint32_t>(bytes[1]) << 16u)
                | (static_cast<std::uint32_t>(bytes[2]) << 8u)
                | static_cast<std::uint32_t>(bytes[3]);
        }

        bool ValidateMp3Frames(std::ifstream& stream, std::uint64_t size,
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
                    end -= 128u;
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

        bool ValidateContainer(std::ifstream& stream, std::uint64_t size,
            assets::AudioCodec codec, std::uint64_t& declaredFrames,
            std::string& failure)
        {
            declaredFrames = 0u;
            if (codec == assets::AudioCodec::Mp3)
                return ValidateMp3Frames(stream, size, failure);

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
                packed = (packed << 8u) | header[index];
            declaredFrames = packed & ((1ull << 36u) - 1u);
            if (declaredFrames == 0u)
            {
                failure = "FLAC must declare a nonzero total sample count";
                return false;
            }
            return true;
        }
    }

    bool ValidateEncodedSource(const std::filesystem::path& source,
        DecodedSource& out, std::string& failure)
    {
        assets::AudioClipSourceMetadata stamp{};
        if (!assets::InspectAudioClipSource(source, stamp, failure)) return false;

        std::ifstream stream(source, std::ios::binary);
        if (!stream)
        {
            failure = "audio source cannot be reopened";
            return false;
        }
        std::uint64_t declaredFrames = 0u;
        if (!ValidateContainer(stream, stamp.payloadSize, stamp.codec,
            declaredFrames, failure)) return false;

        ma_decoder decoder{};
        const ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0u, 0u);
        const ma_result opened = ma_decoder_init_file_w(source.c_str(), &config, &decoder);
        if (opened != MA_SUCCESS)
        {
            failure = std::string("audio decoder open failed: ") + ma_result_description(opened);
            return false;
        }
        ma_format format = ma_format_unknown;
        ma_uint32 channels = 0u;
        ma_uint32 sampleRate = 0u;
        const ma_result formatResult = ma_decoder_get_data_format(&decoder,
            &format, &channels, &sampleRate, nullptr, 0u);
        if (formatResult != MA_SUCCESS || format != ma_format_f32
            || channels == 0u || channels > 2u || sampleRate == 0u)
        {
            ma_decoder_uninit(&decoder);
            failure = "audio decoder reported unsupported channels/sample rate";
            return false;
        }

        std::vector<float> pcm(4096u * channels);
        std::uint64_t decodedFrames = 0u;
        for (;;)
        {
            ma_uint64 framesRead = 0u;
            const ma_result read = ma_decoder_read_pcm_frames(&decoder,
                pcm.data(), 4096u, &framesRead);
            if (read != MA_SUCCESS && read != MA_AT_END)
            {
                ma_decoder_uninit(&decoder);
                failure = std::string("audio decoder read failed: ")
                    + ma_result_description(read);
                return false;
            }
            decodedFrames += framesRead;
            if (read == MA_AT_END) break;
            if (framesRead == 0u)
            {
                ma_decoder_uninit(&decoder);
                failure = "audio decoder made no progress before EOF";
                return false;
            }
        }
        ma_decoder_uninit(&decoder);
        if (decodedFrames == 0u || (declaredFrames != 0u && decodedFrames != declaredFrames))
        {
            failure = "decoded PCM frame count does not match container metadata";
            return false;
        }

        out = DecodedSource{ channels, sampleRate, decodedFrames };
        failure.clear();
        return true;
    }
}
