#include "EnhancedGBufferPass.h"
#include "../../Graph/EnhancedDrawIdentity.h" // I6-C
#include "../../Graph/EnhancedMaterialSealHash.h" // W8
#include "../../../Assets/ModelVertexLayout.h"
#include "../../../RHI/ModelVertexInputLayout.h"
#include "../../../RHI/RHIShaderCompiler.h"
#include "../../../RHI/RHIShaderSource.h"
#include "../../../ShaderMeta.h"
#include "../../../ShaderMetaReflection.h"
#include "../../../ShaderPermutationDomain.h"
#include "../../../StandardMaterialProperty.h"
#include "../../../RHI/DX12/DX12DeviceResources.h"
#include "../../../RHI/DX12/DX12PSOManager.h"
#include "../../../RHI/DX12/DX12RootSignatureCache.h"
#include "../../../RHI/DX12/DX12MeshCache.h"
#include "../../../RHI/DX12/DX12TextureCache.h"
#include "../../../Mesh.h"
#include "../../../Texture.h"
#include "../../../RHI/RHIEncoder.h"
#include "../../Graph/ShadowCasterBounds.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace
{
    // 유니티 빌드에서 익명 네임스페이스가 파일 간 합쳐지므로 이름을 고유하게 둔다.
    std::string GBufferHrToString(HRESULT hr)
    {
        std::ostringstream oss;
        oss << "HRESULT 0x" << std::hex << static_cast<unsigned long>(hr);
        return oss.str();
    }

    // 첫 슬라이스의 셰이더는 소스에 담는다. 재질 셰이더 연결은 씬 연결
    // 슬라이스에서 ShaderSystem·PSOManager와 함께 붙인다.
    //
    // 타깃마다 서로 다른 값을 쓰는 이유는 검증 때문이다 — 다섯 타깃이 실제로
    // 각각 기록되는지 확인하려면 값이 구분되어야 한다. 한 타깃만 기록되고
    // 나머지가 비어 있어도 '그려지긴 한다'로 보이는 것을 막는다.

    constexpr const char* kGBufferShaderFile = "GBuffer.slang";

    bool GBufferSkinPaletteExtentContract(const RHIShaderReflection& reflection,
        bool& declared, std::string& error)
    {
        declared = false;
        constexpr std::array<const char*, 4> fields{
            "gGBufferSkinPaletteMatrixCount", "gGBufferSkinPaletteVersion",
            "gGBufferSkinPalettePadding0", "gGBufferSkinPalettePadding1"};
        for (const auto& resource : reflection.resources)
        {
            if (resource.name != "GBufferSkinPaletteExtentV1")
            {
                continue;
            }
            if (declared || resource.kind != RHIShaderResourceKind::ConstantBuffer ||
                resource.registerIndex != 4u || resource.registerSpace != 0u ||
                resource.arrayElements != 1u || resource.byteSize != 16u || resource.fields.size() != fields.size())
            {
                error = "GBufferSkinPaletteExtentV1 requires its exact 16-byte b4/space0 uniform layout.";
                return false;
            }
            for (std::size_t index = 0; index < fields.size(); ++index)
            {
                const auto& field = resource.fields[index];
                if (field.name != fields[index] || field.byteOffset != index * sizeof(std::uint32_t) ||
                    field.byteSize != sizeof(std::uint32_t) || field.type.scalar != RHIShaderScalarKind::UInt32 ||
                    field.type.rows != 1u || field.type.columns != 1u || field.type.arrayElements != 1u)
                {
                    error = "GBufferSkinPaletteExtentV1 field names, types and offsets must match the V1 ABI.";
                    return false;
                }
            }
            declared = true;
        }
        return true;
    }

    bool GBufferStandardDepthSource(const LX::Runtime::GraphicsGeneration& source)
    {
        const auto& identity = source.shader.compile;
        const auto& indexed = source.pipeline.GetDesc();
        return source.pipeline.IsValid() && identity.visibleInstanceIds && !identity.referencePath
            && identity.geometryVisibility == ShaderGeometryVisibility::IndexedInstanceV1
            && assets::IsSupportedModelVertexLayout(identity.vertexAttributeMask)
            && std::filesystem::path(identity.source).lexically_normal() == std::filesystem::path(kGBufferShaderFile)
            && identity.vertexEntry == "VSMain" && identity.pixelEntry == "PSMain"
            && !(source.shader.shader && source.shader.shader->meta.generatedMaterial)
            && !indexed.blendEnable && !indexed.independentBlend && indexed.depthEnable
            && indexed.depthWriteMask == RHIDepthWrite::All
            && (indexed.depthFunc == RHICompareOp::Less || indexed.depthFunc == RHICompareOp::LessEqual)
            && indexed.topologyType == RHITopologyType::Triangle && indexed.sampleCount == 1;
    }

    bool VerifyGBufferGeneration(const LX::Runtime::GraphicsCompileIdentity& identity,
        RHIShaderCompiler::VerifiedShader& vertexProof, RHIShaderCompiler::VerifiedShader& pixelProof,
        std::string& diagnostic)
    {
        if (!RHIShaderCompiler::VerifyFile(identity.source, identity.vertexEntry, identity.vertexProfile,
                identity.backend, identity.permutation, vertexProof, diagnostic, identity.options)
            || !RHIShaderCompiler::VerifyFile(identity.source, identity.pixelEntry, identity.pixelProfile,
                identity.backend, identity.permutation, pixelProof, diagnostic, identity.options))
        {
            return false;
        }
        if (vertexProof.dependencyIdentity != identity.vertexDependencies
            || pixelProof.dependencyIdentity != identity.pixelDependencies)
        {
            diagnostic = "Derivative shader source no longer matches the accepted GBuffer generation.";
            return false;
        }
        return true;
    }

    math::vector4 GBufferMeshletLocalBounds(const experiment::MeshletPayload& payload)
    {
        if (payload.descriptors.empty())
        {
            return {};
        }
        std::array<double, 3> minimum{}, maximum{};
        for (size_t axis = 0; axis < 3; ++axis)
        {
            minimum[axis] = std::numeric_limits<double>::infinity();
            maximum[axis] = -std::numeric_limits<double>::infinity();
        }
        for (const auto& descriptor : payload.descriptors)
        {
            for (size_t axis = 0; axis < 3; ++axis)
            {
                const double center = descriptor.sphereCenter[axis];
                minimum[axis] = (std::min)(minimum[axis], center - descriptor.sphereRadius);
                maximum[axis] = (std::max)(maximum[axis], center + descriptor.sphereRadius);
            }
        }
        std::array<float, 3> center{};
        for (size_t axis = 0; axis < 3; ++axis)
        {
            const double midpoint = (minimum[axis] + maximum[axis]) * 0.5;
            if (!std::isfinite(midpoint) || std::abs(midpoint) > (std::numeric_limits<float>::max)())
            {
                return {};
            }
            center[axis] = static_cast<float>(midpoint);
        }
        double radius = 0.0;
        for (const auto& descriptor : payload.descriptors)
        {
            const double x = static_cast<double>(center[0]) - descriptor.sphereCenter[0];
            const double y = static_cast<double>(center[1]) - descriptor.sphereCenter[1];
            const double z = static_cast<double>(center[2]) - descriptor.sphereCenter[2];
            radius = (std::max)(radius, std::sqrt(x * x + y * y + z * z) + descriptor.sphereRadius);
        }
        if (!std::isfinite(radius) || radius > (std::numeric_limits<float>::max)())
        {
            return {};
        }
        const float outward = radius > 0.0
            ? std::nextafter(static_cast<float>(radius), std::numeric_limits<float>::infinity()) : 0.0f;
        // Unknown bounds force LOD0; they never permit additional simplification.
        return std::isfinite(outward) ? math::vector4{center[0], center[1], center[2], outward} : math::vector4{};
    }

    // Isolated fixtures have an explicit instance flag for the property block.
    // alphaCutoff is only a coverage threshold.
    struct LegacyMaterialConstants
    {
        float baseColor[4]{ 1.f, 1.f, 1.f, 1.f };
        float metallic{ 0.f };
        float roughness{ 1.f };
        float normalScale{ 1.f };
        float occlusionStrength{ 1.f };
        float emissive[3]{ 1.f, 1.f, 1.f };
        float alphaCutoff{ 0.5f };
    };
    static_assert(sizeof(LegacyMaterialConstants) == 48u);
    static_assert(offsetof(LegacyMaterialConstants, alphaCutoff) == 44u);
    constexpr LegacyMaterialConstants kLegacyMaterialConstants{};

}

bool EnhancedGBufferPass::MaterialKey::operator==(const MaterialKey& other) const
{
    if (coordinates != other.coordinates || sampler != other.sampler
        || textures != other.textures
        || static_cast<bool>(snapshot) != static_cast<bool>(other.snapshot))
    {
        return false;
    }
    if (!snapshot) return true;
    return snapshot->shaderMetaHandle == other.snapshot->shaderMetaHandle
        && snapshot->permutationKey == other.snapshot->permutationKey
        && snapshot->keywordSelections == other.snapshot->keywordSelections
        && snapshot->propertyBytes == other.snapshot->propertyBytes;
}

bool EnhancedGBufferPass::MaterialKey::operator<(const MaterialKey& other) const
{
    const ShaderMetaHandle leftHandle = snapshot
        ? snapshot->shaderMetaHandle : ShaderMetaHandle{};
    const ShaderMetaHandle rightHandle = other.snapshot
        ? other.snapshot->shaderMetaHandle : ShaderMetaHandle{};
    if (leftHandle.slot != rightHandle.slot)
        return leftHandle.slot < rightHandle.slot;
    if (leftHandle.generation != rightHandle.generation)
        return leftHandle.generation < rightHandle.generation;
    if (snapshot && other.snapshot)
    {
        if (snapshot->keywordSelections != other.snapshot->keywordSelections)
            return snapshot->keywordSelections < other.snapshot->keywordSelections;
        if (snapshot->permutationKey.hi != other.snapshot->permutationKey.hi)
            return snapshot->permutationKey.hi < other.snapshot->permutationKey.hi;
        if (snapshot->permutationKey.lo != other.snapshot->permutationKey.lo)
            return snapshot->permutationKey.lo < other.snapshot->permutationKey.lo;
        if (snapshot->propertyBytes != other.snapshot->propertyBytes)
            return snapshot->propertyBytes < other.snapshot->propertyBytes;
    }
    if (coordinates != other.coordinates) return coordinates < other.coordinates;
    if (sampler != other.sampler) return sampler < other.sampler;
    return std::lexicographical_compare(textures.begin(), textures.end(),
        other.textures.begin(), other.textures.end(), std::less<Texture*>{});
}

EnhancedGBufferPass::MaterialKey EnhancedGBufferPass::MakeMaterialKey(
    const EnhancedDrawItem& draw) const
{
    MaterialKey key{};
    if (draw.materialSnapshot && draw.materialSnapshot->IsValid())
    {
        key.textures = MaterialTextureTable::Owners(*draw.materialSnapshot);
        key.coordinates = MaterialTextureTable::Coordinates(*draw.materialSnapshot);
        key.sampler = MaterialTextureTable::EffectiveSampler(*draw.materialSnapshot);
        key.snapshot = draw.materialSnapshot;
    }
    else
    {
        key.textures = MaterialTextureTable::LegacyOwners(m_legacyTextureSchema, draw);
    }
    return key;
}

bool EnhancedGBufferPass::CaptureShaderVariant(EnhancedMaterialDrawSnapshot& snapshot) const
{
    std::vector<std::shared_ptr<const LX::Runtime::GraphicsGeneration>> candidate;
    const auto capture = [&](const LX::Runtime::GraphicsPipeline& request) {
        const auto generation = request.GetGeneration();
        if (!generation || !generation->shader.shader ||
            generation->shader.shader->codeHandle != snapshot.shaderMetaHandle ||
            generation->shader.materialPermutationKey != snapshot.permutationKey ||
            generation->shader.shader->layout != snapshot.bindingLayout) return false;
        candidate.push_back(generation);
        return true;
    };
    if (snapshot.shaderMetaHandle == m_shaderMetaHandle && snapshot.permutationKey == m_defaultPermutationKey)
    {
        if (!capture(m_pipelineRequest)) return false;
        for (const auto& [mask, request] : m_modelPipelineRequests) if (!capture(request)) return false;
    }
    else
    {
        const auto found = m_shaderVariants.find({snapshot.shaderMetaHandle, snapshot.permutationKey});
        if (found == m_shaderVariants.end() || !capture(found->second.request)) return false;
        for (const auto& [mask, request] : found->second.modelRequests) if (!capture(request)) return false;
    }
    snapshot.pipelineGenerations = std::move(candidate);
    return true;
}

bool EnhancedGBufferPass::ResolveShaderVariant(
    const EnhancedMaterialDrawSnapshot& snapshot,
    uint32_t vertexAttributeMask, RHIPipelineHandle& outPipeline,
    std::shared_ptr<const ShaderMetaBindingLayout>& outLayout) const
{
    // Recording consumes only the accepted generation sealed into this frame.
    const auto generation = LX::Runtime::ResolveGraphicsGeneration(snapshot.pipelineGenerations,
        snapshot.shaderMetaHandle, snapshot.permutationKey, snapshot.bindingLayout, vertexAttributeMask, false);
    if (!generation) return false;
    outPipeline = generation->pipeline.GetHandle();
    outLayout = {generation->shader.shader, &generation->shader.shader->layout};
    return true;
}
RHIPipelineHandle EnhancedGBufferPass::GetShaderVariantPipeline(
    ShaderMetaHandle handle, RHIShaderPermutationKey permutationKey) const
{
    if (handle == m_shaderMetaHandle && permutationKey == m_defaultPermutationKey)
        return m_pipelineRequest.GetHandle();
    const auto found = m_shaderVariants.find({ handle, permutationKey });
    return found == m_shaderVariants.end()
        ? RHIPipelineHandle{} : found->second.request.GetHandle();
}

RHIFormat EnhancedGBufferPass::GetRenderTargetFormat(uint32_t index)
{
    // DX11 쪽 구성과 같아야 대조가 성립한다.
    switch (index)
    {
    case 0: return RHIFormat::RGBA16Float;  // Diffuse
    case 1: return RHIFormat::RGBA16Float;  // MetalRough
    case 2: return RHIFormat::RGBA16Float;  // Normal
    case 3: return RHIFormat::RGBA16Float;  // Emissive
    case 4: return RHIFormat::R32Uint;            // Bitmask
    default: return RHIFormat::Unknown;
    }
}

