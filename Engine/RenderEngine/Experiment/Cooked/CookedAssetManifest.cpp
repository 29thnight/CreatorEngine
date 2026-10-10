#include "CookedAssetManifest.h"
#include "CookedAudioClipFormat.h"
#include "../../Assets/AssetIdentityProfile.h" // MBC11: IsUuidV8

#if defined(_WIN32)
#include <Windows.h>
#include <bcrypt.h>
#else
#include "../../../Utility_Framework/Sha256.h"
#endif

#include <algorithm>
#include <cstring>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <tuple>
#include <unordered_set>
#include <utility>

#if defined(_WIN32)
#pragma comment(lib, "bcrypt.lib")
#endif

// PHASE 3.75 MBC11 — 모델·subasset 신원은 UUIDv8(ce.uuidv8.sha256.v1)이고 나머지 자산은
// 아직 UUIDv4다. manifest는 둘 다 받는다(pseudo-v5 등 그 밖의 표기는 거부).
namespace experiment::cooked
{
    namespace
    {
        [[nodiscard]] bool IsCookedAssetId(const AssetId& id) noexcept
        {
            return IsAssetIdV4(id) || assets::IsUuidV8(id.value);
        }
    }
}

namespace experiment::cooked
{
    namespace
    {
        inline constexpr std::size_t kHeaderBytes = 32u;
        inline constexpr std::size_t kEntryBytes = 80u;
        inline constexpr std::size_t kSourceEntryBytes = 24u;

        struct RawEntry final
        {
            AssetId assetId{};
            CookedAssetKind kind{ CookedAssetKind::Model };
            std::uint32_t formatVersion{};
            std::uint64_t byteSize{};
            Sha256Digest contentSha256{};
            std::uint32_t pathOffset{};
            std::uint32_t pathBytes{};
            std::uint32_t dependencyBegin{};
            std::uint32_t dependencyCount{};
        };

        struct RawSourceEntry final
        {
            AssetId assetId{};
            std::uint32_t pathOffset{};
            std::uint32_t pathBytes{};
        };

        void AddIssue(std::vector<AssetManifestIssue>& issues,
            std::string context, std::string message)
        {
            issues.push_back(AssetManifestIssue{
                std::move(context), std::move(message) });
        }

        [[nodiscard]] bool IsKnownKind(CookedAssetKind kind) noexcept
        {
            switch (kind)
            {
            case CookedAssetKind::Model:
            case CookedAssetKind::Material:
            case CookedAssetKind::Texture:
            case CookedAssetKind::ShaderMeta:
            case CookedAssetKind::Scene:
            case CookedAssetKind::Prefab:
            case CookedAssetKind::AudioClip:
            case CookedAssetKind::SoundGraph:
            case CookedAssetKind::SoundPreset:
            case CookedAssetKind::CollisionGeometry:
            case CookedAssetKind::MaterialProgram:
                return true;
            }
            return false;
        }

        [[nodiscard]] bool HasDigest(const Sha256Digest& digest) noexcept
        {
            return std::ranges::any_of(digest,
                [](std::uint8_t byte) { return byte != 0u; });
        }

        [[nodiscard]] bool IsNormalizedDerivedPath(
            std::string_view path) noexcept
        {
            if (!path.starts_with("Derived/") || path.size() <= 8u
                || path.back() == '/' || path.find('\\') != std::string_view::npos
                || path.find(':') != std::string_view::npos)
            {
                return false;
            }

            std::size_t begin = 0u;
            while (begin < path.size())
            {
                const std::size_t end = path.find('/', begin);
                const std::string_view segment = path.substr(begin,
                    end == std::string_view::npos ? path.size() - begin
                                                  : end - begin);
                if (segment.empty() || segment == "." || segment == "..")
                    return false;
                if (end == std::string_view::npos) break;
                begin = end + 1u;
            }
            return true;
        }

        [[nodiscard]] bool IsNormalizedSourcePath(
            std::string_view path) noexcept
        {
            if (path.empty() || path.front() == '/' || path.back() == '/'
                || path.starts_with("Derived/")
                || path.find('\\') != std::string_view::npos
                || path.find(':') != std::string_view::npos)
            {
                return false;
            }

            std::size_t begin = 0u;
            while (begin < path.size())
            {
                const std::size_t end = path.find('/', begin);
                const std::string_view segment = path.substr(begin,
                    end == std::string_view::npos ? path.size() - begin
                                                  : end - begin);
                if (segment.empty() || segment == "." || segment == "..")
                    return false;
                if (end == std::string_view::npos) break;
                begin = end + 1u;
            }
            return true;
        }

        [[nodiscard]] bool AddWouldOverflow(std::size_t left,
            std::size_t right) noexcept
        {
            return right > (std::numeric_limits<std::size_t>::max)() - left;
        }

        [[nodiscard]] bool MultiplyWouldOverflow(std::size_t left,
            std::size_t right) noexcept
        {
            return left != 0u
                && right > (std::numeric_limits<std::size_t>::max)() / left;
        }

        class Writer final
        {
        public:
            void U8(std::uint8_t value) { bytes_.push_back(std::byte{ value }); }

            void U16(std::uint16_t value)
            {
                U8(static_cast<std::uint8_t>(value));
                U8(static_cast<std::uint8_t>(value >> 8u));
            }

            void U32(std::uint32_t value)
            {
                for (unsigned shift = 0u; shift < 32u; shift += 8u)
                    U8(static_cast<std::uint8_t>(value >> shift));
            }

            void U64(std::uint64_t value)
            {
                for (unsigned shift = 0u; shift < 64u; shift += 8u)
                    U8(static_cast<std::uint8_t>(value >> shift));
            }

            void Raw(const void* data, std::size_t size)
            {
                if (size == 0u) return;
                const auto* first = static_cast<const std::byte*>(data);
                bytes_.insert(bytes_.end(), first, first + size);
            }

            [[nodiscard]] std::vector<std::byte> Take() noexcept
            {
                return std::move(bytes_);
            }

        private:
            std::vector<std::byte> bytes_{};
        };

        class Reader final
        {
        public:
            explicit Reader(std::span<const std::byte> bytes) noexcept
                : bytes_(bytes) {}

            [[nodiscard]] bool Ok() const noexcept { return ok_; }
            [[nodiscard]] std::size_t Offset() const noexcept { return offset_; }

            [[nodiscard]] std::uint8_t U8() noexcept
            {
                if (!Ensure(1u)) return 0u;
                return std::to_integer<std::uint8_t>(bytes_[offset_++]);
            }

            [[nodiscard]] std::uint16_t U16() noexcept
            {
                std::uint16_t value{};
                for (unsigned shift = 0u; shift < 16u; shift += 8u)
                    value |= static_cast<std::uint16_t>(U8()) << shift;
                return value;
            }

            [[nodiscard]] std::uint32_t U32() noexcept
            {
                std::uint32_t value{};
                for (unsigned shift = 0u; shift < 32u; shift += 8u)
                    value |= static_cast<std::uint32_t>(U8()) << shift;
                return value;
            }

            [[nodiscard]] std::uint64_t U64() noexcept
            {
                std::uint64_t value{};
                for (unsigned shift = 0u; shift < 64u; shift += 8u)
                    value |= static_cast<std::uint64_t>(U8()) << shift;
                return value;
            }

            [[nodiscard]] bool Raw(void* data, std::size_t size) noexcept
            {
                if (!Ensure(size)) return false;
                std::memcpy(data, bytes_.data() + offset_, size);
                offset_ += size;
                return true;
            }

        private:
            [[nodiscard]] bool Ensure(std::size_t size) noexcept
            {
                if (!ok_ || size > bytes_.size() - offset_)
                {
                    ok_ = false;
                    return false;
                }
                return true;
            }

            std::span<const std::byte> bytes_{};
            std::size_t offset_{};
            bool ok_{ true };
        };

