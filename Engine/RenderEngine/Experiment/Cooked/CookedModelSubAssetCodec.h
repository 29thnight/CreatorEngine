#include "../../../Containers/ContainerAPI.h"
#pragma once
#include "../../../Utility_Framework/Foundation/EngineFunc.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../../Tools/build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif

#include "CookedAssetManifest.h"
#include "../ModelData.h"
#include "../../../Utility_Framework/Foundation/StandardLibrary.h"

namespace experiment::cooked
{
// Independent CEMF3 payloads. Spatial DAG geometry requires artifact v4 / CEMC v15.
inline constexpr std::uint32_t kModelDescriptorRepresentation = 2u;
inline constexpr std::uint32_t kModelGeometryRepresentation = 1u;
inline constexpr std::uint32_t kModelGeometryArtifactVersion = 9u;
inline constexpr std::uint32_t kSkeletonRepresentation = 1u;
inline constexpr std::uint32_t kAnimationClipRepresentation = 1u;
// Version 5 requires the canonical LX profile-2 geometry contract.
inline constexpr std::uint32_t kModelDescriptorVersion = 5u;
inline constexpr std::uint32_t kSkeletonArtifactVersion = 1u;
inline constexpr std::uint32_t kAnimationClipArtifactVersion = 1u;
inline constexpr std::size_t kModelSubAssetMaxBytes = 256u * 1024u * 1024u;
inline constexpr std::uint32_t kModelSubAssetMaxBones = 65536u;
inline constexpr std::uint32_t kModelSubAssetMaxClips = 65536u;

struct SkeletonArtifact final
{
    // clips must be empty: this payload never owns animation tracks.
    Skeleton skeleton{};
    Sha256Digest boneLayoutSha256{};
};

struct AnimationClipArtifact final
{
    AssetId skeletonAssetId{};
    Sha256Digest requiredBoneLayoutSha256{};
    std::uint32_t requiredBoneCount{};
    AnimationClip clip{};
};

struct ModelClipSummary final
{
    AssetId clipAssetId{};
    std::string name{};
    double durationTicks{};
    double ticksPerSecond{};
    bool looping{true};
};

struct ModelGeometryArtifact final
{
    // Mesh는 저장소 어댑터다. name/material은 wire에서 제외하고 decode도 비워 둔다.
    // 논리 ID를 manifest/세대에 남겨 서로 다른 자산이 raw bytes를 공유할 수 있다.
    Mesh mesh{};
    std::uint32_t requiredBoneCount{};
    Sha256Digest requiredSkinBindingSha256{};
};

// Thin streaming admission owns descriptors/DAG and authenticated page locators,
// never original vertex, index or meshlet connectivity arrays.
struct ModelStreamLodCounts final
{
    std::uint32_t indexCount{}, vertexRemapCount{}, triangleByteCount{}, primitiveRemapCount{};
};
struct ModelGeometryStreamMetadata final
{
    VertexAttributeMask attributes{};
    std::uint32_t stride{}, vertexCount{}, requiredBoneCount{};
    math::aabb bounds{};
    Sha256Digest requiredSkinBindingSha256{};
    MeshletPayload meshlets{}; // Descriptors only; connectivity lives in pages.
    MeshLodChain lods{};       // Descriptors/DAG only; level.indices are empty.
    ce::dynamic_array<ModelStreamLodCounts> counts;
    own::shared_owner<const assets::cluster::Archive> archive;
    ce::dynamic_array<Sha256Digest> pageHashes;
    own::shared_owner<const ce::dynamic_array<MeshletDescriptor>> flatDescriptors;
    std::uint64_t metadataBytes{}, cpuGeometryBytes{};
    Sha256Digest cpuGeometrySha256{};
    ce::owned_func<ce::dynamic_array<std::byte>()> cpuReader;
};
struct ResolvedAssetEntry;
[[nodiscard]] bool ModelGeometryStreamPrefixSize(std::span<const std::byte> header, std::uint64_t totalBytes,
                                                std::uint32_t& prefixBytes, std::string& failure);
[[nodiscard]] bool ReadModelGeometryStreamMetadata(std::span<const std::byte> prefix, std::uint64_t totalBytes,
                                                  ModelGeometryStreamMetadata& out, std::string& failure);
[[nodiscard]] bool ModelGeometryStreamMetadataDigest(std::span<const std::byte> bytes, Sha256Digest& digest,
                                                    std::string& failure);
[[nodiscard]] std::string ModelGeometryStreamDirectory(const Sha256Digest& digest);
[[nodiscard]] bool ModelGeometryStreamIdentity(std::string_view path, Sha256Digest& digest);
[[nodiscard]] bool LoadModelGeometryStreamMetadata(ResolvedAssetEntry& resolved,
    own::shared_owner<const ModelGeometryStreamMetadata>& out, std::string& failure,
    std::atomic<std::size_t>* inputStaging = nullptr);

struct ModelMeshSummary final
{
    AssetId meshAssetId{};
    AssetId materialAssetId{};
    std::string name{};
    math::aabb bounds{};
    VertexAttributeMask attributes{};
    std::uint32_t stride{};
    std::uint32_t vertexCount{};
    std::uint32_t indexCount{};
    bool skinned{};
};

struct ModelNodeSummary final
{
    std::string name{};
    NodeIndex parent{};
    math::matrix4x4 localTransform{math::matrix4x4::identity()};
    ce::dynamic_array<AssetId> meshAssetIds{};
};

struct ModelMaterialSummary final
{
    // 연결용 설명 정보다. 준비된 material을 소유하거나 source-free 생산을 뜻하지 않는다.
    // Material is an independent Loadable record; the descriptor pins no bulk.
    AssetId materialAssetId{};
    std::string name{};
    MaterialBlendMode blendMode{MaterialBlendMode::Opaque};
};

struct ModelDescriptorArtifact final
{
    AssetId modelAssetId{};
    AssetId skeletonAssetId{};
    std::string name{};
    ce::dynamic_array<ModelClipSummary> clips{};
    ce::dynamic_array<ModelMeshSummary> meshes{};
    ce::dynamic_array<ModelNodeSummary> nodes{};
    ce::dynamic_array<ModelMaterialSummary> materials{};
    bool createMeshCollider{};
};

// Ordered, unambiguous bone names/parents/root. Bind matrices and transforms
// are deliberately excluded so pose-only edits can preserve clip bytes.
[[nodiscard]] bool ComputeBoneLayoutDigest(const Skeleton& skeleton, Sha256Digest& out, std::string& failure);
[[nodiscard]] bool ValidateAnimationClipBinding(const AnimationClipArtifact& clip, const SkeletonArtifact& skeleton,
                                                std::string& failure);

// skin은 bind/root 행렬까지 같아야 한다. 포즈 독립적인 위 animation layout
// digest를 대신 쓰면 같은 bone 순서의 다른 bind pose가 잘못 재결합된다.
[[nodiscard]] bool ComputeSkinBindingDigest(const Skeleton& skeleton, Sha256Digest& out, std::string& failure);
[[nodiscard]] bool ValidateModelGeometryBinding(const ModelGeometryArtifact& geometry, const SkeletonArtifact& skeleton,
                                                std::string& failure);
[[nodiscard]] bool ValidateModelMeshSummary(const ModelMeshSummary& summary, const ModelGeometryArtifact& geometry,
                                            std::string& failure);
[[nodiscard]] bool ValidateModelDescriptorDependencies(const ModelDescriptorArtifact& descriptor,
                                                       std::span<const AssetDependency> dependencies,
                                                       std::string& failure);
[[nodiscard]] bool WriteModelGeometryArtifact(const ModelGeometryArtifact& value, ce::dynamic_array<std::byte>& out,
                                              std::string& failure);
// Explicit CPU expansion of current QMC2 pages. Attributes are quantized, not original. Exact collision uses CECP.
// Old geometry schemas and malformed metadata/pages are rejected; recook required.
[[nodiscard]] bool ExpandModelGeometryPages(std::span<const std::byte> bytes, ModelGeometryArtifact& out,
                                             std::string& failure, ce::dynamic_array<std::string>* warnings = nullptr);

// Bounded little-endian wire fields; no native struct layout or padding.
// Failure leaves the output unchanged. Readers reject trailing bytes.
[[nodiscard]] bool WriteSkeletonArtifact(const SkeletonArtifact& value, ce::dynamic_array<std::byte>& out,
                                         std::string& failure);
[[nodiscard]] bool ReadSkeletonArtifact(std::span<const std::byte> bytes, SkeletonArtifact& out, std::string& failure);
[[nodiscard]] bool WriteAnimationClipArtifact(const AnimationClipArtifact& value, ce::dynamic_array<std::byte>& out,
                                              std::string& failure);
[[nodiscard]] bool ReadAnimationClipArtifact(std::span<const std::byte> bytes, AnimationClipArtifact& out,
                                             std::string& failure);
[[nodiscard]] bool WriteModelDescriptorArtifact(const ModelDescriptorArtifact& value, ce::dynamic_array<std::byte>& out,
                                                std::string& failure);
[[nodiscard]] bool ReadModelDescriptorArtifact(std::span<const std::byte> bytes, ModelDescriptorArtifact& out,
                                               std::string& failure);
} // namespace experiment::cooked