bool EnhancedGBufferPass::PrepareFrame(const EnhancedFrameContext& context, std::string& outError)
{
    m_visibilityFrame.reset();
    m_occluderVisibilityFrame.reset();
    m_occlusionPyramid.reset();
    m_batches.clear();
    m_instances.clear();
    m_instanceBounds.clear();
    m_drawGeometry.clear();
    m_drawTextures.clear();
    m_bonePalettes.clear();
    m_boneOffsets.clear();
    m_boneCounts.clear();
    m_lastDrawCount = 0;
    m_lastSkinnedCount = 0;
    m_lastBatchCount = 0;
    m_lastMeshletBatchCount = 0;
    m_lastMeshletFallback.clear();
    m_geometryRouteAudit.clear();
    m_lastOcclusionFallback.clear();
    m_lastSkinningFallback.clear();
    // W8: 장부는 프레임마다 비운다. 비우지 않으면 지난 프레임의 배치가 이번
    // 프레임의 충돌로 보고된다.
    m_sealLedger.Begin(context.frameId, context.sceneEpoch);
    m_rejectedSnapshots.clear();

    // 프레임 밀봉된 카메라에서 뷰·투영을 만든다. 스냅샷이 없으면 항등으로 두는데,
    // 그러면 클립 공간에 바로 그리게 되므로 '카메라가 안 붙었다'가 화면에 드러난다.
    m_frameViewProjection = (nullptr != context.camera)
        ? context.camera->view * context.camera->projection
        : math::matrix4x4::identity();

    if (nullptr == context.draws || nullptr == context.meshCache) return true;

    // 지오메트리와 재질을 따로 훑는다.
    //
    // 예전에는 메시 중복 제거 블록 안에서 재질까지 올렸는데, 그러면 같은 메시를
    // 다른 재질로 두 번 그릴 때 두 번째 재질이 통째로 건너뛰어진다. 중복 제거의
    // 단위가 둘이 다르다 — 지오메트리는 메시별로, 재질은 재질별로 한 번이다.
    for (const auto& draw : *context.draws)
    {
        if (0 == enhanced_draw::GeometryKey(draw)) continue;

        if (draw.materialSnapshot)
        {
            const EnhancedMaterialDrawSnapshot& material = *draw.materialSnapshot;
            if (!material.IsValid()
                || material.bindingLayout.constantBufferRegister != 2
                || material.bindingLayout.constantBufferSpace != 0)
            {
                outError = "GBuffer draw material snapshot의 b2/ShaderMeta 계약이 invalid다";
                return false;
            }
            RHIPipelineHandle materialPipeline{};
            std::shared_ptr<const ShaderMetaBindingLayout> materialLayout;
            // 이 지점은 메시 업로드 전이라 마스크를 모른다 — 여기의 목적은
            // 재질 레이아웃 검증이므로 legacy 축(0)으로 확인한다. 마스크별
            // PSO 선택은 BuildBatches가 한다.
            if (!ResolveShaderVariant(material, 0, materialPipeline, materialLayout)
                || material.bindingLayout != *materialLayout)
            {
                outError = "GBuffer draw material permutation/layout이 준비된 variant와 다르다";
                return false;
            }
            if (!MaterialTextureTable::ValidateSnapshot(material, outError)) return false;
            if (!MaterialTextureTable::ValidateMeshCoordinates(material, draw.modelMeshView.vertexAttributeMask, outError)) return false;

            // W8: 값 계약이 맞아도 **이번 프레임 것이 아니면** 섞인 것이다.
            // 프레임 전체를 실패시키지 않는 이유는 계획의 처방 그대로다 —
            // 일부만 새 세대로 섞이면 그 draw를 생략하고 원인을 남긴다.
            if (!m_sealLedger.Accept(material.seal,
                    EnhancedMaterialSeal::ComputeHash(material)))
            {
                m_rejectedSnapshots.insert(draw.materialSnapshot.get());
                continue;
            }
        }

        if (m_drawGeometry.find(enhanced_draw::GeometryKey(draw)) == m_drawGeometry.end())
        {
            std::string uploadError;
            // I5-D4b: 핸들이 실린 아이템은 legacy Mesh 없이 완결되는 핸들
            // 진입점으로 올린다(키=experiment 자산 신원). mesh 포인터는 정렬·
            // 지오메트리 맵 키로 남는다(은퇴는 D4f).
            const auto entry = draw.modelMeshView.IsComplete()
                ? context.meshCache->GetOrUploadModel(draw.modelMeshView, uploadError)
                : context.meshCache->GetOrUpload(draw.mesh, uploadError);
            if (!entry.IsValid())
            {
                // 빈 메시는 그냥 건너뛴다. 업로드 실패는 알린다 — 조용히 안 그리면
                // '왜 이 오브젝트만 안 보이지'가 된다.
                if (!uploadError.empty()) outError = uploadError;
                continue;
            }

            m_drawGeometry.emplace(enhanced_draw::GeometryKey(draw), entry);
        }
        else if (!m_drawGeometry[enhanced_draw::GeometryKey(draw)].IsValid())
        {
            continue;
        }

        if (nullptr != context.textureCache)
        {
            const MaterialKey key = MakeMaterialKey(draw);
            if (m_drawTextures.find(key) == m_drawTextures.end())
            {
                auto schema = m_legacyTextureSchema;
                if (key.snapshot && !MaterialTextureTable::FromLayout(
                        key.snapshot->bindingLayout, schema, outError)) return false;
                DrawTextures textures;
                if (!MaterialTextureTable::Upload(*context.textureCache, schema,
                        key.textures, textures.views, outError, !key.snapshot)) return false;
                m_drawTextures.emplace(key, std::move(textures));
            }
        }
        // 본 팔레트를 애니메이터별로 한 번씩만 담는다.
        //
        // 한 캐릭터의 메시가 여럿이면 프록시도 여럿인데 팔레트는 하나다 —
        // 중복 제거가 없으면 같은 512행렬(32KB)을 메시 수만큼 올린다.
        if (!draw.bonePalette && draw.boneCount != 0)
        {
            outError = "GBuffer draw declares a palette count without palette storage.";
            return false;
        }
        if (nullptr != draw.bonePalette && 0 != draw.boneCount)
        {
            if (m_boneOffsets.find(draw.animatorKey) == m_boneOffsets.end())
            {
                if (context.animationPalettes)
                {
                    const auto& shared = context.animationPalettes->Offsets();
                    if (const auto found = shared.find(draw.animatorKey); found != shared.end())
                        m_boneOffsets.emplace(draw.animatorKey, found->second);
                }
                else
                {
                    if (m_bonePalettes.size() > (std::numeric_limits<uint32_t>::max)()
                        || draw.boneCount > (std::numeric_limits<uint32_t>::max)() - m_bonePalettes.size())
                    {
                        outError = "GBuffer local palette upload exceeds 32-bit addressing.";
                        return false;
                    }
                    const uint32_t offset = static_cast<uint32_t>(m_bonePalettes.size());
                    m_bonePalettes.resize(m_bonePalettes.size() + draw.boneCount);
                    for (uint32_t i = 0; i < draw.boneCount; ++i)
                        m_bonePalettes[offset + i] = PackedBoneMatrix::From(draw.bonePalette[i]);
                    m_boneOffsets.emplace(draw.animatorKey, offset);
                    m_boneCounts.emplace(draw.animatorKey, draw.boneCount);
                }
            }
            else if (!context.animationPalettes)
            {
                const auto offset = m_boneOffsets.at(draw.animatorKey);
                if (m_boneCounts.at(draw.animatorKey) != draw.boneCount)
                {
                    outError = "GBuffer local animator key refers to different palette counts.";
                    return false;
                }
                for (uint32_t bone = 0; bone < draw.boneCount; ++bone)
                {
                    const auto expected = PackedBoneMatrix::From(draw.bonePalette[bone]);
                    if (std::memcmp(&m_bonePalettes[offset + bone], &expected, sizeof(expected)) != 0)
                    {
                        outError = "GBuffer local animator key refers to different poses in one frame.";
                        return false;
                    }
                }
            }
            ++m_lastSkinnedCount;
        }

        ++m_lastDrawCount;
    }

    m_lastMeshCount = static_cast<uint32_t>(m_drawGeometry.size());
    m_lastMaterialCount = static_cast<uint32_t>(m_drawTextures.size());

    if (!BuildBatches(context, outError))
    {
        return false;
    }
    std::erase_if(m_skinningBounds, [this](const auto& entry) {
        return !m_drawGeometry.contains(HashModelMeshHandle(entry.first));
    });
    std::erase_if(m_meshletLocalBounds, [this](const auto& entry) {
        return !m_drawGeometry.contains(HashModelMeshHandle(entry.first));
    });
    return true;
}

bool EnhancedGBufferPass::HasGpuVisibilityCandidates() const
{
    return std::any_of(m_batches.begin(), m_batches.end(), [](const auto& batch) {
        return batch.gpuEligible || batch.meshletPipeline.IsValid();
    });
}

bool EnhancedGBufferPass::PrepareGpuVisibility(const EnhancedFrameContext& context, std::string& outError)
{
    m_occluderVisibilityFrame.reset();
    m_lastMeshletBatchCount = 0;
    m_occlusionPyramid.reset();
    const bool hasPotentialOccluders = std::any_of(m_batches.begin(), m_batches.end(), [](const auto& batch) {
        return batch.occluderPipeline.IsValid() || batch.meshletOccluderPipeline.IsValid();
    });
    const bool hasIndexedCandidates = std::any_of(m_batches.begin(), m_batches.end(), [](const auto& batch) {
        return batch.gpuEligible;
    });
    const bool hasMeshletCandidates = std::any_of(m_batches.begin(), m_batches.end(), [](const auto& batch) {
        return batch.meshletPipeline.IsValid();
    });
    if (hasPotentialOccluders
        && (!m_depthPyramid.Prepare(context, m_frameViewProjection, m_occlusionPyramid, m_lastOcclusionFallback)
            || (hasIndexedCandidates && !m_visibility.PrepareOcclusionPipelines(context, m_lastOcclusionFallback))
            || (hasMeshletCandidates && !m_meshletVisibility.PrepareOcclusionPipelines(context, m_lastOcclusionFallback))))
    {
        // A missing optional pyramid prerequisite retains the complete frustum
        // route. No donor pass or partially usable depth is declared in that case.
        m_occlusionPyramid.reset();
    }
    std::vector<GpuGeometryVisibility::Candidate> candidates;
    std::vector<GpuGeometryVisibility::Bin> bins;
    std::uint64_t outputOffset = 0;
    constexpr auto maximum = (std::numeric_limits<std::uint32_t>::max)();
    for (auto& batch : m_batches)
    {
        batch.visibilityBin = UINT32_MAX;
        batch.meshletDraws.clear();
        batch.occluderMeshletDraws.clear();
        const auto& geometry = m_drawGeometry.at(batch.geometryKey);
        if (batch.meshletPipeline.IsValid())
        {
            std::vector<math::matrix4x4> worlds;
            worlds.reserve(batch.instanceCount);
            for (uint32_t local = 0; local < batch.instanceCount; ++local)
            {
                worlds.push_back(math::transpose(m_instances[batch.firstInstance + local].world));
            }
            std::string meshletError;
            std::array<RHIMeshletBinding, kRHIMaxCoarseMeshLods + 1> lods;
            std::array<float, kRHIMaxCoarseMeshLods + 1> errors{};
            std::vector<std::shared_ptr<const GpuMeshletVisibility::Frame>> frames;
            lods[0] = geometry.meshlets;
            if (geometry.coarseLodCount > kRHIMaxCoarseMeshLods)
            {
                outError = "GBuffer coarse LOD binding count exceeds its immutable ABI.";
                return false;
            }
            const auto levelCount = geometry.coarseLodCount + 1u;
            for (uint32_t level = 0; level < geometry.coarseLodCount; ++level)
            {
                lods[level + 1u] = geometry.coarseLods[level].meshlets;
                errors[level + 1u] = geometry.coarseLods[level].geometricError;
            }
            if (!m_meshletVisibility.PrepareLods(context, m_frameViewProjection,
                    std::span(lods.data(), levelCount), std::span(errors.data(), levelCount),
                    batch.meshletLocalSphere, worlds, frames, meshletError))
            {
                outError = std::move(meshletError);
                return false;
            }
            if (!frames.empty())
            {
                if (frames.size() != levelCount
                    || std::any_of(frames.begin(), frames.end(), [](const auto& frame) { return !frame; }))
                {
                    outError = "GBuffer meshlet LOD preparation returned a partial chain.";
                    return false;
                }
                for (uint32_t level = 0; level < levelCount; ++level)
                {
                    batch.occluderMeshletDraws.push_back({lods[level], frames[level]});
                    batch.meshletDraws.push_back({lods[level], std::move(frames[level])});
                }
                if (m_occlusionPyramid)
                {
                    // Main HZB filtering consumes the exact already-selected
                    // GPU pair list; LOD is never recomputed by another variant.
                    for (uint32_t level = 0; level < levelCount; ++level)
                    {
                        std::shared_ptr<const GpuMeshletVisibility::Frame> rechecked;
                        if (!m_meshletVisibility.PrepareOcclusionRecheck(context,
                                batch.occluderMeshletDraws[level].visibility, rechecked, meshletError))
                        {
                            outError = std::move(meshletError);
                            return false;
                        }
                        if (!rechecked)
                        {
                            m_lastOcclusionFallback = std::move(meshletError);
                            m_occlusionPyramid.reset();
                            break;
                        }
                        batch.meshletDraws[level].visibility = std::move(rechecked);
                    }
                }
                ++m_lastMeshletBatchCount;
                continue;
            }
            // Capability/work-budget/compiler prerequisites failed before
            // submission. The unchanged indexed batch is still a valid route.
            if (!meshletError.empty())
            {
                m_lastMeshletFallback = std::move(meshletError);
            }
            else
            {
                m_lastMeshletFallback = "Meshlet work prerequisites or capacity are unavailable; using indexed geometry.";
            }
        }
        if (!batch.gpuEligible)
        {
            continue;
        }
        if (bins.size() >= maximum || outputOffset > maximum ||
            batch.instanceCount > maximum - outputOffset || candidates.size() > maximum - batch.instanceCount)
        {
            outError = "GBuffer visibility ranges exceed 32-bit addressing.";
            return false;
        }
        batch.visibilityBin = static_cast<uint32_t>(bins.size());
        batch.visibleIdOffset = static_cast<uint32_t>(outputOffset);
        bins.push_back({geometry.indexCount, 0, 0,
                        batch.compactsVisibleIds ? 0u : batch.instanceCount});
        for (uint32_t local = 0; local < batch.instanceCount; ++local)
        {
            candidates.push_back({m_instanceBounds[batch.firstInstance + local], batch.visibilityBin,
                                  local, static_cast<uint32_t>(outputOffset),
                                  batch.occlusionEligible ? 0u : GpuGeometryVisibility::kNoOcclusion});
        }
        outputOffset += (std::uint64_t(batch.instanceCount) + GpuGeometryVisibility::kOutputAlignment - 1u) /
                        GpuGeometryVisibility::kOutputAlignment * GpuGeometryVisibility::kOutputAlignment;
    }
    if (!m_occlusionPyramid)
    {
        for (auto& batch : m_batches)
        {
            if (!batch.occluderMeshletDraws.empty())
            {
                batch.meshletDraws = batch.occluderMeshletDraws;
            }
        }
    }
    const bool hasIndexedDonors = std::any_of(m_batches.begin(), m_batches.end(), [](const auto& batch) {
        return batch.meshletDraws.empty() && batch.occluderPipeline.IsValid() && batch.gpuEligible;
    });
    if (m_occlusionPyramid && hasIndexedDonors &&
        !m_visibility.Prepare(context, m_frameViewProjection, candidates, bins, m_occluderVisibilityFrame, outError))
    {
        return false;
    }
    // The donor frame has independent zeroed/reset/cull arguments and no depth
    // dependency. Only the main frame can consume the pyramid produced from it.
    if (!m_visibility.Prepare(context, m_frameViewProjection, candidates, bins, m_visibilityFrame, outError,
            bool(m_occlusionPyramid)))
    {
        return false;
    }
    CaptureGeometryRoutes();
    return true;
}

