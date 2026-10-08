#pragma once

#include "CookedAssetManifest.h"
#include "../ModelData.h"

namespace experiment::cooked
{
    // Independent CEMF3 payloads. These do not change the CEMC v11 contract.
    inline constexpr std::uint32_t kModelDescriptorRepresentation = 2u;
    inline constexpr std::uint32_t kModelGeometryRepresentation = 1u;
    inline constexpr std::uint32_t kModelGeometryArtifactVersion = 1u;
    inline constexpr std::uint32_t kSkeletonRepresentation = 1u;
    inline constexpr std::uint32_t kAnimationClipRepresentation = 1u;
    // Version 3 declares independent Material records as Loadable dependencies.
    inline constexpr std::uint32_t kModelDescriptorVersion = 3u;
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
        bool looping{ true };
    };

    struct ModelGeometryArtifact final
    {
        // Mesh는 저장소 어댑터다. name/material은 wire에서 제외하고 decode도 비워 둔다.
        // 논리 ID를 manifest/세대에 남겨 서로 다른 자산이 raw bytes를 공유할 수 있다.
        Mesh mesh{};
        std::uint32_t requiredBoneCount{};
        Sha256Digest requiredSkinBindingSha256{};
    };

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
        math::matrix4x4 localTransform{ math::matrix4x4::identity() };
        std::vector<AssetId> meshAssetIds{};
    };

    struct ModelMaterialSummary final
    {
        // 연결용 설명 정보다. 준비된 material을 소유하거나 source-free 생산을 뜻하지 않는다.
        // Material is an independent Loadable record; the descriptor pins no bulk.
        AssetId materialAssetId{};
        std::string name{};
        MaterialBlendMode blendMode{ MaterialBlendMode::Opaque };
    };

    struct ModelDescriptorArtifact final
    {
        AssetId modelAssetId{};
        AssetId skeletonAssetId{};
        std::string name{};
        std::vector<ModelClipSummary> clips{};
        std::vector<ModelMeshSummary> meshes{};
        std::vector<ModelNodeSummary> nodes{};
        std::vector<ModelMaterialSummary> materials{};
    };

    // Ordered, unambiguous bone names/parents/root. Bind matrices and transforms
    // are deliberately excluded so pose-only edits can preserve clip bytes.
    [[nodiscard]] bool ComputeBoneLayoutDigest(const Skeleton& skeleton,
        Sha256Digest& out, std::string& failure);
    [[nodiscard]] bool ValidateAnimationClipBinding(const AnimationClipArtifact& clip,
        const SkeletonArtifact& skeleton, std::string& failure);

    // skin은 bind/root 행렬까지 같아야 한다. 포즈 독립적인 위 animation layout
    // digest를 대신 쓰면 같은 bone 순서의 다른 bind pose가 잘못 재결합된다.
    [[nodiscard]] bool ComputeSkinBindingDigest(const Skeleton& skeleton,
        Sha256Digest& out, std::string& failure);
    [[nodiscard]] bool ValidateModelGeometryBinding(const ModelGeometryArtifact& geometry,
        const SkeletonArtifact& skeleton, std::string& failure);
    [[nodiscard]] bool ValidateModelMeshSummary(const ModelMeshSummary& summary,
        const ModelGeometryArtifact& geometry, std::string& failure);
    [[nodiscard]] bool ValidateModelDescriptorDependencies(const ModelDescriptorArtifact& descriptor,
        std::span<const AssetDependency> dependencies, std::string& failure);
    [[nodiscard]] bool WriteModelGeometryArtifact(const ModelGeometryArtifact& value,
        std::vector<std::byte>& out, std::string& failure);
    // 정본 geometry 오류는 실패한다. 손상된 선택적 meshlet/LOD만 버릴 수 있게
    // section을 따로 경계 짓고 indexed fallback 경고를 선택적으로 돌려준다.
    [[nodiscard]] bool ReadModelGeometryArtifact(std::span<const std::byte> bytes,
        ModelGeometryArtifact& out, std::string& failure,
        std::vector<std::string>* warnings = nullptr);

    // Bounded little-endian wire fields; no native struct layout or padding.
    // Failure leaves the output unchanged. Readers reject trailing bytes.
    [[nodiscard]] bool WriteSkeletonArtifact(const SkeletonArtifact& value,
        std::vector<std::byte>& out, std::string& failure);
    [[nodiscard]] bool ReadSkeletonArtifact(std::span<const std::byte> bytes,
        SkeletonArtifact& out, std::string& failure);
    [[nodiscard]] bool WriteAnimationClipArtifact(const AnimationClipArtifact& value,
        std::vector<std::byte>& out, std::string& failure);
    [[nodiscard]] bool ReadAnimationClipArtifact(std::span<const std::byte> bytes,
        AnimationClipArtifact& out, std::string& failure);
    [[nodiscard]] bool WriteModelDescriptorArtifact(const ModelDescriptorArtifact& value,
        std::vector<std::byte>& out, std::string& failure);
    [[nodiscard]] bool ReadModelDescriptorArtifact(std::span<const std::byte> bytes,
        ModelDescriptorArtifact& out, std::string& failure);
}
