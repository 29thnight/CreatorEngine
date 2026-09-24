#pragma once

#include "Sha256.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace assets
{
    inline constexpr std::uint32_t kAudioClipMetaSchemaVersion = 1u;

    enum class AudioCodec : std::uint8_t { Wav, Mp3, Flac };

    [[nodiscard]] inline std::string_view AudioCodecName(AudioCodec codec) noexcept
    {
        switch (codec)
        {
        case AudioCodec::Wav: return "Wav";
        case AudioCodec::Mp3: return "Mp3";
        case AudioCodec::Flac: return "Flac";
        }
        return {};
    }

    [[nodiscard]] inline bool IsAudioLoadMode(std::string_view value) noexcept
    {
        return value == "Auto" || value == "Resident" || value == "Stream";
    }

    [[nodiscard]] inline bool IsAudioSpatialKind(std::string_view value) noexcept
    {
        return value == "PointMono" || value == "NonSpatial";
    }

    [[nodiscard]] inline bool IsAudioClipSource(const std::filesystem::path& path)
    {
        std::string extension = path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
        return extension == ".wav" || extension == ".mp3" || extension == ".flac";
    }

    struct AudioClipSourceMetadata final
    {
        AudioCodec codec{};
        std::uint64_t payloadSize{};
        Hash::Sha256Digest sourceContentHash{};
    };

    // This is a source fingerprint and a cheap signature check, not a full
    // decoder. Cook must validate all encoded frames before publishing audio.
    // A later change to the source is detected by comparing size and digest.
    [[nodiscard]] inline bool InspectAudioClipSource(const std::filesystem::path& path,
        AudioClipSourceMetadata& out, std::string& error)
    {
        std::string extension = path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](unsigned char value) { return static_cast<char>(std::tolower(value)); });

        AudioClipSourceMetadata candidate{};
        if (extension == ".wav") candidate.codec = AudioCodec::Wav;
        else if (extension == ".mp3") candidate.codec = AudioCodec::Mp3;
        else if (extension == ".flac") candidate.codec = AudioCodec::Flac;
        else
        {
            error = "unsupported audio source extension";
            return false;
        }

        std::error_code fileError;
        const auto originalSize = std::filesystem::file_size(path, fileError);
        if (fileError || originalSize == 0u)
        {
            error = "audio source is empty or its size cannot be read";
            return false;
        }
        const auto originalTime = std::filesystem::last_write_time(path, fileError);
        if (fileError)
        {
            error = "audio source timestamp cannot be read";
            return false;
        }

        std::ifstream input(path, std::ios::binary);
        if (!input)
        {
            error = "audio source cannot be opened";
            return false;
        }
        Hash::Sha256 hash;
        std::array<char, 64u * 1024u> buffer{};
        std::array<unsigned char, 12u> prefix{};
        std::size_t prefixSize = 0u;
        std::uint64_t count = 0u;
        while (input)
        {
            input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const auto read = static_cast<std::size_t>(input.gcount());
            if (read == 0u) break;
            hash.Update(buffer.data(), read);
            const std::size_t copied = std::min(read, prefix.size() - prefixSize);
            for (std::size_t index = 0u; index < copied; ++index)
                prefix[prefixSize + index] = static_cast<unsigned char>(buffer[index]);
            prefixSize += copied;
            count += read;
        }
        if (!input.eof() || input.bad() || count != originalSize)
        {
            error = "audio source changed or could not be read completely";
            return false;
        }
        const auto finalSize = std::filesystem::file_size(path, fileError);
        if (fileError || finalSize != originalSize
            || std::filesystem::last_write_time(path, fileError) != originalTime || fileError)
        {
            error = "audio source changed while being inspected";
            return false;
        }

        const auto signature = [&prefix, prefixSize](std::size_t offset,
            std::string_view bytes) noexcept
        {
            if (offset + bytes.size() > prefixSize) return false;
            for (std::size_t index = 0u; index < bytes.size(); ++index)
                if (prefix[offset + index] != static_cast<unsigned char>(bytes[index]))
                    return false;
            return true;
        };
        const bool recognized = candidate.codec == AudioCodec::Wav
            ? count >= 44u && signature(0u, "RIFF") && signature(8u, "WAVE")
            : candidate.codec == AudioCodec::Flac
                ? count >= 42u && signature(0u, "fLaC")
                    && (prefix[4] & 0x7Fu) == 0u
                    && prefix[5] == 0u && prefix[6] == 0u && prefix[7] == 34u
                : (prefixSize >= 10u && signature(0u, "ID3")) || (prefixSize >= 4u
                    && prefix[0] == 0xFFu && (prefix[1] & 0xE0u) == 0xE0u);
        if (!recognized)
        {
            error = "audio source signature does not match its extension";
            return false;
        }

        candidate.payloadSize = count;
        candidate.sourceContentHash = hash.Finish();
        out = candidate;
        error.clear();
        return true;
    }
}