        [[nodiscard]] bool ValidateManifest(
            const CookedAssetManifest& manifest,
            std::vector<AssetManifestIssue>& issues)
        {
            if (manifest.entries.empty())
            {
                AddIssue(issues, "manifest.entries",
                    "빈 cooked asset manifest는 게시하지 않는다.");
                return false;
            }

            bool valid = true;
            std::vector<AssetId> ids;
            ids.reserve(manifest.entries.size());
            for (std::size_t index = 0; index < manifest.entries.size(); ++index)
            {
                const CookedAssetManifestEntry& entry = manifest.entries[index];
                const std::string context =
                    "entries[" + std::to_string(index) + "]";
                if (!IsCookedAssetId(entry.assetId))
                {
                    AddIssue(issues, context + ".assetId",
                        "manifest key는 canonical UUIDv4/UUIDv8 asset identity여야 한다.");
                    valid = false;
                }
                else if (std::ranges::find(ids, entry.assetId) != ids.end())
                {
                    AddIssue(issues, context + ".assetId",
                        "manifest에 중복 asset identity가 있다.");
                    valid = false;
                }
                else
                {
                    ids.push_back(entry.assetId);
                }

                if (!IsKnownKind(entry.kind))
                {
                    AddIssue(issues, context + ".kind",
                        "알 수 없는 cooked asset kind다.");
                    valid = false;
                }
                if (entry.formatVersion == 0u)
                {
                    AddIssue(issues, context + ".formatVersion",
                        "format version 0은 게시할 수 없다.");
                    valid = false;
                }
                if ((entry.kind == CookedAssetKind::AudioClip
                     && entry.formatVersion != kAudioClipArtifactVersion) ||
                    (entry.kind == CookedAssetKind::MaterialProgram
                     && entry.formatVersion != kMaterialProgramArtifactVersion) ||
                    (entry.kind == CookedAssetKind::CollisionGeometry && entry.formatVersion != 1u) ||
                    ((entry.kind == CookedAssetKind::SoundGraph || entry.kind == CookedAssetKind::SoundPreset)
                     && entry.formatVersion != kSoundAssetArtifactVersion))
                {
                    AddIssue(issues, context + ".formatVersion",
                        "지원하지 않는 audio/material program/collision geometry artifact version이다.");
                    valid = false;
                }
                if (!HasDigest(entry.contentSha256))
                {
                    AddIssue(issues, context + ".contentSha256",
                        "SHA-256 digest가 비어 있다.");
                    valid = false;
                }
                if (!IsNormalizedDerivedPath(entry.artifactPath))
                {
                    AddIssue(issues, context + ".artifactPath",
                        "artifact path는 Derived/ 아래의 normalized relative path여야 한다.");
                    valid = false;
                }

                std::vector<AssetId> dependencies;
                dependencies.reserve(entry.dependencies.size());
                for (std::size_t dependencyIndex = 0;
                    dependencyIndex < entry.dependencies.size(); ++dependencyIndex)
                {
                    const AssetId dependency = entry.dependencies[dependencyIndex];
                    const std::string dependencyContext = context + ".dependencies["
                        + std::to_string(dependencyIndex) + "]";
                    if (!IsCookedAssetId(dependency))
                    {
                        AddIssue(issues, dependencyContext,
                            "dependency는 UUIDv4 asset identity여야 한다.");
                        valid = false;
                    }
                    else if (dependency == entry.assetId)
                    {
                        AddIssue(issues, dependencyContext,
                            "asset이 자기 자신을 dependency로 가리킨다.");
                        valid = false;
                    }
                    else if (std::ranges::find(dependencies, dependency)
                        != dependencies.end())
                    {
                        AddIssue(issues, dependencyContext,
                            "dependency identity가 중복됐다.");
                        valid = false;
                    }
                    else
                    {
                        dependencies.push_back(dependency);
                    }
                }
            }

            // 부분 manifest를 허용하면 Player가 source/.meta fallback으로 새기 쉽다.
            // 모든 dependency가 같은 manifest 안에서 해석되어야 한다.
            for (std::size_t index = 0; index < manifest.entries.size(); ++index)
            {
                for (std::size_t dependencyIndex = 0;
                    dependencyIndex < manifest.entries[index].dependencies.size();
                    ++dependencyIndex)
                {
                    const AssetId dependency =
                        manifest.entries[index].dependencies[dependencyIndex];
                    if (std::ranges::find(ids, dependency) == ids.end())
                    {
                        AddIssue(issues,
                            "entries[" + std::to_string(index) + "].dependencies["
                                + std::to_string(dependencyIndex) + "]",
                            "dependency GUID가 manifest entry로 해석되지 않는다: "
                                + Uuid::ToString(dependency.value) + " (entry "
                                + Uuid::ToString(manifest.entries[index].assetId.value)
                                + " " + manifest.entries[index].artifactPath + ")");
                        valid = false;
                    }
                }
            }

            std::vector<AssetId> sourceIds;
            std::vector<std::string_view> sourcePaths;
            sourceIds.reserve(manifest.sourceAssets.size());
            sourcePaths.reserve(manifest.sourceAssets.size());
            for (std::size_t index = 0u; index < manifest.sourceAssets.size(); ++index)
            {
                const AssetSourceManifestEntry& source = manifest.sourceAssets[index];
                const std::string context =
                    "sourceAssets[" + std::to_string(index) + "]";
                if (!IsCookedAssetId(source.assetId))
                {
                    AddIssue(issues, context + ".assetId",
                        "source catalog key는 UUIDv4 asset identity여야 한다.");
                    valid = false;
                }
                else if (std::ranges::find(sourceIds, source.assetId)
                    != sourceIds.end())
                {
                    AddIssue(issues, context + ".assetId",
                        "source catalog에 중복 asset identity가 있다.");
                    valid = false;
                }
                else
                {
                    sourceIds.push_back(source.assetId);
                }

                if (!IsNormalizedSourcePath(source.sourcePath))
                {
                    AddIssue(issues, context + ".sourcePath",
                        "source path는 Assets root 상대 normalized path여야 한다.");
                    valid = false;
                }
                else if (std::ranges::find(sourcePaths,
                    std::string_view(source.sourcePath)) != sourcePaths.end())
                {
                    AddIssue(issues, context + ".sourcePath",
                        "source catalog에 중복 path가 있다.");
                    valid = false;
                }
                else
                {
                    sourcePaths.push_back(source.sourcePath);
                }
            }
            return valid;
        }
    }

    const CookedAssetManifestEntry* CookedAssetManifest::Find(
        const AssetId& assetId) const noexcept
    {
        const auto found = std::ranges::lower_bound(entries, assetId,
            {}, &CookedAssetManifestEntry::assetId);
        if (found == entries.end() || found->assetId != assetId) return nullptr;
        return &*found;
    }

    const AssetSourceManifestEntry* CookedAssetManifest::FindSource(
        const AssetId& assetId) const noexcept
    {
        const auto found = std::ranges::lower_bound(sourceAssets, assetId,
            {}, &AssetSourceManifestEntry::assetId);
        if (found == sourceAssets.end() || found->assetId != assetId) return nullptr;
        return &*found;
    }

    std::string MakeDerivedModelArtifactPath(const AssetId& modelAssetId)
    {
        if (!IsAssetIdV4(modelAssetId)) return {};
        const std::string guid = Uuid::ToString(modelAssetId.value);
        return "Derived/Models/" + guid.substr(0u, 2u) + "/" + guid + ".cemc";
    }

    std::string MakeDerivedTextureArtifactPath(const AssetId& textureAssetId,
        std::string_view extension)
    {
        if (!IsAssetIdV4(textureAssetId)) return {};

        // 확장자는 호출자가 이미 소문자로 접어 allowlist 를 통과시킨 값이지만,
        // 경로를 만드는 것은 여기이므로 경로가 깨질 수 있는 표기는 여기서도
        // 막는다. 위쪽 검사에 기대면 다른 호출자가 생기는 날 조용히 뚫린다.
        if (extension.size() < 2u || extension.front() != '.'
            || extension.find('/') != std::string_view::npos
            || extension.find('\\') != std::string_view::npos
            || extension.find('.', 1u) != std::string_view::npos)
        {
            return {};
        }

        const std::string guid = Uuid::ToString(textureAssetId.value);
        return "Derived/Textures/" + guid.substr(0u, 2u) + "/" + guid
            + std::string(extension);
    }

