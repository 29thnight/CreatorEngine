#include "../../Scene/MaterialTextureTable.h"
#include "EnhancedShadowPass.h"
#include "../../Graph/EnhancedDrawIdentity.h" // I6-C
#include "../../../RHI/DX12/DX12DeviceResources.h"
#include "../../../RHI/DX12/DX12PSOManager.h"
#include "../../../RHI/DX12/DX12RootSignatureCache.h"
#include "../../../RHI/RHIEncoder.h"
#include "../../../Assets/ModelVertexLayout.h"
#include "../../../RHI/ModelVertexInputLayout.h"
#include "../../../Mesh.h"
#include "../../../StandardMaterialProperty.h"

#include <mathematics/frustum.hpp>
#include <mathematics/transform.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <sstream>
#include <limits>
#include <new>
#include <stdexcept>
#include <tuple>
#include "../../Graph/ShadowCasterBounds.h"
#include "../../../RHI/RHIShaderCompiler.h"

namespace
{
    // Static and skinned inputs share alpha/face coverage. UV0 and optional
    // COLOR alpha are required for MASK; bone attributes remain skin-only.
    constexpr const char* kShadowShaderFile = "Shadow.slang";

    bool CompileShadowShader(const char* entry, const char* target, bool skinning,
        uint32_t modelVertexMask, RHIShaderBlob& outBlob, std::string& outError)
    {
        RHIShaderPermutation permutation;
        if (skinning && !permutation.Enable("SHADOW_SKINNING", outError))
        {
            return false;
        }
        if (0 != modelVertexMask
            && !ModelVertexInput::ApplyShaderPermutation(modelVertexMask,
                permutation, outError))
        {
            return false;
        }

        return RHIShaderCompiler::CompileFile(kShadowShaderFile, entry, target,
            permutation, outBlob, outError);
    }
}

bool EnhancedShadowPass::CreatePipeline(const EnhancedFrameContext& context, std::string& outError)
{
    RHIShaderBlob psBlob;
    if (!CompileShadowShader("PSMain", "ps_5_0", false, 0, psBlob, outError))
    {
        return false;
    }
    RHIShaderBlob vsBlob;
    RHIShaderBlob skinnedVsBlob;
    if (!CompileShadowShader("VSMain", "vs_5_0", false, 0, vsBlob, outError))
    {
        return false;
    }
    if (!CompileShadowShader("VSMain", "vs_5_0", true, 0, skinnedVsBlob, outError))
    {
        return false;
    }
    // 인스턴스 배열은 루트 SRV로 넘긴다. 업로드 링에서 자른 조각의 GPU 주소를
    // 그대로 꽂으면 되므로 디스크립터를 만들 필요가 없다 — 배치마다 바뀌는
    // 값이라 디스크립터 테이블보다 이쪽이 싸다.
    //
    // 본 팔레트(t1)는 정적 PSO가 읽지 않지만 루트 시그니처는 하나로 둔다 —
    // 둘로 나누면 PSO 전환마다 루트까지 갈아야 하고, 그건 이 패스가 아끼려는
    // 상태 변경을 도로 늘리는 일이다.
    const RHIPipelineLayoutParam params[] = {
        RHILayout::Cbv(0, RHIShaderVisibility::Vertex),
        RHILayout::Srv(0, RHIShaderVisibility::Vertex),
        RHILayout::Srv(1, RHIShaderVisibility::Vertex),
        RHILayout::SrvTable(1, 2, RHIShaderVisibility::Pixel),
        RHILayout::SamplerTable(1, 0, RHIShaderVisibility::Pixel),
        RHILayout::Srv(3, RHIShaderVisibility::Vertex),
    };

    RHIPipelineLayoutDesc rootDesc{};
    rootDesc.params = params;
    rootDesc.allowInputAssembler = true;

    const auto root = context.rootSignatures->GetOrCreate(rootDesc, outError);
    if (!root.IsValid())
    {
        return false;
    }
    // Alpha-tested depth needs POSITION and UV0.
    static const RHIInputElement kInputElements[] = {
        { "POSITION", 0, RHIFormat::RGB32Float, 0, 0, 0 },
        { "TEXCOORD", 0, RHIFormat::RG32Float, 0, 24, 0 },
    };

    // 스킨드 경로만 본 인덱스·가중치를 읽는다. 오프셋은 엔진 Vertex를 따른다.
    static const RHIInputElement kSkinnedInputElements[] = {
        { "POSITION",     0, RHIFormat::RGB32Float,    0,  0, 0 },
        { "TEXCOORD", 0, RHIFormat::RG32Float, 0, 24, 0 },
        { "BLENDINDICES", 0, RHIFormat::RGBA32Float, 0, 64, 0 },
        { "BLENDWEIGHT",  0, RHIFormat::RGBA32Float, 0, 80, 0 },
    };
    static_assert(offsetof(Vertex, boneIndices) == 64, "Vertex 레이아웃이 바뀌었다");
    static_assert(offsetof(Vertex, boneWeights) == 80, "Vertex 레이아웃이 바뀌었다");

    RHIGraphicsPipelineDesc desc{};
    desc.inputElements = kInputElements;
    desc.inputElementCount = _countof(kInputElements);
    desc.vsBytecode = vsBlob.Data();
    desc.vsSize = vsBlob.Size();
    desc.psBytecode = psBlob.Data();
    desc.psSize = psBlob.Size();
    desc.layout = root;
    desc.depthEnable = true;
    // Pixel coverage applies each instance's authored face policy.
    desc.cullMode = RHICullMode::None;
    desc.numRenderTargets = 0;
    desc.dsvFormat = kShadowFormat;

    m_pso = context.psoManager->GetOrCreate(desc, outError);
    if (!m_pso.IsValid())
    {
        return false;
    }
    // 스킨드 변형 — 입력 레이아웃과 정점 셰이더만 다르고 나머지는 같다.
    desc.inputElements = kSkinnedInputElements;
    desc.inputElementCount = _countof(kSkinnedInputElements);
    desc.vsBytecode = skinnedVsBlob.Data();
    desc.vsSize = skinnedVsBlob.Size();

    m_skinnedPso = context.psoManager->GetOrCreate(desc, outError);
    if (!m_skinnedPso.IsValid())
    {
        return false;
    }
    m_modelPsos.clear();
    for (uint32_t mask : assets::kModelVertexMasks)
    {
        const bool skinned = 0 != (mask & assets::kSkinVertexAttributes);
        RHIShaderBlob modelVsBlob;
        if (!CompileShadowShader("VSMain", "vs_5_0", skinned, mask,
                modelVsBlob, outError))
        {
            return false;
        }
        // Depth-only Shadow가 실제로 읽는 필드만 IA에 선언한다. 오프셋은 전체
        // layout mask에서 유도하므로 COLOR가 들어간 SU의 bone 64/68도 보존된다.
        // Vulkan은 셰이더가 읽지 않는 NORMAL/TANGENT까지 선언하면
        // validation warning을 내므로 full mask와 consumed mask를 섞지 않는다.
        const uint32_t consumedMask =
            assets::Bit(assets::VertexAttribute::Position)
            | assets::Bit(assets::VertexAttribute::Uv0)
            | (mask & assets::Bit(assets::VertexAttribute::Uv1))
            | (mask & (assets::kSkinVertexAttributes | assets::kColorVertexAttributes));
        const auto* elements = ModelVertexInput::ResolveInputElements(
            mask, consumedMask, outError);
        if (nullptr == elements)
        {
            return false;
        }
        desc.inputElements = elements->data();
        desc.inputElementCount = static_cast<uint32_t>(elements->size());
        desc.vsBytecode = modelVsBlob.Data();
        desc.vsSize = modelVsBlob.Size();
        const RHIPipelineHandle pipeline =
            context.psoManager->GetOrCreate(desc, outError);
        if (!pipeline.IsValid())
        {
            return false;
        }
        m_modelPsos.emplace(mask, pipeline);
    }
    return m_modelPsos.size() == assets::kModelVertexMasks.size();
}