void EnhancedGBufferPass::CaptureGeometryRoutes()
{
    m_geometryRouteAudit.clear();
    m_geometryRouteAudit.reserve(m_batches.size());
    for (const auto& batch : m_batches)
    {
        const bool meshShader = !batch.meshletDraws.empty();
        GeometryRouteAudit route;
        route.geometryKey = batch.geometryKey;
        route.materialSeal = batch.material.snapshot ? batch.material.snapshot->seal.sealHash : 0u;
        route.sourcePipeline = batch.pipeline.id;
        route.recordedPipeline = (meshShader ? batch.meshletPipeline : batch.pipeline).id;
        route.lodCount = meshShader ? static_cast<uint32_t>(batch.meshletDraws.size()) : 1u;
        route.meshShader = meshShader;
        route.occluderPipeline = meshShader
            ? (batch.occluderMeshletDraws.empty() ? 0u : batch.meshletOccluderPipeline.id) : batch.occluderPipeline.id;
        route.occluderLodCount = route.occluderPipeline == 0u ? 0u
            : (meshShader ? static_cast<uint32_t>(batch.occluderMeshletDraws.size()) : 1u);
        m_geometryRouteAudit.push_back(route);
    }
}

const LX::Runtime::GraphicsCompileIdentity* EnhancedGBufferPass::VisibilityContract(
    const EnhancedMaterialDrawSnapshot* snapshot, uint32_t vertexMask) const
{
    const auto generation = ResolveVisibilityGeneration(snapshot, vertexMask);
    return generation ? &generation->shader.compile : nullptr;
}

std::shared_ptr<const LX::Runtime::GraphicsGeneration> EnhancedGBufferPass::ResolveVisibilityGeneration(
    const EnhancedMaterialDrawSnapshot* snapshot, uint32_t vertexMask) const
{
    std::shared_ptr<const LX::Runtime::GraphicsGeneration> generation;
    if (snapshot)
    {
        generation = LX::Runtime::ResolveGraphicsGeneration(snapshot->pipelineGenerations,
            snapshot->shaderMetaHandle, snapshot->permutationKey, snapshot->bindingLayout, vertexMask, false);
    }
    else if (vertexMask == 0)
    {
        generation = m_pipelineRequest.GetGeneration();
    }
    else if (const auto found = m_modelPipelineRequests.find(vertexMask); found != m_modelPipelineRequests.end())
    {
        generation = found->second.GetGeneration();
    }
    return generation;
}

RHIPipelineHandle EnhancedGBufferPass::ResolveMeshletPipeline(const EnhancedFrameContext& context,
    const std::shared_ptr<const LX::Runtime::GraphicsGeneration>& source, std::string& diagnostic)
{
    diagnostic.clear();
    if (!source || !context.resources || !context.psoManager || !context.rootSignatures)
    {
        return {};
    }
    const auto caps = context.resources->GetMeshShaderCapabilities();
    // 64 vertices * a conservatively padded 128-byte VSOut plus primitive
    // outputs fit this profile without relying on a particular driver packing.
    if (!caps.meshShader || !caps.meshIndirect || caps.maxOutputVertices < 64u
        || caps.maxOutputPrimitives < 126u || caps.maxThreadsPerGroup < 64u
        || caps.maxThreadGroupSizeX < 64u || caps.maxThreadGroupSizeY < 1u
        || caps.maxThreadGroupSizeZ < 1u || caps.maxOutputMemoryBytes < 16384u
        || !caps.SupportsDispatch(1u, 1u, 1u))
    {
        return {};
    }
    const auto& identity = source->shader.compile;
    const auto& indexed = source->pipeline.GetDesc();
    if (!GBufferStandardDepthSource(*source)
        || assets::Has(identity.vertexAttributeMask, assets::VertexAttribute::BoneIndices))
    {
        return {};
    }
    if (const auto found = m_meshletPipelines.find(source.get()); found != m_meshletPipelines.end())
    {
        diagnostic = found->second.diagnostic;
        return found->second.pipeline;
    }

    MeshletPipeline candidate;
    candidate.source = source;
    const auto build = [&]() -> bool
    {
        // Re-prove the currently compiled source against this exact accepted
        // indexed generation. A pending source hot reload cannot replace its
        // pixel or vertex semantics behind a retained material snapshot.
        RHIShaderCompiler::ModuleReuseScope module;
        RHIShaderCompiler::VerifiedShader vertexProof, pixelProof, meshShader;
        if (!VerifyGBufferGeneration(identity, vertexProof, pixelProof, candidate.diagnostic))
        {
            return false;
        }
        auto permutation = identity.permutation;
        if (!permutation.Enable("GBUFFER_MESH_SHADER", candidate.diagnostic)
            || !RHIShaderCompiler::VerifyFile(identity.source, "MSMain", "ms_6_5", identity.backend,
                permutation, meshShader, candidate.diagnostic, identity.options))
        {
            return false;
        }
        MaterialTextureTable::Schema textures;
        if (!MaterialTextureTable::FromReflection(pixelProof.reflection, textures, candidate.diagnostic))
        {
            return false;
        }
        std::vector<RHIPipelineLayoutParam> parameters{
            RHILayout::Cbv(0, RHIShaderVisibility::Mesh),
            RHILayout::Srv(4, RHIShaderVisibility::Mesh),
            RHILayout::SrvTable(static_cast<uint32_t>(textures.size()), MaterialTextureTable::FirstRegister,
                RHIShaderVisibility::Pixel),
            RHILayout::SamplerTable(1, 0, RHIShaderVisibility::Pixel),
            RHILayout::Srv(5, RHIShaderVisibility::Mesh),
            RHILayout::Cbv(2, RHIShaderVisibility::Pixel),
            RHILayout::Cbv(3, RHIShaderVisibility::Pixel),
            RHILayout::Srv(6, RHIShaderVisibility::Mesh),
            RHILayout::Srv(7, RHIShaderVisibility::Mesh),
            RHILayout::Srv(8, RHIShaderVisibility::Mesh)};
        bool paletteExtent = false;
        if (!GBufferSkinPaletteExtentContract(meshShader.reflection, paletteExtent, candidate.diagnostic))
        {
            return false;
        }
        if (paletteExtent)
        {
            candidate.paletteExtentRoot = static_cast<std::uint32_t>(parameters.size());
            parameters.push_back(RHILayout::Cbv(4, RHIShaderVisibility::Mesh));
        }
        RHIPipelineLayoutDesc layoutDescription;
        layoutDescription.params = parameters;
        layoutDescription.allowInputAssembler = false;
        const auto layout = context.rootSignatures->GetOrCreate(layoutDescription, candidate.diagnostic);
        if (!layout.IsValid())
        {
            return false;
        }
        RHIMeshPipelineDesc description;
        description.msBytecode = meshShader.bytecode.Data();
        description.msSize = meshShader.bytecode.Size();
        // Reuse the exact accepted pixel bytecode and complete raster state.
        description.psBytecode = indexed.psBytecode;
        description.psSize = indexed.psSize;
        description.layout = layout;
        description.fillMode = indexed.fillMode;
        description.cullMode = indexed.cullMode;
        description.depthEnable = indexed.depthEnable;
        description.blendEnable = indexed.blendEnable;
        description.depthWriteMask = indexed.depthWriteMask;
        description.depthFunc = indexed.depthFunc;
        description.independentBlend = indexed.independentBlend;
        description.numRenderTargets = indexed.numRenderTargets;
        description.dsvFormat = indexed.dsvFormat;
        description.sampleCount = indexed.sampleCount;
        std::copy_n(indexed.renderTargetBlend, 8, description.renderTargetBlend);
        std::copy_n(indexed.rtvFormats, 8, description.rtvFormats);
        candidate.pipeline = context.psoManager->GetOrCreateMesh(description, candidate.diagnostic);
        if (candidate.pipeline.IsValid())
        {
            RHIShaderCompiler::VerifiedShader depthShader;
            if (RHIShaderCompiler::VerifyFile(identity.source, "PSDepthOnly", identity.pixelProfile, identity.backend,
                    identity.permutation, depthShader, candidate.occluderDiagnostic, identity.options))
            {
                description.psBytecode = depthShader.bytecode.Data();
                description.psSize = depthShader.bytecode.Size();
                description.numRenderTargets = 0;
                std::fill_n(description.rtvFormats, 8, RHIFormat::Unknown);
                candidate.occluderPipeline = context.psoManager->GetOrCreateMesh(description, candidate.occluderDiagnostic);
            }
        }
        return candidate.pipeline.IsValid();
    };
    if (!build() && candidate.diagnostic.empty())
    {
        candidate.diagnostic = "Mesh shader prerequisites failed; retaining the indexed GBuffer generation.";
    }
    diagnostic = candidate.diagnostic;
    const auto pipeline = candidate.pipeline;
    m_meshletPipelines.emplace(source.get(), std::move(candidate));
    return pipeline;
}

RHIPipelineHandle EnhancedGBufferPass::ResolveOccluderPipeline(const EnhancedFrameContext& context,
    const std::shared_ptr<const LX::Runtime::GraphicsGeneration>& source, std::string& diagnostic)
{
    diagnostic.clear();
    if (!source || !context.psoManager || !GBufferStandardDepthSource(*source))
    {
        return {};
    }
    if (const auto found = m_occluderPipelines.find(source.get()); found != m_occluderPipelines.end())
    {
        diagnostic = found->second.diagnostic;
        return found->second.pipeline;
    }
    MeshletPipeline candidate;
    candidate.source = source;
    const auto& identity = source->shader.compile;
    RHIShaderCompiler::ModuleReuseScope module;
    RHIShaderCompiler::VerifiedShader vertexProof, pixelProof, depthShader;
    if (VerifyGBufferGeneration(identity, vertexProof, pixelProof, candidate.diagnostic)
        && RHIShaderCompiler::VerifyFile(identity.source, "PSDepthOnly", identity.pixelProfile, identity.backend,
            identity.permutation, depthShader, candidate.diagnostic, identity.options))
    {
        // Preserve the accepted VS bytecode, raster/depth state and root layout.
        // Only the pixel coverage entry and absence of color attachments differ.
        auto description = source->pipeline.GetDesc();
        description.psBytecode = depthShader.bytecode.Data();
        description.psSize = depthShader.bytecode.Size();
        description.numRenderTargets = 0;
        std::fill_n(description.rtvFormats, 8, RHIFormat::Unknown);
        candidate.pipeline = context.psoManager->GetOrCreate(description, candidate.diagnostic);
    }
    diagnostic = candidate.diagnostic;
    const auto pipeline = candidate.pipeline;
    m_occluderPipelines.emplace(source.get(), std::move(candidate));
    return pipeline;
}

bool EnhancedGBufferPass::HasSafeSkinningBounds(
    const EnhancedFrameContext& context, const EnhancedDrawItem& draw, bool& unsafeSkinAccess)
{
    unsafeSkinAccess = false;
    const bool finitePose = shadow_math::FinitePose(draw);
    if (draw.boneCount == 0)
    {
        return finitePose;
    }
    const auto& mesh = draw.modelMeshView;
    // Legacy mutable bytes carry no immutable generation to cache/verify.
    // They retain their instance stream and unknown (non-rejecting) posed bounds.
    if (!mesh.IsComplete() || mesh.vertexStride != assets::StrideOf(mesh.vertexAttributeMask) ||
        !assets::Has(mesh.vertexAttributeMask, assets::VertexAttribute::BoneIndices) ||
        !assets::Has(mesh.vertexAttributeMask, assets::VertexAttribute::BoneWeights))
    {
        return false;
    }
    auto [found, inserted] = m_skinningBounds.try_emplace(mesh.handle);
    auto& contract = found->second;
    if (inserted)
    {
        contract.vertices = mesh.vertexData;
        contract.bytes = mesh.vertexBytes;
        contract.mask = mesh.vertexAttributeMask;
        contract.stride = mesh.vertexStride;
        contract.valid = true;
        for (uint64_t offset = 0; offset < mesh.vertexBytes; offset += mesh.vertexStride)
        {
            const auto* vertex = static_cast<const std::byte*>(mesh.vertexData) + offset;
            std::array<uint8_t, 4> indices;
            std::array<float, 4> weights;
            std::memcpy(indices.data(), vertex + assets::OffsetOf(mesh.vertexAttributeMask,
                assets::VertexAttribute::BoneIndices), sizeof(indices));
            std::memcpy(weights.data(), vertex + assets::OffsetOf(mesh.vertexAttributeMask,
                assets::VertexAttribute::BoneWeights), sizeof(weights));
            double sum = 0;
            for (size_t i = 0; i < weights.size(); ++i)
            {
                if (!std::isfinite(weights[i]) || weights[i] < 0.f || weights[i] > 1.f)
                {
                    contract.valid = false;
                }
                sum += weights[i];
                if (weights[i] > 0.f)
                {
                    contract.requiredBones = (std::max)(contract.requiredBones, uint32_t(indices[i]) + 1u);
                }
            }
            if (!std::isfinite(sum) || (sum != 0.0 &&
                (std::abs(sum - 1.0) > 0.00001 || weights[0] <= 0.f)))
            {
                contract.valid = false;
            }
        }
    }
    if (contract.vertices != mesh.vertexData || contract.bytes != mesh.vertexBytes ||
        contract.mask != mesh.vertexAttributeMask || contract.stride != mesh.vertexStride ||
        contract.requiredBones > draw.boneCount)
    {
        // A canonical skinning route must not retain an out-of-range positive
        // bone fetch or accept mutable bytes under an immutable handle.
        unsafeSkinAccess = true;
        return false;
    }
    if (!contract.valid)
    {
        unsafeSkinAccess = true;
        return false;
    }
    if (!finitePose)
    {
        return false;
    }

    // Bounds must enclose the actual uploaded palette, not merely a source
    // pointer sharing an animator key with a different pose in this frame.
    const auto& offsets = context.animationPalettes ? context.animationPalettes->Offsets() : m_boneOffsets;
    const auto palette = offsets.find(draw.animatorKey);
    if (palette == offsets.end())
    {
        return false;
    }
    const void* uploaded = m_bonePalettes.data();
    uint64_t uploadedBytes = m_bonePalettes.size() * sizeof(PackedBoneMatrix);
    if (context.animationPalettes)
    {
        const auto slice = context.animationPalettes->Upload();
        uploaded = slice.cpuAddress;
        uploadedBytes = slice.size;
    }
    const uint64_t begin = uint64_t(palette->second) * sizeof(PackedBoneMatrix);
    const uint64_t bytes = uint64_t(draw.boneCount) * sizeof(PackedBoneMatrix);
    if (!uploaded || begin > uploadedBytes || bytes > uploadedBytes - begin)
    {
        return false;
    }
    for (uint32_t bone = 0; bone < draw.boneCount; ++bone)
    {
        const auto expected = PackedBoneMatrix::From(draw.bonePalette[bone]);
        if (std::memcmp(static_cast<const std::byte*>(uploaded) + begin +
                uint64_t(bone) * sizeof(PackedBoneMatrix), &expected, sizeof(expected)) != 0)
        {
            return false;
        }
    }
    return true;
}

