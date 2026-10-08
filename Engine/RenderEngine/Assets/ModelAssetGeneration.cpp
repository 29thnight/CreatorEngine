#include "ModelAssetGeneration.h"
#include "ModelAnimationSampler.h"

#include <chrono>

#include "ModelSidecarV2.h"
#include "AuthoringParsedDocument.h"
#include "AuthoringCookedDocument.h"
#include "../Experiment/Cooked/CookedModelCodec.h"
#include "../Experiment/ModelLoader.h"
#include "../Texture.h"
#include "../../EngineDiagnostics/ProfileScope.h"
#include "../Interfaces/AssetAuthoringPort.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <fstream>
#include <limits>
#include <optional>
#include <ranges>
#include <set>
#include <string_view>
#include <type_traits>
#include <utility>

namespace assets
{
    namespace
    {
        namespace ck = experiment::cooked;

        struct GenerationSubAsset final
        {
            SubAssetKind kind{ SubAssetKind::Mesh };
            std::string stableKey{};
            Uuid::Uuid16 assetId{};
            std::filesystem::path artifactPath{};
            std::string artifactFingerprint{};
        };

        struct GenerationRecord final
        {
            std::string identityProfile{};
            std::string identityEpoch{};
            Uuid::Uuid16 assetId{};
            std::uint64_t generation{};
            std::string sourceFingerprint{};
            std::string sidecarFingerprint{};
            std::filesystem::path modelArtifactPath{};
            std::string modelArtifactFingerprint{};
            std::vector<GenerationSubAsset> subAssets{};
        };

        void AddIssue(ModelAssetGenerationLoadResult& result,
            ModelAssetGenerationIssueCode code, std::string context,
            std::string message)
        {
            result.issues.push_back({ code, std::move(context), std::move(message) });
        }

        [[nodiscard]] bool ReadBytes(const std::filesystem::path& path,
            std::vector<std::byte>& out)
        {
            std::ifstream stream(path, std::ios::binary);
            if (!stream) return false;
            stream.seekg(0, std::ios::end);
            const std::streamoff size = stream.tellg();
            if (size < 0) return false;
            stream.seekg(0, std::ios::beg);
            out.resize(static_cast<std::size_t>(size));
            if (!out.empty())
            {
                stream.read(reinterpret_cast<char*>(out.data()),
                    static_cast<std::streamsize>(out.size()));
            }
            return stream.good() || stream.eof();
        }

        [[nodiscard]] bool ReadText(const std::filesystem::path& path,
            std::string& out)
        {
            std::vector<std::byte> bytes;
            if (!ReadBytes(path, bytes)) return false;
            out.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            return true;
        }

        [[nodiscard]] std::string Fingerprint(std::span<const std::byte> bytes)
        {
            return MakeSourceFingerprint(std::span<const std::uint8_t>{
                reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size() });
        }

        [[nodiscard]] std::string Fingerprint(std::string_view text)
        {
            return MakeSourceFingerprint(std::span<const std::uint8_t>{
                reinterpret_cast<const std::uint8_t*>(text.data()), text.size() });
        }

        [[nodiscard]] bool IsSafeRelativePath(const std::filesystem::path& path)
        {
            if (path.empty() || path.is_absolute() || path.has_root_path()) return false;
            const std::filesystem::path normalized = path.lexically_normal();
            if (normalized.empty() || normalized == ".") return false;
            for (const std::filesystem::path& part : normalized)
            {
                if (part == "..") return false;
            }
            return true;
        }

        [[nodiscard]] bool ReadRequiredScalar(const Authoring::ReadNode& node,
            std::string_view key, std::string& out)
        {
            const std::string ownedKey(key);
            const Authoring::ReadNode value = node[ownedKey.c_str()];
            if (!value || !value.IsScalar()) return false;
            out = value.AsString();
            return !out.empty();
        }

        [[nodiscard]] bool ParseGenerationRecord(std::string_view text,
            GenerationRecord& out, std::string& failure)
        {
            std::string parseError;
            const std::span<const std::byte> bytes{
                reinterpret_cast<const std::byte*>(text.data()), text.size() };
            const Authoring::ParsedDocument document = Authoring::IsCookedDocument(bytes)
                ? Authoring::ParsedDocument::ParseCooked(bytes, parseError)
                : Authoring::ParsedDocument::ParseText(std::string(text), parseError);
            if (!document || !document.Root().IsMap())
            {
                failure = "generation record를 파싱하지 못했다: " + parseError;
                return false;
            }
            const Authoring::ReadNode root = document.Root();
            if (root["schemaVersion"].As(0u) != 1u)
            {
                failure = "지원하는 generation record schemaVersion은 1이다.";
                return false;
            }

            GenerationRecord record;
            std::string assetIdText;
            if (!ReadRequiredScalar(root, "identityProfile", record.identityProfile)
                || !ReadRequiredScalar(root, "identityEpoch", record.identityEpoch)
                || !ReadRequiredScalar(root, "assetId", assetIdText)
                || !TryParseCanonicalUuidV8(assetIdText, record.assetId)
                || !ReadRequiredScalar(root, "sourceFingerprint", record.sourceFingerprint)
                || !ReadRequiredScalar(root, "sidecarFingerprint", record.sidecarFingerprint))
            {
                failure = "generation record identity 필드가 없거나 잘못됐다.";
                return false;
            }
            record.generation = root["generation"].As(std::uint64_t{ 0 });
            if (record.generation == 0u
                || !IsFingerprintText(record.sourceFingerprint)
                || !IsFingerprintText(record.sidecarFingerprint))
            {
                failure = "generation/source/sidecar fingerprint가 잘못됐다.";
                return false;
            }

            const Authoring::ReadNode model = root["modelArtifact"];
            std::string modelPath;
            if (!model || !model.IsMap()
                || !ReadRequiredScalar(model, "path", modelPath)
                || !ReadRequiredScalar(model, "fingerprint",
                    record.modelArtifactFingerprint))
            {
                failure = "modelArtifact가 없거나 불완전하다.";
                return false;
            }
            record.modelArtifactPath = std::filesystem::path(modelPath);
            if (!IsSafeRelativePath(record.modelArtifactPath)
                || !IsFingerprintText(record.modelArtifactFingerprint))
            {
                failure = "modelArtifact 경로 또는 fingerprint가 잘못됐다.";
                return false;
            }

            const Authoring::ReadNode subAssets = root["subAssets"];
            if (!subAssets || !subAssets.IsSequence())
            {
                failure = "generation record subAssets sequence가 없다.";
                return false;
            }
            std::set<Uuid::Uuid16> ids;
            for (std::size_t index = 0; index < subAssets.Size(); ++index)
            {
                const Authoring::ReadNode node = subAssets.At(index);
                GenerationSubAsset entry;
                std::string kind;
                std::string id;
                if (!node.IsMap()
                    || !ReadRequiredScalar(node, "kind", kind)
                    || !TryParseKindName(kind, entry.kind)
                    || !ReadRequiredScalar(node, "stableKey", entry.stableKey)
                    || !ReadRequiredScalar(node, "assetId", id)
                    || !TryParseCanonicalUuidV8(id, entry.assetId)
                    || !ids.insert(entry.assetId).second)
                {
                    failure = "generation record subAssets["
                        + std::to_string(index) + "]가 잘못됐다.";
                    return false;
                }
                if (entry.kind == SubAssetKind::Texture)
                {
                    std::string artifactPath;
                    if (!ReadRequiredScalar(node, "artifactPath", artifactPath)
                        || !ReadRequiredScalar(node, "artifactFingerprint",
                            entry.artifactFingerprint))
                    {
                        failure = "embedded texture artifact가 불완전하다: "
                            + entry.stableKey;
                        return false;
                    }
                    entry.artifactPath = std::filesystem::path(artifactPath);
                    if (!IsSafeRelativePath(entry.artifactPath)
                        || !IsFingerprintText(entry.artifactFingerprint))
                    {
                        failure = "embedded texture artifact 경로/fingerprint가 잘못됐다: "
                            + entry.stableKey;
                        return false;
                    }
                }
                record.subAssets.push_back(std::move(entry));
            }
            out = std::move(record);
            return true;
        }

