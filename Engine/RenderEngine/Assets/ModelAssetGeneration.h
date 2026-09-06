#pragma once
// PHASE 3.75 MBC5 — model runtime의 immutable publication unit.
//
// 디스크의 schema-v2 sidecar, generation record, CEMC, embedded texture를 모두
// 검증한 뒤에만 이 객체가 만들어진다. 소비자는 legacy Model/Mesh/Material이나
// experiment::Model을 보지 않고 이 snapshot과 generation handle만 보유한다.

#include "AssetIdentityProfile.h"
#include "ModelAssetPhaseTiming.h"
#include "ModelVertexLayout.h"
#include "TextureCoordinates.h"
#include "../RHI/RHIFormat.h"

#include <mathematics/bounds.hpp>
#include <mathematics/matrix4x4.hpp>
#include <mathematics/quaternion.hpp>
#include <mathematics/vector2.hpp>
#include <mathematics/vector3.hpp>
#include <mathematics/vector4.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace assets
{
    inline constexpr std::uint32_t kInvalidModelAssetIndex =
        (std::numeric_limits<std::uint32_t>::max)();

    struct ModelAssetGenerationHandle final
    {
        Uuid::Uuid16 modelId{};
        std::uint64_t generation{};

        [[nodiscard]] bool IsValid() const noexcept
        {
            return IsUuidV8(modelId) && generation != 0u;
        }
        friend auto operator<=>(const ModelAssetGenerationHandle&,
            const ModelAssetGenerationHandle&) noexcept = default;
    };

    struct ModelMeshHandle final
    {
        Uuid::Uuid16 modelId{};
        Uuid::Uuid16 meshId{};
        std::uint64_t generation{};

        [[nodiscard]] bool IsValid() const noexcept
        {
            return IsUuidV8(modelId) && IsUuidV8(meshId) && generation != 0u;
        }
        friend auto operator<=>(const ModelMeshHandle&,
            const ModelMeshHandle&) noexcept = default;
    };

    struct ModelTextureHandle final
    {
        Uuid::Uuid16 textureId{};
        // 0은 이 모델 generation 밖의 독립 texture asset이다. 같은 model
        // closure 안의 embedded texture만 model generation 번호를 공유한다.
        std::uint64_t generation{};

        [[nodiscard]] bool IsValid() const noexcept
        {
            // Embedded texture는 model identity epoch의 UUIDv8을 쓰지만,
            // 독립 texture asset은 아직 그 epoch 바깥의 UUID일 수 있다.
            // 이 handle의 유효성 계약은 UUID version이 아니라 non-nil이다.
            return !textureId.IsNil();
        }
        friend auto operator<=>(const ModelTextureHandle&,
            const ModelTextureHandle&) noexcept = default;
    };

    enum class ModelTextureColorSpace : std::uint8_t
    {
        Linear,
        Srgb,
    };

    struct ModelMaterialTexture final
    {
        ModelTextureHandle handle{};
        TextureCoordinates coordinates{};
    };

    using ModelMaterialPropertyValue = std::variant<
        bool,
        std::int32_t,
        std::uint32_t,
        float,
        math::vector2,
        math::vector3,
        math::vector4,
        std::string,
        ModelMaterialTexture>;

    struct ModelMaterialProperty final
    {
        std::string name{};
        ModelMaterialPropertyValue value{};
    };

    struct ModelMaterialAsset final
    {
        Uuid::Uuid16 materialId{};
        Uuid::Uuid16 shaderAssetId{};
        std::string name{};
        bool transparent{};
        bool masked{};
        std::vector<ModelMaterialProperty> properties{};
        std::vector<std::string> keywords{};
        std::vector<std::uint16_t> keywordSelections{};
    };

    struct ModelMeshAsset final
    {
        Uuid::Uuid16 meshId{};
        Uuid::Uuid16 materialId{};
        std::string name{};
        std::uint32_t vertexAttributeMask{};
        std::uint32_t vertexStride{};
        std::uint64_t vertexLayoutHash{};
        std::vector<std::byte> vertexBytes{};
        std::vector<std::uint32_t> indices{};
        math::aabb bounds{};
    };

    struct ModelTextureSubresource final
    {
        std::uint32_t width{};
        std::uint32_t height{};
        std::uint64_t offset{};
        std::uint64_t rowPitch{};
        std::uint64_t slicePitch{};
    };

    struct ModelTextureAsset final
    {
        Uuid::Uuid16 textureId{};
        std::string name{};
        ModelTextureColorSpace colorSpace{ ModelTextureColorSpace::Linear };
        RHIFormat format{ RHIFormat::Unknown };
        std::uint32_t width{};
        std::uint32_t height{};
        std::uint32_t mipLevels{};
        std::uint32_t arraySize{};
        bool isCube{};
        std::vector<ModelTextureSubresource> subresources{};
        std::vector<std::byte> pixels{};
    };

    struct ModelNodeAsset final
    {
        std::string name{};
        std::uint32_t parent{ kInvalidModelAssetIndex };
        math::matrix4x4 localTransform{ math::matrix4x4::identity() };
        std::vector<Uuid::Uuid16> meshes{};
    };

    struct ModelBoneAsset final
    {
        std::string name{};
        std::uint32_t parent{ kInvalidModelAssetIndex };
        math::matrix4x4 inverseBindMatrix{ math::matrix4x4::identity() };
    };

    enum class ModelInterpolationMode : std::uint8_t
    {
        Linear,
        Step,
    };

    struct ModelTranslationKey final
    {
        double time{};
        math::vector3 value{};
    };

    struct ModelRotationKey final
    {
        double time{};
        math::quaternion value{};
    };

    struct ModelScaleKey final
    {
        double time{};
        math::vector3 value{ 1.0f, 1.0f, 1.0f };
    };

    struct ModelAnimationTrack final
    {
        std::uint32_t bone{ kInvalidModelAssetIndex };
        ModelInterpolationMode translationInterpolation{ ModelInterpolationMode::Linear };
        ModelInterpolationMode rotationInterpolation{ ModelInterpolationMode::Linear };
        ModelInterpolationMode scaleInterpolation{ ModelInterpolationMode::Linear };
        std::vector<ModelTranslationKey> translations{};
        std::vector<ModelRotationKey> rotations{};
        std::vector<ModelScaleKey> scales{};
    };

    struct ModelAnimationEvent final
    {
        double time{};
        std::string name{};
    };

    struct ModelAnimationAsset final
    {
        Uuid::Uuid16 animationId{};
        std::string name{};
        double durationTicks{};
        double ticksPerSecond{};
        bool looping{ true };
        std::vector<ModelAnimationTrack> tracks{};
        std::vector<ModelAnimationEvent> events{};
    };

    struct ModelSkeletonAsset final
    {
        Uuid::Uuid16 skeletonId{};
        std::uint32_t rootBone{ kInvalidModelAssetIndex };
        math::matrix4x4 rootTransform{ math::matrix4x4::identity() };
        math::matrix4x4 globalInverseTransform{ math::matrix4x4::identity() };
        std::vector<ModelBoneAsset> bones{};
    };

    struct ModelAnimatorAsset final
    {
        Uuid::Uuid16 motionAssetId{};
        Uuid::Uuid16 defaultAnimationId{};
    };

    enum class ModelGpuUploadKind : std::uint8_t
    {
        VertexBuffer,
        IndexBuffer,
        Texture2D,
    };

    // 백엔드 객체를 만들지 않는 immutable request. MBC6의 DX12/Vulkan adapter는
    // assetId와 sourceIndex로 generation 내부 저장소를 찾아 실제 upload를 수행한다.
    struct ModelGpuUploadDescriptor final
    {
        ModelGpuUploadKind kind{ ModelGpuUploadKind::VertexBuffer };
        Uuid::Uuid16 assetId{};
        std::uint32_t sourceIndex{};
        std::uint64_t byteSize{};
        std::uint32_t stride{};
        std::uint32_t elementCount{};
        RHIFormat format{ RHIFormat::Unknown };
        std::uint32_t width{};
        std::uint32_t height{};
        std::uint32_t mipLevels{};
        std::uint32_t arraySize{};
        bool isCube{};
    };

    struct ModelAssetGenerationIdentity final
    {
        Uuid::Uuid16 modelId{};
        std::uint64_t generation{};
        std::string identityProfile{};
        std::string identityEpoch{};
        std::string sourceFingerprint{};
    };

    struct ModelAssetGenerationLoadRequest final
    {
        std::filesystem::path identityHeaderPath{};
        std::filesystem::path generationRoot{};
        std::filesystem::path generationPath{};
        std::filesystem::path canonicalSidecarPath{};
        Uuid::Uuid16 expectedModelId{};
        std::uint64_t expectedGeneration{};
    };

    enum class ModelAssetGenerationIssueCode : std::uint8_t
    {
        InvalidRequest,
        MissingFile,
        InvalidEpoch,
        InvalidSidecar,
        InvalidGenerationRecord,
        FingerprintMismatch,
        IdentityMismatch,
        ClosureMismatch,
        CookedModelRejected,
        TextureDecodeFailed,
        InvalidGpuDescriptor,
    };

    struct ModelAssetGenerationIssue final
    {
        ModelAssetGenerationIssueCode code{ ModelAssetGenerationIssueCode::InvalidRequest };
        std::string context{};
        std::string message{};
    };

    struct ModelAssetGenerationLoadResult;
    [[nodiscard]] ModelAssetGenerationLoadResult LoadModelAssetGeneration(
        const ModelAssetGenerationLoadRequest& request);

    class ModelAssetGeneration final
    {
    public:
        using Shared = std::shared_ptr<const ModelAssetGeneration>;

        ModelAssetGeneration(const ModelAssetGeneration&) = delete;
        ModelAssetGeneration& operator=(const ModelAssetGeneration&) = delete;
        ModelAssetGeneration(ModelAssetGeneration&&) = delete;
        ModelAssetGeneration& operator=(ModelAssetGeneration&&) = delete;
        ~ModelAssetGeneration() = default;

        [[nodiscard]] const ModelAssetGenerationIdentity& Identity() const noexcept;
        [[nodiscard]] ModelAssetGenerationHandle Handle() const noexcept;
        [[nodiscard]] const std::string& Name() const noexcept;
        [[nodiscard]] const std::filesystem::path& SourcePath() const noexcept;
        [[nodiscard]] std::span<const ModelNodeAsset> Nodes() const noexcept;
        [[nodiscard]] std::span<const ModelMeshAsset> Meshes() const noexcept;
        [[nodiscard]] std::span<const ModelMaterialAsset> Materials() const noexcept;
        [[nodiscard]] std::span<const ModelTextureAsset> Textures() const noexcept;
        [[nodiscard]] const ModelSkeletonAsset* Skeleton() const noexcept;
        [[nodiscard]] std::span<const ModelAnimationAsset> Animations() const noexcept;
        [[nodiscard]] const ModelAnimatorAsset* Animator() const noexcept;
        [[nodiscard]] std::span<const ModelGpuUploadDescriptor> GpuDescriptors() const noexcept;
        [[nodiscard]] const ModelMeshAsset* FindMesh(const Uuid::Uuid16& meshId) const noexcept;
        [[nodiscard]] const ModelMaterialAsset* FindMaterial(const Uuid::Uuid16& materialId) const noexcept;
        [[nodiscard]] const ModelTextureAsset* FindTexture(const Uuid::Uuid16& textureId) const noexcept;

    private:
        friend ModelAssetGenerationLoadResult LoadModelAssetGeneration(
            const ModelAssetGenerationLoadRequest& request);

        ModelAssetGeneration(ModelAssetGenerationIdentity identity,
            std::string name, std::filesystem::path sourcePath,
            std::vector<ModelNodeAsset> nodes,
            std::vector<ModelMeshAsset> meshes,
            std::vector<ModelMaterialAsset> materials,
            std::vector<ModelTextureAsset> textures,
            std::optional<ModelSkeletonAsset> skeleton,
            std::vector<ModelAnimationAsset> animations,
            std::optional<ModelAnimatorAsset> animator,
            std::vector<ModelGpuUploadDescriptor> gpuDescriptors) noexcept;

        ModelAssetGenerationIdentity identity_{};
        std::string name_{};
        std::filesystem::path sourcePath_{};
        std::vector<ModelNodeAsset> nodes_{};
        std::vector<ModelMeshAsset> meshes_{};
        std::vector<ModelMaterialAsset> materials_{};
        std::vector<ModelTextureAsset> textures_{};
        std::optional<ModelSkeletonAsset> skeleton_{};
        std::vector<ModelAnimationAsset> animations_{};
        std::optional<ModelAnimatorAsset> animator_{};
        std::vector<ModelGpuUploadDescriptor> gpuDescriptors_{};
    };

    struct ModelAssetGenerationLoadResult final
    {
        ModelAssetGeneration::Shared generation{};
        std::vector<ModelAssetGenerationIssue> issues{};
        // 단계별 경과(ms, 순서대로) — 진단 전용. 실패 시엔 도달한 단계까지만 남는다.
        ModelAssetPhaseTimeline phases{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return static_cast<bool>(generation) && issues.empty();
        }
    };

    enum class ModelAssetPublishOutcome : std::uint8_t
    {
        Published,
        Replaced,
        AlreadyCurrent,
        RejectedInvalid,
        RejectedStale,
        RejectedGenerationCollision,
    };

    struct ModelAssetPublishResult final
    {
        ModelAssetPublishOutcome outcome{ ModelAssetPublishOutcome::RejectedInvalid };
        ModelAssetGeneration::Shared current{};
        ModelAssetGeneration::Shared retired{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return outcome == ModelAssetPublishOutcome::Published
                || outcome == ModelAssetPublishOutcome::Replaced
                || outcome == ModelAssetPublishOutcome::AlreadyCurrent;
        }
    };

    struct ModelAssetGenerationCacheSnapshot final
    {
        std::size_t currentAssets{};
        std::size_t addressableGenerations{};
        std::uint64_t publishes{};
        std::uint64_t replacements{};
        std::uint64_t retires{};
        std::uint64_t hits{};
        std::uint64_t misses{};
    };

    class ModelAssetGenerationCache final
    {
    public:
        [[nodiscard]] ModelAssetPublishResult Publish(
            ModelAssetGeneration::Shared generation);
        [[nodiscard]] ModelAssetGeneration::Shared ResolveCurrent(
            const Uuid::Uuid16& modelId) const;
        [[nodiscard]] ModelAssetGeneration::Shared Resolve(
            ModelAssetGenerationHandle handle) const;
        [[nodiscard]] const ModelMeshAsset* ResolveMesh(ModelMeshHandle handle,
            ModelAssetGeneration::Shared& outOwner) const;
        [[nodiscard]] ModelAssetGeneration::Shared Retire(
            const Uuid::Uuid16& modelId);
        void Clear();
        [[nodiscard]] ModelAssetGenerationCacheSnapshot Snapshot() const;
        // MBC9 — current generation 전수(에디터 목록용). 정렬은 ModelId 순.
        [[nodiscard]] std::vector<ModelAssetGeneration::Shared> SnapshotCurrent() const;

    private:
        using Key = ModelAssetGenerationHandle;

        mutable std::mutex mutex_{};
        std::map<Key, ModelAssetGeneration::Shared> generations_{};
        std::map<Uuid::Uuid16, Key> currentByAsset_{};
        mutable ModelAssetGenerationCacheSnapshot stats_{};
    };
}