    std::string MakeDerivedShaderMetaArtifactPath(
        const AssetId& shaderMetaAssetId)
    {
        if (!IsAssetIdV4(shaderMetaAssetId)) return {};
        const std::string guid = Uuid::ToString(shaderMetaAssetId.value);
        return "Derived/ShaderMeta/" + guid.substr(0u, 2u) + "/" + guid
            + ".shadermeta";
    }

    std::string MakeDerivedMaterialArtifactPath(const AssetId& materialAssetId)
    {
        if (!IsAssetIdV4(materialAssetId)) return {};
        const std::string guid = Uuid::ToString(materialAssetId.value);
        return "Derived/Materials/" + guid.substr(0u, 2u) + "/" + guid
            + ".asset";
    }

    std::string MakeDerivedMaterialProgramArtifactPath(const AssetId& graphAssetId)
    {
        if (!IsAssetIdV4(graphAssetId) && !assets::IsUuidV8(graphAssetId.value)) return {};
        const std::string guid = Uuid::ToString(graphAssetId.value);
        return "Derived/MaterialPrograms/" + guid.substr(0u, 2u) + "/" + guid + ".lxmaterial";
    }

    std::string MakeDerivedSceneArtifactPath(const AssetId& sceneAssetId)
    {
        if (!IsAssetIdV4(sceneAssetId)) return {};
        const std::string guid = Uuid::ToString(sceneAssetId.value);
        return "Derived/Scenes/" + guid.substr(0u, 2u) + "/" + guid
            + ".creator";
    }

    std::string MakeDerivedPrefabArtifactPath(const AssetId& prefabAssetId)
    {
        if (!IsAssetIdV4(prefabAssetId)) return {};
        const std::string guid = Uuid::ToString(prefabAssetId.value);
        return "Derived/Prefabs/" + guid.substr(0u, 2u) + "/" + guid
            + ".prefab";
    }

    std::string MakeDerivedSoundGraphArtifactPath(const AssetId& assetId)
    {
        if (!IsAssetIdV4(assetId))
        {
            return {};
        }
        const auto guid = Uuid::ToString(assetId.value);
        return "Derived/SoundGraphs/" + guid.substr(0u, 2u) + "/" + guid + ".cesg";
    }

    std::string MakeDerivedSoundPresetArtifactPath(const AssetId& assetId)
    {
        if (!IsAssetIdV4(assetId))
        {
            return {};
        }
        const auto guid = Uuid::ToString(assetId.value);
        return "Derived/SoundPresets/" + guid.substr(0u, 2u) + "/" + guid + ".cesp";
    }

    std::string MakeDerivedAudioClipArtifactPath(const AssetId& audioClipAssetId)
    {
        if (!IsAssetIdV4(audioClipAssetId)) return {};
        const std::string guid = Uuid::ToString(audioClipAssetId.value);
        return "Derived/Audio/" + guid.substr(0u, 2u) + "/" + guid + ".ceac";
    }

    bool ComputeSha256(std::span<const std::byte> bytes,
        Sha256Digest& outDigest, std::string& outError) noexcept
    {
#if defined(_WIN32)
        BCRYPT_ALG_HANDLE algorithm{};
        BCRYPT_HASH_HANDLE hash{};
        std::vector<std::uint8_t> object;
        const auto close = [&]() noexcept
        {
            if (hash) BCryptDestroyHash(hash);
            if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0u);
        };

        NTSTATUS status = BCryptOpenAlgorithmProvider(&algorithm,
            BCRYPT_SHA256_ALGORITHM, nullptr, 0u);
        if (status < 0)
        {
            outError = "BCryptOpenAlgorithmProvider(SHA256) failed";
            close();
            return false;
        }

        DWORD objectBytes{};
        DWORD returned{};
        status = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&objectBytes), sizeof(objectBytes),
            &returned, 0u);
        if (status < 0 || objectBytes == 0u)
        {
            outError = "BCryptGetProperty(SHA256 object length) failed";
            close();
            return false;
        }

        object.resize(objectBytes);
        status = BCryptCreateHash(algorithm, &hash, object.data(), objectBytes,
            nullptr, 0u, 0u);
        if (status < 0)
        {
            outError = "BCryptCreateHash(SHA256) failed";
            close();
            return false;
        }

        std::size_t offset = 0u;
        while (offset < bytes.size())
        {
            const std::size_t remaining = bytes.size() - offset;
            const ULONG chunk = static_cast<ULONG>((std::min)(remaining,
                static_cast<std::size_t>((std::numeric_limits<ULONG>::max)())));
            status = BCryptHashData(hash,
                reinterpret_cast<PUCHAR>(const_cast<std::byte*>(bytes.data() + offset)),
                chunk, 0u);
            if (status < 0)
            {
                outError = "BCryptHashData(SHA256) failed";
                close();
                return false;
            }
            offset += chunk;
        }

        Sha256Digest digest{};
        status = BCryptFinishHash(hash, digest.data(),
            static_cast<ULONG>(digest.size()), 0u);
        if (status < 0)
        {
            outError = "BCryptFinishHash(SHA256) failed";
            close();
            return false;
        }

        close();
        outDigest = digest;
        outError.clear();
        return true;
#else
        Hash::Sha256 hash;
        hash.Update(bytes.data(), bytes.size());
        outDigest = hash.Finish();
        outError.clear();
        return true;