bool EnhancedGBufferPass::BuildBatches(const EnhancedFrameContext& context, std::string& outError)
{
    m_instances.clear();
    m_instanceBounds.clear();
    m_batches.clear();
    m_lastBatchCount = 0;

    if (nullptr == context.draws)
    {
        return true;
    }

    if (context.draws->size() > (std::numeric_limits<std::uint32_t>::max)())
    {
        outError = "GBuffer draw count exceeds 32-bit instance addressing.";
        return false;
    }
    m_instances.reserve(context.draws->size());

    // 같은 (메시, 재질)을 묶는다.
    //
    // 둘 중 하나라도 다르면 같은 드로우로 묶을 수 없다 — 메시가 다르면 정점·
    // 인덱스 버퍼를, 재질이 다르면 SRV 테이블을 바꿔야 하기 때문이다.
    //
    // ★ 정렬해야 실제로 묶인다.
    //
    // 처음에는 '연속한 같은 것'만 묶었다. 순서를 흔들면 깊이 테스트 결과가
    // 달라질까 봐서였는데, 재 보니 배치 수가 드로우 수와 똑같았다(704 → 704) —
    // 씬의 드로우 순서는 대개 메시가 번갈아 나오므로 연속이 거의 없다.
    // 병합이 통째로 죽어 있었고, 수치를 안 봤으면 몰랐을 것이다.
    //
    // 정렬해도 되는 이유: GBuffer는 불투명만 그리고 깊이 함수가 LESS다.
    // 겹치는 픽셀은 더 가까운 쪽이 이기므로 그리는 순서와 무관하다.
    // (같은 깊이면 순서를 타지만 그건 원래 불안정한 경우다.)
    //
    // 투명 재질이 들어오면 이 전제가 깨진다 — 그때는 불투명만 정렬하고
    // 투명은 뒤에서 앞으로 따로 그려야 한다.
    std::vector<const EnhancedDrawItem*> sorted;
    sorted.reserve(context.draws->size());
    for (const auto& draw : *context.draws)
    {
        if (0 == enhanced_draw::GeometryKey(draw)) continue;

        const auto geometry = m_drawGeometry.find(enhanced_draw::GeometryKey(draw));
        if (geometry == m_drawGeometry.end() || !geometry->second.IsValid()) continue;

        // W8: PrepareFrame이 세대 위반으로 거부한 snapshot은 그리지 않는다.
        // 거부를 PrepareFrame에만 두면 이 순회가 그대로 그려 버린다.
        if (draw.materialSnapshot
            && m_rejectedSnapshots.contains(draw.materialSnapshot.get())) continue;

        sorted.push_back(&draw);
    }

    std::stable_sort(sorted.begin(), sorted.end(),
        [this](const EnhancedDrawItem* a, const EnhancedDrawItem* b)
        {
            if (enhanced_draw::GeometryKey(*a) != enhanced_draw::GeometryKey(*b))
                return enhanced_draw::GeometryKey(*a) < enhanced_draw::GeometryKey(*b);

            const MaterialKey keyA = MakeMaterialKey(*a);
            const MaterialKey keyB = MakeMaterialKey(*b);
            return keyA < keyB;
        });

    for (const auto* drawPtr : sorted)
    {
        const auto& draw = *drawPtr;

        // W8: 이 순회 도중에 거부된 snapshot(아래 Observe)도 다시 만나면
        // 건너뛴다. 같은 위반을 여러 번 세면 장부의 수가 사건 수가 아니게 된다.
        if (draw.materialSnapshot
            && m_rejectedSnapshots.contains(draw.materialSnapshot.get())) continue;

        const MaterialKey key = MakeMaterialKey(draw);
        const auto geometry = m_drawGeometry.find(enhanced_draw::GeometryKey(draw));
        const uint32_t vertexMask = geometry->second.vertexAttributeMask;
        const auto sourceGeneration = ResolveVisibilityGeneration(key.snapshot.get(), vertexMask);
        const auto* contract = sourceGeneration ? &sourceGeneration->shader.compile : nullptr;
        const bool usesVisibleIds = contract && contract->visibleInstanceIds;
        const bool knownDeformation = contract &&
            contract->geometryVisibility == ShaderGeometryVisibility::IndexedInstanceV1;
        bool unsafeSkinAccess = false;
        // Canonical skin weights are a promise of IndexedInstanceV1 only.
        // An arbitrary accepted vertex program may normalize, reinterpret or
        // ignore those attributes; do not reject it using another shader's ABI.
        const bool safePose = knownDeformation && HasSafeSkinningBounds(context, draw, unsafeSkinAccess);
        if (unsafeSkinAccess)
        {
            outError = "GBuffer skin weights/indices are invalid for the supplied palette or its immutable vertex contract changed.";
            return false;
        }
        if (draw.boneCount != 0 && !safePose)
        {
            m_lastSkinningFallback = "Skin bounds are unproven for this pose or legacy layout; preserving the full indexed instance stream.";
        }
        // Indirect execution preserves any accepted VS/PS pipeline. Only
        // compaction needs the versioned visible-ID/deformation promise.
        const bool gpuEligible = context.resources->GetIndirectDrawCapabilities().indexedDraw;
        const bool compactsVisibleIds = usesVisibleIds && knownDeformation && safePose;
        const auto& coverage = key.snapshot ? key.snapshot->coverage : draw.coverage;
        RHIPipelineHandle meshletPipeline;
        math::vector4 meshletLocalSphere{};
        const auto* authoredMeshlets = draw.modelMeshView.Meshlets();
        if (safePose && draw.boneCount == 0 && geometry->second.meshlets.IsValid()
            && authoredMeshlets && draw.modelMeshView.sourceLodIndex == 0
            && geometry->second.meshlets.profileVersion == experiment::kMeshletProfileVersion
            && geometry->second.vertices.size <= (std::numeric_limits<uint32_t>::max)()
            && !assets::Has(vertexMask, assets::VertexAttribute::BoneIndices)
            && (coverage.flags & (EnhancedMaterialCoverage::Masked | EnhancedMaterialCoverage::Blended)) == 0)
        {
            std::string diagnostic;
            meshletPipeline = ResolveMeshletPipeline(context, sourceGeneration, diagnostic);
            if (!diagnostic.empty())
            {
                m_lastMeshletFallback = std::move(diagnostic);
            }
            if (meshletPipeline.IsValid())
            {
                auto [bounds, inserted] = m_meshletLocalBounds.try_emplace(draw.modelMeshView.handle);
                if (inserted)
                {
                    bounds->second = GBufferMeshletLocalBounds(*authoredMeshlets);
                }
                meshletLocalSphere = bounds->second;
            }
        }
        RHIPipelineHandle occluderPipeline, meshletOccluderPipeline;
        bool occlusionEligible = false;
        if (sourceGeneration)
        {
            // The vertex visibility contract alone says nothing about a custom
            // PS writing SV_Depth. Only the shared Standard surface preserves
            // the rasterized depth enclosed by the geometry sphere.
            occlusionEligible = GBufferStandardDepthSource(*sourceGeneration)
                && (coverage.flags & EnhancedMaterialCoverage::Blended) == 0;
        }
        if ((gpuEligible || meshletPipeline.IsValid()) && safePose && draw.modelMeshView.SourceMesh()
            && (coverage.flags & (EnhancedMaterialCoverage::Masked | EnhancedMaterialCoverage::Blended)) == 0)
        {
            std::string diagnostic;
            occluderPipeline = ResolveOccluderPipeline(context, sourceGeneration, diagnostic);
            if (!diagnostic.empty())
            {
                m_lastOcclusionFallback = std::move(diagnostic);
            }
            if (meshletPipeline.IsValid())
            {
                const auto& prepared = m_meshletPipelines.at(sourceGeneration.get());
                meshletOccluderPipeline = prepared.occluderPipeline;
                if (!prepared.occluderDiagnostic.empty())
                {
                    m_lastOcclusionFallback = prepared.occluderDiagnostic;
                }
            }
        }
        // Unknown custom position semantics and malformed/legacy poses cannot
        // borrow a bind-pose sphere as a rejection proof. Their GPU bin keeps
        // the entire original instance stream visible, without a shader rewrite.
        const auto bounds = knownDeformation && safePose
            ? shadow_math::WorldBounds(draw) : shadow_math::Sphere{};
        if (!gpuEligible && !meshletPipeline.IsValid() && !shadow_math::IntersectsClip(bounds, m_frameViewProjection))
        {
            continue;
        }

        if (m_batches.empty()
            || m_batches.back().geometryKey != enhanced_draw::GeometryKey(draw)
            || m_batches.back().material != key
            || m_batches.back().gpuEligible != gpuEligible
            || m_batches.back().compactsVisibleIds != compactsVisibleIds
            || m_batches.back().meshletPipeline != meshletPipeline
            || m_batches.back().occluderPipeline != occluderPipeline
            || m_batches.back().occlusionEligible != occlusionEligible)
        {
            DrawBatch batch{};
            batch.geometryKey = enhanced_draw::GeometryKey(draw);
            batch.material = key;
            batch.usesVisibleIds = usesVisibleIds;
            batch.paletteExtentRoot = contract ? contract->gbufferSkinPaletteExtentV1Root : UINT32_MAX;
            batch.meshletPaletteExtentRoot = meshletPipeline.IsValid()
                ? m_meshletPipelines.at(sourceGeneration.get()).paletteExtentRoot : UINT32_MAX;
            batch.compactsVisibleIds = compactsVisibleIds;
            batch.gpuEligible = gpuEligible;
            batch.meshletPipeline = meshletPipeline;
            batch.meshletLocalSphere = meshletLocalSphere;
            batch.occluderPipeline = occluderPipeline;
            batch.meshletOccluderPipeline = meshletOccluderPipeline;
            batch.occlusionEligible = occlusionEligible;
            // I5-D34a: 메시 바인딩의 마스크가 레이아웃 축이다. 배치는 메시별로
            // 갈리므로 마스크가 배치 안에서 섞일 수 없다.
            const auto geometry = m_drawGeometry.find(enhanced_draw::GeometryKey(draw));
            const uint32_t vertexMask = geometry != m_drawGeometry.end()
                ? geometry->second.vertexAttributeMask : 0;
            if (key.snapshot)
            {
                std::shared_ptr<const ShaderMetaBindingLayout> ignoredLayout;
                ResolveShaderVariant(*key.snapshot, vertexMask,
                    batch.pipeline, ignoredLayout);
            }
            else
            {
                if (0 == vertexMask)
                    batch.pipeline = m_pipelineRequest.GetHandle();
                else if (const auto model = m_modelPipelineRequests.find(vertexMask);
                    model != m_modelPipelineRequests.end()) batch.pipeline = model->second.GetHandle();
            }
            // W8: 이 배치가 무엇으로 그려질지 확정된 자리다. 같은 값 신원이
            // 한 프레임 안에서 다른 PSO나 다른 texture 묶음으로 갈리면 그것이
            // 곧 세대 혼합이다 — 그 자리에서 배치를 버린다.
            if (key.snapshot)
            {
                EnhancedDrawSealLedger::Binding binding{};
                binding.textureDigest = EnhancedMaterialSeal::ComputeTextureDigest(
                    key.snapshot->textureBindings);
                // W7 — 패스 전역 하나가 아니라 **이 배치가 걸 것**의 신원이다.
                // 배치 키가 샘플러로 갈리므로 이 값과 아래 SetSamplers 의 테이블은
                // 같은 key.sampler 에서 나온다.
                binding.samplerIdentity = EnhancedMaterialSeal::ComputeSamplerIdentity(
                    key.sampler.ToDesc());
                // Source-generation identity stays stable across legitimate
                // indexed/mesh front-end choices. CaptureGeometryRoutes records
                // and Record verifies the actual prepared derivative PSO.
                binding.pipelineId = batch.pipeline.id;
                // W0 — 이 배치의 descriptor 가 어느 버전에서 잘렸는지. 기록만 하고
                // 신원(operator==)에는 넣지 않는다(장부 주석 참조).
                binding.descriptorVersion = nullptr != context.resources
                    ? context.resources->GetDescriptorVersionToken() : 0ull;
                if (!m_sealLedger.Observe(key.snapshot->seal.sealHash, binding))
                {
                    m_rejectedSnapshots.insert(key.snapshot.get());
                    continue;
                }
            }
            batch.firstInstance = static_cast<uint32_t>(m_instances.size());
            batch.instanceCount = 0;
            m_batches.push_back(batch);
        }

        InstanceData instance{};
        instance.world = math::transpose(draw.worldMatrix);
        instance.baseColorFactor = draw.baseColorFactor;
        instance.metallic = draw.metallic;
        instance.roughness = draw.roughness;
        // 제품 draw는 immutable material snapshot이 의미 상태의 정본이다.
        // draw.useNormalMap은 snapshot이 없는 격리 fixture 호환 경계에만 남는다.
        instance.useNormalMap = key.snapshot
            ? key.snapshot->useNormalMap : draw.useNormalMap;
        instance.coverageFlags = coverage.flags;
        instance.coverageCutoff = coverage.cutoff;
        instance.usePropertyBlock = key.snapshot ? 1u : 0u;

        // 스키닝 오프셋. 팔레트가 없으면 kNoSkinning으로 남아 셰이더가
        // 바인드 포즈로 그린다 — 스킨드와 비스킨드가 한 배치에 섞여도 된다.
        instance.boneOffset = kNoSkinning;
        if (nullptr != draw.bonePalette && 0 != draw.boneCount)
        {
            const auto& offsets = context.animationPalettes
                ? context.animationPalettes->Offsets() : m_boneOffsets;
            const auto found = offsets.find(draw.animatorKey);
            if (found != offsets.end())
            {
                const uint64_t paletteCount = context.animationPalettes
                    ? context.animationPalettes->Upload().size / sizeof(PackedBoneMatrix)
                    : m_bonePalettes.size();
                if (found->second > paletteCount || draw.boneCount > paletteCount - found->second
                    || draw.boneCount > (std::numeric_limits<uint32_t>::max)() - found->second)
                {
                    outError = "GBuffer skin palette offset/count is outside the sealed upload.";
                    return false;
                }
                instance.boneOffset = found->second;
                // Only the explicit instance contract owns this former padding
                // word. Direct wrappers use the separate palette-extent uniform.
                instance.boneCount = knownDeformation ? draw.boneCount : 0u;
            }
        }

        m_instances.push_back(instance);
        m_instanceBounds.emplace_back(bounds.center.x, bounds.center.y, bounds.center.z, bounds.radius);
        ++m_batches.back().instanceCount;
    }

    m_lastBatchCount = static_cast<uint32_t>(m_batches.size());
    CaptureGeometryRoutes();
    return true;
}