        [[nodiscard]] bool MatchesSidecar(const GenerationRecord& record,
            const ModelSidecarV2& sidecar, std::string& failure)
        {
            if (record.identityProfile != sidecar.identityProfile
                || record.identityEpoch != sidecar.identityEpoch
                || record.assetId != sidecar.assetId
                || record.generation != sidecar.generation
                || record.sourceFingerprint != sidecar.sourceFingerprint
                || record.subAssets.size() != sidecar.subAssets.size())
            {
                failure = "generation record identity가 sidecar와 다르다.";
                return false;
            }
            for (std::size_t index = 0; index < sidecar.subAssets.size(); ++index)
            {
                const GenerationSubAsset& generation = record.subAssets[index];
                const ModelSubAssetRecord& canonical = sidecar.subAssets[index];
                if (generation.kind != canonical.kind
                    || generation.stableKey != canonical.stableKey
                    || generation.assetId != canonical.assetId)
                {
                    failure = "generation record subasset closure가 sidecar와 다르다: "
                        + std::to_string(index);
                    return false;
                }
            }
            return true;
        }

        template <typename T>
        [[nodiscard]] std::vector<const ModelSubAssetRecord*> RecordsOf(
            const ModelSidecarV2& sidecar, T kind)
        {
            std::vector<const ModelSubAssetRecord*> out;
            for (const ModelSubAssetRecord& record : sidecar.subAssets)
            {
                if (record.kind == kind) out.push_back(&record);
            }
            return out;
        }

        [[nodiscard]] bool CopyTexturePixels(const std::vector<std::byte>& encoded,
            ModelTextureColorSpace colorSpace, ModelTextureAsset& out,
            std::string& failure)
        {
            // ★ 디코드는 Texture 가 한다(축 A). 여기 있던 DirectXTex 호출
            //   — LoadSharedFromMemory 로 한 번 텍스처를 세운 뒤 Convert ·
            //   Decompress · IsSRGB 를 직접 부르던 것 — 은 전부
            //   Texture::DecodeToRgba8 로 옮겼다. "색공간은 라벨로만 정한다"
            //   는 규약과 그것이 생긴 사연(43% 탈색)도 그 함수의 주석에 함께
            //   있다. cook 경로가 필요한 것은 "바이트 -> 중립 RGBA8" 하나였고,
            //   그것 때문에 디코더를 아는 파일이 둘이었다.
            TextureImage decoded;
            if (!Texture::DecodeToRgba8(encoded, decoded, failure)) return false;
            auto owner = Texture::CreateSharedFromImage("Model.embedded", std::move(decoded));
            owner = Texture::WithColorSpace(owner, colorSpace == ModelTextureColorSpace::Srgb);
            owner = Texture::WithMipChain(owner, failure);
            if (!owner) return false;
            const auto image = owner->GetImageView();

            out.colorSpace = colorSpace;
            out.format = colorSpace == ModelTextureColorSpace::Srgb
                ? RHIFormat::RGBA8UnormSrgb : RHIFormat::RGBA8Unorm;
            out.width = image.Width();
            out.height = image.Height();
            out.mipLevels = image.MipLevels();
            out.arraySize = image.ArraySize();
            out.isCube = image.IsCube();

            // item 바깥, mip 안쪽. TextureImage 도 같은 규약이라 순서가 같다.
            for (std::uint32_t item = 0; item < image.ArraySize(); ++item)
            {
                for (std::uint32_t mip = 0; mip < image.MipLevels(); ++mip)
                {
                    const TextureSubimage* source = image.Find(mip, item);
                    const std::byte* pixels = (nullptr != source)
                        ? source->pixels : nullptr;
                    if (nullptr == source || nullptr == pixels
                        || 0u == source->slicePitch)
                    {
                        failure = "decoded texture subresource가 비었다.";
                        return false;
                    }
                    ModelTextureSubresource subresource;
                    subresource.width = source->width;
                    subresource.height = source->height;
                    subresource.offset = out.pixels.size();
                    subresource.rowPitch = source->rowPitch;
                    subresource.slicePitch = source->slicePitch;
                    out.pixels.insert(out.pixels.end(), pixels,
                        pixels + source->slicePitch);
                    out.subresources.push_back(subresource);
                }
            }
            return !out.pixels.empty();
        }

        // A validated PNG still has to be decoded and have its mip chain built.
        // Keep that derived RGBA8 chain outside the immutable model generation.
        // The caller verifies the source artifact SHA-256 before consulting this cache.
        [[nodiscard]] bool ReadDecodedTextureCache(const std::filesystem::path& path,
            ModelTextureColorSpace colorSpace, ModelTextureAsset& out)
        {
            std::ifstream stream(path, std::ios::binary);
            if (!stream) return false;
            const auto read = [&stream](auto& value) {
                stream.read(reinterpret_cast<char*>(&value), sizeof(value));
                return static_cast<bool>(stream);
            };
            std::array<char, 8> magic{};
            std::uint32_t width{}, height{}, levels{}, arrays{}, format{}, space{}, count{};
            std::uint64_t bytes{};
            stream.read(magic.data(), magic.size());
            if (!stream || magic != std::array<char, 8>{'C','E','I','M','0','0','0','1'} ||
                !read(width) || !read(height) || !read(levels) || !read(arrays) ||
                !read(format) || !read(space) || !read(count) || !read(bytes) ||
                !width || !height || width > 16384 || height > 16384 ||
                !levels || levels > 16 || arrays != 1 || count != levels ||
                bytes == 0 || bytes > (512ull << 20) ||
                space != static_cast<std::uint32_t>(colorSpace) ||
                format != static_cast<std::uint32_t>(colorSpace == ModelTextureColorSpace::Srgb
                    ? RHIFormat::RGBA8UnormSrgb : RHIFormat::RGBA8Unorm))
                return false;
            ModelTextureAsset candidate;
            candidate.colorSpace = colorSpace;
            candidate.format = static_cast<RHIFormat>(format);
            candidate.width = width;
            candidate.height = height;
            candidate.mipLevels = levels;
            candidate.arraySize = arrays;
            candidate.subresources.resize(count);
            std::uint64_t expectedOffset = 0;
            for (std::uint32_t mip = 0; mip < count; ++mip)
            {
                auto& sub = candidate.subresources[mip];
                if (!read(sub.width) || !read(sub.height) || !read(sub.offset) ||
                    !read(sub.rowPitch) || !read(sub.slicePitch) ||
                    sub.width != (std::max)(1u, width >> mip) ||
                    sub.height != (std::max)(1u, height >> mip) ||
                    sub.offset != expectedOffset || sub.rowPitch < std::uint64_t(sub.width) * 4 ||
                    sub.slicePitch < sub.rowPitch * sub.height ||
                    sub.slicePitch > bytes - expectedOffset)
                    return false;
                expectedOffset += sub.slicePitch;
            }
            if (expectedOffset != bytes) return false;
            candidate.pixels.resize(static_cast<std::size_t>(bytes));
            stream.read(reinterpret_cast<char*>(candidate.pixels.data()),
                static_cast<std::streamsize>(bytes));
            if (!stream || stream.peek() != std::char_traits<char>::eof()) return false;
            out.colorSpace = candidate.colorSpace;
            out.format = candidate.format;
            out.width = candidate.width;
            out.height = candidate.height;
            out.mipLevels = candidate.mipLevels;
            out.arraySize = candidate.arraySize;
            out.isCube = false;
            out.subresources = std::move(candidate.subresources);
            out.pixels = std::move(candidate.pixels);
            return true;
        }