bool EnhancedShadowPass::Initialize(const EnhancedFrameContext& context, std::string& outError)
{
    if (nullptr == context.resources || nullptr == context.psoManager ||
        nullptr == context.rootSignatures)
    {
        outError = "그림자 패스 컨텍스트가 불완전하다";
        return false;
    }

    if (!CreatePipeline(context, outError))
    {
        return false;
    }
    const auto sampler = RHISampler::Linear(RHIAddressMode::Wrap);
    m_sampler = context.resources->CreateSamplers({&sampler, 1});
    if (!m_sampler.IsValid())
    {
        outError = "Shadow alpha sampler creation failed";
        return false;
    }
    return true;
}

void EnhancedShadowPass::ComputeCascades(const EnhancedFrameContext& context)
{
    m_hasDirectionalLight = false;
    m_shadowData = EnhancedShadowData{};
    for (auto& cascade : m_cascades)
    {
        cascade = Cascade{};
    }

    if (nullptr == context.camera || nullptr == context.lights)
    {
        return;
    }
    // 방향광 하나만 그림자를 드리운다. 여러 방향광의 그림자는 맵이 그만큼
    // 필요하고, 실제로 둘 이상 쓰는 씬이 나왔을 때 정하는 것이 맞다.
    //
    // ★ 그 하나를 "가장 센 것"으로 고른다. 예전에는 배열의 첫 방향광이었고,
    //   그러면 등록 순서가 그림자의 주인을 정했다 — 보조 방향광을 나중에
    //   더하면 그림자가 그쪽으로 옮겨 가는 부류다. 뷰가 미는 목록은 이미
    //   방향광을 세기순으로 앞에 세우지만(EnhancedLightPacking.h), 그 순서에
    //   말없이 기대면 목록을 만드는 쪽이 바뀔 때 조용히 깨진다.
    float strongest = -1.f;
    uint32_t selectedLight = 0;
    for (const auto& light : *context.lights)
    {
        if (0 != static_cast<uint32_t>(light.position.w))
        {
            continue;
        }
        const float intensity = light.color.a;
        if (intensity <= strongest)
        {
            continue;
        }
        strongest = intensity;
        selectedLight = static_cast<uint32_t>(&light - context.lights->data());
        m_lightDirection = math::vector3{ light.direction.x, light.direction.y,
            light.direction.z };
        m_hasDirectionalLight = true;
    }
    if (!m_hasDirectionalLight)
    {
        return;
    }
    if (math::length_sq(m_lightDirection) < 1e-6f)
    {
        m_lightDirection = { 0.f, -1.f, 0.f };
    }
    m_lightDirection = math::normalize(m_lightDirection);

    const auto receivers = shadow_math::ReceiverCascades(*context.camera, m_lightDirection,
        m_shadowDistance, m_blendBand, kShadowMapSize);
    for (uint32_t index = 0; index < kCascadeCount; ++index)
    {
        const auto& receiver = receivers[index];
        const auto center = receiver.bounds.center;
        const float radius = receiver.bounds.radius;
        const float sliceFar = receiver.split;
        const float worldTexel = radius * 2.f / float(kShadowMapSize);
        float backOff = radius * 2.f;
        for (const auto& bounds : m_casterBounds)
        {
            const auto offset = bounds.center - center;
            const float along = math::dot(offset, m_lightDirection);
            const auto perpendicular = offset - m_lightDirection * along;
            if (math::length(perpendicular) <= radius + bounds.radius)
            {
                backOff = (std::max)(backOff, -along + bounds.radius + worldTexel);
            }
        }
        const math::vector3 lightPosition = center - m_lightDirection * backOff;

        // 광원이 정확히 위나 아래를 볼 때 up이 평행해지는 것을 피한다.
        const math::vector3 up = (std::fabs(m_lightDirection.y) > 0.99f)
            ? math::vector3{ 0.f, 0.f, 1.f }
            : math::vector3{ 0.f, 1.f, 0.f };

        const math::matrix4x4 lightView = math::look_at_lh(lightPosition, center, up);
        const math::matrix4x4 lightProjection = math::orthographic_off_center_lh(
            -radius, radius, -radius, radius, 0.f, backOff + radius * 2.f);

        Cascade& cascade = m_cascades[index];
        cascade.lightViewProjection = lightView * lightProjection;
        cascade.center = center;
        cascade.radius = radius;
        cascade.depthSpan = backOff + radius * 2.f;
        cascade.splitDepth = sliceFar;

        m_shadowData.lightViewProjection[index] = cascade.lightViewProjection;
    }

    // 셰이더가 읽는 형태로 옮긴다. 분할 지점과 편향을 float4 하나씩에 담고
    // 있으므로 캐스케이드가 셋을 넘으면 담는 방식부터 바꿔야 한다.
    static_assert(3 == kCascadeCount, "splitDepths·bias가 float4 하나에 셋을 담는다");

    m_shadowData.splitDepths = math::vector4{ m_cascades[0].splitDepth,
        m_cascades[1].splitDepth, m_cascades[2].splitDepth, float(selectedLight + 1) };

    // 먼 캐스케이드는 텍셀 하나가 덮는 월드 범위가 넓다. 같은 편향을 쓰면
    // 그쪽에만 여드름이 남으므로 반지름 비만큼 키운다.
    // Bias is expressed in shadow texels, then converted exactly once to light depth.
    const auto bias = [&](uint32_t i) {
        return m_baseBias * (2.f * m_cascades[i].radius / float(kShadowMapSize))
            / m_cascades[i].depthSpan;
    };
    m_shadowData.bias = math::vector4{bias(0), bias(1), bias(2), m_slopeScale};

    m_shadowData.cascadeBlendBand = m_blendBand;

    const math::vector3& cameraForward = context.camera->forward;
    const uint32_t packedOptions = static_cast<uint32_t>(m_debugView)
        | (static_cast<uint32_t>(m_filter) << 4u);
    m_shadowData.cameraForward = math::vector4{
        cameraForward.x, cameraForward.y, cameraForward.z, static_cast<float>(packedOptions) };
    m_shadowData.lightDirection = math::vector4{ m_lightDirection.x, m_lightDirection.y,
        m_lightDirection.z, 0.f };
    m_shadowData.enabled = true;
}

EnhancedShadowPass::DebugStats EnhancedShadowPass::GetDebugStats() const
{
    DebugStats stats;
    stats.hasDirectionalLight = m_hasDirectionalLight;
    stats.lightIndex = (std::max)(1u, static_cast<uint32_t>(m_shadowData.splitDepths.w)) - 1u;
    stats.lightDirection = m_lightDirection;
    stats.shadowDistance = m_shadowDistance;
    stats.slopeScale = m_slopeScale;
    stats.casterCandidates = m_lastCasterCandidates;
    stats.gpuVisibilityActive = m_gpuVisibilityActive;
    stats.visibilityCountsExact = !m_gpuVisibilityActive;
    stats.gpuSubmittedCandidates = m_lastGpuSubmittedCandidates.load(std::memory_order_relaxed);
    stats.gpuSubmittedBins = m_lastGpuSubmittedBins.load(std::memory_order_relaxed);
    if (!m_hasDirectionalLight)
    {
        return stats;
    }
    for (uint32_t index = 0; index < kCascadeCount; ++index)
    {
        const Cascade& cascade = m_cascades[index];
        CascadeStats& out = stats.cascades[index];
        out.splitDepth = cascade.splitDepth;
        out.radius = cascade.radius;
        out.worldTexel = cascade.radius * 2.f / float(kShadowMapSize);
        out.depthSpan = cascade.depthSpan;
        // ComputeCascades 의 편향은 깊이 범위로 나눈 값이다 — 되곱해 월드로 돌린다.
        out.constantBias = m_shadowData.bias[static_cast<int>(index)] * cascade.depthSpan;
    }
    return stats;
}

