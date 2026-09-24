#pragma once

#include "../../Assets/AudioClipSourceMetadata.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace experiment::cooked
{
    inline constexpr std::uint16_t kAudioClipArtifactVersion = 1u;
    inline constexpr std::size_t kAudioClipHeaderBytes = 72u;

    enum class AudioLoadMode : std::uint8_t { Auto, Resident, Stream };
    enum class AudioSpatialKind : std::uint8_t { PointMono, NonSpatial };

    struct CookedAudioClipHeader final
    {
        assets::AudioCodec codec{};
        AudioLoadMode loadMode{};
        AudioSpatialKind spatialKind{};
        std::uint8_t channels{};
        std::uint32_t sampleRate{};
        std::uint64_t frameCount{};
        std::uint64_t payloadOffset{ kAudioClipHeaderBytes };
        std::uint64_t payloadBytes{};
        Hash::Sha256Digest payloadSha256{};
    };

    [[nodiscard]] inline std::array<std::byte, kAudioClipHeaderBytes>
        WriteAudioClipHeader(const CookedAudioClipHeader& value) noexcept
    {
        std::array<std::byte, kAudioClipHeaderBytes> bytes{};
        const auto put = [&bytes](std::size_t offset, std::uint64_t number,
            unsigned width) noexcept
        {
            for (unsigned index = 0u; index < width; ++index)
                bytes[offset + index] = std::byte(number >> (index * 8u));
        };
        bytes[0] = std::byte{ 'C' }; bytes[1] = std::byte{ 'E' };
        bytes[2] = std::byte{ 'A' }; bytes[3] = std::byte{ 'C' };
        put(4u, kAudioClipArtifactVersion, 2u);
        put(6u, kAudioClipHeaderBytes, 2u);
        put(8u, static_cast<std::uint8_t>(value.codec), 1u);
        put(9u, static_cast<std::uint8_t>(value.loadMode), 1u);
        put(10u, static_cast<std::uint8_t>(value.spatialKind), 1u);
        put(11u, value.channels, 1u);
        put(12u, value.sampleRate, 4u);
        put(16u, value.frameCount, 8u);
        put(24u, value.payloadOffset, 8u);
        put(32u, value.payloadBytes, 8u);
        for (std::size_t index = 0u; index < value.payloadSha256.size(); ++index)
            bytes[40u + index] = std::byte{ value.payloadSha256[index] };
        return bytes;
    }

    // fileBytes is the bounded artifact extent supplied by the manifest/VFS.
    [[nodiscard]] inline bool ReadAudioClipHeader(
        std::span<const std::byte> bytes, std::uint64_t fileBytes,
        CookedAudioClipHeader& out) noexcept
    {
        if (bytes.size() < kAudioClipHeaderBytes || fileBytes < kAudioClipHeaderBytes
            || bytes[0] != std::byte{ 'C' } || bytes[1] != std::byte{ 'E' }
            || bytes[2] != std::byte{ 'A' } || bytes[3] != std::byte{ 'C' })
            return false;
        const auto get = [bytes](std::size_t offset, unsigned width) noexcept
        {
            std::uint64_t number = 0u;
            for (unsigned index = 0u; index < width; ++index)
                number |= std::uint64_t(std::to_integer<std::uint8_t>(
                    bytes[offset + index])) << (index * 8u);
            return number;
        };
        if (get(4u, 2u) != kAudioClipArtifactVersion
            || get(6u, 2u) != kAudioClipHeaderBytes) return false;
        CookedAudioClipHeader candidate{};
        candidate.codec = static_cast<assets::AudioCodec>(get(8u, 1u));
        candidate.loadMode = static_cast<AudioLoadMode>(get(9u, 1u));
        candidate.spatialKind = static_cast<AudioSpatialKind>(get(10u, 1u));
        candidate.channels = static_cast<std::uint8_t>(get(11u, 1u));
        candidate.sampleRate = static_cast<std::uint32_t>(get(12u, 4u));
        candidate.frameCount = get(16u, 8u);
        candidate.payloadOffset = get(24u, 8u);
        candidate.payloadBytes = get(32u, 8u);
        for (std::size_t index = 0u; index < candidate.payloadSha256.size(); ++index)
            candidate.payloadSha256[index] = std::to_integer<std::uint8_t>(
                bytes[40u + index]);
        if (candidate.codec > assets::AudioCodec::Flac
            || candidate.loadMode > AudioLoadMode::Stream
            || candidate.spatialKind > AudioSpatialKind::NonSpatial
            || candidate.channels == 0u || candidate.channels > 2u
            || candidate.sampleRate == 0u || candidate.frameCount == 0u
            || candidate.payloadOffset != kAudioClipHeaderBytes
            || candidate.payloadBytes == 0u
            || candidate.payloadBytes != fileBytes - candidate.payloadOffset
            || (candidate.spatialKind == AudioSpatialKind::PointMono
                && candidate.channels != 1u)) return false;
        bool hasDigest = false;
        for (const std::uint8_t value : candidate.payloadSha256)
            hasDigest |= value != 0u;
        if (!hasDigest) return false;
        out = candidate;
        return true;
    }
}