bool EnhancedGBufferPass::BuildPipelineDesc(const EnhancedFrameContext& context,
    const char* shaderFile, const char* vertexEntry, const char* pixelEntry,
    const ShaderRenderState* renderState,
    const RHIShaderPermutation& permutation, uint32_t experimentMask,
    RHIGraphicsPipelineDesc& outDesc,
    RHIShaderBlob& outVs, RHIShaderBlob& outPs, std::string& outError,
    LX::Runtime::CompiledGraphics* compiled, ShaderGeometryVisibility visibilityContract)
{
    // I5-D34a/b: experiment 짝은 호출자의 퍼뮤테이션 위에 레이아웃 매크로를
    // 얹는다. 키워드 축과 독립인 별도 축이라 여기서 합성한다 — 호출자마다
    // 얹게 하면 하나가 빠뜨렸을 때 화면이 조용히 틀린다. 매크로는 마스크에서
    // 유도한다(스킨 유무) — 마스크와 매크로가 갈리면 레이아웃과 VSIn이
    // 어긋나 PSO 생성이 거부된다.
    RHIShaderPermutation experimentPermutation;
    const RHIShaderPermutation* effectivePermutation = &permutation;
    if (0 != experimentMask)
    {
        experimentPermutation = permutation;
        if (!ModelVertexInput::ApplyShaderPermutation(experimentMask,
                experimentPermutation, outError))
            return false;
        effectivePermutation = &experimentPermutation;
    }

    LX::Runtime::CompiledGraphics verified;
    if (!LX::Runtime::CompileGraphics(shaderFile, vertexEntry, pixelEntry,
            *effectivePermutation, {}, verified, outError)) return false;
    verified.identity.vertexAttributeMask = experimentMask;
    outVs = std::move(verified.vertex.bytecode);
    outPs = std::move(verified.pixel.bytecode);

    // 루트 시그니처는 캐시가 식별자를 준다 — 손번호를 붙이지 않는 것이 3-4의 계약이다.
    //
    // 상수는 디스크립터 테이블이 아니라 루트 CBV로 넘긴다. 업로드 링에서 자른
    // 조각의 GPU 주소를 그대로 꽂으면 되므로 디스크립터를 만들 필요가 없고,
    // 드로우마다 바뀌는 값에는 이쪽이 싸다(테이블은 디스크립터 힙을 거친다).
    // 인스턴스 버퍼와 본 팔레트는 루트 SRV로 넘긴다. 디스크립터를 만들 필요
    // 없이 업로드 링의 GPU 주소를 그대로 꽂으면 되고, 본 팔레트는 프레임당
    // 한 번이면 배치가 몇 개든 그대로 쓴다 — 인스턴스가 자기 오프셋을
    // 들고 있어서다.
    MaterialTextureTable::Schema textureSchema;
    if (!MaterialTextureTable::FromReflection(verified.pixel.reflection, textureSchema, outError)) return false;
    std::vector<RHIPipelineLayoutParam> params = {
        RHILayout::Cbv(0, RHIShaderVisibility::Vertex),          // b0 — 프레임 상수
        RHILayout::Srv(4, RHIShaderVisibility::Vertex),          // t4 — 인스턴스 데이터
        RHILayout::SrvTable(static_cast<uint32_t>(textureSchema.size()),
            MaterialTextureTable::FirstRegister, RHIShaderVisibility::Pixel),   // baseColor · normal · occRoughMetal · emissive
        RHILayout::SamplerTable(1, 0, RHIShaderVisibility::Pixel),
        RHILayout::Srv(5, RHIShaderVisibility::Vertex),          // t5 — 본 팔레트
        RHILayout::Cbv(2, RHIShaderVisibility::Pixel),           // b2 — M6 Material property block
        RHILayout::Cbv(3, RHIShaderVisibility::Pixel), // texture coordinates by reflected register
    };
    // Reflection verifies binding shape only. Semantic eligibility is an
    // explicit versioned ShaderMeta promise retained with this generation.
    const bool visibleIds = std::any_of(verified.vertex.reflection.resources.begin(),
        verified.vertex.reflection.resources.end(), [](const auto& resource) {
            return resource.name == "gVisibleInstanceIds" &&
                   resource.kind == RHIShaderResourceKind::StructuredBuffer &&
                   resource.registerIndex == 6 && resource.registerSpace == 0 && resource.arrayElements == 1;
        });
    if (visibilityContract != ShaderGeometryVisibility::Direct && !visibleIds)
    {
        outError = "indexed-instance-v1 requires StructuredBuffer<uint> gVisibleInstanceIds at t6/space0";
        return false;
    }
    verified.identity.visibleInstanceIds = visibleIds;
    verified.identity.geometryVisibility = visibilityContract;
    if (visibleIds)
    {
        params.push_back(RHILayout::Srv(6, RHIShaderVisibility::Vertex));
    }
    bool paletteExtent = false;
    if (!GBufferSkinPaletteExtentContract(verified.vertex.reflection, paletteExtent, outError))
    {
        return false;
    }
    if (paletteExtent)
    {
        verified.identity.gbufferSkinPaletteExtentV1Root = static_cast<std::uint32_t>(params.size());
        params.push_back(RHILayout::Cbv(4, RHIShaderVisibility::Vertex));
    }

    RHIPipelineLayoutDesc rootDesc{};
    rootDesc.params = params;
    rootDesc.allowInputAssembler = true;

    const auto root = context.rootSignatures->GetOrCreate(rootDesc, outError);
    if (!root.IsValid()) return false;

    // 입력 레이아웃. 정점 구조체와 순서가 맞아야 하고, 어긋나면 검증 레이어가
    // 잡아 주지 않는 경우도 있어 화면이 조용히 이상해진다.
    // 오프셋은 엔진 Vertex 구조체를 그대로 따른다:
    //   position 0 · normal 12 · uv0 24 · uv1 32 · tangent 40 · bitangent 52
    //   · boneIndices 64 · boneWeights 80
    // 어긋나면 검증 레이어가 잡아 주지 않는 경우도 있어 화면이 조용히 이상해진다.
    static const RHIInputElement kInputElements[] = {
        { "POSITION",     0, RHIFormat::RGB32Float,    0,  0, 0 },
        { "NORMAL",       0, RHIFormat::RGB32Float,    0, 12, 0 },
        { "TEXCOORD",     0, RHIFormat::RG32Float,       0, 24, 0 },
        { "TANGENT",      0, RHIFormat::RGB32Float,    0, 40, 0 },
        { "BINORMAL",     0, RHIFormat::RGB32Float,    0, 52, 0 },
        // 본 인덱스가 float4인 것은 엔진 Vertex를 그대로 따르는 것이다.
        // UINT4로 읽으면 float 비트를 정수로 해석해 팔레트 밖을 짚는다.
        { "BLENDINDICES", 0, RHIFormat::RGBA32Float, 0, 64, 0 },
        { "BLENDWEIGHT",  0, RHIFormat::RGBA32Float, 0, 80, 0 },
    };

    // 오프셋이 Vertex와 어긋나면 조용히 틀리므로 컴파일 시점에 못박는다.
    static_assert(offsetof(Vertex, normal) == 12, "Vertex 레이아웃이 바뀌었다 — 입력 요소 오프셋을 맞출 것");
    static_assert(offsetof(Vertex, uv0) == 24, "Vertex 레이아웃이 바뀌었다");
    static_assert(offsetof(Vertex, tangent) == 40, "Vertex 레이아웃이 바뀌었다");
    static_assert(offsetof(Vertex, bitangent) == 52, "Vertex 레이아웃이 바뀌었다");
    static_assert(offsetof(Vertex, boneIndices) == 64, "Vertex 레이아웃이 바뀌었다");
    static_assert(offsetof(Vertex, boneWeights) == 80, "Vertex 레이아웃이 바뀌었다");

    outDesc = {};
    if (0 != experimentMask)
    {
        const std::vector<RHIInputElement>* elements =
            ModelVertexInput::ResolveInputElements(experimentMask, outError);
        if (nullptr == elements)
        {
            outError = "GBuffer model 입력 레이아웃 유도 실패: " + outError;
            return false;
        }
        outDesc.inputElements = elements->data();
        outDesc.inputElementCount = static_cast<uint32_t>(elements->size());
    }
    else
    {
        outDesc.inputElements = kInputElements;
        outDesc.inputElementCount = _countof(kInputElements);
    }
    outDesc.vsBytecode = outVs.Data();
    outDesc.vsSize = outVs.Size();
    outDesc.psBytecode = outPs.Data();
    outDesc.psSize = outPs.Size();
    outDesc.layout = root;
    outDesc.depthEnable = true;
    outDesc.cullMode = RHICullMode::None;
    outDesc.numRenderTargets = kRenderTargetCount;
    for (uint32_t i = 0; i < kRenderTargetCount; ++i)
    {
        outDesc.rtvFormats[i] = GetRenderTargetFormat(i);
    }
    outDesc.dsvFormat = kDepthFormat;

    if (nullptr != renderState) renderState->ApplyTo(outDesc);
    // W4 coverage owns sidedness per instance, including depth writes.
    outDesc.cullMode = RHICullMode::None;
    if (compiled) *compiled = std::move(verified);
    return true;
}

bool EnhancedGBufferPass::CreatePipeline(const EnhancedFrameContext& context, std::string& outError)
{
    RHIShaderBlob vsBlob;
    RHIShaderBlob psBlob;
    RHIGraphicsPipelineDesc desc{};
    const RHIShaderPermutation emptyPermutation;
    if (!MaterialTextureTable::Reflect(kGBufferShaderFile, "PSMain", emptyPermutation,
            m_legacyTextureSchema, outError)) return false;
    LX::Runtime::CompiledGraphics compiled;
    if (!BuildPipelineDesc(context, kGBufferShaderFile, "VSMain", "PSMain", nullptr,
            emptyPermutation, 0, desc, vsBlob, psBlob, outError, &compiled,
            ShaderGeometryVisibility::IndexedInstanceV1))
    {
        return false;
    }
    LX::Runtime::GraphicsShaderDescription shader;
    shader.compile = std::move(compiled.identity);
    if (!m_pipelineRequest.Create(*context.psoManager, desc, std::move(shader), outError))
    {
        return false;
    }

    for (std::size_t i = 0; i < assets::kModelVertexMasks.size(); ++i)
    {
        // desc가 shader blob을 빌리므로 mask마다 blob 수명을 Create까지 보존한다.
        RHIShaderBlob modelVsBlob;
        RHIShaderBlob modelPsBlob;
        RHIGraphicsPipelineDesc modelDesc{};
        if (!BuildPipelineDesc(context, kGBufferShaderFile, "VSMain", "PSMain",
                nullptr, emptyPermutation, assets::kModelVertexMasks[i], modelDesc,
                modelVsBlob, modelPsBlob, outError, &compiled,
                ShaderGeometryVisibility::IndexedInstanceV1))
        {
            return false;
        }
        LX::Runtime::GraphicsShaderDescription modelShader;
        modelShader.compile = std::move(compiled.identity);
        if (!m_modelPipelineRequests[assets::kModelVertexMasks[i]].Create(
                *context.psoManager, modelDesc, std::move(modelShader), outError))
        {
            return false;
        }
    }
    return true;
}

bool EnhancedGBufferPass::BuildShaderMetaPipelineDesc(
    const EnhancedFrameContext& context, const ShaderMeta& meta,
    std::span<const std::uint16_t> keywordSelections, uint32_t experimentMask,
    RHIGraphicsPipelineDesc& outDesc, RHIShaderBlob& outVs,
    RHIShaderBlob& outPs, RHIShaderPermutationKey& outPermutationKey,
    std::shared_ptr<const ShaderMetaBindingLayout>& outLayout,
    std::string& outError, LX::Runtime::GraphicsShaderDescription* shader, ShaderMetaHandle ownerHandle)
{
    const auto passIt = std::find_if(meta.passes.begin(), meta.passes.end(),
        [](const ShaderPassDesc& pass) { return pass.name == "GBuffer"; });
    if (passIt == meta.passes.end())
    {
        outError = "GBuffer ShaderMeta에 GBuffer pass가 없다";
        return false;
    }

    const ShaderPassDesc& pass = *passIt;
    if (pass.IsCompute() || !pass.vertex || !pass.pixel
        || ShaderPassQueue::Opaque != pass.queue)
    {
        outError = "GBuffer ShaderMeta pass는 opaque VS+PS graphics여야 한다";
        return false;
    }

    const std::uint32_t passIndex = static_cast<std::uint32_t>(
        std::distance(meta.passes.begin(), passIt));
    ShaderMetaPermutation permutation;
    if (!ShaderPermutationDomain::Resolve(meta, passIndex, keywordSelections,
            permutation, outError))
    {
        return false;
    }

    std::filesystem::path shaderPath = meta.source;
    if (!meta.originPath.empty())
    {
        std::error_code pathError;
        shaderPath = std::filesystem::relative(meta.ResolveSource(meta.originPath),
            RHIShaderSource::Resolve(""), pathError);
        const auto first = shaderPath.begin();
        if (pathError || shaderPath.empty() || shaderPath.is_absolute()
            || (first != shaderPath.end() && *first == ".."))
        {
            outError = "GBuffer ShaderMeta source가 shader root 밖이다";
            return false;
        }
    }

    const std::string shaderFile = shaderPath.generic_string();
    if (shaderFile.empty() || pass.vertex->entry.empty() || pass.pixel->entry.empty())
    {
        outError = "GBuffer ShaderMeta source/entry가 비었다";
        return false;
    }

    LX::Runtime::CompiledGraphics compiled;
    if (!BuildPipelineDesc(context, shaderFile.c_str(), pass.vertex->entry.c_str(),
            pass.pixel->entry.c_str(), &pass.state, permutation.defines,
            experimentMask, outDesc, outVs, outPs, outError, &compiled, pass.geometryVisibility))
    {
        return false;
    }

    std::shared_ptr<const ShaderMetaBindingLayout> candidateLayout;
    if (!meta.properties.empty())
    {
        const RHIShaderReflection reflections[]{compiled.vertex.reflection, compiled.pixel.reflection};

        ShaderMetaBindingLayout layout;
        if (!ShaderMetaReflection::Resolve(meta, reflections, layout, outError))
            return false;
        if (layout.constantBufferName.empty()
            || 2 != layout.constantBufferRegister
            || 0 != layout.constantBufferSpace
            || 0 == layout.constantBufferByteSize)
        {
            outError = "GBuffer Material property layout은 b2/space0이어야 한다";
            return false;
        }
        if (!MaterialTextureTable::ValidateLayout(layout, reflections[1], outError)) return false;
        candidateLayout = std::make_shared<ShaderMetaBindingLayout>(std::move(layout));
    }

    if (shader)
    {
        LX::Runtime::GraphicsShaderDescription candidate;
        const ShaderMetaBindingLayout empty;
        if (!LX::Runtime::CreateCodeShader(meta, candidateLayout ? *candidateLayout : empty,
                ownerHandle, candidate.shader, outError)) return false;
        candidate.compile = std::move(compiled.identity);
        candidate.materialPermutationKey = permutation.key;
        *shader = std::move(candidate);
    }
    outPermutationKey = permutation.key;
    outLayout = std::move(candidateLayout);
    return true;
}