bool EnhancedShadowPass::CastsInto(const Cascade& cascade, const math::vector3& center,
    float radius) const
{
    // Receiver-cylinder selection is open toward the light. Unknown arithmetic
    // cannot prove that a caster misses the receiver, including on CPU fallback.
    const math::vector3 offset = center - cascade.center;
    const float along = math::dot(offset, m_lightDirection);
    const math::vector3 perpendicular = offset - m_lightDirection * along;
    const float perpendicularDistance = math::length(perpendicular);
    const float combinedRadius = cascade.radius + radius;
    if (!(radius > 0.f) || !(cascade.radius > 0.f) || !std::isfinite(along) ||
        !std::isfinite(perpendicularDistance) || !std::isfinite(combinedRadius))
    {
        return true;
    }
    return along <= combinedRadius && perpendicularDistance <= combinedRadius;
}

EnhancedShadowPass::ShadowInstance EnhancedShadowPass::MakeInstance(
    const EnhancedDrawItem& draw, bool skinned) const
{
    ShadowInstance instance{};
    instance.world = math::transpose(draw.worldMatrix);
    const auto& coverage = draw.materialSnapshot ? draw.materialSnapshot->coverage : draw.coverage;
    instance.coverageFlags = coverage.flags;
    instance.cutoff = coverage.cutoff;
    instance.baseAlpha = draw.materialSnapshot ? coverage.baseAlpha : draw.baseColorFactor.a;
    if (draw.materialSnapshot)
    {
        for (const auto& binding : draw.materialSnapshot->textureBindings)
        {
            if (binding.propertyName == standard_material::property::BaseColorMap)
            {
                const auto uv = MaterialTextureTable::PackCoordinates(binding.coordinates);
                instance.uvU = uv.u;
                instance.uvV = uv.v;
            }
        }
    }
    if (skinned)
    {
        const auto offset = m_boneOffsets.find(draw.animatorKey);
        if (offset != m_boneOffsets.end())
        {
            instance.boneOffset = offset->second;
            instance.boneCount = m_boneCounts.at(draw.animatorKey);
        }
    }
    return instance;
}

RHIPipelineHandle EnhancedShadowPass::PipelineFor(const RHIMeshBinding& geometry, bool skinned) const
{
    if (geometry.vertexAttributeMask != 0)
    {
        const auto model = m_modelPsos.find(geometry.vertexAttributeMask);
        return model == m_modelPsos.end() ? RHIPipelineHandle{} : model->second;
    }
    return skinned ? m_skinnedPso : m_pso;
}

bool EnhancedShadowPass::PrepareGpuVisibility(const EnhancedFrameContext& context, std::string& outError)
{
    // Transient tables, instance slices and visibility owners must belong to the
    // post-prefix recording that will execute this graph, never the upload prefix.
    m_visibilityFrames.fill(nullptr);
    m_gpuBatches.clear();
    m_gpuVisibilityActive = false;
    if (!context.resources)
    {
        outError = "Shadow requires current device services";
        return false;
    }
    if (m_hasGpuVisibilityCandidates)
    {
        try
        {
            m_gpuVisibilityActive = TryPrepareGpuVisibility(context);
        }
        catch (const std::bad_alloc&)
        {
            // This route is optional. Mandatory CPU uploads remain checked by
            // the recording callback and abort rather than silently drop casters.
            m_gpuVisibilityActive = false;
        }
    }
    outError.clear();
    return true;
}

bool EnhancedShadowPass::TryPrepareGpuVisibility(const EnhancedFrameContext& context)
{
    // A product shadow queue includes off-camera casters. Never substitute the
    // camera queue or its visibility/HZB, even when the camera route is GPU-driven.
    if (!context.shadowDraws || !m_hasDirectionalLight || m_sortedDraws.empty()
        || !context.resources->GetIndirectDrawCapabilities().indexedDraw
        || !context.resources->GetCurrentUploadRecordingId())
    {
        return false;
    }

    // Bound worst-case candidate/output/upload work before narrowing any count.
    // 65,535 groups of 64 is a portable one-dimensional compute dispatch budget.
    constexpr uint64_t kMaximumCandidates = 65535ull * 64ull;
    constexpr uint64_t kMaximumAddress = (std::numeric_limits<uint32_t>::max)();
    const uint64_t groupCount = m_batchStarts.size() - 1;
    if (m_sortedDraws.size() > kMaximumCandidates || groupCount > kMaximumCandidates)
    {
        return false;
    }
    uint64_t outputCount = 0;
    uint64_t instanceBytes = 0;
    for (size_t group = 0; group < groupCount; ++group)
    {
        const uint64_t count = m_batchStarts[group + 1] - m_batchStarts[group];
        const uint64_t outputStride = (count + GpuGeometryVisibility::kOutputAlignment - 1)
            / GpuGeometryVisibility::kOutputAlignment * GpuGeometryVisibility::kOutputAlignment;
        const uint64_t uploadStride = (count * sizeof(ShadowInstance) + 255ull) & ~255ull;
        if (count > kMaximumAddress || outputStride > kMaximumAddress - outputCount
            || uploadStride > kMaximumAddress - instanceBytes)
        {
            return false;
        }
        outputCount += outputStride;
        instanceBytes += uploadStride;
    }
    if (instanceBytes == 0)
    {
        return false;
    }
    const auto upload = context.resources->AllocateUpload(
        {instanceBytes, RHIUploadUsage::Raw, 256});
    if (!upload.IsValid() || !upload.IsWritable())
    {
        return false;
    }

    std::vector<GpuBatch> batches(static_cast<size_t>(groupCount));
    std::vector<GpuGeometryVisibility::Candidate> candidates;
    std::vector<GpuGeometryVisibility::Bin> bins;
    candidates.reserve(m_sortedDraws.size());
    bins.reserve(static_cast<size_t>(groupCount));
    uint64_t uploadOffset = 0;
    uint32_t outputOffset = 0;
    for (size_t group = 0; group < groupCount; ++group)
    {
        const size_t begin = m_batchStarts[group];
        const size_t end = m_batchStarts[group + 1];
        const auto& firstDraw = (*context.shadowDraws)[m_sortedDraws[begin]];
        const auto found = m_drawGeometry.find(enhanced_draw::GeometryKey(firstDraw));
        const uint32_t count = static_cast<uint32_t>(end - begin);
        const uint64_t bytes = uint64_t(count) * sizeof(ShadowInstance);
        auto& batch = batches[group];
        if (found == m_drawGeometry.end() || !found->second.entry.IsValid())
        {
            return false;
        }
        batch.geometry = found->second.entry;
        const bool skinned = firstDraw.bonePalette && firstDraw.boneCount
            && (batch.geometry.vertexAttributeMask == 0
                || (batch.geometry.vertexAttributeMask & assets::kSkinVertexAttributes) != 0);
        batch.pipeline = PipelineFor(batch.geometry, skinned);
        if (!batch.pipeline.IsValid())
        {
            return false;
        }
        const auto& texture = m_alphaTextures[m_sortedDraws[begin]];
        const auto srv = RHIBindingDesc::Srv2D(texture.handle,
            texture.IsValid() ? texture.format : RHIFormat::RGBA8Unorm,
            0, texture.IsValid() ? texture.mipLevels : 1).OrNull();
        batch.alphaBinding = context.resources->CreateBindings({&srv, 1});
        if (!batch.alphaBinding.IsValid())
        {
            return false;
        }
        batch.instanceCount = count;
        batch.visibilityBin = static_cast<uint32_t>(bins.size());
        batch.visibleIdOffset = outputOffset;
        batch.instances = upload.SubRange(uploadOffset, bytes);
        if (!batch.instances.IsValid() || !batch.instances.IsWritable())
        {
            return false;
        }
        bins.push_back({batch.geometry.indexCount, 0, 0, 0});
        for (size_t sorted = begin; sorted < end; ++sorted)
        {
            const auto drawIndex = m_sortedDraws[sorted];
            const auto& draw = (*context.shadowDraws)[drawIndex];
            const auto instance = MakeInstance(draw, skinned);
            const auto local = static_cast<uint32_t>(sorted - begin);
            memcpy(static_cast<uint8_t*>(batch.instances.cpuAddress) + uint64_t(local) * sizeof(instance),
                &instance, sizeof(instance));
            const auto& bounds = m_casterBounds[drawIndex];
            const uint32_t flags = m_conservativeCasters[drawIndex]
                ? GpuGeometryVisibility::kConservative : 0u;
            candidates.push_back({{bounds.center.x, bounds.center.y, bounds.center.z, bounds.radius},
                batch.visibilityBin, local, outputOffset, flags});
        }
        uploadOffset += (bytes + 255ull) & ~255ull;
        outputOffset += static_cast<uint32_t>((uint64_t(count) + GpuGeometryVisibility::kOutputAlignment - 1)
            / GpuGeometryVisibility::kOutputAlignment * GpuGeometryVisibility::kOutputAlignment);
    }

    // Publish the GPU route only after every independent cascade is prepared.
    // Optional failure leaves Record on its existing CPU compact-batch route.
    std::array<std::shared_ptr<const GpuGeometryVisibility::Frame>, kCascadeCount> frames;
    const math::vector4 direction{m_lightDirection.x, m_lightDirection.y, m_lightDirection.z, 0.f};
    std::string error;
    for (uint32_t index = 0; index < kCascadeCount; ++index)
    {
        const auto& cascade = m_cascades[index];
        const math::vector4 receiver{cascade.center.x, cascade.center.y, cascade.center.z, cascade.radius};
        if (!m_visibility.PrepareShadow(context, receiver, direction, candidates, bins, frames[index], error)
            || !frames[index])
        {
            return false;
        }
    }
    m_gpuBatches = std::move(batches);
    m_visibilityFrames = std::move(frames);
    return true;
}