        void PublishDecodedTextureCache(const std::filesystem::path& path,
            const ModelTextureAsset& texture)
        {
            // Runtime prepares bytes; the Editor owns staging and atomic publication.
            // A missing host writer leaves the decoded in-memory texture usable.
            std::vector<std::byte> payload;
            payload.reserve(44u + texture.subresources.size() * 32u + texture.pixels.size());

            const auto append = [&payload](std::span<const std::byte> bytes) {
                payload.insert(payload.end(), bytes.begin(), bytes.end());
            };
            const auto write = [&append](const auto& value) {
                append(std::as_bytes(std::span{ &value, std::size_t{ 1 } }));
            };

            constexpr std::array<char, 8> magic{'C','E','I','M','0','0','0','1'};
            append(std::as_bytes(std::span{ magic }));
            write(texture.width);
            write(texture.height);
            write(texture.mipLevels);
            write(texture.arraySize);

            const auto format = static_cast<std::uint32_t>(texture.format);
            const auto space = static_cast<std::uint32_t>(texture.colorSpace);
            const auto count = static_cast<std::uint32_t>(texture.subresources.size());
            const auto bytes = static_cast<std::uint64_t>(texture.pixels.size());
            write(format);
            write(space);
            write(count);
            write(bytes);

            for (const auto& sub : texture.subresources)
            {
                write(sub.width);
                write(sub.height);
                write(sub.offset);
                write(sub.rowPitch);
                write(sub.slicePitch);
            }

            append(texture.pixels);
            (void)AssetAuthoringPort::WriteModelCache(path, payload);
        }

        [[nodiscard]] ModelInterpolationMode ConvertInterpolation(
            experiment::InterpolationMode value) noexcept
        {
            return value == experiment::InterpolationMode::Step
                ? ModelInterpolationMode::Step : ModelInterpolationMode::Linear;
        }

        [[nodiscard]] bool IsValidAggregate(
            const ModelAssetGenerationIdentity& identity,
            const std::vector<ModelMeshAsset>& meshes,
            const std::vector<ModelMaterialAsset>& materials,
            const std::vector<ModelTextureAsset>& textures,
            const std::vector<ModelGpuUploadDescriptor>& descriptors)
        {
            if (!IsUuidV8(identity.modelId) || identity.generation == 0u
                || identity.identityProfile != kIdentityProfile
                || !IsFingerprintText(identity.sourceFingerprint)
                || descriptors.size() != meshes.size() * 2u + textures.size())
            {
                return false;
            }
            for (const ModelMeshAsset& mesh : meshes)
            {
                if (!IsUuidV8(mesh.meshId) || mesh.vertexBytes.empty()
                    || mesh.indices.empty() || mesh.vertexStride == 0u
                    || mesh.vertexBytes.size() % mesh.vertexStride != 0u)
                {
                    return false;
                }
                if (!mesh.materialId.IsNil()
                    && std::ranges::none_of(materials,
                        [&](const ModelMaterialAsset& material)
                        { return material.materialId == mesh.materialId; }))
                {
                    return false;
                }
            }
            for (const ModelTextureAsset& texture : textures)
            {
                if (!IsUuidV8(texture.textureId) || texture.pixels.empty()
                    || texture.subresources.empty()
                    || texture.format == RHIFormat::Unknown)
                {
                    return false;
                }
            }
            return true;
        }
    }

    ModelAssetGeneration::ModelAssetGeneration(LoadKey, ModelAssetGenerationIdentity identity,
        std::string name, std::filesystem::path sourcePath,
        std::vector<ModelNodeAsset> nodes,
        std::vector<ModelMeshAsset> meshes,
        std::vector<ModelMaterialAsset> materials,
        std::vector<ModelTextureAsset> textures,
        std::optional<ModelSkeletonAsset> skeleton,
        std::vector<ModelAnimationAsset> animations,
        std::optional<ModelAnimatorAsset> animator,
        std::vector<ModelGpuUploadDescriptor> gpuDescriptors)
        : identity_(std::move(identity)), name_(std::move(name)),
        sourcePath_(std::move(sourcePath)), nodes_(std::move(nodes)),
        meshes_(std::move(meshes)), materials_(std::move(materials)),
        textures_(std::move(textures)), skeleton_(std::move(skeleton)),
        animations_(std::move(animations)), animator_(std::move(animator)),
        gpuDescriptors_(std::move(gpuDescriptors))
    {
        if (!skeleton_)
        {
            return;
        }
        m_animationTracks.resize(animations_.size());
        for (std::size_t clip = 0; clip < animations_.size(); ++clip)
        {
            animation::BuildTrackTable(animations_[clip], skeleton_->bones.size(), m_animationTracks[clip]);
        }
    }

    const ModelAssetGenerationIdentity& ModelAssetGeneration::Identity() const noexcept
    {
        return identity_;
    }

    ModelAssetGenerationHandle ModelAssetGeneration::Handle() const noexcept
    {
        return { identity_.modelId, identity_.generation };
    }

    std::size_t ModelAssetGeneration::EstimatedCpuBytes() const noexcept
    {
        // Transitional aggregate charge: geometry/LOD, embedded pixels, skeleton
        // and every clip remain physically owned together until payload splitting.
        std::size_t bytes = sizeof(*this);
        const auto add = [&bytes](std::size_t count, std::size_t width = 1)
        {
            const auto maximum = (std::numeric_limits<std::size_t>::max)();
            if (width != 0 && count > (maximum - bytes) / width)
            {
                bytes = maximum;
            }
            else
            {
                bytes += count * width;
            }
        };
        const auto addString = [&add](const auto& value)
        {
            using Character = typename std::decay_t<decltype(value)>::value_type;
            add(value.capacity(), sizeof(Character));
        };
        const auto addVector = [&add](const auto& values)
        {
            using Element = typename std::decay_t<decltype(values)>::value_type;
            add(values.capacity(), sizeof(Element));
        };
        const auto addMeshlets = [&addVector](const experiment::MeshletPayload& meshlets)
        {
            addVector(meshlets.descriptors);
            addVector(meshlets.vertexRemap);
            addVector(meshlets.triangleIndices);
            addVector(meshlets.primitiveRemap);
        };
        addString(identity_.identityProfile);
        addString(identity_.identityEpoch);
        addString(identity_.sourceFingerprint);
        addString(name_);
        addString(sourcePath_.native());
        addVector(nodes_);
        for (const auto& node : nodes_)
        {
            addString(node.name);
            addVector(node.meshes);
        }
        addVector(meshes_);
        for (const auto& mesh : meshes_)
        {
            addString(mesh.name);
            addVector(mesh.vertexBytes);
            addVector(mesh.indices);
            addMeshlets(mesh.meshlets);
            addVector(mesh.coarseLods.levels);
            for (const auto& lod : mesh.coarseLods.levels)
            {
                addVector(lod.indices);
                addMeshlets(lod.meshlets);
            }
        }
        addVector(materials_);
        for (const auto& material : materials_)
        {
            addString(material.name);
            addVector(material.properties);
            for (const auto& property : material.properties)
            {
                addString(property.name);
                if (const auto* text = std::get_if<std::string>(&property.value))
                {
                    addString(*text);
                }
            }
            addVector(material.keywords);
            for (const auto& keyword : material.keywords)
            {
                addString(keyword);
            }
            addVector(material.keywordSelections);
        }
        addVector(textures_);
        for (const auto& texture : textures_)
        {
            addString(texture.name);
            addVector(texture.subresources);
            addVector(texture.pixels);
        }
        if (skeleton_)
        {
            addVector(skeleton_->bones);
            for (const auto& bone : skeleton_->bones)
            {
                addString(bone.name);
            }
        }
        addVector(animations_);
        for (const auto& animation : animations_)
        {
            addString(animation.name);
            addVector(animation.tracks);
            for (const auto& track : animation.tracks)
            {
                addVector(track.translations);
                addVector(track.rotations);
                addVector(track.scales);
            }
            addVector(animation.events);
            for (const auto& event : animation.events)
            {
                addString(event.name);
            }
        }
        addVector(m_animationTracks);
        for (const auto& tracks : m_animationTracks)
        {
            addVector(tracks);
        }
        addVector(gpuDescriptors_);
        return bytes;
    }