bool EnhancedGBufferPass::BuildVariantCandidate(const EnhancedFrameContext& context,
    const ShaderMeta& meta, std::span<const std::uint16_t> selections,
    ShaderVariant& candidate, RHIShaderPermutationKey& key, std::string& error, ShaderMetaHandle ownerHandle)
{
    // A failed mask must not publish any part of this material generation.
    for (size_t i = 0; i <= assets::kModelVertexMasks.size(); ++i)
    {
        const uint32_t mask = i == 0 ? 0 : assets::kModelVertexMasks[i - 1];
        RHIShaderBlob vs, ps;
        RHIGraphicsPipelineDesc desc{};
        RHIShaderPermutationKey resolved{};
        LX::Runtime::GraphicsShaderDescription shader;
        std::shared_ptr<const ShaderMetaBindingLayout> layout;
        if (!BuildShaderMetaPipelineDesc(context, meta, selections, mask,
                desc, vs, ps, resolved, layout, error, &shader, ownerHandle)) return false;
        if (i == 0) { key = resolved; candidate.layout = layout; }
        // Bootstrap ShaderMeta has no properties, so all masks may have no layout.
        else if (bool(layout) != bool(candidate.layout)
            || (layout && *layout != *candidate.layout))
        { error = "GBuffer vertex mask changed material layout"; return false; }
        auto& request = i == 0 ? candidate.request : candidate.modelRequests[mask];
        if (!request.Create(*context.psoManager, desc, std::move(shader), error)) return false;
    }
    return true;
}

void EnhancedGBufferPass::RetireUnusedPipelines(const EnhancedFrameContext& context,
    const std::vector<RHIPipelineHandle>& candidates, RHICompletionPoint retireAfter)
{
    const auto contains = [](const auto& requests, RHIPipelineHandle pipeline) {
        return std::any_of(requests.begin(), requests.end(), [pipeline](const auto& entry) {
            return entry.second.GetHandle() == pipeline;
        });
    };
    std::vector<uint32_t> invalidated;
    for (const auto pipeline : candidates)
    {
        if (!pipeline.IsValid() || pipeline == m_pipelineRequest.GetHandle()
            || contains(m_modelPipelineRequests, pipeline)
            || std::find(invalidated.begin(), invalidated.end(), pipeline.id) != invalidated.end()
            || std::any_of(m_shaderVariants.begin(), m_shaderVariants.end(), [&](const auto& entry) {
                return entry.second.request.GetHandle() == pipeline || contains(entry.second.modelRequests, pipeline);
            })
            || std::any_of(m_meshletPipelines.begin(), m_meshletPipelines.end(), [&](const auto& entry) {
                return entry.second.pipeline == pipeline || entry.second.occluderPipeline == pipeline;
            })
            || std::any_of(m_occluderPipelines.begin(), m_occluderPipelines.end(), [&](const auto& entry) {
                return entry.second.pipeline == pipeline;
            })) continue;
        context.psoManager->InvalidatePipeline(pipeline, retireAfter);
        invalidated.push_back(pipeline.id);
    }
}

bool EnhancedGBufferPass::ApplyShaderMeta(const EnhancedFrameContext& context,
    ShaderMetaHandle handle, const ShaderMeta& meta,
    RHICompletionPoint retireAfter, std::string& outError)
{
    if (!handle.IsValid()) { outError = "Missing GBuffer ShaderMeta generation"; return false; }
    if (handle == m_shaderMetaHandle) return true;
    ShaderVariant candidate;
    RHIShaderPermutationKey key{};
    const std::vector<std::uint16_t> selections(meta.keywords.size(), 0);
    if (!BuildVariantCandidate(context, meta, selections, candidate, key, outError, handle)) return false;
    std::vector<RHIPipelineHandle> removed{m_pipelineRequest.GetHandle()};
    for (const auto& [mask, request] : m_modelPipelineRequests) removed.push_back(request.GetHandle());
    if (m_shaderMetaHandle.IsValid() && m_shaderBindingLayout)
    {
        ShaderVariant previous;
        previous.request = std::move(m_pipelineRequest);
        previous.modelRequests = std::move(m_modelPipelineRequests);
        previous.layout = m_shaderBindingLayout;
        // A later material/texture seal can still fail. Keep the last accepted
        // generation until CommitShaderMetaFrame accepts the complete frame.
        m_shaderVariants.insert_or_assign({m_shaderMetaHandle, m_defaultPermutationKey}, std::move(previous));
    }
    m_pipelineRequest = std::move(candidate.request);
    m_modelPipelineRequests = std::move(candidate.modelRequests);
    m_shaderBindingLayout = std::move(candidate.layout);
    m_shaderMetaHandle = handle; m_defaultPermutationKey = key;
    RetireUnusedPipelines(context, removed, retireAfter);
    return true;
}

bool EnhancedGBufferPass::EnsureShaderMetaVariant(
    const EnhancedFrameContext& context, ShaderMetaHandle handle,
    const ShaderMeta& meta,
    std::span<const std::uint16_t> keywordSelections,
    RHIShaderPermutationKey& outPermutationKey,
    std::shared_ptr<const ShaderMetaBindingLayout>& outLayout,
    std::string& outError)
{
    if (!handle.IsValid())
    {
        outError = "GBuffer material variant의 ShaderMeta generation이 비었다";
        return false;
    }

    const auto passIt = std::find_if(meta.passes.begin(), meta.passes.end(),
        [](const ShaderPassDesc& pass) { return pass.name == "GBuffer"; });
    if (passIt == meta.passes.end())
    {
        outError = "GBuffer material variant에 GBuffer pass가 없다";
        return false;
    }
    const std::uint32_t passIndex = static_cast<std::uint32_t>(
        std::distance(meta.passes.begin(), passIt));
    ShaderMetaPermutation resolved;
    if (!ShaderPermutationDomain::Resolve(meta, passIndex, keywordSelections,
            resolved, outError))
    {
        return false;
    }

    outPermutationKey = resolved.key;
    if (handle == m_shaderMetaHandle && resolved.key == m_defaultPermutationKey)
    {
        outLayout = m_shaderBindingLayout;
        return nullptr != outLayout && m_pipelineRequest.IsValid();
    }

    const ShaderVariantKey key{ handle, resolved.key };
    const auto existing = m_shaderVariants.find(key);
    if (existing != m_shaderVariants.end())
    {
        outLayout = existing->second.layout;
        return existing->second.request.IsValid() && nullptr != outLayout;
    }

    ShaderVariant candidate;
    RHIShaderPermutationKey candidateKey{};
    if (!BuildVariantCandidate(context, meta, keywordSelections, candidate, candidateKey, outError, handle)) return false;
    if (candidateKey != resolved.key || !candidate.layout)
    { outError = "GBuffer material permutation/layout mismatch"; return false; }
    auto [inserted, accepted] = m_shaderVariants.emplace(key, std::move(candidate));
    if (!accepted)
    {
        outError = "GBuffer material variant cache insert가 충돌했다";
        return false;
    }
    outLayout = inserted->second.layout;
    outPermutationKey = candidateKey;
    outError.clear();
    return true;
}

std::uint32_t EnhancedGBufferPass::CommitShaderMetaFrame(
    const EnhancedFrameContext& context,
    std::span<const ShaderMetaHandle> activeHandles,
    RHICompletionPoint retireAfter)
{
    if (nullptr == context.psoManager) return 0;
    const auto isActive = [activeHandles](ShaderMetaHandle handle)
    {
        return std::find(activeHandles.begin(), activeHandles.end(), handle)
            != activeHandles.end();
    };

    std::vector<RHIPipelineHandle> removedPipelines;
    std::uint32_t removedKeys = 0;
    for (auto it = m_shaderVariants.begin(); it != m_shaderVariants.end();)
    {
        if (!isActive(it->first.meta))
        {
            removedPipelines.push_back(it->second.request.GetHandle());
            for (const auto& [mask, request] : it->second.modelRequests)
                removedPipelines.push_back(request.GetHandle());
            it = m_shaderVariants.erase(it);
            ++removedKeys;
        }
        else
        {
            ++it;
        }
    }

    for (auto it = m_meshletPipelines.begin(); it != m_meshletPipelines.end();)
    {
        const auto& source = it->second.source;
        if (source->shader.shader && !isActive(source->shader.shader->codeHandle))
        {
            removedPipelines.push_back(it->second.pipeline);
            removedPipelines.push_back(it->second.occluderPipeline);
            it = m_meshletPipelines.erase(it);
        }
        else
        {
            ++it;
        }
    }

    for (auto it = m_occluderPipelines.begin(); it != m_occluderPipelines.end();)
    {
        const auto& source = it->second.source;
        if (source->shader.shader && !isActive(source->shader.shader->codeHandle))
        {
            removedPipelines.push_back(it->second.pipeline);
            it = m_occluderPipelines.erase(it);
        }
        else
        {
            ++it;
        }
    }

    RetireUnusedPipelines(context, removedPipelines, retireAfter);
    return removedKeys;
}

bool EnhancedGBufferPass::Initialize(const EnhancedFrameContext& context, std::string& outError)
{
    if (nullptr == context.resources || nullptr == context.psoManager ||
        nullptr == context.rootSignatures)
    {
        outError = "GBuffer 패스 컨텍스트가 불완전하다";
        return false;
    }

    if (!CreatePipeline(context, outError)) return false;

    const RHISamplerDesc sampler = RHISampler::Linear(RHIAddressMode::Wrap);
    // W8: 무엇을 걸었는지를 값으로 남긴다. 재질별 sampler(W7 잔여)가 들어오면
    // 이 자리가 그대로 재질 축이 된다.
    m_samplerIdentity = EnhancedMaterialSeal::ComputeSamplerIdentity(sampler);

    m_sampler = context.resources->CreateSamplers({ &sampler, 1 });
    if (!m_sampler.IsValid())
    {
        outError = "GBuffer 샘플러 생성 실패";
        return false;
    }

    // 기본값 샘플러를 캐시에 심어 둔다 — 샘플러를 선언하지 않은 자산은
    // assets::TextureSampler{} 로 오고, 그 값이 여기 있는 desc 와 같다.
    m_samplerTables.clear();
    m_samplerTables.emplace(assets::TextureSampler{}, m_sampler);
    return true;
}

RHISamplerTable EnhancedGBufferPass::SamplerTableFor(
    const EnhancedFrameContext& context, const assets::TextureSampler& sampler)
{
    if (const auto found = m_samplerTables.find(sampler);
        found != m_samplerTables.end())
    {
        return found->second;
    }
    // ★ 만들지 못하면 기본 테이블로 간다. 샘플러 하나 때문에 draw 를 버리면
    //   화면에서 물체가 사라지는데, 그 손실은 wrap 이 틀린 것보다 크다 —
    //   대신 장부의 samplerIdentity 는 **걸려던 것**을 적으므로 어긋남이
    //   게이트에 남는다.
    if (nullptr == context.resources) return m_sampler;
    const RHISamplerDesc desc = sampler.ToDesc();
    const RHISamplerTable table = context.resources->CreateSamplers({ &desc, 1 });
    if (!table.IsValid()) return m_sampler;
    m_samplerTables.emplace(sampler, table);
    return table;
}

void EnhancedGBufferPass::DeclareGraphTargets(EnhancedRenderGraph& graph, const EnhancedFrameContext& context)
{
    static const char* names[kRenderTargetCount] = {
        "GBuffer.Diffuse", "GBuffer.MetalRough", "GBuffer.Normal",
        "GBuffer.Emissive", "GBuffer.Bitmask"};
    std::array<RGHandle, kRenderTargetCount> colors{};
    std::vector<EnhancedRenderGraph::RGPassUsage> usages;
    const auto access = graph.GetSchedulingMode() == RGSchedulingMode::DeclarationOrder
        ? RGAccessMode::LegacyState : RGAccessMode::Write;
    for (uint32_t index = 0; index < kRenderTargetCount; ++index)
    {
        RGTextureDesc desc{};
        desc.width = context.width;
        desc.height = context.height;
        desc.format = GetRenderTargetFormat(index);
        desc.allowRenderTarget = true;
        desc.name = names[index];
        colors[index] = graph.CreateTexture(desc);
        if (graph.GetSchedulingMode() == RGSchedulingMode::ExplicitVersioned)
        {
            colors[index] = graph.Write(colors[index]);
        }
        usages.push_back({colors[index], RHIResourceState::RenderTarget, access});
    }
    RGTextureDesc desc{};
    desc.width = context.width;
    desc.height = context.height;
    desc.format = kDepthFormat;
    desc.allowDepthStencil = true;
    desc.name = "GBuffer.Depth";
    auto depth = graph.CreateTexture(desc);
    if (graph.GetSchedulingMode() == RGSchedulingMode::ExplicitVersioned)
    {
        depth = graph.Write(depth);
    }
    usages.push_back({depth, RHIResourceState::DepthWrite, access});
    m_outputs = {colors[0], colors[1], colors[2], colors[3], colors[4], depth};
    graph.AddPass("GBuffer.Clear", usages,
        [colors, depth, &context](const EnhancedRenderGraph::ExecuteContext& execution)
        {
            std::array<RHITextureHandle, kRenderTargetCount> handles{};
            for (uint32_t index = 0; index < kRenderTargetCount; ++index)
            {
                handles[index] = execution.ResolveHandle(colors[index]);
            }
            const auto depthTarget = RHIDepthTargetDesc::Depth(execution.ResolveHandle(depth), kDepthFormat);
            const auto targets = context.resources->CreateRenderTargets(handles, &depthTarget);
            if (!targets.IsValid())
            {
                throw std::runtime_error("Graph GBuffer clear targets are unavailable.");
            }
            auto& encoder = *execution.encoder;
            encoder.BindRenderTargets(targets);
            constexpr float zero[4]{};
            encoder.ClearRenderTargets(targets, zero);
            encoder.ClearDepthTarget(targets, 1.f);
        });
}