bool EnhancedShadowPass::PrepareFrame(const EnhancedFrameContext& context, std::string& outError)
{
    m_visibilityFrames.fill(nullptr);
    m_gpuBatches.clear();
    m_gpuVisibilityActive = false;
    m_hasGpuVisibilityCandidates = false;
    m_identityCount = 0;
    m_conservativeCasters.clear();
    m_drawGeometry.clear();
    m_sortedDraws.clear();
    m_batchStarts.assign(1, 0);
    m_alphaTextures.clear();
    m_bonePalettes.clear();
    m_boneOffsets.clear();
    m_boneCounts.clear();
    m_lastCasterCandidates = 0;
    m_lastDrawCount.store(0, std::memory_order_relaxed);
    m_lastCulledCount.store(0, std::memory_order_relaxed);
    m_lastBatchCount.store(0, std::memory_order_relaxed);
    m_lastSkinnedDrawCount.store(0, std::memory_order_relaxed);
    m_lastGpuSubmittedCandidates.store(0, std::memory_order_relaxed);
    m_lastGpuSubmittedBins.store(0, std::memory_order_relaxed);

    m_casterBounds.clear();
    if (!context.resources)
    {
        outError = "Shadow requires current device services";
        return false;
    }
    const auto* frameDraws = context.shadowDraws ? context.shadowDraws : context.draws;
    if (frameDraws)
    {
        m_casterBounds.reserve(frameDraws->size());
        m_conservativeCasters.reserve(frameDraws->size());
        for (const auto& draw : *frameDraws)
        {
            // Retain the existing fit bounds even when they cannot prove that a
            // skin or custom position contract is safe to reject from a cascade.
            m_casterBounds.push_back(shadow_math::WorldBounds(draw));
            bool knownPositionContract = !context.shadowDraws && !draw.materialSnapshot;
            if (draw.materialSnapshot)
            {
                const auto& material = *draw.materialSnapshot;
                const auto generation = LX::Runtime::ResolveGraphicsGeneration(
                    material.pipelineGenerations, material.shaderMetaHandle,
                    material.permutationKey, material.bindingLayout,
                    draw.modelMeshView.vertexAttributeMask, false);
                knownPositionContract = generation && generation->shader.compile.geometryVisibility
                    == ShaderGeometryVisibility::IndexedInstanceV1;
            }
            m_conservativeCasters.push_back(draw.boneCount != 0 || !knownPositionContract
                || !shadow_math::FinitePose(draw));
        }
    }
    ComputeCascades(context);

    if (!frameDraws || !context.meshCache)
    {
        return true;
    }
    m_alphaTextures.resize(frameDraws->size());
    for (std::size_t i = 0; i < frameDraws->size(); ++i)
    {
        const auto& draw = (*frameDraws)[i];
        if (draw.materialGraphInstance)
        {
            continue;
        }
        const auto& coverage = draw.materialSnapshot ? draw.materialSnapshot->coverage : draw.coverage;
        if (!coverage.IsValid())
        {
            outError = "Invalid shadow coverage";
            return false;
        }
        if (!(coverage.flags & EnhancedMaterialCoverage::Masked))
        {
            continue;
        }
        if (!context.textureCache)
        {
            outError = "Masked shadow needs texture cache";
            return false;
        }
        Texture* texture = draw.baseColor;
        if (draw.materialSnapshot)
        {
            const auto& bindings = draw.materialSnapshot->textureBindings;
            const auto found = std::find_if(bindings.begin(), bindings.end(), [](const auto& binding) {
                return binding.propertyName == standard_material::property::BaseColorMap;
            });
            if (found == bindings.end())
            {
                outError = "Masked shadow lacks baseColorMap semantic";
                return false;
            }
            if (!found->coordinates.IsValid()
                || !MaterialTextureTable::ValidateMeshCoordinates(*draw.materialSnapshot,
                    draw.modelMeshView.vertexAttributeMask, outError))
            {
                return false;
            }
            texture = found->textureOwner.get();
        }
        m_alphaTextures[i] = context.textureCache->GetOrUpload(texture, outError);
        if (!m_alphaTextures[i].IsValid())
        {
            return false;
        }
    }

    // Reuse the same LOD0 cache entry as the other geometry passes.
    for (const auto& draw : *frameDraws)
    {
        if (draw.materialGraphInstance || 0 == enhanced_draw::GeometryKey(draw)
            || m_drawGeometry.find(enhanced_draw::GeometryKey(draw)) != m_drawGeometry.end())
        {
            continue;
        }
        std::string uploadError;
        const auto entry = draw.modelMeshView.IsComplete()
            ? context.meshCache->GetOrUploadModel(draw.modelMeshView, uploadError)
            : context.meshCache->GetOrUpload(draw.mesh, uploadError);
        if (!entry.IsValid())
        {
            if (!uploadError.empty())
            {
                outError = uploadError;
                return false;
            }
            continue;
        }
        Geometry geometry{};
        geometry.entry = entry;
        geometry.boundRadius = enhanced_draw::BoundRadius(draw);
        m_drawGeometry.emplace(enhanced_draw::GeometryKey(draw), geometry);
    }

    // Seal exact palette ranges before exposing boneCount to the shader. The
    // local fixture route must reject conflicting ranges just like the shared owner.
    for (const auto& draw : *frameDraws)
    {
        if (draw.materialGraphInstance || 0 == enhanced_draw::GeometryKey(draw) || draw.boneCount == 0)
        {
            continue;
        }
        if (!draw.bonePalette)
        {
            outError = "Shadow skin has a bone count without a palette";
            return false;
        }
        if (m_boneOffsets.find(draw.animatorKey) != m_boneOffsets.end())
        {
            if (m_boneCounts.at(draw.animatorKey) != draw.boneCount)
            {
                outError = "Shadow animator key has conflicting palette lengths";
                return false;
            }
            continue;
        }
        uint32_t offset = 0;
        if (context.animationPalettes)
        {
            const auto& shared = context.animationPalettes->Offsets();
            const auto found = shared.find(draw.animatorKey);
            const auto uploaded = context.animationPalettes->Upload();
            if (found == shared.end() || !uploaded.IsValid() ||
                uint64_t(found->second) + draw.boneCount > uploaded.size / sizeof(PackedBoneMatrix) ||
                uint64_t(found->second) + draw.boneCount > (std::numeric_limits<uint32_t>::max)())
            {
                outError = "Shadow shared palette range is unavailable";
                return false;
            }
            offset = found->second;
        }
        else
        {
            if (m_bonePalettes.size() > (std::numeric_limits<uint32_t>::max)() - uint64_t(draw.boneCount))
            {
                outError = "Shadow local palette range exceeds 32-bit addressing";
                return false;
            }
            offset = static_cast<uint32_t>(m_bonePalettes.size());
            m_bonePalettes.resize(m_bonePalettes.size() + draw.boneCount);
            for (uint32_t i = 0; i < draw.boneCount; ++i)
            {
                m_bonePalettes[offset + i] = PackedBoneMatrix::From(draw.bonePalette[i]);
            }
        }
        m_boneOffsets.emplace(draw.animatorKey, offset);
        m_boneCounts.emplace(draw.animatorKey, draw.boneCount);
    }

    // Keep the existing native batch key and slice partitioning.
    m_sortedDraws.reserve(frameDraws->size());
    for (size_t index = 0; index < frameDraws->size(); ++index)
    {
        const auto& draw = (*frameDraws)[index];
        if (draw.materialGraphInstance)
        {
            continue;
        }
        const auto& coverage = draw.materialSnapshot ? draw.materialSnapshot->coverage : draw.coverage;
        if (0 != enhanced_draw::GeometryKey(draw) && !(coverage.flags & EnhancedMaterialCoverage::Blended))
        {
            m_sortedDraws.push_back(index);
        }
    }
    const auto batchKey = [&](size_t index) {
        const auto& draw = (*frameDraws)[index];
        const auto geometry = enhanced_draw::GeometryKey(draw);
        const auto found = m_drawGeometry.find(geometry);
        const auto mask = found == m_drawGeometry.end() ? 0u : found->second.entry.vertexAttributeMask;
        const bool skinned = draw.bonePalette && draw.boneCount
            && (mask == 0 || (mask & assets::kSkinVertexAttributes) != 0);
        return std::tuple{skinned, geometry, m_alphaTextures[index].handle.id};
    };
    std::stable_sort(m_sortedDraws.begin(), m_sortedDraws.end(),
        [&](size_t a, size_t b) { return batchKey(a) < batchKey(b); });
    m_batchStarts.clear();
    for (size_t i = 0; i < m_sortedDraws.size(); ++i)
    {
        if (i == 0 || batchKey(m_sortedDraws[i - 1]) != batchKey(m_sortedDraws[i]))
        {
            m_batchStarts.push_back(i);
        }
    }
    m_batchStarts.push_back(m_sortedDraws.size());
    if (m_sortedDraws.size() > (std::numeric_limits<uint32_t>::max)())
    {
        outError = "Shadow instance count exceeds 32-bit addressing";
        return false;
    }
    m_lastCasterCandidates = static_cast<uint32_t>(m_sortedDraws.size());
    for (size_t group = 0; group + 1 < m_batchStarts.size(); ++group)
    {
        m_identityCount = (std::max)(m_identityCount,
            static_cast<uint32_t>(m_batchStarts[group + 1] - m_batchStarts[group]));
    }
    // The shadow queue is independent from camera-visible GBuffer candidates.
    // Off-camera-only native casters still require the shared prefix boundary.
    m_hasGpuVisibilityCandidates = context.shadowDraws && !m_sortedDraws.empty()
        && m_hasDirectionalLight && context.resources->GetIndirectDrawCapabilities().indexedDraw;
    return true;
}

