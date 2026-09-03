#include "ModelAssetGeneration.h"

#include <chrono>

#include "ModelSidecarV2.h"
#include "AuthoringParsedDocument.h"
#include "../Experiment/Cooked/CookedModelCodec.h"
#include "../Experiment/ModelLoader.h"
#include "../Texture.h"

#include <DirectXTex.h>

#include <algorithm>
#include <fstream>
#include <limits>
#include <ranges>
#include <set>
#include <string_view>
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
            const Authoring::ParsedDocument document =
                Authoring::ParsedDocument::ParseText(std::string(text), parseError);
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
            const std::shared_ptr<Texture> decoded = Texture::LoadSharedFromMemory(
                encoded, false);
            const DirectX::ScratchImage* source = decoded
                ? decoded->GetCpuPixels() : nullptr;
            if (nullptr == source || source->GetImageCount() == 0u)
            {
                failure = "image decoder가 픽셀을 만들지 못했다.";
                return false;
            }

            const DirectX::TexMetadata sourceMetadata = source->GetMetadata();

            // ── 색공간은 라벨로만 정한다 ──────────────────────────────────
            //
            // ★ 여기 있던 코드는 target 을 semantic(_UNORM_SRGB)으로 잡고
            //   Convert 를 불렀다. DirectXTex 는 **출력 포맷이 IsSRGB 면
            //   SRGB_OUT 이 기본 on** 이라고 스스로 문서화한다
            //   (DirectXTex.h "if the output format type is IsSRGB(), then
            //   SRGB_OUT is on by default"). 입력은 _UNORM 이라 SRGB_IN 이
            //   꺼진 채로, 이미 sRGB 로 인코딩된 PNG 바이트에 linear→sRGB
            //   인코드가 한 번 더 먹었다.
            //
            //   런타임 SRV 는 out.format(= semantic) 을 쓰므로 하드웨어가
            //   디코드를 한 번 한다. 두 연산이 정확히 상쇄돼 **셰이더가 받는
            //   알베도가 sRGB 바이트값 그대로**였다 — Gunner 본체 텍스처
            //   기준 밝기 2.1~3.7 배, 채도비 2.96 → 1.70 (43% 탈색).
            //
            //   고침은 "바이트를 건드리지 않는다"다. 레이아웃만 RGBA8 로
            //   맞추되 목표 포맷의 sRGB 성질을 **소스와 같게** 두면
            //   DirectXTex 가 전달 함수에 손대지 않는다. 최종 라벨은
            //   아래 out.format 이 semantic 으로 따로 정한다.
            const DXGI_FORMAT layoutTarget = DirectX::IsSRGB(sourceMetadata.format)
                ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
                : DXGI_FORMAT_R8G8B8A8_UNORM;

            DirectX::ScratchImage converted;
            const DirectX::ScratchImage* finalImage = source;
            HRESULT conversion = S_OK;
            if (DirectX::IsCompressed(sourceMetadata.format))
            {
                conversion = DirectX::Decompress(source->GetImages(),
                    source->GetImageCount(), sourceMetadata, layoutTarget, converted);
                finalImage = &converted;
            }
            else if (sourceMetadata.format != layoutTarget)
            {
                conversion = DirectX::Convert(source->GetImages(),
                    source->GetImageCount(), sourceMetadata, layoutTarget,
                    DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT,
                    converted);
                finalImage = &converted;
            }
            if (FAILED(conversion) || finalImage->GetImageCount() == 0u)
            {
                failure = "image를 backend-neutral RGBA8로 변환하지 못했다.";
                return false;
            }

            const DirectX::TexMetadata metadata = finalImage->GetMetadata();
            if (metadata.dimension != DirectX::TEX_DIMENSION_TEXTURE2D
                || metadata.width == 0u || metadata.height == 0u
                || metadata.mipLevels == 0u || metadata.arraySize == 0u
                || metadata.width > (std::numeric_limits<std::uint32_t>::max)()
                || metadata.height > (std::numeric_limits<std::uint32_t>::max)()
                || metadata.mipLevels > (std::numeric_limits<std::uint32_t>::max)()
                || metadata.arraySize > (std::numeric_limits<std::uint32_t>::max)())
            {
                failure = "2D texture descriptor 범위를 벗어났다.";
                return false;
            }

            out.colorSpace = colorSpace;
            out.format = colorSpace == ModelTextureColorSpace::Srgb
                ? RHIFormat::RGBA8UnormSrgb : RHIFormat::RGBA8Unorm;
            out.width = static_cast<std::uint32_t>(metadata.width);
            out.height = static_cast<std::uint32_t>(metadata.height);
            out.mipLevels = static_cast<std::uint32_t>(metadata.mipLevels);
            out.arraySize = static_cast<std::uint32_t>(metadata.arraySize);
            out.isCube = metadata.IsCubemap();

            for (std::size_t item = 0; item < metadata.arraySize; ++item)
            {
                for (std::size_t mip = 0; mip < metadata.mipLevels; ++mip)
                {
                    const DirectX::Image* image = finalImage->GetImage(mip, item, 0);
                    if (nullptr == image || nullptr == image->pixels
                        || image->slicePitch == 0u)
                    {
                        failure = "decoded texture subresource가 비었다.";
                        return false;
                    }
                    ModelTextureSubresource subresource;
                    subresource.width = static_cast<std::uint32_t>(image->width);
                    subresource.height = static_cast<std::uint32_t>(image->height);
                    subresource.offset = out.pixels.size();
                    subresource.rowPitch = image->rowPitch;
                    subresource.slicePitch = image->slicePitch;
                    const std::byte* begin = reinterpret_cast<const std::byte*>(
                        image->pixels);
                    out.pixels.insert(out.pixels.end(), begin,
                        begin + image->slicePitch);
                    out.subresources.push_back(subresource);
                }
            }
            return !out.pixels.empty();
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

    ModelAssetGeneration::ModelAssetGeneration(ModelAssetGenerationIdentity identity,
        std::string name, std::filesystem::path sourcePath,
        std::vector<ModelNodeAsset> nodes,
        std::vector<ModelMeshAsset> meshes,
        std::vector<ModelMaterialAsset> materials,
        std::vector<ModelTextureAsset> textures,
        std::optional<ModelSkeletonAsset> skeleton,
        std::vector<ModelAnimationAsset> animations,
        std::optional<ModelAnimatorAsset> animator,
        std::vector<ModelGpuUploadDescriptor> gpuDescriptors) noexcept
        : identity_(std::move(identity)), name_(std::move(name)),
        sourcePath_(std::move(sourcePath)), nodes_(std::move(nodes)),
        meshes_(std::move(meshes)), materials_(std::move(materials)),
        textures_(std::move(textures)), skeleton_(std::move(skeleton)),
        animations_(std::move(animations)), animator_(std::move(animator)),
        gpuDescriptors_(std::move(gpuDescriptors))
    {
    }

    const ModelAssetGenerationIdentity& ModelAssetGeneration::Identity() const noexcept
    {
        return identity_;
    }

    ModelAssetGenerationHandle ModelAssetGeneration::Handle() const noexcept
    {
        return { identity_.modelId, identity_.generation };
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
        std::vector<std::byte> cookedBytes;
        if (!ReadBytes(generationPath / record.modelArtifactPath, cookedBytes)
            || cookedBytes.empty())
        {
            AddIssue(result, ModelAssetGenerationIssueCode::MissingFile,
                "modelArtifact", "CEMC model artifact를 읽지 못했다.");
            return result;
        }
        if (record.modelArtifactFingerprint != Fingerprint(cookedBytes))
        {
            AddIssue(result, ModelAssetGenerationIssueCode::FingerprintMismatch,
                "modelArtifact", "CEMC SHA-256이 generation record와 다르다.");
            return result;
        }

        markPhase("cemc-read+sha");
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
                    target.value = ModelTextureHandle{ value->assetId.value,
                        embedded ? sidecar.generation : 0u };
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
            if (!ReadBytes(generationPath / generationRecord->artifactPath, encoded)
                || encoded.empty())
            {
                AddIssue(result, ModelAssetGenerationIssueCode::MissingFile,
                    "textures." + textureRecord->stableKey,
                    "embedded texture artifact를 읽지 못했다.");
                return result;
            }
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
            if (!CopyTexturePixels(encoded, colorSpace, texture, failure))
            {
                AddIssue(result, ModelAssetGenerationIssueCode::TextureDecodeFailed,
                    "textures." + textureRecord->stableKey, failure);
                return result;
            }
            textures.push_back(std::move(texture));
        }

        std::vector<ModelGpuUploadDescriptor> descriptors;
        markPhase("textures-read+sha+decode");
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
        result.generation = ModelAssetGeneration::Shared(
            new ModelAssetGeneration(std::move(identity),
                std::move(draft.metadata.name),
                std::move(draft.metadata.sourcePath), std::move(nodes),
                std::move(meshes), std::move(materials), std::move(textures),
                std::move(skeleton), std::move(animations), std::move(animator),
                std::move(descriptors)));
        return result;
    }

    ModelAssetPublishResult ModelAssetGenerationCache::Publish(
        ModelAssetGeneration::Shared generation)
    {
        ModelAssetPublishResult result;
        if (!generation || !generation->Handle().IsValid()) return result;

        std::lock_guard lock(mutex_);
        const Key incoming = generation->Handle();
        const auto currentPosition = currentByAsset_.find(incoming.modelId);
        if (currentPosition == currentByAsset_.end())
        {
            generations_.emplace(incoming, generation);
            currentByAsset_.emplace(incoming.modelId, incoming);
            ++stats_.publishes;
            stats_.currentAssets = currentByAsset_.size();
            stats_.addressableGenerations = generations_.size();
            result.outcome = ModelAssetPublishOutcome::Published;
            result.current = std::move(generation);
            return result;
        }

        const Key currentKey = currentPosition->second;
        const auto currentGeneration = generations_.find(currentKey);
        if (currentGeneration == generations_.end())
        {
            result.outcome = ModelAssetPublishOutcome::RejectedInvalid;
            return result;
        }
        if (incoming.generation < currentKey.generation)
        {
            result.outcome = ModelAssetPublishOutcome::RejectedStale;
            result.current = currentGeneration->second;
            return result;
        }
        if (incoming.generation == currentKey.generation)
        {
            if (generation->Identity().sourceFingerprint
                != currentGeneration->second->Identity().sourceFingerprint)
            {
                result.outcome = ModelAssetPublishOutcome::RejectedGenerationCollision;
                result.current = currentGeneration->second;
                return result;
            }
            result.outcome = ModelAssetPublishOutcome::AlreadyCurrent;
            result.current = currentGeneration->second;
            return result;
        }

        result.retired = currentGeneration->second;
        generations_.erase(currentGeneration);
        generations_.emplace(incoming, generation);
        currentPosition->second = incoming;
        ++stats_.publishes;
        ++stats_.replacements;
        ++stats_.retires;
        stats_.addressableGenerations = generations_.size();
        result.outcome = ModelAssetPublishOutcome::Replaced;
        result.current = std::move(generation);
        return result;
    }

    ModelAssetGeneration::Shared ModelAssetGenerationCache::ResolveCurrent(
        const Uuid::Uuid16& modelId) const
    {
        std::lock_guard lock(mutex_);
        const auto current = currentByAsset_.find(modelId);
        if (current == currentByAsset_.end())
        {
            ++stats_.misses;
            return {};
        }
        const auto generation = generations_.find(current->second);
        if (generation == generations_.end())
        {
            ++stats_.misses;
            return {};
        }
        ++stats_.hits;
        return generation->second;
    }

    ModelAssetGeneration::Shared ModelAssetGenerationCache::Resolve(
        ModelAssetGenerationHandle handle) const
    {
        std::lock_guard lock(mutex_);
        const auto found = generations_.find(handle);
        if (found == generations_.end())
        {
            ++stats_.misses;
            return {};
        }
        ++stats_.hits;
        return found->second;
    }

    const ModelMeshAsset* ModelAssetGenerationCache::ResolveMesh(
        ModelMeshHandle handle, ModelAssetGeneration::Shared& outOwner) const
    {
        outOwner = Resolve({ handle.modelId, handle.generation });
        return outOwner ? outOwner->FindMesh(handle.meshId) : nullptr;
    }

    ModelAssetGeneration::Shared ModelAssetGenerationCache::Retire(
        const Uuid::Uuid16& modelId)
    {
        std::lock_guard lock(mutex_);
        const auto current = currentByAsset_.find(modelId);
        if (current == currentByAsset_.end()) return {};
        const auto generation = generations_.find(current->second);
        ModelAssetGeneration::Shared retired = generation != generations_.end()
            ? generation->second : ModelAssetGeneration::Shared{};
        if (generation != generations_.end()) generations_.erase(generation);
        currentByAsset_.erase(current);
        ++stats_.retires;
        stats_.currentAssets = currentByAsset_.size();
        stats_.addressableGenerations = generations_.size();
        return retired;
    }

    void ModelAssetGenerationCache::Clear()
    {
        std::lock_guard lock(mutex_);
        generations_.clear();
        currentByAsset_.clear();
        stats_ = {};
    }

    ModelAssetGenerationCacheSnapshot ModelAssetGenerationCache::Snapshot() const
    {
        std::lock_guard lock(mutex_);
        ModelAssetGenerationCacheSnapshot snapshot = stats_;
        snapshot.currentAssets = currentByAsset_.size();
        snapshot.addressableGenerations = generations_.size();
        return snapshot;
    }

    std::vector<ModelAssetGeneration::Shared> ModelAssetGenerationCache::SnapshotCurrent() const
    {
        std::lock_guard lock(mutex_);
        std::vector<ModelAssetGeneration::Shared> current;
        current.reserve(currentByAsset_.size());
        for (const auto& [modelId, key] : currentByAsset_)
        {
            const auto found = generations_.find(key);
            if (found != generations_.end() && found->second) current.push_back(found->second);
        }
        return current;
    }
}