void EnhancedGBufferPass::Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context)
{
    const bool hasDonors = std::any_of(m_batches.begin(), m_batches.end(), [](const auto& batch) {
        return batch.meshletDraws.empty() ? batch.occluderPipeline.IsValid()
            : (batch.meshletOccluderPipeline.IsValid() && !batch.occluderMeshletDraws.empty());
    });
    if (m_occlusionPyramid && graph.GetSchedulingMode() == RGSchedulingMode::ExplicitVersioned && hasDonors)
    {
        // Separate depth keeps the original GBuffer clear/compare untouched.
        // Prepass work has only frustum/LOD dependencies, never its own HZB.
        const auto depth = DeclareDrawPass(graph, context, true);
        m_occlusionPyramid->Declare(graph, depth);
        if (m_visibilityFrame)
        {
            m_visibilityFrame->DeclareWithOcclusion(graph, m_occlusionPyramid);
        }
        for (const auto& batch : m_batches)
        {
            for (const auto& level : batch.meshletDraws)
            {
                level.visibility->DeclareWithOcclusion(graph, m_occlusionPyramid);
            }
        }
    }
    else
    {
        m_occlusionPyramid.reset();
        for (auto& batch : m_batches)
        {
            if (!batch.occluderMeshletDraws.empty())
            {
                batch.meshletDraws = batch.occluderMeshletDraws;
            }
        }
        CaptureGeometryRoutes();
    }
    (void)DeclareDrawPass(graph, context, false);
}

RGHandle EnhancedGBufferPass::DeclareDrawPass(
    EnhancedRenderGraph& graph, const EnhancedFrameContext& context, bool occluders)
{
    const bool hasMeshletWork = std::any_of(m_batches.begin(), m_batches.end(), [occluders](const auto& batch) {
        return !(occluders ? batch.occluderMeshletDraws : batch.meshletDraws).empty();
    });
    // Once depth has hidden receivers, silently dropping its donors in the main
    // pass would leave phantom occluders. Advanced routes must fail atomically.
    const bool requireCompleteGeometry = hasMeshletWork || occluders || bool(m_occlusionPyramid);
    const auto visibility = occluders ? m_occluderVisibilityFrame : m_visibilityFrame;
    if (visibility)
    {
        visibility->Declare(graph);
    }
    const bool versioned = graph.GetSchedulingMode() == RGSchedulingMode::ExplicitVersioned;
    const auto outputAccess = graph.GetSchedulingMode() == RGSchedulingMode::DeclarationOrder
        ? RGAccessMode::LegacyState : RGAccessMode::Write;
    // 타깃을 그래프에 선언한다. 실제 생성과 배리어는 그래프가 맡는다 —
    // 이 패스는 무엇을 어떤 상태로 쓸지만 말한다.
    static const char* kNames[kRenderTargetCount] = {
        "GBuffer.Diffuse", "GBuffer.MetalRough", "GBuffer.Normal",
        "GBuffer.Emissive", "GBuffer.Bitmask" };

    RGHandle targets[kRenderTargetCount]{};
    const uint32_t colorCount = occluders ? 0u : kRenderTargetCount;
    for (uint32_t i = 0; i < colorCount; ++i)
    {
        RGTextureDesc desc{};
        desc.width = context.width;
        desc.height = context.height;
        desc.format = GetRenderTargetFormat(i);
        desc.allowRenderTarget = true;
        desc.name = kNames[i];
        targets[i] = graph.CreateTexture(desc);
        if (versioned)
        {
            targets[i] = graph.Write(targets[i]);
        }
    }

    RGTextureDesc depthDesc{};
    depthDesc.width = context.width;
    depthDesc.height = context.height;
    depthDesc.format = kDepthFormat;
    depthDesc.allowDepthStencil = true;
    depthDesc.name = occluders ? "GBuffer.OccluderDepth" : "GBuffer.Depth";
    RGHandle depth = graph.CreateTexture(depthDesc);
    if (versioned)
    {
        depth = graph.Write(depth);
    }

    if (!occluders)
    {
        m_outputs.diffuse = targets[0];
        m_outputs.metalRough = targets[1];
        m_outputs.normal = targets[2];
        m_outputs.emissive = targets[3];
        m_outputs.bitmask = targets[4];
        m_outputs.depth = depth;
    }

    std::vector<EnhancedRenderGraph::RGPassUsage> usages;
    usages.reserve(kRenderTargetCount + 1);
    for (uint32_t i = 0; i < colorCount; ++i)
    {
        usages.push_back({ targets[i], RHIResourceState::RenderTarget, outputAccess });
    }
    usages.push_back({ depth, RHIResourceState::DepthWrite, outputAccess });
    if (visibility)
    {
        visibility->AddReadUsages(graph, usages);
    }
    for (const auto& batch : m_batches)
    {
        if (occluders && !batch.meshletOccluderPipeline.IsValid())
        {
            continue;
        }
        const auto& meshletDraws = occluders ? batch.occluderMeshletDraws : batch.meshletDraws;
        if (!meshletDraws.empty())
        {
            for (const auto& level : meshletDraws)
            {
                level.visibility->AddReadUsages(graph, usages);
            }
            const auto& geometry = m_drawGeometry.at(batch.geometryKey);
            auto vertices = graph.FindImportedBuffer(geometry.vertices.buffer);
            if (!vertices.IsValid())
            {
                vertices = graph.ImportBuffer(geometry.vertices.buffer, RHIResourceState::VertexAndShaderResource,
                    "GBuffer.MeshletVertices");
            }
            const auto existing = std::find_if(usages.begin(), usages.end(), [&](const auto& usage) {
                return usage.handle.index == vertices.index && usage.handle.version == vertices.version
                    && usage.handle.kind == vertices.kind && usage.handle.epoch == vertices.epoch;
            });
            if (existing == usages.end())
            {
                usages.push_back({vertices, RHIResourceState::VertexAndShaderResource,
                    graph.GetSchedulingMode() == RGSchedulingMode::DeclarationOrder
                        ? RGAccessMode::LegacyState : RGAccessMode::Read});
            }
        }
    }

    // 소비자가 없을 때만 뿌리로 표시해 살려 둔다(SetKeepAlive).
    // Deferred가 붙으면 그쪽이 읽으므로 표시 없이도 살아남아야 한다.
    const bool keepAlive = !occluders && m_keepAlive;

    // 쪼갤 수 있는 패스로 선언한다.
    //
    // 이 패스가 기록 시간의 대부분을 차지한다 — 패스 단위 병렬화만으로는
    // '가장 무거운 패스'보다 빨라질 수 없어 1.25배에서 평평했다.
    //
    // 조각 상한은 워커 상한과 맞춘다. 그보다 잘게 쪼개도 같은 워커가 연달아
    // 맡게 되고, 그러면 조각마다 상태를 다시 거는 비용만 늘어난다.
    graph.AddSplitPass(occluders ? "GBuffer.Occluders" : GetName(), usages,
        [this, &context, targets, depth, visibility, requireCompleteGeometry, occluders, colorCount](const EnhancedRenderGraph::ExecuteContext& executeContext,
            uint32_t slice, uint32_t sliceCount)
        {
            RHIEncoder& encoder = *executeContext.encoder;

            // 뷰는 매 프레임 만든다. 그래프가 리소스를 프레임마다 다르게 줄 수
            // 있으므로(컬링·앨리어싱) 캐시하면 어긋난다.
            //
            // ★ 조각마다 다시 만든다. 조각들이 워커에 흩어져 동시에 기록하므로
            //   한 벌을 나눠 쓰면 만드는 쪽과 거는 쪽이 어긋날 수 있다 —
            //   프레임 힙에서 조각별로 잘라 오면 그 경합 자체가 없다.
            RHITextureHandle colors[kRenderTargetCount]{};
            for (uint32_t i = 0; i < colorCount; ++i)
            {
                colors[i] = executeContext.ResolveHandle(targets[i]);
            }

            const auto depthDesc = RHIDepthTargetDesc::Depth(
                executeContext.ResolveHandle(depth), kDepthFormat);
            const auto boundTargets = context.resources->CreateRenderTargets(
                std::span<const RHITextureHandle>(colors, colorCount), &depthDesc);
            if (!boundTargets.IsValid())
            {
                if (requireCompleteGeometry)
                {
                    throw std::runtime_error("Mesh GBuffer render-target binding failed.");
                }
                return;
            }

            encoder.SetViewportAndScissor(context.width, context.height);

            encoder.BindRenderTargets(boundTargets);

            // 클리어 값은 0으로 둔다. 그려진 곳과 안 그려진 곳이 값으로 구분되어야
            // 픽셀 검증이 '다섯 타깃 각각이 실제로 기록됐는가'를 볼 수 있다.
            //
            // 클리어는 첫 조각에서만 한다. 조각들은 순서대로 실행되므로 뒤
            // 조각이 또 지우면 앞 조각이 그린 것이 사라진다.
            constexpr float kZero[4] = { 0.f, 0.f, 0.f, 0.f };
            if (0 == slice)
            {
                if (!occluders)
                {
                    encoder.ClearRenderTargets(boundTargets, kZero);
                }
                encoder.ClearDepthTarget(boundTargets, 1.f);
            }

            // Install the default layout before preparing frame resources.
            // Each batch rebinds arguments after selecting its reflected layout.
            encoder.SetPipeline(RHIBindPoint::Graphics, m_pipelineRequest.GetHandle());
            encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);

            // 프레임 상수는 한 번만 올린다. 드로우마다 올리면 같은 값을 수백 번
            // 복사하는 꼴이고, 그건 CE 단계를 늘리는 방향이다.
            //
            // HLSL은 행 우선으로 읽으므로 전치해서 넣는다.
            const math::matrix4x4 viewProjection = math::transpose(m_frameViewProjection);
            const auto frameConstants = context.resources->UploadConstants(
                &viewProjection, sizeof(viewProjection));
            if (!frameConstants.IsValid())
            {
                if (requireCompleteGeometry)
                {
                    throw std::runtime_error("Mesh GBuffer frame constant upload failed.");
                }
                return;
            }
            encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0, frameConstants);

            // 그릴 것이 없으면 클리어만 하고 끝난다 — 빈 씬도 정상 경로다.
            if (nullptr == context.draws)
            {
                if (requireCompleteGeometry)
                {
                    throw std::runtime_error("Prepared GBuffer geometry input is unavailable during recording.");
                }
                return;
            }

            encoder.SetSamplers(RHIBindPoint::Graphics, 3, m_sampler);

            // ── 본 팔레트 ──
            //
            // 배치 밖에서 한 번 올린다. 애니메이터가 몇이든 인스턴스가 자기
            // 오프셋을 들고 있으므로 배치마다 다시 꽂을 이유가 없다 — DX11이
            // 애니메이터마다 cbuffer를 다시 올리며 드로우를 끊던 자리다.
            //
            // 팔레트가 없어도 t5는 꽂는다. 스킨드가 없는 프레임에서도
            // 루트 SRV가 비면 셰이더가 읽지 않더라도 검증 레이어가 경고하고,
            // 조각(slice)마다 상태를 다시 걸어야 하므로 여기가 그 자리다.
            RHIBufferSlice paletteBuffer{};
            if (context.animationPalettes)
                paletteBuffer = context.animationPalettes->Upload();
            else
            {
                const uint64_t paletteBytes = m_bonePalettes.empty()
                    ? sizeof(PackedBoneMatrix)
                    : sizeof(PackedBoneMatrix) * static_cast<uint64_t>(m_bonePalettes.size());
                paletteBuffer = context.resources->AllocateUpload(
                    RHIUploadRequest{ paletteBytes, RHIUploadUsage::BufferCopy,
                        sizeof(PackedBoneMatrix) });
                if (!paletteBuffer.IsValid() || !paletteBuffer.IsWritable())
                {
                    if (requireCompleteGeometry)
                    {
                        throw std::runtime_error("Mesh GBuffer palette placeholder upload failed.");
                    }
                    return;
                }
                if (m_bonePalettes.empty())
                {
                    const PackedBoneMatrix identity = PackedBoneMatrix::Identity();
                    memcpy(paletteBuffer.cpuAddress, &identity, sizeof(identity));
                }
                else memcpy(paletteBuffer.cpuAddress, m_bonePalettes.data(),
                    static_cast<size_t>(paletteBytes));
            }
            if (!paletteBuffer.IsValid())
            {
                if (requireCompleteGeometry)
                {
                    throw std::runtime_error("Mesh GBuffer palette binding is unavailable.");
                }
                return;
            }

            encoder.SetRootBuffer(RHIBindPoint::Graphics, 4, paletteBuffer);

            // 자기 몫의 배치만 그린다.
            //
            // 단위가 드로우가 아니라 배치다. 같은 메시·재질을 쓰는 드로우들이
            // 인스턴스 하나로 묶여 DrawIndexedInstanced 한 번에 나간다 —
            // 드로우마다 상수를 올리고 SRV 테이블을 만들던 것이 배치마다 한 번이 된다.
            //
            // 조각들은 선언 순서대로 실행되므로 결과는 통째로 그린 것과 같다.
            const size_t batchCount = m_batches.size();
            const size_t sliceBegin = batchCount * slice / sliceCount;
            const size_t sliceEnd = batchCount * (slice + 1) / sliceCount;
            if (sliceBegin >= sliceEnd) return;

            RHIBufferSlice paletteExtentConstants;
            const bool needsPaletteExtent = std::any_of(m_batches.begin() + sliceBegin, m_batches.begin() + sliceEnd,
                [occluders](const auto& batch) {
                    const auto& meshlets = occluders ? batch.occluderMeshletDraws : batch.meshletDraws;
                    return (meshlets.empty() ? batch.paletteExtentRoot : batch.meshletPaletteExtentRoot) != UINT32_MAX;
                });
            if (needsPaletteExtent)
            {
                const std::uint64_t matrixCount = context.animationPalettes
                    ? context.animationPalettes->MatrixCount() : m_bonePalettes.size();
                if (matrixCount > (std::numeric_limits<std::uint32_t>::max)() ||
                    matrixCount > paletteBuffer.size / sizeof(PackedBoneMatrix))
                {
                    throw std::runtime_error("GBuffer sealed palette extent exceeds its uploaded matrix storage.");
                }
                const std::array<std::uint32_t, 4> extent{static_cast<std::uint32_t>(matrixCount), 1u, 0u, 0u};
                paletteExtentConstants = context.resources->UploadConstants(extent.data(), sizeof(extent));
                if (!paletteExtentConstants.IsValid())
                {
                    throw std::runtime_error("GBufferSkinPaletteExtentV1 constant upload failed.");
                }
            }

            // ── 조각의 인스턴스를 블록 하나로 올린다 (일괄 할당) ──
            //
            // 예전에는 배치마다 링에서 잘랐다. dx12.bench11 실측이 그 비용을
            // 정확히 보여 줬다 — Allocate 한 번이 원자 연산 몇 개를 지나며
            // 호출당 ~175ns, 드로우당 할당 3.568ms가 일괄 5.25배(0.679ms)로
            // 줄었다. Keep one upload per slice, with each batch's root SRV
            // beginning at a portable 256-byte storage-buffer offset.
            std::vector<uint64_t> batchOffsets(sliceEnd - sliceBegin);
            uint64_t sliceInstanceBytes = 0;
            uint32_t identityCount = 0;
            for (size_t batchIndex = sliceBegin; batchIndex < sliceEnd; ++batchIndex)
            {
                sliceInstanceBytes = (sliceInstanceBytes + 255u) & ~uint64_t{255u};
                batchOffsets[batchIndex - sliceBegin] = sliceInstanceBytes;
                sliceInstanceBytes += sizeof(InstanceData) * uint64_t(m_batches[batchIndex].instanceCount);
                const auto& batch = m_batches[batchIndex];
                const auto& meshletDraws = occluders ? batch.occluderMeshletDraws : batch.meshletDraws;
                if (meshletDraws.empty() && batch.usesVisibleIds
                    && (!visibility || batch.visibilityBin == UINT32_MAX || !batch.compactsVisibleIds))
                {
                    identityCount = (std::max)(identityCount, batch.instanceCount);
                }
            }
            if (sliceInstanceBytes == 0)
            {
                return;
            }
            const auto instanceBlock = context.resources->AllocateUpload(
                RHIUploadRequest{ sliceInstanceBytes, RHIUploadUsage::BufferCopy, 256 });
            if (!instanceBlock.IsValid() || !instanceBlock.IsWritable())
            {
                if (requireCompleteGeometry)
                {
                    throw std::runtime_error("Mesh GBuffer instance upload failed.");
                }
                // W8: 조각 전체가 빠진다. 조용히 돌아가면 그 프레임은 물체
                // 여럿이 없는 채로 성공으로 보고된다.
                m_sealLedger.NoteDrop(EnhancedDrawDropReason::Instances,
                    static_cast<std::uint32_t>(sliceEnd - sliceBegin));
                return;
            }
            for (size_t batchIndex = sliceBegin; batchIndex < sliceEnd; ++batchIndex)
            {
                const auto& batch = m_batches[batchIndex];
                std::memcpy(static_cast<std::byte*>(instanceBlock.cpuAddress) + batchOffsets[batchIndex - sliceBegin],
                            &m_instances[batch.firstInstance], sizeof(InstanceData) * size_t(batch.instanceCount));
            }
            RHIBufferSlice identityVisibleIds;
            if (identityCount != 0)
            {
                std::string error;
                identityVisibleIds = GpuGeometryVisibility::UploadIdentity(context, identityCount, error);
                if (!identityVisibleIds.IsValid())
                {
                    throw std::runtime_error(error);
                }
            }

            for (size_t batchIndex = sliceBegin; batchIndex < sliceEnd; ++batchIndex)
            {
                const DrawBatch& batch = m_batches[batchIndex];
                const bool meshlet = !batch.meshletDraws.empty();
                const auto& meshletDraws = occluders ? batch.occluderMeshletDraws : batch.meshletDraws;
                const auto recordedPipeline = occluders
                    ? (meshlet ? batch.meshletOccluderPipeline : batch.occluderPipeline)
                    : (meshlet ? batch.meshletPipeline : batch.pipeline);
                if (occluders && (!recordedPipeline.IsValid() || (meshlet && meshletDraws.empty())))
                {
                    continue;
                }
                if (batchIndex >= m_geometryRouteAudit.size()
                    || (occluders ? m_geometryRouteAudit[batchIndex].occluderPipeline
                        : m_geometryRouteAudit[batchIndex].recordedPipeline) != recordedPipeline.id
                    || m_geometryRouteAudit[batchIndex].meshShader != meshlet
                    || (occluders ? m_geometryRouteAudit[batchIndex].occluderLodCount
                        : m_geometryRouteAudit[batchIndex].lodCount) != (meshlet ? meshletDraws.size() : 1u))
                {
                    m_sealLedger.NoteDrop(EnhancedDrawDropReason::Pipeline);
                    throw std::runtime_error("GBuffer geometry route changed after its prepared PSO audit.");
                }
                if (0 == batch.instanceCount) continue;
                if (!recordedPipeline.IsValid())
                {
                    if (requireCompleteGeometry)
                    {
                        throw std::runtime_error("Prepared mesh GBuffer pipeline is unavailable.");
                    }
                    m_sealLedger.NoteDrop(EnhancedDrawDropReason::Pipeline);
                    continue;
                }

                const auto mesh = m_drawGeometry.find(batch.geometryKey);
                if (mesh == m_drawGeometry.end() || !mesh->second.IsValid())
                {
                    if (requireCompleteGeometry)
                    {
                        throw std::runtime_error("Prepared mesh GBuffer geometry is unavailable.");
                    }
                    m_sealLedger.NoteDrop(EnhancedDrawDropReason::Geometry);
                    continue;
                }

                // M6-P1b2b1: PSO는 pass 전역 한 번이 아니라 material batch 직전에
                // 고른다. 같은 texture/property라도 keyword permutation이 다르면
                // 서로 다른 pipeline handle로 기록된다.
                encoder.SetPipeline(RHIBindPoint::Graphics, recordedPipeline);
                // A material may change the reflected table length/root layout.
                encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0, frameConstants);
                encoder.SetSamplers(RHIBindPoint::Graphics, 3,
                    SamplerTableFor(context, batch.material.sampler));
                encoder.SetRootBuffer(RHIBindPoint::Graphics, 4, paletteBuffer);
                const auto paletteExtentRoot = meshlet ? batch.meshletPaletteExtentRoot : batch.paletteExtentRoot;
                if (paletteExtentRoot != UINT32_MAX)
                {
                    encoder.SetConstantBuffer(RHIBindPoint::Graphics, paletteExtentRoot, paletteExtentConstants);
                }

                // M6-P1a: property bytes는 batch key의 일부다. 같은 texture/mesh라도
                // 값이 다르면 batch가 갈리고, 그 batch를 기록하기 직전에 b2를
                // 바꾼다. active ShaderMeta와 다른 handle은 PrepareFrame에서 이미
                // fail-closed되어 이 지점에 도달하지 않는다.
                const bool hasSnapshot = batch.material.snapshot
                    && !batch.material.snapshot->propertyBytes.empty();
                const void* materialData = !hasSnapshot
                    ? static_cast<const void*>(&kLegacyMaterialConstants)
                    : static_cast<const void*>(
                        batch.material.snapshot->propertyBytes.data());
                const std::size_t materialSize = !hasSnapshot
                    ? sizeof(kLegacyMaterialConstants)
                    : batch.material.snapshot->propertyBytes.size();
                const auto materialConstants = context.resources->UploadConstants(
                    materialData, materialSize);
                if (!materialConstants.IsValid())
                {
                    if (requireCompleteGeometry)
                    {
                        throw std::runtime_error("Mesh GBuffer material upload failed.");
                    }
                    m_sealLedger.NoteDrop(EnhancedDrawDropReason::MaterialConstants);
                    continue;
                }
                encoder.SetConstantBuffer(RHIBindPoint::Graphics, 5, materialConstants);
                const auto coordinates = MaterialTextureTable::UploadCoordinates(*context.resources, batch.material.coordinates);
                if (!coordinates.IsValid())
                {
                    if (requireCompleteGeometry)
                    {
                        throw std::runtime_error("Mesh GBuffer texture-coordinate upload failed.");
                    }
                    m_sealLedger.NoteDrop(EnhancedDrawDropReason::Coordinates);
                    continue;
                }
                encoder.SetConstantBuffer(RHIBindPoint::Graphics, 6, coordinates);

                encoder.SetRootBuffer(RHIBindPoint::Graphics, 1,
                    instanceBlock.SubRange(
                        batchOffsets[batchIndex - sliceBegin],
                        sizeof(InstanceData) * batch.instanceCount));
                const bool indirect = visibility && batch.visibilityBin != UINT32_MAX;
                if (meshlet)
                {
                    encoder.SetRootBuffer(RHIBindPoint::Graphics, 9, mesh->second.vertices);
                }
                else if (batch.usesVisibleIds)
                {
                    encoder.SetRootBuffer(RHIBindPoint::Graphics, 7,
                        indirect && batch.compactsVisibleIds
                            ? visibility->VisibleIds(batch.visibleIdOffset, batch.instanceCount)
                            : identityVisibleIds);
                }

                const auto textures = m_drawTextures.find(batch.material);
                if (textures == m_drawTextures.end())
                {
                    if (requireCompleteGeometry)
                    {
                        throw std::runtime_error("Mesh GBuffer material textures are unavailable.");
                    }
                    m_sealLedger.NoteDrop(EnhancedDrawDropReason::TextureTable);
                    continue;
                }
                const auto srvTable = context.resources->CreateBindings(textures->second.views);
                if (!srvTable.IsValid())
                {
                    if (requireCompleteGeometry)
                    {
                        throw std::runtime_error("Mesh GBuffer texture bindings failed.");
                    }
                    // descriptor 버전이 만료됐거나 구간이 찼다. 인코더는 이 표를
                    // 걸어도 조용히 돌아가므로 여기서 세지 않으면 증거가 없다.
                    m_sealLedger.NoteDrop(EnhancedDrawDropReason::Bindings);
                    continue;
                }
                encoder.SetBindings(RHIBindPoint::Graphics, 2, srvTable);

                if (meshlet)
                {
                    // GPU selection admits exactly one LOD per instance. Every
                    // prepared level must be submitted with its matching data;
                    // omitting a level would omit those instances completely.
                    for (const auto& level : meshletDraws)
                    {
                        if (!level.geometry.IsValid())
                        {
                            throw std::runtime_error("Mesh GBuffer lost a validated meshlet LOD binding.");
                        }
                        encoder.SetRootBuffer(RHIBindPoint::Graphics, 7, level.visibility->VisibleIds());
                        encoder.SetRootBuffer(RHIBindPoint::Graphics, 8, level.geometry.data);
                        if (!encoder.DispatchMeshIndirect(level.visibility->Arguments(), level.visibility->ArgsOffset()))
                        {
                            throw std::runtime_error("GBuffer mesh indirect submission failed.");
                        }
                    }
                }
                else if (indirect)
                {
                    encoder.SetVertexBuffer(mesh->second.vertices, mesh->second.vertexStride);
                    encoder.SetIndexBuffer(mesh->second.indices, mesh->second.indexFormat);
                    if (!encoder.DrawIndexedIndirect(visibility->Arguments(),
                                                     visibility->ArgsOffset(batch.visibilityBin)))
                    {
                        throw std::runtime_error("GBuffer indexed indirect submission failed.");
                    }
                }
                else
                {
                    encoder.SetVertexBuffer(mesh->second.vertices, mesh->second.vertexStride);
                    encoder.SetIndexBuffer(mesh->second.indices, mesh->second.indexFormat);
                    encoder.DrawIndexed(mesh->second.indexCount, batch.instanceCount);
                }
            }
        },
        // 조각 수는 드로우 수로 정한다.
        //
        // ★ 상한만 두고 무조건 쪼갰더니 작은 씬에서 오히려 느려졌다.
        // 실측: 드로우 44에서 1.23배(분할 없음) → 0.71배(6조각)로 뒤집혔다.
        // 조각마다 상태를 다시 걸어야 하기 때문이다 — RTV 5개와 DSV를 만들고
        // 뷰포트·루트 시그니처·PSO·힙을 세우고 프레임 상수를 올린다. 그 비용이
        // 드로우 일곱 개 그리는 것보다 크다.
        //
        // 그래서 조각당 최소 드로우 수를 둔다. 그 아래로는 쪼개지 않는다.
        ComputeSliceCount(),
        keepAlive,
        // ★ 기록량은 배치 수다. 드로우 수가 아니다.
        //
        // 처음에는 인스턴스 수 + 배치 수로 뒀다. 드로우마다 컬링과 인스턴스
        // 수집이 도니 그쪽이 비용을 따를 거라고 봤는데, 재 보니 아니었다.
        // 같은 드로우 704를 배치 11로 묶으면 순차 0.34 ms, 배치 704로 흩으면
        // 1.25~2.08 ms다 — 네 배 넘게 차이 난다. 인스턴스를 배열에 밀어 넣는
        // 일은 싸고, 배치마다 디스크립터·버퍼·PSO를 다시 거는 일이 비싸다.
        //
        // 병렬 이득도 배치를 따라간다: 배치 11이면 드로우 11264에서도
        // 0.95~1.52배로 갈리고, 배치 704면 드로우 704에서 이미 1.43~2.10배다.
        m_lastBatchCount);
    return depth;
}