#endif
    }

    AssetManifestWriteResult WriteAssetManifest(
        const CookedAssetManifest& manifest)
    {
        AssetManifestWriteResult result;
        if (!ValidateManifest(manifest, result.issues)) return result;

        CookedAssetManifest canonical = manifest;
        std::ranges::sort(canonical.entries, {},
            &CookedAssetManifestEntry::assetId);
        std::ranges::sort(canonical.sourceAssets, {},
            &AssetSourceManifestEntry::assetId);
        for (CookedAssetManifestEntry& entry : canonical.entries)
            std::ranges::sort(entry.dependencies, {}, &AssetId::value);

        std::size_t dependencyCount = 0u;
        std::size_t artifactStringBytes = 0u;
        std::size_t sourceStringBytes = 0u;
        for (const CookedAssetManifestEntry& entry : canonical.entries)
        {
            if (AddWouldOverflow(dependencyCount, entry.dependencies.size())
                || AddWouldOverflow(artifactStringBytes, entry.artifactPath.size()))
            {
                AddIssue(result.issues, "manifest",
                    "manifest count/문자열 크기가 size_t 범위를 넘는다.");
                return result;
            }
            dependencyCount += entry.dependencies.size();
            artifactStringBytes += entry.artifactPath.size();
        }
        for (const AssetSourceManifestEntry& source : canonical.sourceAssets)
        {
            if (AddWouldOverflow(sourceStringBytes, source.sourcePath.size()))
            {
                AddIssue(result.issues, "manifest.sourceAssets",
                    "source path 문자열 크기가 size_t 범위를 넘는다.");
                return result;
            }
            sourceStringBytes += source.sourcePath.size();
        }
        if (canonical.entries.size() > (std::numeric_limits<std::uint32_t>::max)()
            || canonical.sourceAssets.size() > (std::numeric_limits<std::uint32_t>::max)()
            || dependencyCount > (std::numeric_limits<std::uint32_t>::max)()
            || artifactStringBytes > (std::numeric_limits<std::uint32_t>::max)()
            || sourceStringBytes > (std::numeric_limits<std::uint32_t>::max)())
        {
            AddIssue(result.issues, "manifest",
                "CEMF v2의 32-bit count 범위를 넘는다.");
            return result;
        }

        Writer writer;
        writer.U32(kAssetManifestMagic);
        writer.U16(kAssetManifestVersion);
        writer.U16(static_cast<std::uint16_t>(kHeaderBytes));
        writer.U32(static_cast<std::uint32_t>(canonical.entries.size()));
        writer.U32(static_cast<std::uint32_t>(dependencyCount));
        writer.U32(static_cast<std::uint32_t>(artifactStringBytes));
        writer.U32(static_cast<std::uint32_t>(canonical.sourceAssets.size()));
        writer.U32(static_cast<std::uint32_t>(sourceStringBytes));
        writer.U32(0u);

        std::uint32_t pathOffset = 0u;
        std::uint32_t dependencyBegin = 0u;
        for (const CookedAssetManifestEntry& entry : canonical.entries)
        {
            writer.Raw(entry.assetId.value.data.data(), entry.assetId.value.data.size());
            writer.U8(static_cast<std::uint8_t>(entry.kind));
            writer.U8(0u);
            writer.U8(0u);
            writer.U8(0u);
            writer.U32(entry.formatVersion);
            writer.U64(entry.byteSize);
            writer.Raw(entry.contentSha256.data(), entry.contentSha256.size());
            writer.U32(pathOffset);
            writer.U32(static_cast<std::uint32_t>(entry.artifactPath.size()));
            writer.U32(dependencyBegin);
            writer.U32(static_cast<std::uint32_t>(entry.dependencies.size()));
            pathOffset += static_cast<std::uint32_t>(entry.artifactPath.size());
            dependencyBegin += static_cast<std::uint32_t>(entry.dependencies.size());
        }

        std::uint32_t sourcePathOffset = 0u;
        for (const AssetSourceManifestEntry& source : canonical.sourceAssets)
        {
            writer.Raw(source.assetId.value.data.data(), source.assetId.value.data.size());
            writer.U32(sourcePathOffset);
            writer.U32(static_cast<std::uint32_t>(source.sourcePath.size()));
            sourcePathOffset += static_cast<std::uint32_t>(source.sourcePath.size());
        }

        for (const CookedAssetManifestEntry& entry : canonical.entries)
        {
            for (const AssetId& dependency : entry.dependencies)
            {
                writer.Raw(dependency.value.data.data(),
                    dependency.value.data.size());
            }
        }
        for (const CookedAssetManifestEntry& entry : canonical.entries)
            writer.Raw(entry.artifactPath.data(), entry.artifactPath.size());
        for (const AssetSourceManifestEntry& source : canonical.sourceAssets)
            writer.Raw(source.sourcePath.data(), source.sourcePath.size());

        result.bytes = writer.Take();
        return result;
    }

    bool ReadAssetManifest(std::span<const std::byte> bytes,
        CookedAssetManifest& outManifest,
        std::vector<AssetManifestIssue>& outIssues)
    {
        if (bytes.size() < kHeaderBytes)
        {
            AddIssue(outIssues, "manifest.header", "CEMF header보다 짧다.");
            return false;
        }

        Reader reader(bytes);
        const std::uint32_t magic = reader.U32();
        const std::uint16_t version = reader.U16();
        const std::uint16_t headerBytes = reader.U16();
        const std::uint32_t entryCount = reader.U32();
        const std::uint32_t dependencyCount = reader.U32();
        const std::uint32_t artifactStringBytes = reader.U32();
        const std::uint32_t sourceEntryCount = reader.U32();
        const std::uint32_t sourceStringBytes = reader.U32();
        const std::uint32_t reserved = reader.U32();
        if (!reader.Ok() || magic != kAssetManifestMagic
            || version != kAssetManifestVersion || headerBytes != kHeaderBytes
            || reserved != 0u)
        {
            AddIssue(outIssues, "manifest.header",
                "magic/version/header/reserved 계약이 맞지 않는다.");
            return false;
        }
        if (entryCount == 0u)
        {
            AddIssue(outIssues, "manifest.entries", "빈 manifest는 읽지 않는다.");
            return false;
        }

        const std::size_t entries = entryCount;
        const std::size_t dependencies = dependencyCount;
        const std::size_t sourceEntries = sourceEntryCount;
        std::size_t expected = kHeaderBytes;
        if (MultiplyWouldOverflow(entries, kEntryBytes)
            || AddWouldOverflow(expected, entries * kEntryBytes))
        {
            AddIssue(outIssues, "manifest.header", "entry table 크기가 overflow한다.");
            return false;
        }
        expected += entries * kEntryBytes;
        if (MultiplyWouldOverflow(sourceEntries, kSourceEntryBytes)
            || AddWouldOverflow(expected, sourceEntries * kSourceEntryBytes))
        {
            AddIssue(outIssues, "manifest.header",
                "source identity table 크기가 overflow한다.");
            return false;
        }
        expected += sourceEntries * kSourceEntryBytes;
        if (MultiplyWouldOverflow(dependencies, sizeof(Uuid::Uuid16))
            || AddWouldOverflow(expected, dependencies * sizeof(Uuid::Uuid16)))
        {
            AddIssue(outIssues, "manifest.header",
                "dependency table 크기가 overflow한다.");
            return false;
        }
        expected += dependencies * sizeof(Uuid::Uuid16);
        if (AddWouldOverflow(expected, artifactStringBytes)
            || AddWouldOverflow(expected + artifactStringBytes, sourceStringBytes))
        {
            AddIssue(outIssues, "manifest.header", "string table 크기가 overflow한다.");
            return false;
        }
        expected += artifactStringBytes;
        expected += sourceStringBytes;
        if (expected != bytes.size())
        {
            AddIssue(outIssues, "manifest.header",
                "header count와 실제 파일 크기가 정확히 맞지 않는다.");
            return false;
        }

        std::vector<RawEntry> rawEntries;
        rawEntries.reserve(entryCount);
        for (std::uint32_t index = 0u; index < entryCount; ++index)
        {
            RawEntry raw;
            static_cast<void>(reader.Raw(raw.assetId.value.data.data(), raw.assetId.value.data.size()));
            raw.kind = static_cast<CookedAssetKind>(reader.U8());
            const std::uint8_t reserved0 = reader.U8();
            const std::uint8_t reserved1 = reader.U8();
            const std::uint8_t reserved2 = reader.U8();
            raw.formatVersion = reader.U32();
            raw.byteSize = reader.U64();
            static_cast<void>(reader.Raw(raw.contentSha256.data(), raw.contentSha256.size()));
            raw.pathOffset = reader.U32();
            raw.pathBytes = reader.U32();
            raw.dependencyBegin = reader.U32();
            raw.dependencyCount = reader.U32();
            if (!reader.Ok() || reserved0 != 0u || reserved1 != 0u || reserved2 != 0u)
            {
                AddIssue(outIssues, "entries[" + std::to_string(index) + "]",
                    "entry가 잘렸거나 reserved byte가 0이 아니다.");
                return false;
            }
            rawEntries.push_back(raw);
        }

        std::vector<RawSourceEntry> rawSourceEntries;
        rawSourceEntries.reserve(sourceEntryCount);
        for (std::uint32_t index = 0u; index < sourceEntryCount; ++index)
        {
            RawSourceEntry raw;
            static_cast<void>(reader.Raw(raw.assetId.value.data.data(), raw.assetId.value.data.size()));
            raw.pathOffset = reader.U32();
            raw.pathBytes = reader.U32();
            if (!reader.Ok())
            {
                AddIssue(outIssues, "sourceAssets[" + std::to_string(index) + "]",
                    "source identity entry가 잘렸다.");
                return false;
            }
            rawSourceEntries.push_back(raw);
        }

        std::vector<AssetId> dependencyIds(dependencyCount);
        for (AssetId& dependency : dependencyIds)
        {
            if (!reader.Raw(dependency.value.data.data(), dependency.value.data.size()))
            {
                AddIssue(outIssues, "manifest.dependencies",
                    "dependency table이 잘렸다.");
                return false;
            }
        }
        const std::size_t artifactStringsBegin = reader.Offset();
        const std::size_t sourceStringsBegin =
            artifactStringsBegin + artifactStringBytes;

        CookedAssetManifest parsed;
        parsed.entries.reserve(entryCount);
        for (std::size_t index = 0u; index < rawEntries.size(); ++index)
        {
            const RawEntry& raw = rawEntries[index];
            if (static_cast<std::uint64_t>(raw.pathOffset) + raw.pathBytes > artifactStringBytes
                || static_cast<std::uint64_t>(raw.dependencyBegin)
                    + raw.dependencyCount > dependencyIds.size())
            {
                AddIssue(outIssues, "entries[" + std::to_string(index) + "]",
                    "path/dependency range가 table 밖을 가리킨다.");
                return false;
            }

            CookedAssetManifestEntry entry;
            entry.assetId = raw.assetId;
            entry.kind = raw.kind;
            entry.formatVersion = raw.formatVersion;
            entry.byteSize = raw.byteSize;
            entry.contentSha256 = raw.contentSha256;
            const auto* path = reinterpret_cast<const char*>(
                bytes.data() + artifactStringsBegin + raw.pathOffset);
            entry.artifactPath.assign(path, raw.pathBytes);
            entry.dependencies.assign(
                dependencyIds.begin() + raw.dependencyBegin,
                dependencyIds.begin() + raw.dependencyBegin + raw.dependencyCount);
            parsed.entries.push_back(std::move(entry));
        }

        parsed.sourceAssets.reserve(sourceEntryCount);
        for (std::size_t index = 0u; index < rawSourceEntries.size(); ++index)
        {
            const RawSourceEntry& raw = rawSourceEntries[index];
            if (static_cast<std::uint64_t>(raw.pathOffset) + raw.pathBytes
                > sourceStringBytes)
            {
                AddIssue(outIssues, "sourceAssets[" + std::to_string(index) + "]",
                    "source path range가 string table 밖을 가리킨다.");
                return false;
            }

            AssetSourceManifestEntry source;
            source.assetId = raw.assetId;
            const auto* path = reinterpret_cast<const char*>(
                bytes.data() + sourceStringsBegin + raw.pathOffset);
            source.sourcePath.assign(path, raw.pathBytes);
            parsed.sourceAssets.push_back(std::move(source));
        }

        std::vector<AssetManifestIssue> validationIssues;
        if (!ValidateManifest(parsed, validationIssues))
        {
            outIssues.insert(outIssues.end(),
                std::make_move_iterator(validationIssues.begin()),
                std::make_move_iterator(validationIssues.end()));
            return false;
        }
        if (!std::ranges::is_sorted(parsed.entries, {},
            &CookedAssetManifestEntry::assetId))
        {
            AddIssue(outIssues, "manifest.entries",
                "entry table이 UUID 순서로 정렬되지 않았다.");
            return false;
        }
        if (!std::ranges::is_sorted(parsed.sourceAssets, {},
            &AssetSourceManifestEntry::assetId))
        {
            AddIssue(outIssues, "manifest.sourceAssets",
                "source identity table이 UUID 순서로 정렬되지 않았다.");
            return false;
        }
        for (std::size_t index = 0; index < parsed.entries.size(); ++index)
        {
            if (!std::ranges::is_sorted(parsed.entries[index].dependencies,
                {}, &AssetId::value))
            {
                AddIssue(outIssues,
                    "entries[" + std::to_string(index) + "].dependencies",
                    "dependency table이 UUID 순서로 정렬되지 않았다.");
                return false;
            }
        }

        outManifest = std::move(parsed);
        return true;
    }

    bool VerifyArtifact(const CookedAssetManifestEntry& entry,
        std::uint64_t actualByteSize, const Sha256Digest& actualSha256,
        std::vector<AssetManifestIssue>& outIssues)
    {
        bool valid = true;
        if (entry.byteSize != actualByteSize)
        {
            AddIssue(outIssues, "artifact.byteSize",
                "manifest byte size와 실제 artifact가 다르다.");
            valid = false;
        }
        if (entry.contentSha256 != actualSha256)
        {
            AddIssue(outIssues, "artifact.contentSha256",
                "manifest SHA-256과 실제 artifact가 다르다.");
            valid = false;
        }
        return valid;
    }
}