    const std::string& ModelAssetGeneration::Name() const noexcept { return name_; }
    const std::filesystem::path& ModelAssetGeneration::SourcePath() const noexcept
    {
        return sourcePath_;
    }
    std::span<const ModelNodeAsset> ModelAssetGeneration::Nodes() const noexcept
    {
        return nodes_;
    }
    std::span<const ModelMeshAsset> ModelAssetGeneration::Meshes() const noexcept
    {
        return meshes_;
    }
    std::span<const ModelMaterialAsset> ModelAssetGeneration::Materials() const noexcept
    {
        return materials_;
    }
    std::span<const ModelTextureAsset> ModelAssetGeneration::Textures() const noexcept
    {
        return textures_;
    }
    const ModelSkeletonAsset* ModelAssetGeneration::Skeleton() const noexcept
    {
        return skeleton_ ? &*skeleton_ : nullptr;
    }
    std::span<const ModelAnimationAsset> ModelAssetGeneration::Animations() const noexcept
    {
        return animations_;
    }
    const ModelAnimatorAsset* ModelAssetGeneration::Animator() const noexcept
    {
        return animator_ ? &*animator_ : nullptr;
    }

    std::span<const ModelAnimationTrack* const> ModelAssetGeneration::AnimationTracks(int clipIndex) const noexcept
    {
        if (clipIndex < 0 || static_cast<std::size_t>(clipIndex) >= m_animationTracks.size()) return {};
        return m_animationTracks[static_cast<std::size_t>(clipIndex)];
    }
    std::span<const ModelGpuUploadDescriptor>
        ModelAssetGeneration::GpuDescriptors() const noexcept
    {
        return gpuDescriptors_;
    }

    const ModelMeshAsset* ModelAssetGeneration::FindMesh(
        const Uuid::Uuid16& meshId) const noexcept
    {
        const auto found = std::ranges::find(meshes_, meshId, &ModelMeshAsset::meshId);
        return found != meshes_.end() ? &*found : nullptr;
    }

    const ModelMaterialAsset* ModelAssetGeneration::FindMaterial(
        const Uuid::Uuid16& materialId) const noexcept
    {
        const auto found = std::ranges::find(
            materials_, materialId, &ModelMaterialAsset::materialId);
        return found != materials_.end() ? &*found : nullptr;
    }

    const ModelTextureAsset* ModelAssetGeneration::FindTexture(
        const Uuid::Uuid16& textureId) const noexcept
    {
        const auto found = std::ranges::find(
            textures_, textureId, &ModelTextureAsset::textureId);
        return found != textures_.end() ? &*found : nullptr;
    }