uint32_t EnhancedGBufferPass::ComputeSliceCount() const
{
    // 조각당 최소 드로우 수. 실측으로 정했다 — 드로우 44를 6조각으로 나누면
    // 조각당 일곱이고 그때 상태 설정 비용이 이겼다. 176(조각당 29)부터는
    // 분할이 이겼으므로 그 사이 어딘가가 경계다.
    constexpr uint32_t kMinDrawsPerSlice = 32;

    // 기준은 드로우가 아니라 배치다. 인스턴싱으로 묶인 뒤에는 배치 수가
    // 기록 비용을 결정한다 — 드로우 700개가 배치 11개로 묶였다면 쪼갤 것이 없다.
    if (m_lastBatchCount <= kMinDrawsPerSlice) return 1;

    const uint32_t byBatches = m_lastBatchCount / kMinDrawsPerSlice;
    return (std::max)(1u, (std::min)(IRHIParallelCommandPool::kMaxWorkers, byBatches));
}

void EnhancedGBufferPass::Shutdown()
{
    // Batches hold meshlet Frames too. Drop those owners while device services
    // are still alive, before the visibility manager releases recording owners.
    m_batches.clear();
    m_visibilityFrame.reset();
    m_occluderVisibilityFrame.reset();
    m_occlusionPyramid.reset();
    m_visibility.ShutdownAfterIdle();
    m_meshletVisibility.ShutdownAfterIdle();
    m_depthPyramid.ShutdownAfterIdle();
    m_meshletPipelines.clear();
    m_occluderPipelines.clear();
    m_meshletLocalBounds.clear();
    m_geometryRouteAudit.clear();
    m_skinningBounds.clear();
    m_instanceBounds.clear();
    m_drawGeometry.clear();
    m_drawTextures.clear();
    m_bonePalettes.clear();
    m_boneOffsets.clear();
    m_boneCounts.clear();
    m_shaderVariants.clear();
    m_pipelineRequest = {};
    m_modelPipelineRequests.clear();
    m_shaderMetaHandle = {};
    m_defaultPermutationKey = {};
    m_shaderBindingLayout.reset();
}
