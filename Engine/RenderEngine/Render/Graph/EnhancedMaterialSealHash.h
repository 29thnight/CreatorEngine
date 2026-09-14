#pragma once

#include "EnhancedRenderPass.h"
#include "../../RHI/RHIPipelineLayout.h"
#include "../../Texture.h"

// W8 — draw snapshot의 값 신원을 한 수로 접는다.
//
// 이 digest는 프레임과 무관하다. 같은 값이면 프레임이 달라도 같고, 한 자리라도
// 다르면 다르다. 그래서 두 가지를 동시에 물을 수 있다.
//   ① 같은 material로 묶인 두 draw가 정말 같은 값인가 (sealing 중복 제거의 근거)
//   ② 한 프레임 안에서 같은 값이 서로 다른 PSO·descriptor로 그려지지 않았는가
//
// ★ 여기에 frameId를 섞지 않는다. 섞으면 매 프레임 값이 달라져 ①을 못 묻는다.
namespace EnhancedMaterialSeal
{
    inline void AppendGuid(EnhancedSealDigest& digest, const FileGuid& guid) noexcept
    {
        digest.Bytes(guid.m_guid.data.data(), guid.m_guid.data.size());
    }

    inline void AppendCoordinates(EnhancedSealDigest& digest,
        const assets::TextureCoordinates& coordinates) noexcept
    {
        digest.U32(coordinates.set);
        digest.F32(coordinates.offset[0]);
        digest.F32(coordinates.offset[1]);
        digest.F32(coordinates.scale[0]);
        digest.F32(coordinates.scale[1]);
        digest.F32(coordinates.rotation);
    }

    // 논리 property·register와 **실제 GPU 소유자**를 함께 접는다. 둘 중 하나만
    // 보면 같은 GUID가 다른 런타임 텍스처로 갈린 경우(색공간·mip 체인이 다른
    // 캐시 신원)를 놓친다 — Texture에는 generation 필드가 없고 캐시 키가
    // m_assetId이므로 그 값이 곧 세대의 자리다.
    inline void AppendTextureBinding(EnhancedSealDigest& digest,
        const EnhancedMaterialTextureBinding& binding) noexcept
    {
        digest.Text(binding.propertyName);
        AppendGuid(digest, binding.textureGuid);
        digest.U32(binding.registerIndex);
        digest.U32(binding.registerSpace);
        digest.U64(binding.textureOwner
            ? static_cast<std::uint64_t>(binding.textureOwner->m_assetId.m_ID_Data)
            : 0ull);
        AppendCoordinates(digest, binding.coordinates);
    }

    inline void AppendTextureBindings(EnhancedSealDigest& digest,
        const std::vector<EnhancedMaterialTextureBinding>& bindings) noexcept
    {
        digest.U64(static_cast<std::uint64_t>(bindings.size()));
        for (const auto& binding : bindings) AppendTextureBinding(digest, binding);
    }

    inline void AppendShared(EnhancedSealDigest& digest, ShaderMetaHandle handle,
        const RHIShaderPermutationKey& permutation,
        const ShaderMetaBindingLayout& layout,
        const std::vector<std::uint16_t>& keywordSelections,
        const std::vector<std::uint8_t>& propertyBytes,
        const EnhancedMaterialCoverage& coverage,
        std::uint32_t useNormalMap,
        const std::vector<EnhancedMaterialTextureBinding>& bindings) noexcept
    {
        digest.U32(handle.slot);
        digest.U32(handle.generation);
        digest.U64(permutation.lo);
        digest.U64(permutation.hi);
        digest.Text(layout.constantBufferName);
        digest.U32(layout.constantBufferRegister);
        digest.U32(layout.constantBufferSpace);
        digest.U32(static_cast<std::uint32_t>(layout.constantBufferByteSize));
        digest.U64(static_cast<std::uint64_t>(keywordSelections.size()));
        for (std::uint16_t selection : keywordSelections)
            digest.U32(static_cast<std::uint32_t>(selection));
        digest.U64(static_cast<std::uint64_t>(propertyBytes.size()));
        digest.Bytes(propertyBytes.data(), propertyBytes.size());
        digest.U32(coverage.flags);
        digest.F32(coverage.cutoff);
        digest.F32(coverage.baseAlpha);
        digest.U32(useNormalMap);
        AppendTextureBindings(digest, bindings);
    }