    ModelAssetGenerationLoadResult LoadModelAssetGeneration(
        const ModelAssetGenerationLoadRequest& request)
    {
        ModelAssetGenerationLoadResult result;
        ce::profile_scope profile{ ce::marker<"ModelGen.Load">() };
        // markPhase 와 같은 경계로 캡처에도 남긴다. 다음 단계를 열기 전에 앞 단계를
        // 닫으므로 이른 반환에서도 짝이 맞는다.
        std::optional<ce::profile_scope> phaseProfile;
        phaseProfile.emplace(ce::marker<"ModelGen.Identity">());
        auto phaseClock = std::chrono::steady_clock::now();
        const auto markPhase = [&result, &phaseClock](const char* phase)
            {
                const auto now = std::chrono::steady_clock::now();
                result.phases.push_back({ phase,
                    std::chrono::duration<double, std::milli>(now - phaseClock).count() });
                phaseClock = now;
            };
        if (request.identityHeaderPath.empty()
            || (request.generationPath.empty()
                && (request.generationRoot.empty()
                    || request.canonicalSidecarPath.empty())))
        {
            AddIssue(result, ModelAssetGenerationIssueCode::InvalidRequest,
                "request", "epoch header와 generation 위치가 필요하다.");
            return result;
        }

        std::string headerText;
        IdentityEpochHeader header;
        std::vector<EpochHeaderIssue> headerIssues;
        if (!ReadText(request.identityHeaderPath, headerText)
            || !ReadIdentityEpochHeader(headerText, header, headerIssues))
        {
            AddIssue(result, ModelAssetGenerationIssueCode::InvalidEpoch,
                "identityHeader", headerIssues.empty()
                    ? "identity epoch header를 읽지 못했다."
                    : headerIssues.front().message);
            return result;
        }

        std::filesystem::path generationPath = request.generationPath;
        std::string sidecarText;
        ModelSidecarV2 sidecar;
        std::vector<SidecarIssue> sidecarIssues;
        if (!request.canonicalSidecarPath.empty()
            && !ReadText(request.canonicalSidecarPath, sidecarText))
        {
            AddIssue(result, ModelAssetGenerationIssueCode::InvalidSidecar,
                "canonicalSidecar", "canonical sidecar를 읽지 못했다.");
            return result;
        }
        if (generationPath.empty())
        {
            if (!ReadModelSidecarV2(sidecarText, sidecar, sidecarIssues))
            {
                AddIssue(result, ModelAssetGenerationIssueCode::InvalidSidecar,
                    "canonicalSidecar", sidecarIssues.empty()
                        ? "canonical sidecar를 읽지 못했다."
                        : sidecarIssues.front().message);
                return result;
            }
            generationPath = request.generationRoot
                / Uuid::ToString(sidecar.assetId)
                / std::to_string(sidecar.generation);
        }

        std::string generationSidecarText;
        if (!ReadText(generationPath / "sidecar.meta", generationSidecarText))
        {
            AddIssue(result, ModelAssetGenerationIssueCode::MissingFile,
                "generation.sidecar", "generation sidecar.meta를 읽지 못했다.");
            return result;
        }
        if (sidecarText.empty()) sidecarText = generationSidecarText;
        if (sidecarText != generationSidecarText)
        {
            AddIssue(result, ModelAssetGenerationIssueCode::FingerprintMismatch,
                "canonicalSidecar", "canonical sidecar와 generation sidecar가 다르다.");
            return result;
        }
        sidecarIssues.clear();
        if (!ReadModelSidecarV2(generationSidecarText, sidecar, sidecarIssues)
            || !ValidateModelSidecarV2Closure(sidecar, header, sidecarIssues))
        {
            AddIssue(result, ModelAssetGenerationIssueCode::InvalidSidecar,
                "generation.sidecar", sidecarIssues.empty()
                    ? "schema-v2 closure 검증이 실패했다."
                    : sidecarIssues.front().message);
            return result;
        }
        if ((!request.expectedModelId.IsNil()
                && request.expectedModelId != sidecar.assetId)
            || (request.expectedGeneration != 0u
                && request.expectedGeneration != sidecar.generation))
        {
            AddIssue(result, ModelAssetGenerationIssueCode::IdentityMismatch,
                "request.expected", "요청한 model identity/generation과 sidecar가 다르다.");
            return result;
        }

        std::string generationText;
        GenerationRecord record;
        std::string failure;
        if (!ReadText(generationPath / "generation.asset", generationText)
            || !ParseGenerationRecord(generationText, record, failure)
            || !MatchesSidecar(record, sidecar, failure))
        {
            AddIssue(result, ModelAssetGenerationIssueCode::InvalidGenerationRecord,
                "generation.asset", failure.empty()
                    ? "generation record를 읽지 못했다." : failure);
            return result;
        }
        if (record.identityProfile != header.identityProfile
            || record.identityEpoch != header.identityEpoch)
        {
            AddIssue(result, ModelAssetGenerationIssueCode::InvalidEpoch,
                "generation.asset", "generation profile/epoch가 project header와 다르다.");
            return result;
        }
        if (record.sidecarFingerprint != Fingerprint(generationSidecarText))
        {
            AddIssue(result, ModelAssetGenerationIssueCode::FingerprintMismatch,
                "generation.sidecar", "sidecar SHA-256이 generation record와 다르다.");
            return result;
        }

        markPhase("identity+sidecar");
        phaseProfile.reset();
        phaseProfile.emplace(ce::marker<"ModelGen.CemcRead">());
        std::vector<std::byte> cookedBytes;
        if (!ReadBytes(generationPath / record.modelArtifactPath, cookedBytes)
            || cookedBytes.empty())
        {
            AddIssue(result, ModelAssetGenerationIssueCode::MissingFile,
                "modelArtifact", "CEMC model artifact를 읽지 못했다.");
            return result;
        }
        // ★ 읽기와 해시를 따로 표시한다(MBC11 §8.4). B2 비교 예산의 기준은 legacy
        //   `.asset` 읽기인데 legacy는 artifact를 해시하지 않았다 — 한 칸에 묶어
        //   두면 "축이 다르다"는 말을 수치로 보일 수 없다.
        markPhase("cemc-read");
        phaseProfile.reset();
        phaseProfile.emplace(ce::marker<"ModelGen.CemcSha">());
        if (record.modelArtifactFingerprint != Fingerprint(cookedBytes))
        {
            AddIssue(result, ModelAssetGenerationIssueCode::FingerprintMismatch,
                "modelArtifact", "CEMC SHA-256이 generation record와 다르다.");
            return result;
        }

        markPhase("cemc-sha");
        phaseProfile.reset();
        phaseProfile.emplace(ce::marker<"ModelGen.CemcDecode">());
        experiment::ModelDraft draft;
        std::vector<experiment::ModelLoadIssue> cookedIssues;
        if (!ck::Read(cookedBytes, draft, cookedIssues))
        {
            AddIssue(result, ModelAssetGenerationIssueCode::CookedModelRejected,
                "modelArtifact", cookedIssues.empty()
                    ? "CEMC decoder가 payload를 거부했다."
                    : cookedIssues.front().message);
            return result;
        }
        for (const experiment::ModelLoadIssue& issue : cookedIssues)
        {
            result.warnings.push_back({ ModelAssetGenerationIssueCode::CookedModelRejected,
                issue.context, issue.message });
        }
        const std::vector<experiment::ModelLoadIssue> validation =
            experiment::ModelLoader::Validate(draft);
        if (!validation.empty() || draft.metadata.assetId.value != sidecar.assetId)
        {
            AddIssue(result, validation.empty()
                    ? ModelAssetGenerationIssueCode::IdentityMismatch
                    : ModelAssetGenerationIssueCode::CookedModelRejected,
                "modelArtifact", validation.empty()
                    ? "CEMC ModelId가 sidecar와 다르다."
                    : validation.front().message);
            return result;
        }

        markPhase("cemc-decode+validate");
        phaseProfile.reset();
        phaseProfile.emplace(ce::marker<"ModelGen.Build">());
        const auto meshRecords = RecordsOf(sidecar, SubAssetKind::Mesh);
        const auto materialRecords = RecordsOf(sidecar, SubAssetKind::Material);
        const auto textureRecords = RecordsOf(sidecar, SubAssetKind::Texture);
        const auto skeletonRecords = RecordsOf(sidecar, SubAssetKind::Skeleton);
        const auto animationRecords = RecordsOf(sidecar, SubAssetKind::Animation);
        const std::size_t skeletonCount = draft.skeleton ? 1u : 0u;
        const std::size_t animationCount = draft.skeleton
            ? draft.skeleton->clips.size() : 0u;
        if (meshRecords.size() != draft.meshes.size()
            || materialRecords.size() != draft.materials.size()
            || skeletonRecords.size() != skeletonCount
            || animationRecords.size() != animationCount)
        {
            AddIssue(result, ModelAssetGenerationIssueCode::ClosureMismatch,
                "modelArtifact", "CEMC와 sidecar의 mesh/material/skeleton/animation 수가 다르다.");
            return result;
        }

        std::set<Uuid::Uuid16> embeddedTextureIds;
        for (const ModelSubAssetRecord* texture : textureRecords)
            embeddedTextureIds.insert(texture->assetId);

        std::map<Uuid::Uuid16, ModelTextureColorSpace> textureColorSpaces;
        std::vector<ModelMaterialAsset> materials;
        materials.reserve(draft.materials.size());
        for (std::size_t index = 0; index < draft.materials.size(); ++index)
        {
            experiment::Material& source = draft.materials[index];
            const Uuid::Uuid16 materialId = materialRecords[index]->assetId;
            if (source.assetId.value != materialId)
            {
                AddIssue(result, ModelAssetGenerationIssueCode::ClosureMismatch,
                    "materials[" + std::to_string(index) + "]",
                    "CEMC MaterialId가 sidecar와 다르다.");
                return result;
            }
            ModelMaterialAsset material;
            material.materialId = materialId;
            material.shaderAssetId = source.shaderAssetId.value;
            material.name = std::move(source.name);
            material.transparent = source.blendMode
                == experiment::MaterialBlendMode::Transparent;
            material.masked = source.blendMode == experiment::MaterialBlendMode::Masked;
            material.keywords = std::move(source.keywords);
            material.keywordSelections = std::move(source.keywordSelections);
            material.properties.reserve(source.properties.size());
            for (experiment::MaterialProperty& property : source.properties)
            {
                ModelMaterialProperty target;
                target.name = std::move(property.name);
                if (auto* value = std::get_if<bool>(&property.value)) target.value = *value;
                else if (auto* value = std::get_if<std::int32_t>(&property.value)) target.value = *value;
                else if (auto* value = std::get_if<std::uint32_t>(&property.value)) target.value = *value;
                else if (auto* value = std::get_if<float>(&property.value)) target.value = *value;
                else if (auto* value = std::get_if<math::vector2>(&property.value)) target.value = *value;
                else if (auto* value = std::get_if<math::vector3>(&property.value)) target.value = *value;
                else if (auto* value = std::get_if<math::vector4>(&property.value)) target.value = *value;
                else if (auto* value = std::get_if<std::string>(&property.value)) target.value = std::move(*value);
                else if (auto* value = std::get_if<experiment::TextureReference>(&property.value))
                {
                    const bool embedded = embeddedTextureIds.contains(value->assetId.value);
                    const ModelTextureColorSpace colorSpace = value->colorSpace
                        == experiment::TextureColorSpace::Srgb
                        ? ModelTextureColorSpace::Srgb
                        : ModelTextureColorSpace::Linear;
                    if (embedded)
                    {
                        const auto [found, inserted] = textureColorSpaces.emplace(
                            value->assetId.value, colorSpace);
                        if (!inserted && found->second != colorSpace)
                        {
                            AddIssue(result, ModelAssetGenerationIssueCode::ClosureMismatch,
                                "materials." + target.name,
                                "같은 embedded TextureId가 서로 다른 color space로 참조됐다.");
                            return result;
                        }
                    }
                    target.value = ModelMaterialTexture{{ value->assetId.value,
                        embedded ? sidecar.generation : 0u }, value->coordinates,
                        value->sampler };
                }
                material.properties.push_back(std::move(target));
            }
            materials.push_back(std::move(material));
        }

        std::vector<ModelMeshAsset> meshes;
        meshes.reserve(draft.meshes.size());
        for (std::size_t index = 0; index < draft.meshes.size(); ++index)
        {
            experiment::Mesh& source = draft.meshes[index];
            ModelMeshAsset mesh;
            mesh.meshId = meshRecords[index]->assetId;
            mesh.name = std::move(source.name);
            mesh.vertexAttributeMask = source.vertices.AttributeMask();
            mesh.vertexStride = source.vertices.Stride();
            mesh.vertexLayoutHash = VertexLayoutHash(
                source.vertices.AttributeMask());
            const std::span<const std::byte> vertexBytes = source.vertices.Bytes();
            mesh.vertexBytes.assign(vertexBytes.begin(), vertexBytes.end());
            mesh.indices = std::move(source.indices);
            mesh.meshlets = std::move(source.meshlets);
            mesh.coarseLods = std::move(source.coarseLods);
            mesh.bounds = source.bounds;
            if (source.material.IsValid())
                mesh.materialId = materialRecords[source.material.Value()]->assetId;
            meshes.push_back(std::move(mesh));
        }

        std::vector<ModelNodeAsset> nodes;
        nodes.reserve(draft.nodes.size());
        for (experiment::ModelNode& source : draft.nodes)
        {
            ModelNodeAsset node;
            node.name = std::move(source.name);
            node.parent = source.parent.IsValid()
                ? source.parent.Value() : kInvalidModelAssetIndex;
            node.localTransform = source.localTransform;
            node.meshes.reserve(source.meshes.size());
            for (experiment::MeshIndex mesh : source.meshes)
                node.meshes.push_back(meshRecords[mesh.Value()]->assetId);
            nodes.push_back(std::move(node));
        }

        std::optional<ModelSkeletonAsset> skeleton;
        std::vector<ModelAnimationAsset> animations;
        if (draft.skeleton)
        {
            ModelSkeletonAsset target;
            target.skeletonId = skeletonRecords.front()->assetId;
            target.rootBone = draft.skeleton->rootBone.Value();
            target.rootTransform = draft.skeleton->rootTransform;
            target.globalInverseTransform = draft.skeleton->globalInverseTransform;
            target.bones.reserve(draft.skeleton->bones.size());
            for (experiment::Bone& source : draft.skeleton->bones)
            {
                target.bones.push_back({ std::move(source.name),
                    source.parent.IsValid() ? source.parent.Value()
                        : kInvalidModelAssetIndex,
                    source.inverseBindMatrix });
            }
            animations.reserve(draft.skeleton->clips.size());
            for (std::size_t clipIndex = 0;
                clipIndex < draft.skeleton->clips.size(); ++clipIndex)
            {
                experiment::AnimationClip& source = draft.skeleton->clips[clipIndex];
                ModelAnimationAsset animation;
                animation.animationId = animationRecords[clipIndex]->assetId;
                animation.name = std::move(source.name);
                animation.durationTicks = source.durationTicks;
                animation.ticksPerSecond = source.ticksPerSecond;
                animation.looping = source.looping;
                animation.tracks.reserve(source.channels.size());
                for (experiment::AnimationChannel& channel : source.channels)
                {
                    ModelAnimationTrack track;
                    track.bone = channel.bone.Value();
                    track.translationInterpolation = ConvertInterpolation(
                        channel.translationInterpolation);
                    track.rotationInterpolation = ConvertInterpolation(
                        channel.rotationInterpolation);
                    track.scaleInterpolation = ConvertInterpolation(
                        channel.scaleInterpolation);
                    track.translations.reserve(channel.translations.size());
                    for (const experiment::TranslationKey& key : channel.translations)
                        track.translations.push_back({ key.time, key.value });
                    track.rotations.reserve(channel.rotations.size());
                    for (const experiment::RotationKey& key : channel.rotations)
                        track.rotations.push_back({ key.time, key.quaternion });
                    track.scales.reserve(channel.scales.size());
                    for (const experiment::ScaleKey& key : channel.scales)
                        track.scales.push_back({ key.time, key.value });
                    animation.tracks.push_back(std::move(track));
                }
                animations.push_back(std::move(animation));
            }
            skeleton = std::move(target);
        }

        std::optional<ModelAnimatorAsset> animator;
        if (draft.animator)
        {
            ModelAnimatorAsset target;
            target.motionAssetId = draft.animator->motionAssetId.value;
            if (draft.animator->defaultClip.IsValid())
            {
                target.defaultAnimationId =
                    animationRecords[draft.animator->defaultClip.Value()]->assetId;
            }
            animator = target;
        }

        std::vector<ModelTextureAsset> textures;
        markPhase("materials+meshes+skeleton");
        phaseProfile.reset();
        phaseProfile.emplace(ce::marker<"ModelGen.Textures">());
        textures.reserve(textureRecords.size());
        for (const ModelSubAssetRecord* textureRecord : textureRecords)
        {
            const auto generationRecord = std::ranges::find(
                record.subAssets, textureRecord->assetId,
                &GenerationSubAsset::assetId);
            if (generationRecord == record.subAssets.end())
            {
                AddIssue(result, ModelAssetGenerationIssueCode::ClosureMismatch,
                    "textures", "generation record에서 TextureId를 찾지 못했다.");
                return result;
            }
            std::vector<std::byte> encoded;
            std::optional<ce::profile_scope> textureStep;
            textureStep.emplace(ce::marker<"ModelGen.TextureRead">());
            if (!ReadBytes(generationPath / generationRecord->artifactPath, encoded)
                || encoded.empty())
            {
                AddIssue(result, ModelAssetGenerationIssueCode::MissingFile,
                    "textures." + textureRecord->stableKey,
                    "embedded texture artifact를 읽지 못했다.");
                return result;
            }
            textureStep.reset();
            textureStep.emplace(ce::marker<"ModelGen.TextureSha">());
            if (Fingerprint(encoded) != generationRecord->artifactFingerprint)
            {
                AddIssue(result, ModelAssetGenerationIssueCode::FingerprintMismatch,
                    "textures." + textureRecord->stableKey,
                    "embedded texture SHA-256이 generation record와 다르다.");
                return result;
            }
            ModelTextureAsset texture;
            texture.textureId = textureRecord->assetId;
            texture.name = textureRecord->name.empty()
                ? textureRecord->stableKey : textureRecord->name;
            const auto color = textureColorSpaces.find(texture.textureId);
            const ModelTextureColorSpace colorSpace = color != textureColorSpaces.end()
                ? color->second : ModelTextureColorSpace::Linear;
            std::filesystem::path decodedCache;
            if (!request.decodedTextureCacheRoot.empty())
            {
                const auto digest = generationRecord->artifactFingerprint.substr(
                    generationRecord->artifactFingerprint.find(':') + 1);
                decodedCache = request.decodedTextureCacheRoot /
                    (Uuid::ToString(texture.textureId) + "-" + digest +
                     (colorSpace == ModelTextureColorSpace::Srgb ? "-srgb.ceim" : "-linear.ceim"));
            }
            textureStep.reset();
            textureStep.emplace(ce::marker<"ModelGen.TextureCacheRead">());
            const bool cached = !decodedCache.empty() && ReadDecodedTextureCache(decodedCache, colorSpace, texture);
            textureStep.reset();
            if (!cached) textureStep.emplace(ce::marker<"ModelGen.TextureDecode">());
            if (!cached && !CopyTexturePixels(encoded, colorSpace, texture, failure))
            {
                AddIssue(result, ModelAssetGenerationIssueCode::TextureDecodeFailed,
                    "textures." + textureRecord->stableKey, failure);
                return result;
            }
            textureStep.reset();
            if (!cached && !decodedCache.empty())
            {
                ce::profile_scope publish{ ce::marker<"ModelGen.TextureCachePublish">() };
                PublishDecodedTextureCache(decodedCache, texture);
            }
            textures.push_back(std::move(texture));
        }

        std::vector<ModelGpuUploadDescriptor> descriptors;
        markPhase("textures-read+sha+decode");
        phaseProfile.reset();
        phaseProfile.emplace(ce::marker<"ModelGen.Descriptors">());
        descriptors.reserve(meshes.size() * 2u + textures.size());
        for (std::size_t index = 0; index < meshes.size(); ++index)
        {
            const ModelMeshAsset& mesh = meshes[index];
            descriptors.push_back({ ModelGpuUploadKind::VertexBuffer, mesh.meshId,
                static_cast<std::uint32_t>(index), mesh.vertexBytes.size(),
                mesh.vertexStride,
                static_cast<std::uint32_t>(mesh.vertexBytes.size() / mesh.vertexStride) });
            descriptors.push_back({ ModelGpuUploadKind::IndexBuffer, mesh.meshId,
                static_cast<std::uint32_t>(index),
                mesh.indices.size() * sizeof(std::uint32_t),
                sizeof(std::uint32_t), static_cast<std::uint32_t>(mesh.indices.size()) });
        }
        for (std::size_t index = 0; index < textures.size(); ++index)
        {
            const ModelTextureAsset& texture = textures[index];
            ModelGpuUploadDescriptor descriptor;
            descriptor.kind = ModelGpuUploadKind::Texture2D;
            descriptor.assetId = texture.textureId;
            descriptor.sourceIndex = static_cast<std::uint32_t>(index);
            descriptor.byteSize = texture.pixels.size();
            descriptor.format = texture.format;
            descriptor.width = texture.width;
            descriptor.height = texture.height;
            descriptor.mipLevels = texture.mipLevels;
            descriptor.arraySize = texture.arraySize;
            descriptor.isCube = texture.isCube;
            descriptors.push_back(descriptor);
        }

        ModelAssetGenerationIdentity identity;
        identity.modelId = sidecar.assetId;
        identity.generation = sidecar.generation;
        identity.identityProfile = sidecar.identityProfile;
        identity.identityEpoch = sidecar.identityEpoch;
        identity.sourceFingerprint = sidecar.sourceFingerprint;
        if (!IsValidAggregate(identity, meshes, materials, textures, descriptors))
        {
            AddIssue(result, ModelAssetGenerationIssueCode::InvalidGpuDescriptor,
                "aggregate", "pending generation 또는 GPU upload descriptor가 불완전하다.");
            return result;
        }

        markPhase("assemble");
        phaseProfile.reset();
        result.generation = own::make_shared<const ModelAssetGeneration>(
                ModelAssetGeneration::LoadKey{}, std::move(identity),
                std::move(draft.metadata.name),
                std::move(draft.metadata.sourcePath), std::move(nodes),
                std::move(meshes), std::move(materials), std::move(textures),
                std::move(skeleton), std::move(animations), std::move(animator),
                std::move(descriptors));
        return result;
    }