namespace experiment::cooked
{
    namespace
    {
        // CEMF v3 uses explicit little-endian fields, never sizeof(C++ structs).
        inline constexpr std::size_t kSetHeaderBytes = 80u;
        inline constexpr std::size_t kSetEntryBytes = 48u;
        inline constexpr std::size_t kSetBlobBytes = 80u;
        inline constexpr std::size_t kSetDependencyBytes = 36u;
        inline constexpr std::size_t kSetRootBytes = 36u;
        inline constexpr std::size_t kSetMaxPathBytes = 4096u;
        inline constexpr std::size_t kSetMaxTargetBytes = 256u;

        [[nodiscard]] bool IsAssetSetKind(CookedAssetKind kind) noexcept
        {
            return IsKnownKind(kind) || kind == CookedAssetKind::Mesh
                || kind == CookedAssetKind::Skeleton
                || kind == CookedAssetKind::AnimationClip
                || kind == CookedAssetKind::InputGraph;
        }

        [[nodiscard]] bool IsAssetSetIdentity(const AssetIdentity& identity) noexcept
        {
            return IsCookedAssetId(identity.assetId)
                && (!identity.subassetId.IsValid()
                    || IsCookedAssetId(identity.subassetId));
        }

        [[nodiscard]] std::string IdentityText(const AssetIdentity& identity)
        {
            std::string text = Uuid::ToString(identity.assetId.value);
            if (identity.subassetId.IsValid())
            {
                text += "/" + Uuid::ToString(identity.subassetId.value);
            }
            return text;
        }

        [[nodiscard]] bool IsTargetToken(std::string_view text) noexcept
        {
            if (text.empty() || text.size() > kSetMaxTargetBytes)
            {
                return false;
            }
            return std::ranges::all_of(text, [](unsigned char ch)
            {
                return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')
                    || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-'
                    || ch == '.' || ch == '+';
            });
        }