bool EnhancedShadowPass::PrepareGraphFrame(const EnhancedFrameContext& context, std::string& outError)
{
    m_casterBounds.clear();
    const auto* casters = context.shadowDraws;
    if (!context.resources || !casters)
    {
        outError = "Graph shadow requires device services and the identified caster stream.";
        return false;
    }
    for (const auto& draw : *casters)
    {
        if (!draw.materialGraphInstance)
        {
            outError = "Graph shadow caster has no material graph instance.";
            return false;
        }
        m_casterBounds.push_back(shadow_math::WorldBounds(draw));
    }
    m_lastCasterCandidates = static_cast<uint32_t>(casters->size());
    m_lastDrawCount = 0;
    m_lastCulledCount = 0;
    m_lastBatchCount = 0;
    m_lastSkinnedDrawCount = 0;
    m_lastGpuSubmittedCandidates = 0;
    m_lastGpuSubmittedBins = 0;
    ComputeCascades(context);
    outError.clear();
    return true;
}

void EnhancedShadowPass::DeclareGraphTargets(EnhancedRenderGraph& graph, const EnhancedFrameContext& context)
{
    RGTextureDesc desc{};
    desc.width = kShadowMapSize;
    desc.height = kShadowMapSize;
    desc.arraySize = kCascadeCount;
    desc.format = kShadowFormat;
    desc.allowDepthStencil = true;
    desc.name = "Shadow.Cascades";
    m_shadowMap = graph.CreateTexture(desc);
    if (graph.GetSchedulingMode() == RGSchedulingMode::ExplicitVersioned)
    {
        m_shadowMap = graph.Write(m_shadowMap);
    }
    const auto access = graph.GetSchedulingMode() == RGSchedulingMode::DeclarationOrder
        ? RGAccessMode::LegacyState : RGAccessMode::Write;
    graph.AddPass("Shadow.Clear", {{m_shadowMap, RHIResourceState::DepthWrite, access}},
        [this, &context](const EnhancedRenderGraph::ExecuteContext& execution)
        {
            auto& encoder = *execution.encoder;
            for (uint32_t cascade = 0; cascade < kCascadeCount; ++cascade)
            {
                const auto depth = RHIDepthTargetDesc::DepthSlice(
                    execution.ResolveHandle(m_shadowMap), kShadowFormat, cascade);
                const auto targets = context.resources->CreateRenderTargets(
                    std::span<const RHITextureHandle>{}, &depth);
                if (!targets.IsValid())
                {
                    throw std::runtime_error("Graph shadow clear target is unavailable.");
                }
                encoder.BindRenderTargets(targets);
                encoder.ClearDepthTarget(targets, 1.f);
            }
        });
}