    ModelAssetGeneration::Shared ModelAssetGenerationCache::AcquireLocked(
        const Key& key, Entry& entry) const
    {
        auto generation = entry.live.lock();
        if (!generation || generation->Handle() != key
            || generation->Identity().identityProfile != entry.identity.identityProfile
            || generation->Identity().identityEpoch != entry.identity.identityEpoch
            || generation->Identity().sourceFingerprint != entry.identity.sourceFingerprint)
        {
            return {};
        }
        if (accessSerial_ != (std::numeric_limits<std::uint64_t>::max)())
        {
            ++accessSerial_;
        }
        entry.lastAccess = accessSerial_;
        return generation;
    }

    void ModelAssetGenerationCache::TrimRetainedLocked(std::size_t budget,
        std::vector<ModelAssetGeneration::Shared>& released) const
    {
        while (retainedBytes_ > budget)
        {
            auto oldest = generations_.end();
            for (auto entry = generations_.begin(); entry != generations_.end(); ++entry)
            {
                if (entry->second.retained && (oldest == generations_.end()
                    || entry->second.lastAccess < oldest->second.lastAccess))
                {
                    oldest = entry;
                }
            }
            if (oldest == generations_.end())
            {
                break;
            }
            released.push_back(std::move(oldest->second.retained));
            retainedBytes_ -= oldest->second.estimatedBytes;
            ++stats_.retentionEvictions;
        }
    }

