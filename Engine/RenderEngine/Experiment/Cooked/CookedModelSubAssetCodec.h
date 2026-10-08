#pragma once

#include "CookedAssetManifest.h"
#include "../ModelData.h"

namespace experiment::cooked
{
    // Independent CEMF3 payloads. These do not change the CEMC v11 contract.
    inline constexpr std::uint32_t kModelDescriptorRepresentation = 2u;
    inline constexpr std::uint32_t kSkeletonRepresentation = 1u;
    inline constexpr std::uint32_t kAnimationClipRepresentation = 1u;
    inline constexpr std::uint32_t kModelDescriptorVersion = 1u;
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

    struct ModelDescriptorArtifact final
    {
        AssetId modelAssetId{};
        AssetId skeletonAssetId{};
        std::string name{};
        std::vector<ModelClipSummary> clips{};
    };

    // Ordered, unambiguous bone names/parents/root. Bind matrices and transforms
    // are deliberately excluded so pose-only edits can preserve clip bytes.
    [[nodiscard]] bool ComputeBoneLayoutDigest(const Skeleton& skeleton,
        Sha256Digest& out, std::string& failure);
    [[nodiscard]] bool ValidateAnimationClipBinding(const AnimationClipArtifact& clip,
        const SkeletonArtifact& skeleton, std::string& failure);

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
