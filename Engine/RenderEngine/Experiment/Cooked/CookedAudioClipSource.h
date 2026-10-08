#pragma once

#include "ArtifactByteSource.h"
#include "CookedAssetManifest.h"
#include "CookedAudioClipFormat.h"
#include "../../Assets/AssetIdentityProfile.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace experiment::cooked
{
    [[nodiscard]] inline bool IsAudioArtifactVirtualPath(
        std::string_view path) noexcept
    {
        if (!path.starts_with("Derived/Audio/") || !path.ends_with(".ceac")
            || path.back() == '/' || path.find('\\') != std::string_view::npos
            || path.find(':') != std::string_view::npos
            || path.find('\0') != std::string_view::npos) return false;
        std::size_t begin = 0u;
        while (begin < path.size())
        {
            const std::size_t end = path.find('/', begin);
            const std::string_view part = path.substr(begin,
                end == std::string_view::npos ? path.size() - begin : end - begin);
            if (part.empty() || part == "." || part == "..") return false;
            if (end == std::string_view::npos) break;
            begin = end + 1u;
        }
        return true;
    }

    [[nodiscard]] inline bool IsMaterialProgramArtifactVirtualPath(std::string_view path)
    {
        AssetId graphId;
        const auto lastSlash = path.rfind('/');
        return lastSlash != std::string_view::npos && path.ends_with(".lxmaterial") && path.size() >= lastSlash + 12 &&
               (TryParseCanonicalAssetId(path.substr(lastSlash + 1, path.size() - lastSlash - 12), graphId) ||
                assets::TryParseCanonicalUuidV8(path.substr(lastSlash + 1, path.size() - lastSlash - 12),
                                                graphId.value)) &&
               path == MakeDerivedMaterialProgramArtifactPath(graphId);
    }

    [[nodiscard]] inline bool IsCollisionGeometryArtifactVirtualPath(std::string_view path) noexcept
    {
        constexpr std::string_view prefix = "Derived/CollisionGeometry/";
        if (!path.starts_with(prefix) || !path.ends_with(".cepg") || path.size() != prefix.size() + 3 + 41) return false;

        AssetId id;
        const auto uuid = path.substr(prefix.size() + 3, 36);
        return path[prefix.size() + 2] == '/' && TryParseCanonicalAssetId(uuid, id) &&
               path.substr(prefix.size(), 2) == uuid.substr(0, 2);
    }

    [[nodiscard]] inline bool IsSoundAssetArtifactVirtualPath(std::string_view path)
    {
        const auto slash = path.rfind('/');
        if (slash == std::string_view::npos || path.size() - slash != 42u)
        {
            return false;
        }
        AssetId id;
        if (!TryParseCanonicalAssetId(path.substr(slash + 1u, 36u), id))
        {
            return false;
        }
        return path == MakeDerivedSoundGraphArtifactPath(id) || path == MakeDerivedSoundPresetArtifactPath(id);
    }

    class CookedAudioClipSource final
    {
    public:
        [[nodiscard]] const AssetId& Id() const noexcept { return id_; }
        [[nodiscard]] const CookedAudioClipHeader& Metadata() const noexcept
        {
            return metadata_;
        }
        [[nodiscard]] std::uint64_t PayloadSize() const noexcept
        {
            return metadata_.payloadBytes;
        }
        [[nodiscard]] bool ReadPayload(std::uint64_t offset,
            std::span<std::byte> out, std::string& failure) const
        {
            if (!bytes_ || offset > metadata_.payloadBytes
                || out.size() > metadata_.payloadBytes - offset)
            {
                failure = "audio payload read exceeds bounded range";
                return false;
            }
            return bytes_->ReadAt(path_, metadata_.payloadOffset + offset,
                out, failure);
        }

        friend bool OpenCookedAudioClipEntry(const CookedAssetManifestEntry&,
            std::shared_ptr<const ArtifactByteSource>, CookedAudioClipSource&,
            std::string&);

    private:
        AssetId id_{};
        std::shared_ptr<const ArtifactByteSource> bytes_{};
        std::string path_{};
        CookedAudioClipHeader metadata_{};
    };

    // Opening streams through the artifact once to verify both the CEMF
    // digest and CEAC payload digest. Later reads remain bounded by CEAC.
    // The caller must keep the mounted bytes immutable for the clip lifetime.
    // Windows loose mounts pin a read handle denying writes; other byte sources
    // retain their own immutability contract.
    [[nodiscard]] inline bool OpenCookedAudioClipEntry(
        const CookedAssetManifestEntry& entry,
        std::shared_ptr<const ArtifactByteSource> bytes,
        CookedAudioClipSource& out, std::string& failure)
    {
        if (!bytes || entry.kind != CookedAssetKind::AudioClip
            || entry.formatVersion != kAudioClipArtifactVersion
            || !IsAudioArtifactVirtualPath(entry.artifactPath)
            || entry.artifactPath != MakeDerivedAudioClipArtifactPath(entry.assetId))
        {
            failure = "manifest entry is not a supported audio clip";
            return false;
        }
        std::uint64_t fileSize = 0u;
        if (!bytes->Size(entry.artifactPath, fileSize, failure)) return false;
        if (fileSize != entry.byteSize || fileSize < kAudioClipHeaderBytes)
        {
            failure = "audio artifact size differs from manifest";
            return false;
        }
        std::array<std::byte, kAudioClipHeaderBytes> headerBytes{};
        if (!bytes->ReadAt(entry.artifactPath, 0u, headerBytes, failure)) return false;
        CookedAudioClipHeader metadata{};
        if (!ReadAudioClipHeader(headerBytes, fileSize, metadata))
        {
            failure = "audio artifact header is invalid";
            return false;
        }

        Hash::Sha256 artifactHash;
        Hash::Sha256 payloadHash;
        artifactHash.Update(headerBytes.data(), headerBytes.size());
        std::array<std::byte, 64u * 1024u> block{};
        std::uint64_t position = 0u;
        while (position < metadata.payloadBytes)
        {
            const std::size_t count = static_cast<std::size_t>(
                std::min<std::uint64_t>(block.size(), metadata.payloadBytes - position));
            if (!bytes->ReadAt(entry.artifactPath,
                metadata.payloadOffset + position,
                std::span(block.data(), count), failure)) return false;
            artifactHash.Update(block.data(), count);
            payloadHash.Update(block.data(), count);
            position += count;
        }
        if (artifactHash.Finish() != entry.contentSha256
            || payloadHash.Finish() != metadata.payloadSha256)
        {
            failure = "audio artifact or payload hash differs from manifest";
            return false;
        }

        CookedAudioClipSource candidate;
        candidate.id_ = entry.assetId;
        candidate.bytes_ = std::move(bytes);
        candidate.path_ = entry.artifactPath;
        candidate.metadata_ = metadata;
        out = std::move(candidate);
        failure.clear();
        return true;
    }
}