    void ModelAssetGenerationCache::RetainLocked(Entry& entry,
        const ModelAssetGeneration::Shared& generation,
        std::vector<ModelAssetGeneration::Shared>& released) const
    {
        if (entry.retained || entry.estimatedBytes > retentionBudgetBytes_)
        {
            return;
        }
        // Allocate before transferring any cache pins or changing accounting.
        released.reserve(released.size() + generations_.size());
        // Make room before addition so even a caller's SIZE_MAX budget cannot
        // overflow accounting. A too-large aggregate remains consumer-owned.
        TrimRetainedLocked(retentionBudgetBytes_ - entry.estimatedBytes, released);
        entry.retained = generation;
        retainedBytes_ += entry.estimatedBytes;
    }

    ModelAssetPublishResult ModelAssetGenerationCache::Publish(
        ModelAssetGeneration::Shared generation)
    {
        ModelAssetPublishResult result;
        if (!generation || !generation->Handle().IsValid())
        {
            return result;
        }

        // All candidate allocations precede publication. A failed candidate
        // must leave the prior current index, live owner and cache pin intact.
        Entry candidate;
        candidate.identity = generation->Identity();
        candidate.live = generation;
        candidate.estimatedBytes = generation->EstimatedCpuBytes();
        std::vector<ModelAssetGeneration::Shared> released;
        std::lock_guard lock(mutex_);
        released.reserve(generations_.size() + 2);
        const Key incoming = generation->Handle();
        auto currentPosition = currentByAsset_.find(incoming.modelId);
        auto currentGeneration = generations_.end();
        if (currentPosition != currentByAsset_.end())
        {
            const Key currentKey = currentPosition->second;
            currentGeneration = generations_.find(currentKey);
            if (currentGeneration == generations_.end())
            {
                result.outcome = ModelAssetPublishOutcome::RejectedInvalid;
                return result;
            }
            Entry& entry = currentGeneration->second;
            result.current = AcquireLocked(currentKey, entry);
            if (incoming.generation < currentKey.generation)
            {
                result.outcome = ModelAssetPublishOutcome::RejectedStale;
                return result;
            }
            if (incoming.generation == currentKey.generation)
            {
                const auto& identity = generation->Identity();
                if (identity.sourceFingerprint != entry.identity.sourceFingerprint
                    || identity.identityProfile != entry.identity.identityProfile
                    || identity.identityEpoch != entry.identity.identityEpoch)
                {
                    result.outcome = ModelAssetPublishOutcome::RejectedGenerationCollision;
                    return result;
                }
                // An expired weak current may be reloaded only at the exact
                // published identity; never expose an old generation again.
                if (!result.current)
                {
                    entry.live = generation;
                    entry.estimatedBytes = candidate.estimatedBytes;
                    result.current = std::move(generation);
                    (void)AcquireLocked(incoming, entry);
                }
                RetainLocked(entry, result.current, released);
                result.outcome = ModelAssetPublishOutcome::AlreadyCurrent;
                return result;
            }
        }

        const auto [position, inserted] = generations_.emplace(incoming, std::move(candidate));
        if (!inserted)
        {
            result.outcome = ModelAssetPublishOutcome::RejectedInvalid;
            return result;
        }
        if (currentPosition == currentByAsset_.end())
        {
            try
            {
                currentByAsset_.emplace(incoming.modelId, incoming);
            }
            catch (...)
            {
                generations_.erase(position);
                throw;
            }
            result.outcome = ModelAssetPublishOutcome::Published;
        }
        else
        {
            result.retired = std::move(result.current);
            result.retiredHandle = currentPosition->second;
            Entry& previous = currentGeneration->second;
            if (previous.retained)
            {
                released.push_back(std::move(previous.retained));
                retainedBytes_ -= previous.estimatedBytes;
            }
            currentPosition->second = incoming;
            generations_.erase(currentGeneration);
            ++stats_.replacements;
            ++stats_.retires;
            result.outcome = ModelAssetPublishOutcome::Replaced;
        }

        // Release capacity was reserved before either index was changed.
        (void)AcquireLocked(incoming, position->second);
        RetainLocked(position->second, generation, released);
        ++stats_.publishes;
        result.current = std::move(generation);
        return result;
    }