void EnhancedShadowPass::Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context)
{
    RGTextureDesc desc{};
    desc.width = kShadowMapSize;
    desc.height = kShadowMapSize;
    desc.arraySize = kCascadeCount;
    desc.format = kShadowFormat;
    desc.allowDepthStencil = true;
    desc.name = "Shadow.Cascades";
    m_shadowMap = graph.CreateTexture(desc);
    if (graph.GetSchedulingMode() == RGSchedulingMode::ExplicitVersioned)
    {
        m_shadowMap = graph.Write(m_shadowMap);
    }
    const auto outputAccess = graph.GetSchedulingMode() == RGSchedulingMode::DeclarationOrder
        ? RGAccessMode::LegacyState : RGAccessMode::Write;

    // 방향광이 없으면 그리지 않는다. 다만 리소스는 선언해 두어야 Deferred가
    // 읽을 것이 있다 — 클리어만 된 맵은 '그림자 없음'과 같은 뜻이다.
    // 쪼갤 수 있는 패스로 선언한다.
    //
    // 병렬 경로의 패스별 GPU 시간을 재 보니 이 패스가 가장 무거웠다
    // (드로우 704에서 Shadow 0.4864 ms · GBuffer 0.2345 ms). 캐스케이드 셋에
    // 드로우를 전부 다시 그리기 때문이다.
    //
    // 조각 경계는 두 층이다. 캐스케이드가 바깥, 드로우 범위가 안쪽 —
    // 캐스케이드가 셋뿐이라 그것만으로는 세 조각이 상한이 된다.
    std::vector<EnhancedRenderGraph::RGPassUsage> usages{
        {m_shadowMap, RHIResourceState::DepthWrite, outputAccess}};
    const auto visibilityFrames = m_visibilityFrames;
    const bool gpuVisibility = m_gpuVisibilityActive;
    if (gpuVisibility)
    {
        for (const auto& visibility : visibilityFrames)
        {
            visibility->AddReadUsages(graph, usages);
        }
    }
    graph.AddSplitPass(GetName(), usages,
        [this, &context, visibilityFrames, gpuVisibility](const EnhancedRenderGraph::ExecuteContext& executeContext,
            uint32_t slice, uint32_t sliceCount)
        {
            RHIEncoder& encoder = *executeContext.encoder;
            const RHITextureHandle shadowMap = executeContext.ResolveHandle(m_shadowMap);

            encoder.SetViewportAndScissor(kShadowMapSize, kShadowMapSize);

            const bool draws = m_hasDirectionalLight && nullptr != (context.shadowDraws ? context.shadowDraws : context.draws);
            RHIBufferSlice identityVisibleIds{};
            if (draws)
            {
                if (!gpuVisibility && m_identityCount != 0)
                {
                    // Each recording callback owns this mandatory fallback
                    // upload. Fixtures need no post-prefix preparation step.
                    const uint64_t identityBytes = uint64_t(m_identityCount) * sizeof(uint32_t);
                    identityVisibleIds = context.resources->AllocateUpload(
                        {identityBytes, RHIUploadUsage::Raw, 256});
                    if (!identityVisibleIds.IsValid() || !identityVisibleIds.IsWritable()
                        || identityVisibleIds.size < identityBytes)
                    {
                        throw std::runtime_error("Shadow identity visibility upload failed.");
                    }
                    auto* ids = static_cast<uint32_t*>(identityVisibleIds.cpuAddress);
                    for (uint32_t index = 0; index < m_identityCount; ++index)
                    {
                        ids[index] = index;
                    }
                }
                // 캐스케이드마다 상태를 다시 걸지 않는다. 루트 시그니처는 셋이
                // 같으므로 바깥에서 한 번이면 된다.
                //
                // PSO는 배치가 스킨드인지에 따라 갈리므로 여기서 걸지 않는다 —
                // 아래 flushBatch가 필요할 때만 바꾼다.
                // ★ A-1 이후 "파이프라인 없이 루트만"이 표현 불가능하다.
                //   PSO 는 아래 flushBatch 가 배치마다 고르므로, 여기서는 기본
                //   경로(비스킨드)를 걸어 루트 시그니처를 세운다 — 레이아웃이
                //   둘 사이에 같아서 성립하고, 인코더가 중복 바인딩을 걸러 낸다.
                encoder.SetPipeline(RHIBindPoint::Graphics, m_pso);
                encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);

                // 본 팔레트는 조각당 한 번. 스킨드가 없어도 꽂는다 — 루트에
                // 선언된 슬롯을 비워 두면 검증 레이어가 경고한다.
                RHIBufferSlice paletteBuffer{};
                if (context.animationPalettes)
                {
                    paletteBuffer = context.animationPalettes->Upload();
                }
                else
                {
                    const uint64_t paletteBytes = m_bonePalettes.empty()
                        ? sizeof(PackedBoneMatrix)
                        : sizeof(PackedBoneMatrix) * static_cast<uint64_t>(m_bonePalettes.size());
                    paletteBuffer = context.resources->AllocateUpload(
                        RHIUploadRequest{ paletteBytes, RHIUploadUsage::BufferCopy,
                            sizeof(PackedBoneMatrix) });
                    if (!paletteBuffer.IsValid() || !paletteBuffer.IsWritable()
                        || paletteBuffer.size < paletteBytes)
                    {
                        throw std::runtime_error("Shadow bone palette upload failed.");
                    }
                    if (m_bonePalettes.empty())
                    {
                        const PackedBoneMatrix identity = PackedBoneMatrix::Identity();
                        memcpy(paletteBuffer.cpuAddress, &identity, sizeof(identity));
                    }
                    else
                    {
                        memcpy(paletteBuffer.cpuAddress, m_bonePalettes.data(),
                            static_cast<size_t>(paletteBytes));
                    }
                }
                if (!paletteBuffer.IsValid() || paletteBuffer.size < sizeof(PackedBoneMatrix))
                {
                    throw std::runtime_error("Shadow bone palette is unavailable.");
                }
                encoder.SetRootBuffer(RHIBindPoint::Graphics, 2, paletteBuffer);
            }

            // 조각 하나가 (캐스케이드, 드로우 범위) 하나를 맡는다.
            //
            // 캐스케이드당 조각 수를 먼저 정하고, 그 안에서 드로우를 나눈다.
            // sliceCount가 1이면 캐스케이드 전부·드로우 전부가 되어 통째로
            // 기록하는 것과 같다 — 그것이 분할 콜백의 계약이다.
            const uint32_t slicesPerCascade = (std::max)(1u, sliceCount / kCascadeCount);
            const uint32_t cascadeBegin = (sliceCount <= kCascadeCount)
                ? (kCascadeCount * slice / sliceCount) : (slice / slicesPerCascade);
            const uint32_t cascadeEnd = (sliceCount <= kCascadeCount)
                ? (kCascadeCount * (slice + 1) / sliceCount) : (cascadeBegin + 1);
            const uint32_t drawSlice = (sliceCount <= kCascadeCount)
                ? 0u : (slice % slicesPerCascade);
            const uint32_t drawSliceCount = (sliceCount <= kCascadeCount) ? 1u : slicesPerCascade;

            for (uint32_t index = cascadeBegin; index < cascadeEnd && index < kCascadeCount; ++index)
            {
                // 캐스케이드 하나 = 배열 슬라이스 하나. 색 타깃 없이 깊이만 묶는다.
                const auto depthDesc = RHIDepthTargetDesc::DepthSlice(
                    shadowMap, kShadowFormat, index);
                const auto boundTargets = context.resources->CreateRenderTargets(
                    std::span<const RHITextureHandle>{}, &depthDesc);
                if (!boundTargets.IsValid())
                {
                    throw std::runtime_error("Shadow depth target is unavailable.");
                }

                encoder.BindRenderTargets(boundTargets);

                // 클리어는 그 캐스케이드의 첫 조각에서만. 뒤 조각이 또 지우면
                // 앞 조각이 그린 것이 사라진다.
                if (0 == drawSlice)
                {
                    encoder.ClearDepthTarget(boundTargets, 1.f);
                }

                if (!draws)
                {
                    continue;
                }
                const Cascade& cascade = m_cascades[index];

                // 광원 행렬은 캐스케이드마다 한 번만 올린다. 예전에는 드로우마다
                // 월드와 함께 올렸는데, 같은 값을 수백 번 복사하는 것이 CE 단계를
                // 늘리는 방향이다.
                ShadowConstants constants{};
                constants.lightViewProjection = math::transpose(cascade.lightViewProjection);

                const auto cbAllocation = context.resources->AllocateUpload(
                    RHIUploadRequest{ sizeof(ShadowConstants),
                        RHIUploadUsage::ConstantBuffer, 1 });
                if (!cbAllocation.IsValid() || !cbAllocation.IsWritable()
                    || cbAllocation.size < sizeof(ShadowConstants))
                {
                    throw std::runtime_error("Shadow cascade upload failed.");
                }

                memcpy(cbAllocation.cpuAddress, &constants, sizeof(constants));
                encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0, cbAllocation);

                const size_t groupCount = m_batchStarts.size() - 1;
                const size_t groupBegin = groupCount * drawSlice / drawSliceCount;
                const size_t groupEnd = groupCount * (drawSlice + 1) / drawSliceCount;
                if (gpuVisibility)
                {
                    // The CPU records only immutable full-batch commands. Each
                    // cascade's GPU output chooses its own batch-local IDs/count.
                    const auto& visibility = visibilityFrames[index];
                    for (size_t group = groupBegin; group < groupEnd; ++group)
                    {
                        const auto& batch = m_gpuBatches[group];
                        encoder.SetPipeline(RHIBindPoint::Graphics, batch.pipeline);
                        encoder.SetBindings(RHIBindPoint::Graphics, 3, batch.alphaBinding);
                        encoder.SetSamplers(RHIBindPoint::Graphics, 4, m_sampler);
                        encoder.SetRootBuffer(RHIBindPoint::Graphics, 1, batch.instances);
                        encoder.SetRootBuffer(RHIBindPoint::Graphics, 5,
                            visibility->VisibleIds(batch.visibleIdOffset, batch.instanceCount));
                        encoder.SetVertexBuffer(batch.geometry.vertices, batch.geometry.vertexStride);
                        encoder.SetIndexBuffer(batch.geometry.indices, batch.geometry.indexFormat);
                        if (!encoder.DrawIndexedIndirect(visibility->Arguments(),
                            visibility->ArgsOffset(batch.visibilityBin)))
                        {
                            // A late direct draw would bypass GPU visibility and
                            // partially mix routes. Abort the frame instead.
                            throw std::runtime_error("Prepared GPU shadow indexed-indirect draw failed.");
                        }
                        m_lastBatchCount.fetch_add(1, std::memory_order_relaxed);
                        m_lastGpuSubmittedCandidates.fetch_add(batch.instanceCount, std::memory_order_relaxed);
                        m_lastGpuSubmittedBins.fetch_add(1, std::memory_order_relaxed);
                    }
                    continue;
                }

                // Compatibility fallback retains CPU cascade rejection and local
                // compaction. Its identity mapping restarts at zero for each batch.
                const size_t drawBegin = m_batchStarts[groupBegin];
                const size_t drawEnd = m_batchStarts[groupEnd];
                std::vector<ShadowInstance> instances;
                instances.reserve(drawEnd - drawBegin);

                std::size_t batchGeometryKey = 0;
                bool  batchSkinned = false;
                RHITextureEntry batchTexture{};

                // 지금 걸려 있는 PSO. 조각마다 상태를 새로 걸어야 하므로
                // 처음에는 아무것도 안 걸린 것으로 둔다.
                RHIPipelineHandle boundPso;

                // 모아 둔 인스턴스를 드로우 하나로 낸다.
                const auto flushBatch = [&]()
                {
                    if (instances.empty() || 0 == batchGeometryKey)
                    {
                        return;
                    }
                    const auto found = m_drawGeometry.find(batchGeometryKey);
                    if (found != m_drawGeometry.end() && found->second.entry.IsValid())
                    {
                        // 배치마다 링에서 자른다. dx12.bench11 실측으로 Allocate
                        // 호출당 ~175ns가 붙는 것을 알지만(GBuffer는 그래서 조각당
                        // 블록 하나로 바꿨다), 여기는 그대로 둔다 — 배치가 컬링
                        // 결과에 따라 즉석에서 만들어져 크기를 미리 모르고, 상한
                        // (조각의 후보 전부)으로 잡으면 컬링이 잘 될수록 링을
                        // 낭비한다. 배치 수가 고유 메시 수 × 캐스케이드로 묶여
                        // 있어 호출 수 자체가 작다는 것이 근거다.
                        const uint64_t instanceBytes =
                            sizeof(ShadowInstance) * static_cast<uint64_t>(instances.size());
                        const auto instanceBuffer = context.resources->AllocateUpload(
                            RHIUploadRequest{ instanceBytes, RHIUploadUsage::BufferCopy,
                                sizeof(ShadowInstance) });

                        if (instanceBuffer.IsValid() && instanceBuffer.IsWritable()
                            && instanceBuffer.size >= instanceBytes)
                        {
                            // 스킨드 배치만 스킨드 PSO로 바꾼다. 정렬이 스킨드를
                            // 뭉쳐 두었으므로 전환은 그룹 경계에서 한 번뿐이다.
                            // I5-D34b: 스킨드 안에서도 버퍼 레이아웃으로 갈린다 —
                            // 배치는 메시 단위라 마스크가 배치 안에서 섞이지 않는다.
                            const auto wanted = PipelineFor(found->second.entry, batchSkinned);
                            if (!wanted.IsValid())
                            {
                                throw std::runtime_error("Shadow batch pipeline is unavailable.");
                            }
                            if (wanted != boundPso)
                            {
                                // 루트 시그니처는 위에서 이미 걸었다. 그대로 다시
                                // 넘기지만 인코더가 중복을 걸러 내므로 여기서
                                // 갈리는 것은 PSO 하나다.
                                encoder.SetPipeline(RHIBindPoint::Graphics,
                                    wanted);
                                boundPso = wanted;
                            }

                            const auto srv = RHIBindingDesc::Srv2D(batchTexture.handle,
                                batchTexture.IsValid() ? batchTexture.format : RHIFormat::RGBA8Unorm,
                                0, batchTexture.IsValid() ? batchTexture.mipLevels : 1).OrNull();
                            const auto binding = context.resources->CreateBindings({&srv, 1});
                            if (!binding.IsValid())
                            {
                                throw std::runtime_error("Shadow alpha binding is unavailable.");
                            }
                            encoder.SetBindings(RHIBindPoint::Graphics, 3, binding);
                            encoder.SetSamplers(RHIBindPoint::Graphics, 4, m_sampler);
                            memcpy(instanceBuffer.cpuAddress, instances.data(),
                                static_cast<size_t>(instanceBytes));
                            encoder.SetRootBuffer(RHIBindPoint::Graphics,
                                1, instanceBuffer);
                            encoder.SetRootBuffer(RHIBindPoint::Graphics,
                                5, identityVisibleIds);

                            encoder.SetVertexBuffer(found->second.entry.vertices, found->second.entry.vertexStride);
                            encoder.SetIndexBuffer(found->second.entry.indices, found->second.entry.indexFormat);
                            encoder.DrawIndexed(found->second.entry.indexCount,
                                static_cast<uint32_t>(instances.size()), 0, 0, 0);

                            // 드로우 수는 인스턴스 수로 센다. 배치로 세면 병합이
                            // 늘수록 '캐스터가 줄었다'로 보여 컬링 단정이 흐려진다.
                            m_lastDrawCount.fetch_add(
                                static_cast<uint32_t>(instances.size()), std::memory_order_relaxed);
                            m_lastBatchCount.fetch_add(1, std::memory_order_relaxed);

                            if (batchSkinned)
                            {
                                m_lastSkinnedDrawCount.fetch_add(
                                    static_cast<uint32_t>(instances.size()),
                                    std::memory_order_relaxed);
                            }
                        }
                        else
                        {
                            throw std::runtime_error("Shadow instance upload failed.");
                        }
                    }

                    instances.clear();
                };

                for (size_t sortedIndex = drawBegin; sortedIndex < drawEnd; ++sortedIndex)
                {
                    const auto& draw = (*(context.shadowDraws ? context.shadowDraws : context.draws))[m_sortedDraws[sortedIndex]];

                    const auto found = m_drawGeometry.find(enhanced_draw::GeometryKey(draw));
                    if (found == m_drawGeometry.end() || !found->second.entry.IsValid())
                    {
                        continue;
                    }
                    // 경계 구를 월드로 옮긴다. 비균등 배율에서는 최대 축으로
                    // 잡아야 보수적이다 — 작게 잡으면 그림자가 사라진다.
                    const auto& bounds = m_casterBounds[m_sortedDraws[sortedIndex]];
                    if (!m_conservativeCasters[m_sortedDraws[sortedIndex]]
                        && bounds.radius > 0.f && !CastsInto(cascade, bounds.center, bounds.radius))
                    {
                        m_lastCulledCount.fetch_add(1, std::memory_order_relaxed);
                        continue;
                    }

                    // I5-D34a/b: 스킨 분류는 팔레트에 더해 버퍼가 스킨
                    // 어트리뷰트를 실제로 실었는지도 본다. experiment core
                    // (48B)는 스킨 오프셋이 없어 스킨 PSO로 분류하면 버퍼
                    // 밖을 읽는다 — 그 메시는 본 데이터가 없어 legacy에서도
                    // 바인드 포즈였으므로 정적 분류가 의미도 같다. experiment
                    // 스킨(68B)은 아래 flushBatch가 uint4 레이아웃 PSO를 고른다.
                    const uint32_t drawVertexMask =
                        found->second.entry.vertexAttributeMask;
                    const bool skinnableBuffer = 0 == drawVertexMask
                        || 0 != (drawVertexMask
                            & assets::kSkinVertexAttributes);
                    const bool skinned =
                        (nullptr != draw.bonePalette) && (0 != draw.boneCount)
                        && skinnableBuffer;

                    // 메시나 스킨드 여부가 바뀌면 지금까지 모은 것을 낸다.
                    // 정렬해 두었으므로 같은 것끼리는 이미 붙어 있다.
                    const auto& alphaTexture = m_alphaTextures[m_sortedDraws[sortedIndex]];
                    if (enhanced_draw::GeometryKey(draw) != batchGeometryKey
                        || skinned != batchSkinned || alphaTexture.handle != batchTexture.handle)
                    {
                        flushBatch();
                        batchGeometryKey = enhanced_draw::GeometryKey(draw);
                        batchSkinned = skinned;
                        batchTexture = alphaTexture;
                    }

                    instances.push_back(MakeInstance(draw, skinned));
                }

                flushBatch();
            }
        },
        // 조각 수는 캐스터 수로 정한다. GBuffer와 같은 이유로 무조건 쪼개지
        // 않는다 — 조각마다 상태를 다시 걸어야 하고, 그 비용이 드로우 몇 개
        // 그리는 것보다 크면 손해다.
        ComputeSliceCount(),
        // 그림자 맵을 읽는 것은 Deferred다. 뿌리로 표시하지 않아도 컬링이
        // 살려야 하고, 그것이 3-5 컬링의 또 한 번의 확인이다.
        false,
        // 기록량은 배치 수다(GBuffer와 같은 기준). 캐스케이드마다 전부 다시
        // 그리므로 고유 메시 수의 세 배가 배치 수의 상한이다.
        //
        // 후보 수(캐스케이드당 드로우 수)를 쓰지 않는 이유는 GBuffer에서와
        // 같다 — 실측이 비용을 정하는 것은 배치라고 말했다.
        static_cast<uint32_t>(m_batchStarts.size() - 1) * kCascadeCount);
}