        [[nodiscard]] bool IsSafeUtf8Path(std::string_view text) noexcept
        {
            if (text.size() > kSetMaxPathBytes || !IsNormalizedDerivedPath(text))
            {
                return false;
            }
            for (std::size_t index = 0u; index < text.size();)
            {
                const auto first = static_cast<unsigned char>(text[index++]);
                if (first < 0x80u)
                {
                    if (first < 0x20u || first == 0x7fu)
                    {
                        return false;
                    }
                    continue;
                }
                std::size_t trailing{};
                std::uint32_t codepoint{};
                std::uint32_t minimum{};
                if (first >= 0xc2u && first <= 0xdfu)
                {
                    trailing = 1u;
                    codepoint = first & 0x1fu;
                    minimum = 0x80u;
                }
                else if (first >= 0xe0u && first <= 0xefu)
                {
                    trailing = 2u;
                    codepoint = first & 0x0fu;
                    minimum = 0x800u;
                }
                else if (first >= 0xf0u && first <= 0xf4u)
                {
                    trailing = 3u;
                    codepoint = first & 0x07u;
                    minimum = 0x10000u;
                }
                else
                {
                    return false;
                }
                if (trailing > text.size() - index)
                {
                    return false;
                }
                for (std::size_t byte = 0u; byte < trailing; ++byte)
                {
                    const auto next = static_cast<unsigned char>(text[index++]);
                    if ((next & 0xc0u) != 0x80u)
                    {
                        return false;
                    }
                    codepoint = (codepoint << 6u) | (next & 0x3fu);
                }
                if (codepoint < minimum || codepoint > 0x10ffffu
                    || (codepoint >= 0xd800u && codepoint <= 0xdfffu))
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool SetIssue(std::vector<AssetManifestIssue>& issues,
            std::string context, std::string message)
        {
            AddIssue(issues, std::move(context), std::move(message));
            return false;
        }

        [[nodiscard]] auto BlobContentKey(const AssetBlobRecord& blob) noexcept
        {
            return std::tie(blob.contentSha256, blob.kind, blob.representation,
                blob.schemaVersion, blob.targetPlatform, blob.targetAbi);
        }

        [[nodiscard]] bool CheckSetBounds(const AssetSetManifest& manifest,
            std::vector<AssetManifestIssue>& issues)
        {
            if (manifest.entries.empty() || manifest.blobs.empty() || manifest.roots.empty()
                || manifest.entries.size() > kAssetSetManifestMaxEntries
                || manifest.blobs.size() > kAssetSetManifestMaxEntries
                || manifest.roots.size() > kAssetSetManifestMaxEntries)
            {
                return SetIssue(issues, "assetSet.tables",
                    "AssetSet requires nonempty, bounded entry, blob and typed root tables.");
            }
            if (manifest.targetPlatform.size() > kSetMaxTargetBytes
                || manifest.targetAbi.size() > kSetMaxTargetBytes)
            {
                return SetIssue(issues, "assetSet.target", "Target token length limit exceeded.");
            }
            std::uint64_t dependencies = 0u;
            std::uint64_t strings = manifest.targetPlatform.size() + manifest.targetAbi.size();
            for (const AssetSetEntry& entry : manifest.entries)
            {
                if (entry.dependencies.size() > kAssetSetManifestMaxDependencies)
                {
                    return SetIssue(issues, "assetSet.dependencies", "Dependency limit exceeded.");
                }
                dependencies += entry.dependencies.size();
            }
            for (const AssetBlobRecord& blob : manifest.blobs)
            {
                if (blob.artifactPath.size() > kSetMaxPathBytes
                    || blob.targetPlatform.size() > kSetMaxTargetBytes
                    || blob.targetAbi.size() > kSetMaxTargetBytes)
                {
                    return SetIssue(issues, "assetSet.blobs", "Blob string length limit exceeded.");
                }
                strings += blob.artifactPath.size() + blob.targetPlatform.size() + blob.targetAbi.size();
            }
            if (dependencies > kAssetSetManifestMaxDependencies
                || strings > kAssetSetManifestMaxStringBytes)
            {
                return SetIssue(issues, "assetSet.tables", "Dependency or string table limit exceeded.");
            }
            const std::uint64_t bytes = kSetHeaderBytes
                + manifest.entries.size() * kSetEntryBytes
                + manifest.blobs.size() * kSetBlobBytes
                + dependencies * kSetDependencyBytes
                + manifest.roots.size() * kSetRootBytes + strings;
            if (bytes > kAssetSetManifestMaxBytes)
            {
                return SetIssue(issues, "assetSet.tables", "AssetSet manifest byte limit exceeded.");
            }
            return true;
        }

        [[nodiscard]] bool CheckHardCycles(const AssetSetManifest& manifest,
            const std::map<AssetIdentity, std::size_t>& entries,
            std::vector<AssetManifestIssue>& issues)
        {
            // Iterative DFS avoids stack exhaustion on a long valid dependency
            // chain. Only hard edges form the ownership graph; loadable loops
            // (including a self-reference) remain legal non-owning ID links.
            struct Frame final
            {
                std::size_t entry{};
                std::size_t nextDependency{};
            };
            std::vector<std::uint8_t> color(manifest.entries.size(), 0u);
            std::vector<Frame> stack;
            for (std::size_t start = 0u; start < manifest.entries.size(); ++start)
            {
                if (color[start] != 0u)
                {
                    continue;
                }
                color[start] = 1u;
                stack.push_back({ start, 0u });
                while (!stack.empty())
                {
                    Frame& frame = stack.back();
                    const auto& dependencies = manifest.entries[frame.entry].dependencies;
                    if (frame.nextDependency == dependencies.size())
                    {
                        color[frame.entry] = 2u;
                        stack.pop_back();
                        continue;
                    }
                    const AssetDependency& edge = dependencies[frame.nextDependency++];
                    if (edge.kind != AssetDependencyKind::Hard
                        || edge.scope != AssetDependencyScope::Internal)
                    {
                        continue;
                    }
                    const std::size_t next = entries.at(edge.target.key);
                    if (color[next] == 1u)
                    {
                        std::string path;
                        bool inCycle = false;
                        for (const Frame& step : stack)
                        {
                            inCycle = inCycle || step.entry == next;
                            if (inCycle)
                            {
                                path += IdentityText(manifest.entries[step.entry].asset.key) + " -> ";
                            }
                        }
                        path += IdentityText(edge.target.key);
                        return SetIssue(issues, "assetSet.dependencies",
                            "Hard dependency ownership cycle: " + path);
                    }
                    if (color[next] == 0u)
                    {
                        color[next] = 1u;
                        stack.push_back({ next, 0u });
                    }
                }
            }
            return true;
        }

        void WriteIdentity(Writer& writer, const AssetIdentity& identity)
        {
            writer.Raw(identity.assetId.value.data.data(), identity.assetId.value.data.size());
            writer.Raw(identity.subassetId.value.data.data(), identity.subassetId.value.data.size());
        }

        [[nodiscard]] bool ReadIdentity(Reader& reader, AssetIdentity& identity)
        {
            return reader.Raw(identity.assetId.value.data.data(), identity.assetId.value.data.size())
                && reader.Raw(identity.subassetId.value.data.data(), identity.subassetId.value.data.size());
        }

        void WriteStringRange(Writer& writer, const std::string& text,
            std::uint32_t& offset)
        {
            writer.U32(offset);
            writer.U32(static_cast<std::uint32_t>(text.size()));
            offset += static_cast<std::uint32_t>(text.size());
        }
    }

    const AssetSetEntry* AssetSetManifest::Find(const AssetIdentity& identity) const noexcept
    {
        const auto found = std::ranges::find_if(entries, [&](const AssetSetEntry& entry)
        {
            return entry.asset.key == identity;
        });
        return found == entries.end() ? nullptr : &*found;
    }

    bool ValidateAssetSetManifest(const AssetSetManifest& manifest,
        std::vector<AssetManifestIssue>& outIssues)
    {
        if (!CheckSetBounds(manifest, outIssues))
        {
            return false;
        }
        if (!IsCookedAssetId(manifest.assetSetId) || manifest.revision == 0u)
        {
            return SetIssue(outIssues, "assetSet.identity", "AssetSet needs a stable UUIDv4/v8 and nonzero revision.");
        }
        if (!IsTargetToken(manifest.targetPlatform) || !IsTargetToken(manifest.targetAbi))
        {
            return SetIssue(outIssues, "assetSet.target", "Target platform and ABI must be nonempty bounded tokens.");
        }

        std::set<std::tuple<Sha256Digest, CookedAssetKind, std::uint32_t,
            std::uint32_t, std::string, std::string>> blobKeys;
        std::map<std::string_view, std::size_t> paths;
        for (std::size_t index = 0u; index < manifest.blobs.size(); ++index)
        {
            const AssetBlobRecord& blob = manifest.blobs[index];
            const std::string context = "assetSet.blobs[" + std::to_string(index) + "]";
            if (!IsAssetSetKind(blob.kind) || blob.representation == 0u || blob.schemaVersion == 0u
                || blob.byteSize == 0u || !HasDigest(blob.contentSha256))
            {
                return SetIssue(outIssues, context, "Blob needs a known type, representation/schema, size and SHA-256.");
            }
            if (blob.targetPlatform != manifest.targetPlatform || blob.targetAbi != manifest.targetAbi)
            {
                return SetIssue(outIssues, context, "Blob platform/ABI is incompatible with its AssetSet.");
            }
            if (!IsSafeUtf8Path(blob.artifactPath))
            {
                return SetIssue(outIssues, context + ".artifactPath", "Blob requires a safe normalized UTF-8 Derived/ path.");
            }
            if (!blobKeys.emplace(BlobContentKey(blob)).second)
            {
                return SetIssue(outIssues, context, "Duplicate compatible content address; entries must share one blob record.");
            }
            const auto [path, inserted] = paths.emplace(blob.artifactPath, index);
            if (!inserted)
            {
                const AssetBlobRecord& previous = manifest.blobs[path->second];
                if (previous.contentSha256 != blob.contentSha256 || previous.byteSize != blob.byteSize)
                {
                    return SetIssue(outIssues, context + ".artifactPath", "One artifact path declares different bytes.");
                }
            }
        }

        std::map<AssetIdentity, std::size_t> entries;
        for (std::size_t index = 0u; index < manifest.entries.size(); ++index)
        {
            const AssetSetEntry& entry = manifest.entries[index];
            const std::string context = "assetSet.entries[" + std::to_string(index) + "]";
            if (!IsAssetSetIdentity(entry.asset.key) || !IsAssetSetKind(entry.asset.kind))
            {
                return SetIssue(outIssues, context, "Entry requires a stable asset/subasset identity and known type.");
            }
            if (!entries.emplace(entry.asset.key, index).second)
            {
                return SetIssue(outIssues, context, "Duplicate asset/subasset identity: " + IdentityText(entry.asset.key));
            }
            if (entry.blobIndex >= manifest.blobs.size()
                || entry.asset.kind != manifest.blobs[entry.blobIndex].kind)
            {
                return SetIssue(outIssues, context + ".blobIndex", "Blob is missing or has a different type.");
            }
        }
        for (const AssetSetEntry& entry : manifest.entries)
        {
            std::set<AssetIdentity> declared;
            for (const AssetDependency& edge : entry.dependencies)
            {
                const std::string path = IdentityText(entry.asset.key) + " -> " + IdentityText(edge.target.key);
                if (!IsAssetSetIdentity(edge.target.key) || !IsAssetSetKind(edge.target.kind)
                    || (edge.kind != AssetDependencyKind::Hard && edge.kind != AssetDependencyKind::Loadable)
                    || (edge.scope != AssetDependencyScope::Internal && edge.scope != AssetDependencyScope::External))
                {
                    return SetIssue(outIssues, path, "Invalid typed dependency identity, kind or scope.");
                }
                if (!declared.insert(edge.target.key).second)
                {
                    return SetIssue(outIssues, path, "Duplicate or contradictory dependency declaration.");
                }
                const auto target = entries.find(edge.target.key);
                if (edge.scope == AssetDependencyScope::External)
                {
                    if (target != entries.end())
                    {
                        return SetIssue(outIssues, path, "External dependency resolves locally; declare its scope as Internal.");
                    }
                    continue;
                }
                if (target == entries.end())
                {
                    return SetIssue(outIssues, path, "Internal dependency is missing; external references must be explicit.");
                }
                if (manifest.entries[target->second].asset.kind != edge.target.kind)
                {
                    return SetIssue(outIssues, path, "Dependency expected type does not match the target entry.");
                }
            }
        }
        std::set<AssetIdentity> roots;
        for (const TypedAssetReference& root : manifest.roots)
        {
            const auto target = entries.find(root.key);
            if (!IsAssetSetIdentity(root.key) || !IsAssetSetKind(root.kind)
                || target == entries.end() || manifest.entries[target->second].asset.kind != root.kind)
            {
                return SetIssue(outIssues, "assetSet.roots", "Typed root is invalid, missing or has the wrong type: " + IdentityText(root.key));
            }
            if (!roots.insert(root.key).second)
            {
                return SetIssue(outIssues, "assetSet.roots", "Duplicate typed root: " + IdentityText(root.key));
            }
        }
        return CheckHardCycles(manifest, entries, outIssues);
    }

    bool NormalizeAssetSetManifest(const AssetSetManifest& manifest,
        AssetSetManifest& outManifest, std::vector<AssetManifestIssue>& outIssues)
    {
        if (!ValidateAssetSetManifest(manifest, outIssues))
        {
            return false;
        }
        AssetSetManifest canonical = manifest;
        std::vector<std::size_t> order(canonical.blobs.size());
        std::iota(order.begin(), order.end(), 0u);
        std::ranges::sort(order, [&](std::size_t left, std::size_t right)
        {
            return manifest.blobs[left] < manifest.blobs[right];
        });
        std::vector<std::uint32_t> remap(order.size());
        for (std::size_t index = 0u; index < order.size(); ++index)
        {
            canonical.blobs[index] = manifest.blobs[order[index]];
            remap[order[index]] = static_cast<std::uint32_t>(index);
        }
        for (AssetSetEntry& entry : canonical.entries)
        {
            entry.blobIndex = remap[entry.blobIndex];
            std::ranges::sort(entry.dependencies);
        }
        std::ranges::sort(canonical.entries, {}, &AssetSetEntry::asset);
        std::ranges::sort(canonical.roots);
        outManifest = std::move(canonical);
        return true;
    }

    AssetManifestWriteResult WriteAssetSetManifest(const AssetSetManifest& manifest)
    {
        AssetManifestWriteResult result;
        AssetSetManifest canonical;
        if (!NormalizeAssetSetManifest(manifest, canonical, result.issues))
        {
            return result;
        }
        std::uint32_t dependencyCount = 0u;
        std::uint32_t stringBytes = static_cast<std::uint32_t>(canonical.targetPlatform.size() + canonical.targetAbi.size());
        for (const AssetSetEntry& entry : canonical.entries)
        {
            dependencyCount += static_cast<std::uint32_t>(entry.dependencies.size());
        }
        for (const AssetBlobRecord& blob : canonical.blobs)
        {
            stringBytes += static_cast<std::uint32_t>(blob.targetPlatform.size()
                + blob.targetAbi.size() + blob.artifactPath.size());
        }
        Writer writer;
        writer.U32(kAssetManifestMagic);
        writer.U16(kAssetSetManifestVersion);
        writer.U16(static_cast<std::uint16_t>(kSetHeaderBytes));
        writer.Raw(canonical.assetSetId.value.data.data(), canonical.assetSetId.value.data.size());
        writer.U64(canonical.revision);
        writer.U32(static_cast<std::uint32_t>(canonical.entries.size()));
        writer.U32(static_cast<std::uint32_t>(canonical.blobs.size()));
        writer.U32(dependencyCount);
        writer.U32(static_cast<std::uint32_t>(canonical.roots.size()));
        writer.U32(stringBytes);
        std::uint32_t stringOffset = 0u;
        WriteStringRange(writer, canonical.targetPlatform, stringOffset);
        WriteStringRange(writer, canonical.targetAbi, stringOffset);
        writer.U32(0u);
        writer.U32(0u);
        writer.U32(0u);

        std::uint32_t dependencyBegin = 0u;
        for (const AssetSetEntry& entry : canonical.entries)
        {
            WriteIdentity(writer, entry.asset.key);
            writer.U8(static_cast<std::uint8_t>(entry.asset.kind));
            writer.U8(0u);
            writer.U16(0u);
            writer.U32(entry.blobIndex);
            writer.U32(dependencyBegin);
            writer.U32(static_cast<std::uint32_t>(entry.dependencies.size()));
            dependencyBegin += static_cast<std::uint32_t>(entry.dependencies.size());
        }
        for (const AssetBlobRecord& blob : canonical.blobs)
        {
            writer.Raw(blob.contentSha256.data(), blob.contentSha256.size());
            writer.U64(blob.byteSize);
            writer.U8(static_cast<std::uint8_t>(blob.kind));
            writer.U8(0u);
            writer.U16(0u);
            writer.U32(blob.representation);
            writer.U32(blob.schemaVersion);
            WriteStringRange(writer, blob.targetPlatform, stringOffset);
            WriteStringRange(writer, blob.targetAbi, stringOffset);
            WriteStringRange(writer, blob.artifactPath, stringOffset);
            writer.U32(0u);
        }
        for (const AssetSetEntry& entry : canonical.entries)
        {
            for (const AssetDependency& edge : entry.dependencies)
            {
                WriteIdentity(writer, edge.target.key);
                writer.U8(static_cast<std::uint8_t>(edge.target.kind));
                writer.U8(static_cast<std::uint8_t>(edge.kind));
                writer.U8(static_cast<std::uint8_t>(edge.scope));
                writer.U8(0u);
            }
        }
        for (const TypedAssetReference& root : canonical.roots)
        {
            WriteIdentity(writer, root.key);
            writer.U8(static_cast<std::uint8_t>(root.kind));
            writer.U8(0u);
            writer.U16(0u);
        }
        writer.Raw(canonical.targetPlatform.data(), canonical.targetPlatform.size());
        writer.Raw(canonical.targetAbi.data(), canonical.targetAbi.size());
        for (const AssetBlobRecord& blob : canonical.blobs)
        {
            writer.Raw(blob.targetPlatform.data(), blob.targetPlatform.size());
            writer.Raw(blob.targetAbi.data(), blob.targetAbi.size());
            writer.Raw(blob.artifactPath.data(), blob.artifactPath.size());
        }
        result.bytes = writer.Take();
        return result;
    }

    bool ReadAssetSetManifest(std::span<const std::byte> bytes,
        AssetSetManifest& outManifest, std::vector<AssetManifestIssue>& outIssues)
    {
        if (bytes.size() < kSetHeaderBytes || bytes.size() > kAssetSetManifestMaxBytes)
        {
            return SetIssue(outIssues, "assetSet.header", "CEMF v3 AssetSet header or bounded file size is invalid.");
        }
        Reader reader(bytes);
        const std::uint32_t magic = reader.U32();
        const std::uint16_t version = reader.U16();
        const std::uint16_t headerBytes = reader.U16();
        if (magic != kAssetManifestMagic || version != kAssetSetManifestVersion || headerBytes != kSetHeaderBytes)
        {
            return SetIssue(outIssues, "assetSet.header",
                "AssetSet requires CEMF v3; legacy v2 dependencies cannot be reinterpreted. Re-cook authoring sources.");
        }
        AssetSetManifest parsed;
        static_cast<void>(reader.Raw(parsed.assetSetId.value.data.data(), parsed.assetSetId.value.data.size()));
        parsed.revision = reader.U64();
        const std::uint32_t entryCount = reader.U32();
        const std::uint32_t blobCount = reader.U32();
        const std::uint32_t dependencyCount = reader.U32();
        const std::uint32_t rootCount = reader.U32();
        const std::uint32_t stringBytes = reader.U32();
        const std::uint32_t platformOffset = reader.U32();
        const std::uint32_t platformBytes = reader.U32();
        const std::uint32_t abiOffset = reader.U32();
        const std::uint32_t abiBytes = reader.U32();
        const std::uint32_t reserved0 = reader.U32();
        const std::uint32_t reserved1 = reader.U32();
        const std::uint32_t reserved2 = reader.U32();
        if (!reader.Ok() || reserved0 != 0u || reserved1 != 0u || reserved2 != 0u
            || entryCount == 0u || entryCount > kAssetSetManifestMaxEntries
            || blobCount == 0u || blobCount > kAssetSetManifestMaxEntries
            || rootCount == 0u || rootCount > kAssetSetManifestMaxEntries
            || dependencyCount > kAssetSetManifestMaxDependencies
            || stringBytes > kAssetSetManifestMaxStringBytes)
        {
            return SetIssue(outIssues, "assetSet.header", "Invalid reserved fields or AssetSet table counts.");
        }
        const std::uint64_t stringsBegin = kSetHeaderBytes
            + static_cast<std::uint64_t>(entryCount) * kSetEntryBytes
            + static_cast<std::uint64_t>(blobCount) * kSetBlobBytes
            + static_cast<std::uint64_t>(dependencyCount) * kSetDependencyBytes
            + static_cast<std::uint64_t>(rootCount) * kSetRootBytes;
        if (stringsBegin + stringBytes != bytes.size())
        {
            return SetIssue(outIssues, "assetSet.header", "Table counts do not exactly match the file size.");
        }
        std::uint32_t expectedStringOffset = 0u;
        const auto readString = [&](std::uint32_t offset, std::uint32_t length,
            std::size_t limit, std::string& out) -> bool
        {
            if (offset != expectedStringOffset || length > limit
                || static_cast<std::uint64_t>(offset) + length > stringBytes)
            {
                return false;
            }
            const auto* data = reinterpret_cast<const char*>(bytes.data()
                + static_cast<std::size_t>(stringsBegin) + offset);
            out.assign(data, length);
            expectedStringOffset += length;
            return true;
        };
        if (!readString(platformOffset, platformBytes, kSetMaxTargetBytes, parsed.targetPlatform)
            || !readString(abiOffset, abiBytes, kSetMaxTargetBytes, parsed.targetAbi))
        {
            return SetIssue(outIssues, "assetSet.target", "Target strings are oversized or noncanonical.");
        }

        parsed.entries.reserve(entryCount);
        std::vector<std::uint32_t> dependencyCounts;
        dependencyCounts.reserve(entryCount);
        std::uint32_t expectedDependency = 0u;
        for (std::uint32_t index = 0u; index < entryCount; ++index)
        {
            AssetSetEntry entry;
            static_cast<void>(ReadIdentity(reader, entry.asset.key));
            entry.asset.kind = static_cast<CookedAssetKind>(reader.U8());
            const std::uint8_t padding0 = reader.U8();
            const std::uint16_t padding1 = reader.U16();
            entry.blobIndex = reader.U32();
            const std::uint32_t begin = reader.U32();
            const std::uint32_t count = reader.U32();
            if (!reader.Ok() || padding0 != 0u || padding1 != 0u
                || begin != expectedDependency
                || static_cast<std::uint64_t>(begin) + count > dependencyCount)
            {
                return SetIssue(outIssues, "assetSet.entries", "Invalid entry fields or noncanonical dependency range.");
            }
            expectedDependency += count;
            dependencyCounts.push_back(count);
            parsed.entries.push_back(std::move(entry));
        }
        if (expectedDependency != dependencyCount)
        {
            return SetIssue(outIssues, "assetSet.dependencies", "Unreferenced dependency table records.");
        }
        parsed.blobs.reserve(blobCount);
        for (std::uint32_t index = 0u; index < blobCount; ++index)
        {
            AssetBlobRecord blob;
            static_cast<void>(reader.Raw(blob.contentSha256.data(), blob.contentSha256.size()));
            blob.byteSize = reader.U64();
            blob.kind = static_cast<CookedAssetKind>(reader.U8());
            const std::uint8_t padding0 = reader.U8();
            const std::uint16_t padding1 = reader.U16();
            blob.representation = reader.U32();
            blob.schemaVersion = reader.U32();
            const std::uint32_t blobPlatformOffset = reader.U32();
            const std::uint32_t blobPlatformBytes = reader.U32();
            const std::uint32_t blobAbiOffset = reader.U32();
            const std::uint32_t blobAbiBytes = reader.U32();
            const std::uint32_t pathOffset = reader.U32();
            const std::uint32_t pathBytes = reader.U32();
            const std::uint32_t padding2 = reader.U32();
            if (!reader.Ok() || padding0 != 0u || padding1 != 0u || padding2 != 0u
                || !readString(blobPlatformOffset, blobPlatformBytes, kSetMaxTargetBytes, blob.targetPlatform)
                || !readString(blobAbiOffset, blobAbiBytes, kSetMaxTargetBytes, blob.targetAbi)
                || !readString(pathOffset, pathBytes, kSetMaxPathBytes, blob.artifactPath))
            {
                return SetIssue(outIssues, "assetSet.blobs", "Invalid blob fields or noncanonical string ranges.");
            }
            parsed.blobs.push_back(std::move(blob));
        }
        if (expectedStringOffset != stringBytes)
        {
            return SetIssue(outIssues, "assetSet.strings", "Unreferenced string table bytes.");
        }
        for (std::size_t index = 0u; index < parsed.entries.size(); ++index)
        {
            AssetSetEntry& entry = parsed.entries[index];
            entry.dependencies.reserve(dependencyCounts[index]);
            for (std::uint32_t edgeIndex = 0u; edgeIndex < dependencyCounts[index]; ++edgeIndex)
            {
                AssetDependency edge;
                static_cast<void>(ReadIdentity(reader, edge.target.key));
                edge.target.kind = static_cast<CookedAssetKind>(reader.U8());
                edge.kind = static_cast<AssetDependencyKind>(reader.U8());
                edge.scope = static_cast<AssetDependencyScope>(reader.U8());
                const std::uint8_t padding = reader.U8();
                if (!reader.Ok() || padding != 0u)
                {
                    return SetIssue(outIssues, "assetSet.dependencies", "Invalid dependency reserved field.");
                }
                entry.dependencies.push_back(edge);
            }
            if (!std::ranges::is_sorted(entry.dependencies))
            {
                return SetIssue(outIssues, "assetSet.dependencies", "Dependency table is not in canonical order.");
            }
        }
        parsed.roots.reserve(rootCount);
        for (std::uint32_t index = 0u; index < rootCount; ++index)
        {
            TypedAssetReference root;
            static_cast<void>(ReadIdentity(reader, root.key));
            root.kind = static_cast<CookedAssetKind>(reader.U8());
            const std::uint8_t padding0 = reader.U8();
            const std::uint16_t padding1 = reader.U16();
            if (!reader.Ok() || padding0 != 0u || padding1 != 0u)
            {
                return SetIssue(outIssues, "assetSet.roots", "Invalid root reserved fields.");
            }
            parsed.roots.push_back(root);
        }
        if (reader.Offset() != stringsBegin
            || !std::ranges::is_sorted(parsed.entries, {}, &AssetSetEntry::asset)
            || !std::ranges::is_sorted(parsed.blobs)
            || !std::ranges::is_sorted(parsed.roots))
        {
            return SetIssue(outIssues, "assetSet.tables", "AssetSet tables are not in canonical order.");
        }
        if (!ValidateAssetSetManifest(parsed, outIssues))
        {
            return false;
        }
        outManifest = std::move(parsed);
        return true;
    }
}