    ModelAssetGeneration::Shared ModelAssetGenerationCache::ResolveCurrent(
        const Uuid::Uuid16& modelId) const
    {
        std::vector<ModelAssetGeneration::Shared> released;
        std::lock_guard lock(mutex_);
        const auto current = currentByAsset_.find(modelId);
        if (current != currentByAsset_.end())
        {
            const auto entry = generations_.find(current->second);
            if (entry != generations_.end())
            {
                auto generation = AcquireLocked(current->second, entry->second);
                if (generation)
                {
                    RetainLocked(entry->second, generation, released);
                    ++stats_.hits;
                    return generation;
                }
            }
        }
        ++stats_.misses;
        return {};
    }

    ModelAssetGeneration::Shared ModelAssetGenerationCache::Resolve(
        ModelAssetGenerationHandle handle) const
    {
        std::vector<ModelAssetGeneration::Shared> released;
        std::lock_guard lock(mutex_);
        const auto current = currentByAsset_.find(handle.modelId);
        if (current != currentByAsset_.end() && current->second == handle)
        {
            const auto entry = generations_.find(handle);
            if (entry != generations_.end())
            {
                auto generation = AcquireLocked(handle, entry->second);
                if (generation)
                {
                    RetainLocked(entry->second, generation, released);
                    ++stats_.hits;
                    return generation;
                }
            }
        }
        ++stats_.misses;
        return {};
    }

    const ModelMeshAsset* ModelAssetGenerationCache::ResolveMesh(
        ModelMeshHandle handle, ModelAssetGeneration::Shared& outOwner) const
    {
        outOwner = Resolve({ handle.modelId, handle.generation });
        return outOwner ? outOwner->FindMesh(handle.meshId) : nullptr;
    }

    ModelAssetGeneration::Shared ModelAssetGenerationCache::Retire(
        const Uuid::Uuid16& modelId, ModelAssetGenerationHandle* outRetiredHandle)
    {
        if (outRetiredHandle)
        {
            *outRetiredHandle = {};
        }
        std::vector<ModelAssetGeneration::Shared> released;
        std::lock_guard lock(mutex_);
        const auto current = currentByAsset_.find(modelId);
        if (current == currentByAsset_.end())
        {
            return {};
        }
        if (outRetiredHandle)
        {
            *outRetiredHandle = current->second;
        }
        const auto entry = generations_.find(current->second);
        ModelAssetGeneration::Shared retired;
        if (entry != generations_.end())
        {
            retired = AcquireLocked(current->second, entry->second);
            if (entry->second.retained)
            {
                released.push_back(std::move(entry->second.retained));
                retainedBytes_ -= entry->second.estimatedBytes;
            }
            generations_.erase(entry);
        }
        currentByAsset_.erase(current);
        ++stats_.retires;
        return retired;
    }

    void ModelAssetGenerationCache::SetRetentionBudgetBytes(std::size_t bytes)
    {
        std::vector<ModelAssetGeneration::Shared> released;
        std::lock_guard lock(mutex_);
        released.reserve(generations_.size());
        retentionBudgetBytes_ = bytes;
        TrimRetainedLocked(retentionBudgetBytes_, released);
    }

    void ModelAssetGenerationCache::DetachAll(RetiredEntries& retired) noexcept
    {
        assert(retired.generations_.empty());
        assert(retired.currentByAsset_.empty());
        std::lock_guard lock(mutex_);
        retired.generations_.swap(generations_);
        retired.currentByAsset_.swap(currentByAsset_);
        retainedBytes_ = 0;
        accessSerial_ = 0;
        stats_ = {};
    }

    void ModelAssetGenerationCache::Clear()
    {
        RetiredEntries retired;
        DetachAll(retired);
    }

    ModelAssetGenerationCacheSnapshot ModelAssetGenerationCache::Snapshot() const
    {
        std::lock_guard lock(mutex_);
        ModelAssetGenerationCacheSnapshot snapshot = stats_;
        snapshot.currentAssets = currentByAsset_.size();
        snapshot.retentionBudgetBytes = retentionBudgetBytes_;
        snapshot.retainedBytes = retainedBytes_;
        for (const auto& [key, entry] : generations_)
        {
            const auto generation = entry.live.lock();
            if (generation && generation->Handle() == key)
            {
                ++snapshot.addressableGenerations;
            }
            if (entry.retained)
            {
                ++snapshot.retainedGenerations;
            }
        }
        return snapshot;
    }

    std::vector<ModelAssetGeneration::Shared> ModelAssetGenerationCache::SnapshotCurrent() const
    {
        std::lock_guard lock(mutex_);
        std::vector<ModelAssetGeneration::Shared> current;
        current.reserve(currentByAsset_.size());
        for (const auto& [modelId, key] : currentByAsset_)
        {
            const auto entry = generations_.find(key);
            if (entry != generations_.end())
            {
                auto generation = AcquireLocked(key, entry->second);
                if (generation)
                {
                    current.push_back(std::move(generation));
                }
            }
        }
        return current;
    }
}