uint32_t EnhancedShadowPass::ComputeSliceCount() const
{
    // 캐스케이드가 셋이므로 최소 단위는 셋이다. 그 위로는 드로우 수를 보고
    // 캐스케이드마다 몇 조각으로 더 나눌지 정한다.
    //
    // 조각당 최소 드로우 수는 GBuffer와 같은 값을 쓴다. 실측으로 정한 경계이고,
    // 두 패스의 드로우당 기록 비용이 비슷하다(둘 다 상수 하나 올리고 드로우 하나).
    constexpr uint32_t kMinDrawsPerSlice = 32;

    // ★ 기준은 드로우가 아니라 '캐스케이드당 배치 수'다.
    //
    // 인스턴싱을 넣기 전에는 드로우 후보 수를 썼다. 지금 그대로 두면 손해가
    // 난다 — 드로우 704가 메시 11종이면 배치는 11개인데, 후보 704로 조각을
    // 계산하면 여덟 조각으로 나뉘고 각 조각이 자기 범위 안에서만 묶으므로
    // 배치가 도로 잘게 쪼개진다. 분할이 병합을 스스로 깨는 꼴이다.
    //
    // 배치 수는 Record에서 나오므로 Declare 시점에는 모른다. 대신 고유 메시
    // 수를 쓴다 — 정렬해서 같은 메시를 묶으므로 캐스케이드당 배치 수는
    // 정확히 그 값이 상한이다(컬링이 빼면 그보다 적어진다).
    const uint32_t drawCount = static_cast<uint32_t>(m_batchStarts.size() - 1);

    if (drawCount <= kMinDrawsPerSlice)
    {
        return kCascadeCount;
    }
    const uint32_t perCascade = (std::max)(1u, drawCount / kMinDrawsPerSlice);
    return kCascadeCount * (std::min)(IRHIParallelCommandPool::kMaxWorkers / kCascadeCount, perCascade);
}

void EnhancedShadowPass::Shutdown()
{
    m_visibilityFrames.fill(nullptr);
    m_visibility.ShutdownAfterIdle();
    m_gpuBatches.clear();
    m_identityCount = 0;
    m_gpuVisibilityActive = false;
    m_hasGpuVisibilityCandidates = false;
    m_conservativeCasters.clear();
    m_drawGeometry.clear();
    m_bonePalettes.clear();
    m_boneOffsets.clear();
    m_boneCounts.clear();
    m_pso = {};
    m_skinnedPso = {};
    m_modelPsos.clear();
    m_alphaTextures.clear();
    m_sampler = {};
}