    [[nodiscard]] inline std::uint64_t ComputeHash(
        const EnhancedMaterialDrawSnapshot& snapshot) noexcept
    {
        EnhancedSealDigest digest;
        digest.Text("gbuffer");
        AppendShared(digest, snapshot.shaderMetaHandle, snapshot.permutationKey,
            snapshot.bindingLayout, snapshot.keywordSelections, snapshot.propertyBytes,
            snapshot.coverage, snapshot.useNormalMap, snapshot.textureBindings);
        return digest.Value();
    }

    [[nodiscard]] inline std::uint64_t ComputeHash(
        const EnhancedForwardMaterialDrawSnapshot& snapshot) noexcept
    {
        EnhancedSealDigest digest;
        digest.Text("forward");
        AppendShared(digest, snapshot.shaderMetaHandle, snapshot.permutationKey,
            snapshot.bindingLayout, snapshot.keywordSelections, snapshot.propertyBytes,
            snapshot.coverage, snapshot.useNormalMap, snapshot.textureBindings);
        // Forward instance upload가 소비하는 축이라 값 신원에 포함한다.
        for (float value : snapshot.flow.Values()) digest.F32(value);
        digest.F32(snapshot.baseColorFactor.r);
        digest.F32(snapshot.baseColorFactor.g);
        digest.F32(snapshot.baseColorFactor.b);
        digest.F32(snapshot.baseColorFactor.a);
        digest.F32(snapshot.metallic);
        digest.F32(snapshot.roughness);
        return digest.Value();
    }

    // texture table만 접은 값 — 한 프레임 안에서 같은 seal이 서로 다른 SRV
    // 묶음으로 그려졌는지 판정할 때 쓴다.
    [[nodiscard]] inline std::uint64_t ComputeTextureDigest(
        const std::vector<EnhancedMaterialTextureBinding>& bindings) noexcept
    {
        EnhancedSealDigest digest;
        AppendTextureBindings(digest, bindings);
        return digest.Value();
    }

    // sampler는 아직 재질 스냅샷 밖에 있다 — 패스가 Initialize에서 하나 만들어
    // 프레임 내내 고정한다(재질별 wrap/filter 전달은 W7의 남은 단위다). 그래도
    // **무엇을 걸었는지**는 적을 수 있어야 한다. 지금 값을 신원으로 남겨 두면
    // 재질별 sampler가 들어올 때 이 자리가 그대로 축이 된다.
    [[nodiscard]] inline std::uint64_t ComputeSamplerIdentity(
        const RHISamplerDesc& desc) noexcept
    {
        EnhancedSealDigest digest;
        digest.U32(static_cast<std::uint32_t>(desc.minMag));
        digest.U32(static_cast<std::uint32_t>(desc.mip));
        digest.U32(static_cast<std::uint32_t>(desc.addressU));
        digest.U32(static_cast<std::uint32_t>(desc.addressV));
        digest.U32(static_cast<std::uint32_t>(desc.addressW));
        digest.U32(static_cast<std::uint32_t>(desc.compare));
        digest.U32(static_cast<std::uint32_t>(desc.border));
        digest.F32(desc.maxLod);
        return digest.Value();
    }

    // 밀봉 직후 한 번만 부른다. 값 digest는 여기서 계산하고 프레임 도장은
    // 호출자가 준다.
    template <typename Snapshot>
    void Stamp(Snapshot& snapshot, std::uint64_t authoredDigest,
        std::uint64_t authoredRevision, std::uint64_t modelGeneration,
        std::uint64_t sceneEpoch, std::uint64_t frameId) noexcept
    {
        snapshot.seal.sealHash = ComputeHash(snapshot);
        snapshot.seal.authoredDigest = authoredDigest;
        snapshot.seal.authoredRevision = authoredRevision;
        snapshot.seal.modelGeneration = modelGeneration;
        snapshot.seal.sceneEpoch = sceneEpoch;
        snapshot.seal.frameId = frameId;
    }
}
