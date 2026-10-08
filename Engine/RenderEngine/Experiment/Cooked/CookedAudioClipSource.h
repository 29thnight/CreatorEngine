#pragma once

#include "CookedAssetManifest.h"
#include "CookedAudioClipFormat.h"
#include "../../Assets/AssetIdentityProfile.h"

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
#if defined(_WIN32)
#include <cstdio>
#include <io.h>
#include <share.h>
#include <mutex>
#include <unordered_map>
#endif

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

    class LooseArtifactByteSource final : public ArtifactByteSource
    {
    public:
        explicit LooseArtifactByteSource(std::filesystem::path root)
            : root_(std::move(root)) {}

        [[nodiscard]] bool Size(std::string_view path,
            std::uint64_t& out, std::string& failure) const override
        {
#if defined(_WIN32)
            const auto pinned = Pin(path, failure);
            if (!pinned)
            {
                return false;
            }
            std::lock_guard lock(pinned->mutex);
            const auto size = _filelengthi64(_fileno(pinned->file));
            if (size < 0)
            {
                failure = "pinned cooked artifact size cannot be read";
                return false;
            }
            out = static_cast<std::uint64_t>(size);
            return true;
#else
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
#endif
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
#if defined(_WIN32)
            const auto pinned = Pin(path, failure);
            if (!pinned)
            {
                return false;
            }
            std::lock_guard lock(pinned->mutex);
            if (_fseeki64(pinned->file, static_cast<__int64>(offset), SEEK_SET) != 0 ||
                std::fread(out.data(), 1, out.size(), pinned->file) != out.size())
            {
                failure = "pinned cooked artifact range cannot be read";
                return false;
            }
            return true;
#else
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
#endif
        }

    private:
#if defined(_WIN32)
        struct PinnedFile
        {
            std::FILE* file{};
            std::mutex mutex;

            ~PinnedFile()
            {
                if (file)
                {
                    std::fclose(file);
                }
            }
        };

        // A clip retains this byte source. Denying writers pins the validated
        // file identity and bytes until the last consumer releases the mount.
        [[nodiscard]] std::shared_ptr<PinnedFile> Pin(std::string_view path,
            std::string& failure) const
        {
            std::lock_guard lock(filesMutex_);
            const std::string key(path);
            if (const auto existing = files_.find(key); existing != files_.end())
            {
                return existing->second;
            }
            std::filesystem::path resolved;
            if (!Resolve(path, resolved, failure))
            {
                return {};
            }
            auto pinned = std::make_shared<PinnedFile>();
            pinned->file = _wfsopen(resolved.c_str(), L"rb", _SH_DENYWR);
            if (!pinned->file)
            {
                failure = "cooked artifact cannot be pinned against writes";
                return {};
            }
            files_.emplace(key, pinned);
            return pinned;
        }

        mutable std::mutex filesMutex_;
        mutable std::unordered_map<std::string, std::shared_ptr<PinnedFile>> files_;
#endif
        [[nodiscard]] bool Resolve(std::string_view virtualPath,
            std::filesystem::path& out, std::string& failure) const
        {
            if (!IsAudioArtifactVirtualPath(virtualPath) && !IsMaterialProgramArtifactVirtualPath(virtualPath) && !IsCollisionGeometryArtifactVirtualPath(virtualPath)
                && !IsSoundAssetArtifactVirtualPath(virtualPath))
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
