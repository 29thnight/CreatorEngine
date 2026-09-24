#pragma once

#include "CookedAssetManifest.h"
#include "CookedAudioClipFormat.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace experiment::cooked
{
    // Paths are normalized virtual paths under Derived/. Implementations may
    // address a loose cooked tree or an entry inside a mounted pak.
    class ArtifactByteSource
    {
    public:
        virtual ~ArtifactByteSource() = default;
        [[nodiscard]] virtual bool Size(std::string_view path,
            std::uint64_t& out, std::string& failure) const = 0;
        [[nodiscard]] virtual bool ReadAt(std::string_view path,
            std::uint64_t offset, std::span<std::byte> out,
            std::string& failure) const = 0;
    };

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

    class LooseArtifactByteSource final : public ArtifactByteSource
    {
    public:
        explicit LooseArtifactByteSource(std::filesystem::path root)
            : root_(std::move(root)) {}

        [[nodiscard]] bool Size(std::string_view path,
            std::uint64_t& out, std::string& failure) const override
        {
            std::filesystem::path file;
            if (!Resolve(path, file, failure)) return false;
            std::error_code error;
            if (!std::filesystem::is_regular_file(file, error) || error)
            {
                failure = "cooked artifact file is missing";
                return false;
            }
            out = std::filesystem::file_size(file, error);
            if (error)
            {
                failure = "cooked artifact size cannot be read";
                return false;
            }
            return true;
        }

        [[nodiscard]] bool ReadAt(std::string_view path,
            std::uint64_t offset, std::span<std::byte> out,
            std::string& failure) const override
        {
            std::uint64_t size = 0u;
            if (!Size(path, size, failure)) return false;
            if (offset > size || out.size() > size - offset
                || offset > static_cast<std::uint64_t>(
                    (std::numeric_limits<std::streamoff>::max)())
                || out.size() > static_cast<std::size_t>(
                    (std::numeric_limits<std::streamsize>::max)()))
            {
                failure = "cooked artifact read exceeds file extent";
                return false;
            }
            if (out.empty()) return true;
            std::filesystem::path file;
            if (!Resolve(path, file, failure)) return false;
            std::ifstream input(file, std::ios::binary);
            input.seekg(static_cast<std::streamoff>(offset));
            input.read(reinterpret_cast<char*>(out.data()),
                static_cast<std::streamsize>(out.size()));
            if (!input || input.gcount() != static_cast<std::streamsize>(out.size()))
            {
                failure = "cooked artifact range cannot be read";
                return false;
            }
            return true;
        }

    private:
        [[nodiscard]] bool Resolve(std::string_view virtualPath,
            std::filesystem::path& out, std::string& failure) const
        {
            if (!IsAudioArtifactVirtualPath(virtualPath))
            {
                failure = "cooked artifact virtual path is invalid";
                return false;
            }
            std::error_code error;
            const std::filesystem::path root =
                std::filesystem::weakly_canonical(root_, error);
            if (error || root.empty())
            {
                failure = "cooked root cannot be resolved";
                return false;
            }
            const auto* first = reinterpret_cast<const char8_t*>(virtualPath.data());
            const std::filesystem::path relative = std::filesystem::path(
                std::u8string(first, first + virtualPath.size()));
            const std::filesystem::path candidate =
                std::filesystem::weakly_canonical(root / relative, error);
            if (error || candidate.empty())
            {
                failure = "cooked artifact path cannot be resolved";
                return false;
            }
            const std::filesystem::path under = candidate.lexically_relative(root);
            if (under.empty() || under.is_absolute())
            {
                failure = "cooked artifact escapes root";
                return false;
            }
            for (const auto& segment : under)
            {
                if (segment == "..")
                {
                    failure = "cooked artifact escapes root";
                    return false;
                }
            }
            out = candidate;
            return true;
        }

        std::filesystem::path root_{};
    };

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
    // The caller must keep the mounted bytes immutable for the clip lifetime;
    // these path-based readers do not pin an OS file identity across reads.
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
