#include "CookedAssetManifest.h"

#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <unordered_set>
#include <utility>

#pragma comment(lib, "bcrypt.lib")

namespace experiment::cooked
{
    namespace
    {
        inline constexpr std::size_t kHeaderBytes = 24u;
        inline constexpr std::size_t kEntryBytes = 80u;

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
                if (!IsAssetIdV4(entry.assetId))
                {
                    AddIssue(issues, context + ".assetId",
                        "manifest key는 UUIDv4 asset identity여야 한다.");
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
                    if (!IsAssetIdV4(dependency))
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
                            "dependency GUID가 manifest entry로 해석되지 않는다.");
                        valid = false;
                    }
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

    bool ComputeSha256(std::span<const std::byte> bytes,
        Sha256Digest& outDigest, std::string& outError) noexcept
    {
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
    }

    AssetManifestWriteResult WriteAssetManifest(
        const CookedAssetManifest& manifest)
    {
        AssetManifestWriteResult result;
        if (!ValidateManifest(manifest, result.issues)) return result;

        CookedAssetManifest canonical = manifest;
        std::ranges::sort(canonical.entries, {},
            &CookedAssetManifestEntry::assetId);
        for (CookedAssetManifestEntry& entry : canonical.entries)
            std::ranges::sort(entry.dependencies, {}, &AssetId::value);

        std::size_t dependencyCount = 0u;
        std::size_t stringBytes = 0u;
        for (const CookedAssetManifestEntry& entry : canonical.entries)
        {
            if (AddWouldOverflow(dependencyCount, entry.dependencies.size())
                || AddWouldOverflow(stringBytes, entry.artifactPath.size()))
            {
                AddIssue(result.issues, "manifest",
                    "manifest count/문자열 크기가 size_t 범위를 넘는다.");
                return result;
            }
            dependencyCount += entry.dependencies.size();
            stringBytes += entry.artifactPath.size();
        }
        if (canonical.entries.size() > (std::numeric_limits<std::uint32_t>::max)()
            || dependencyCount > (std::numeric_limits<std::uint32_t>::max)()
            || stringBytes > (std::numeric_limits<std::uint32_t>::max)())
        {
            AddIssue(result.issues, "manifest",
                "CEMF v1의 32-bit count 범위를 넘는다.");
            return result;
        }

        Writer writer;
        writer.U32(kAssetManifestMagic);
        writer.U16(kAssetManifestVersion);
        writer.U16(static_cast<std::uint16_t>(kHeaderBytes));
        writer.U32(static_cast<std::uint32_t>(canonical.entries.size()));
        writer.U32(static_cast<std::uint32_t>(dependencyCount));
        writer.U32(static_cast<std::uint32_t>(stringBytes));
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
        const std::uint32_t stringBytes = reader.U32();
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
        std::size_t expected = kHeaderBytes;
        if (MultiplyWouldOverflow(entries, kEntryBytes)
            || AddWouldOverflow(expected, entries * kEntryBytes))
        {
            AddIssue(outIssues, "manifest.header", "entry table 크기가 overflow한다.");
            return false;
        }
        expected += entries * kEntryBytes;
        if (MultiplyWouldOverflow(dependencies, sizeof(Uuid::Uuid16))
            || AddWouldOverflow(expected, dependencies * sizeof(Uuid::Uuid16)))
        {
            AddIssue(outIssues, "manifest.header",
                "dependency table 크기가 overflow한다.");
            return false;
        }
        expected += dependencies * sizeof(Uuid::Uuid16);
        if (AddWouldOverflow(expected, stringBytes))
        {
            AddIssue(outIssues, "manifest.header", "string table 크기가 overflow한다.");
            return false;
        }
        expected += stringBytes;
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
            reader.Raw(raw.assetId.value.data.data(), raw.assetId.value.data.size());
            raw.kind = static_cast<CookedAssetKind>(reader.U8());
            const std::uint8_t reserved0 = reader.U8();
            const std::uint8_t reserved1 = reader.U8();
            const std::uint8_t reserved2 = reader.U8();
            raw.formatVersion = reader.U32();
            raw.byteSize = reader.U64();
            reader.Raw(raw.contentSha256.data(), raw.contentSha256.size());
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
        const std::size_t stringsBegin = reader.Offset();

        CookedAssetManifest parsed;
        parsed.entries.reserve(entryCount);
        for (std::size_t index = 0u; index < rawEntries.size(); ++index)
        {
            const RawEntry& raw = rawEntries[index];
            if (static_cast<std::uint64_t>(raw.pathOffset) + raw.pathBytes > stringBytes
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
                bytes.data() + stringsBegin + raw.pathOffset);
            entry.artifactPath.assign(path, raw.pathBytes);
            entry.dependencies.assign(
                dependencyIds.begin() + raw.dependencyBegin,
                dependencyIds.begin() + raw.dependencyBegin + raw.dependencyCount);
            parsed.entries.push_back(std::move(entry));
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
