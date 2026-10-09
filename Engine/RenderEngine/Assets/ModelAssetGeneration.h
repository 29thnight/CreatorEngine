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
#include "TextureSampler.h"
#include "../../Utility_Framework/Ownership.h"
#include "../RHI/RHIFormat.h"
#include "../Experiment/MeshletData.h"
#include "../Experiment/MeshLodData.h"
#include "../Experiment/Cooked/CookedAssetManifest.h"

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

    struct ModelMeshDescriptor;
    struct ModelGeometryPayload;

    enum class ModelMeshDomain : std::uint8_t
    {
        LegacyAggregate,
        Granular,
    };

    struct ModelMeshHandle final
    {
        // Keep the legacy aggregate initializer/source adapter stable.
        Uuid::Uuid16 modelId{};
        Uuid::Uuid16 meshId{};
        std::uint64_t generation{};
        ModelMeshDomain domain{ ModelMeshDomain::LegacyAggregate };
        experiment::cooked::TypedAssetReference asset{};
        experiment::cooked::AssetBlobRecord blob{};
        std::uint64_t resolverRevision{};
        std::uint64_t mountId{};
        experiment::AssetId assetSetId{};
        std::uint64_t manifestRevision{};

        [[nodiscard]] bool IsValid() const noexcept
        {
            if (domain == ModelMeshDomain::Granular)
            {
                return IsUuidV8(meshId) && asset.key.assetId.value == meshId
                    && asset.kind == experiment::cooked::CookedAssetKind::Mesh
                    && blob.kind == experiment::cooked::CookedAssetKind::Mesh
                    && blob.byteSize != 0u && blob.representation != 0u
                    && blob.schemaVersion != 0u && resolverRevision != 0u
                    && mountId != 0u && assetSetId.IsValid() && manifestRevision != 0u;
            }
            return domain == ModelMeshDomain::LegacyAggregate
                && IsUuidV8(modelId) && IsUuidV8(meshId) && generation != 0u;
        }
        friend auto operator<=>(const ModelMeshHandle&,
            const ModelMeshHandle&) noexcept = default;
    };

    // Small descriptor identity only; no parent model or CPU geometry is needed.
    [[nodiscard]] ModelMeshHandle MakeModelMeshHandle(const ModelMeshDescriptor& descriptor);

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
        // ★ W7 — coordinates 와 반드시 **함께** 다닌다. 이 구조체가 저작
        //   TextureReference 와 별개라, 한쪽에만 필드를 더하면 그 값은 모델
        //   자산을 지나며 조용히 사라진다(실제로 sampler 가 그렇게 사라져
        //   장부의 samplerIdentity 가 계속 하나였다).
        TextureSampler sampler{};
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
        experiment::MeshletPayload meshlets{};
        experiment::MeshLodChain coarseLods{};
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
        bool cookedPayload{};
        bool cookedHasAlpha{};
        bool cookedColorSpaceLocked{};
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
        // Optional Editor cache. The source artifact is still hashed before a hit is accepted.
        std::filesystem::path decodedTextureCacheRoot{};
        Uuid::Uuid16 expectedModelId{};
        std::uint64_t expectedGeneration{};
        // Offline export may validate old authoring generations before cooking.
        // Player leaves this false and accepts GPU-ready texture records only.
        bool allowSourceTextureProcessing{};
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
        using Shared = own::shared_owner<const ModelAssetGeneration>;

        // Public construction is factory-compatible; only the validated loader
        // can create this non-aggregate key. No raw-owner adoption is required.
        class LoadKey final
        {
        private:
            friend ModelAssetGenerationLoadResult LoadModelAssetGeneration(
                const ModelAssetGenerationLoadRequest& request);
            LoadKey() {}
        };

        ModelAssetGeneration(LoadKey, ModelAssetGenerationIdentity identity,
            std::string name, std::filesystem::path sourcePath,
            std::vector<ModelNodeAsset> nodes,
            std::vector<ModelMeshAsset> meshes,
            std::vector<ModelMaterialAsset> materials,
            std::vector<ModelTextureAsset> textures,
            std::optional<ModelSkeletonAsset> skeleton,
            std::vector<ModelAnimationAsset> animations,
            std::optional<ModelAnimatorAsset> animator,
            std::vector<ModelGpuUploadDescriptor> gpuDescriptors);

        ModelAssetGeneration(const ModelAssetGeneration&) = delete;
        ModelAssetGeneration& operator=(const ModelAssetGeneration&) = delete;
        ModelAssetGeneration(ModelAssetGeneration&&) = delete;
        ModelAssetGeneration& operator=(ModelAssetGeneration&&) = delete;
        ~ModelAssetGeneration() = default;

        [[nodiscard]] const ModelAssetGenerationIdentity& Identity() const noexcept;
        [[nodiscard]] ModelAssetGenerationHandle Handle() const noexcept;
        // Aggregate estimate includes nested capacities, not allocator/control
        // block overhead. It is a cache retention charge, never reclaimed bytes.
        [[nodiscard]] std::size_t EstimatedCpuBytes() const noexcept;
        [[nodiscard]] const std::string& Name() const noexcept;
        [[nodiscard]] const std::filesystem::path& SourcePath() const noexcept;
        [[nodiscard]] std::span<const ModelNodeAsset> Nodes() const noexcept;
        [[nodiscard]] std::span<const ModelMeshAsset> Meshes() const noexcept;
        [[nodiscard]] std::span<const ModelMaterialAsset> Materials() const noexcept;
        [[nodiscard]] std::span<const ModelTextureAsset> Textures() const noexcept;
        [[nodiscard]] const ModelSkeletonAsset* Skeleton() const noexcept;
        [[nodiscard]] std::span<const ModelAnimationAsset> Animations() const noexcept;
        // Bone-indexed channels baked once before publication. The generation owns
        // both the table and tracks; callers retain its Shared for the whole job.
        [[nodiscard]] std::span<const ModelAnimationTrack* const> AnimationTracks(int clipIndex) const noexcept;
        [[nodiscard]] const ModelAnimatorAsset* Animator() const noexcept;
        [[nodiscard]] std::span<const ModelGpuUploadDescriptor> GpuDescriptors() const noexcept;
        [[nodiscard]] const ModelMeshAsset* FindMesh(const Uuid::Uuid16& meshId) const noexcept;
        [[nodiscard]] const ModelMaterialAsset* FindMaterial(const Uuid::Uuid16& materialId) const noexcept;
        [[nodiscard]] const ModelTextureAsset* FindTexture(const Uuid::Uuid16& textureId) const noexcept;

    private:
        ModelAssetGenerationIdentity identity_{};
        std::string name_{};
        std::filesystem::path sourcePath_{};
        std::vector<ModelNodeAsset> nodes_{};
        std::vector<ModelMeshAsset> meshes_{};
        std::vector<ModelMaterialAsset> materials_{};
        std::vector<ModelTextureAsset> textures_{};
        std::optional<ModelSkeletonAsset> skeleton_{};
        std::vector<ModelAnimationAsset> animations_{};
        std::vector<std::vector<const ModelAnimationTrack*>> m_animationTracks{};
        std::optional<ModelAnimatorAsset> animator_{};
        std::vector<ModelGpuUploadDescriptor> gpuDescriptors_{};
    };

    // One immutable owner table per recorded frame, shared by its selected views.
    // Draw records carry a table index or a stable handle, never another owner.
    struct ModelAssetGenerationPins final
    {
        std::vector<ModelAssetGeneration::Shared> generations{};
        std::vector<own::shared_owner<const ModelMeshDescriptor>> meshes{};
    };

    // Scoped CPU-use/upload handoff, separate from durable descriptor frame pins.
    // Release after the last source read or synchronous native staging copy.
    struct ModelGeometryPreparationPin final
    {
        ModelMeshHandle handle{};
        own::shared_owner<const ModelGeometryPayload> payload{};
    };

    struct ModelGeometryPreparationPins final
    {
        std::vector<ModelGeometryPreparationPin> entries{};
    };

    struct ModelAssetGenerationLoadResult final
    {
        ModelAssetGeneration::Shared generation{};
        std::vector<ModelAssetGenerationIssue> issues{};
        // Recoverable optional acceleration-data failures; indexed geometry remains valid.
        std::vector<ModelAssetGenerationIssue> warnings{};
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
        ModelAssetGenerationHandle retiredHandle{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return outcome == ModelAssetPublishOutcome::Published
                || outcome == ModelAssetPublishOutcome::Replaced
                || outcome == ModelAssetPublishOutcome::AlreadyCurrent;
        }
    };

    struct ModelAssetGenerationCacheSnapshot final
    {
        std::size_t currentAssets{}; // Published identities, including expired residents.
        std::size_t addressableGenerations{}; // Current identities whose weak owner can lock.
        std::uint64_t publishes{};
        std::uint64_t replacements{};
        std::uint64_t retires{};
        std::uint64_t hits{};
        std::uint64_t misses{};
        std::size_t retainedGenerations{};
        std::size_t retainedBytes{};
        std::size_t retentionBudgetBytes{};
        std::uint64_t retentionEvictions{};
    };

    class ModelAssetGenerationCache final
    {
    private:
        using Key = ModelAssetGenerationHandle;

        struct Entry final
        {
            ModelAssetGenerationIdentity identity{};
            own::weak_owner<const ModelAssetGeneration> live{};
            ModelAssetGeneration::Shared retained{};
            std::size_t estimatedBytes{};
            std::uint64_t lastAccess{};
        };

    public:
        // Construct before taking outer admission locks, and destroy after those
        // locks are released. Empty map construction may allocate on some STLs.
        class RetiredEntries final
        {
        public:
            RetiredEntries() = default;
            RetiredEntries(const RetiredEntries&) = delete;
            RetiredEntries& operator=(const RetiredEntries&) = delete;
            RetiredEntries(RetiredEntries&&) = delete;
            RetiredEntries& operator=(RetiredEntries&&) = delete;
            ~RetiredEntries() = default;

        private:
            friend class ModelAssetGenerationCache;
            std::map<Key, Entry> generations_{};
            std::map<Uuid::Uuid16, Key> currentByAsset_{};
        };

        [[nodiscard]] ModelAssetPublishResult Publish(
            ModelAssetGeneration::Shared generation);
        [[nodiscard]] ModelAssetGeneration::Shared ResolveCurrent(
            const Uuid::Uuid16& modelId) const;
        [[nodiscard]] ModelAssetGeneration::Shared Resolve(
            ModelAssetGenerationHandle handle) const;
        [[nodiscard]] const ModelMeshAsset* ResolveMesh(ModelMeshHandle handle,
            ModelAssetGeneration::Shared& outOwner) const;
        [[nodiscard]] ModelAssetGeneration::Shared Retire(
            const Uuid::Uuid16& modelId,
            ModelAssetGenerationHandle* outRetiredHandle = nullptr);
        // Drops only cache-owned pins. Consumer/frame/job owners stay valid.
        void SetRetentionBudgetBytes(std::size_t bytes);
        // Requires empty caller-owned storage; detaches without allocating or
        // destroying retained owners under this cache or outer admission locks.
        void DetachAll(RetiredEntries& retired) noexcept;
        void Clear();
        [[nodiscard]] ModelAssetGenerationCacheSnapshot Snapshot() const;
        // MBC9 — current generation 전수(에디터 목록용). 정렬은 ModelId 순.
        [[nodiscard]] std::vector<ModelAssetGeneration::Shared> SnapshotCurrent() const;

    private:
        [[nodiscard]] ModelAssetGeneration::Shared AcquireLocked(
            const Key& key, Entry& entry) const;
        void RetainLocked(Entry& entry, const ModelAssetGeneration::Shared& generation,
            std::vector<ModelAssetGeneration::Shared>& released) const;
        void TrimRetainedLocked(std::size_t budget,
            std::vector<ModelAssetGeneration::Shared>& released) const;

        mutable std::mutex mutex_{};
        mutable std::map<Key, Entry> generations_{};
        std::map<Uuid::Uuid16, Key> currentByAsset_{};
        std::size_t retentionBudgetBytes_{ 256u * 1024u * 1024u };
        mutable std::size_t retainedBytes_{};
        mutable std::uint64_t accessSerial_{};
        mutable ModelAssetGenerationCacheSnapshot stats_{};
    };
}
