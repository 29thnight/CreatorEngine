#include "EnhancedDecalPass.h"
#include "../../../RHI/DX12/DX12DeviceResources.h"
#include "../../../RHI/DX12/DX12PSOManager.h"
#include "../../../RHI/DX12/DX12RootSignatureCache.h"
#include "../../../RHI/DX12/DX12TextureCache.h"
#include "../../Graph/EnhancedRenderGraph.h"
#include "../../Graph/ShadowCasterBounds.h"
#include "../../../RHI/RHIEncoder.h"
#include "../../../TextureCodecImage.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
#include "../../../RHI/RHIShaderCompiler.h"

namespace
{
    // DX11 Decal.vs.hlsl + Decal.ps.hlsl의 이식.
    //
    // 좌표 복원·상자 판정·아틀라스 계산·채널 마스크를 전부 그대로 옮겼다.
    // 바꾼 것은 넷이다:
    //
    //   · 정점 버퍼를 없애고 상자를 상수 표로 둔다. 원본의 정점 24개와 인덱스
    //     36개를 그대로 옮겨 kCubeIndices[vid]로 짚으므로 삼각형 감김 방향이
    //     보존된다 — 뒷면 컬링 결과가 원본과 같아야 한다.
    //   · 데칼별 상수 버퍼를 StructuredBuffer로 바꾸고 SV_InstanceID로 짚는다.
    //   · 행렬 규약을 DX12 쪽에 맞춘다. DX11은 mul(M, v)(열 벡터)이고 여기는
    //     mul(v, M)(행 벡터, CPU가 전치)다. 수학적으로 같다.
    //   · decalForward를 뺀다. 원본이 계산하지만 그것을 쓰던 discard가 주석
    //     처리돼 있어 결과에 닿지 않는 죽은 값이다.
    constexpr const char* kDecalShaderFile = "Decal.slang";
    // Per-instance interpretation only. Batch.channel remains the 0..7 PSO
    // channel mask; these bits never index m_pipelines or affect blending.
    constexpr uint32_t kNormalBc5 = 1u << 3u;
    constexpr uint32_t kDiffuseLinearSample = 1u << 4u;

    bool IsSrgbTexture(RHIFormat format)
    {
        switch (format)
        {
        case RHIFormat::RGBA8UnormSrgb:
        case RHIFormat::BGRA8UnormSrgb:
        case RHIFormat::BC1UnormSrgb:
        case RHIFormat::BC3UnormSrgb:
        case RHIFormat::BC7UnormSrgb:
            return true;
        default:
            return false;
        }
    }

    struct DecalFrameConstants
    {
        math::matrix4x4 inverseView{};
        math::matrix4x4 inverseProjection{};
        math::matrix4x4 viewProjection{};
        float screenDimensions[2]{};
        uint32_t hasOwners{};
        uint32_t instanceBase{};
    };

    static_assert(sizeof(DecalFrameConstants) == 208);
    static_assert(offsetof(DecalFrameConstants, instanceBase) == 204);
    static_assert(std::is_trivially_copyable_v<DecalFrameConstants>);

    bool CompileDecalShader(const char* entry, const char* target, RHIShaderBlob& outBlob, std::string& outError)
    {
        return RHIShaderCompiler::CompileFile(kDecalShaderFile, entry, target, outBlob, outError);
    }

    /// 한 렌더 타깃의 블렌드. DX11 DecalPass가 채널마다 세우던 것 그대로다.
    ///
    /// 꺼진 채널은 BlendEnable을 끄는 것에 더해 쓰기 마스크를 0으로 둔다.
    /// 마스크가 0이면 타깃이 바인딩돼 있어도 건드리지 않으므로, 채널을 끄려고
    /// 타깃을 떼었다 붙였다 할 필요가 없다.
    RHIRenderTargetBlend MakeDecalBlend(bool enabled)
    {
        RHIRenderTargetBlend blend{};
        if (!enabled)
        {
            blend.enable = false;
            blend.writeMask = 0;
            return blend;
        }

        blend.enable = true;
        blend.srcColor = RHIBlendFactor::SrcAlpha;
        blend.dstColor = RHIBlendFactor::InvSrcAlpha;
        blend.colorOp = RHIBlendOp::Add;
        blend.srcAlpha = RHIBlendFactor::One;
        blend.dstAlpha = RHIBlendFactor::Zero;
        blend.alphaOp = RHIBlendOp::Add;
        blend.writeMask = 0xF;
        return blend;
    }
} // namespace

bool EnhancedDecalPass::Initialize(const EnhancedFrameContext& context, std::string& outError)
{
    if (nullptr == context.resources || nullptr == context.psoManager || nullptr == context.rootSignatures)
    {
        outError = "데칼 패스 컨텍스트가 불완전하다";
        return false;
    }

    return CreatePipelines(context, outError);
}

bool EnhancedDecalPass::CreatePipelines(const EnhancedFrameContext& context, std::string& outError)
{
    // b0 프레임 상수 · t7 데칼 배열(정점·픽셀 둘 다 읽는다) ·
    // t0~t3 GBuffer 사본 · t4~t6 데칼 텍스처 · s0 선형 s1 포인트.
    //
    // GBuffer 사본과 데칼 텍스처를 다른 테이블로 나눈 이유: 사본은 프레임에
    // 한 번 걸면 끝이고 데칼 텍스처는 배치마다 바뀐다. 한 테이블에 두면
    // 배치마다 사본 넷까지 다시 자르게 된다.
    const RHIPipelineLayoutParam params[] = {
        RHILayout::Cbv(0, RHIShaderVisibility::All),           RHILayout::Srv(7, RHIShaderVisibility::All),
        RHILayout::SrvTable(4, 0, RHIShaderVisibility::Pixel), // G버퍼
        RHILayout::SrvTable(3, 4, RHIShaderVisibility::Pixel), // 데칼 텍스처
        RHILayout::SrvTable(1, 8, RHIShaderVisibility::Pixel), // LX/legacy channel ABI
    };

    const RHIStaticSamplerDesc samplers[] = {
        {RHISampler::Linear(RHIAddressMode::Clamp), 0, RHIShaderVisibility::Pixel},
        {RHISampler::Point(RHIAddressMode::Clamp), 1, RHIShaderVisibility::Pixel},
    };

    RHIPipelineLayoutDesc rootDesc{};
    rootDesc.params = params;
    rootDesc.staticSamplers = samplers;

    const auto root = context.rootSignatures->GetOrCreate(rootDesc, outError);
    if (!root.IsValid())
    {
        return false;
    }

    RHIShaderBlob vsBlob;
    RHIShaderBlob psBlob;
    if (!CompileDecalShader("VSMain", "vs_5_0", vsBlob, outError))
    {
        return false;
    }
    if (!CompileDecalShader("PSMain", "ps_5_0", psBlob, outError))
    {
        return false;
    }

    // 채널 조합마다 PSO 하나. 조합 0은 텍스처가 하나도 없다는 뜻이라
    // 큐에 들어오지 않는다(DX11도 프록시 단계에서 걸러 낸다).
    for (uint32_t channel = 1; channel < kChannelCount; ++channel)
    {
        RHIGraphicsPipelineDesc desc{};
        desc.vsBytecode = vsBlob.Data();
        desc.vsSize = vsBlob.Size();
        desc.psBytecode = psBlob.Data();
        desc.psSize = psBlob.Size();
        desc.layout = root;
        desc.inputElements = nullptr;
        desc.inputElementCount = 0;
        desc.topologyType = RHITopologyType::Triangle;

        // 원본은 CD3D11_DEFAULT 래스터라이저다 — 뒷면 컬링.
        desc.cullMode = RHICullMode::Back;

        // 깊이는 보되 쓰지 않는다. 데칼 상자가 깊이를 덮어쓰면 뒤따르는
        // 패스가 상자의 깊이를 표면의 깊이로 착각한다.
        desc.depthEnable = true;
        desc.depthWriteMask = RHIDepthWrite::Zero;
        desc.depthFunc = RHICompareOp::LessEqual;
        desc.dsvFormat = EnhancedGBufferPass::kDepthFormat;

        desc.independentBlend = true;
        desc.renderTargetBlend[0] = MakeDecalBlend(0 != (channel & kChannelDiffuse));
        desc.renderTargetBlend[1] = MakeDecalBlend(0 != (channel & kChannelNormal));
        desc.renderTargetBlend[2] = MakeDecalBlend(0 != (channel & kChannelOrm));

        desc.numRenderTargets = 3;
        desc.rtvFormats[0] = EnhancedGBufferPass::GetRenderTargetFormat(0); // Diffuse
        desc.rtvFormats[1] = EnhancedGBufferPass::GetRenderTargetFormat(2); // Normal
        desc.rtvFormats[2] = EnhancedGBufferPass::GetRenderTargetFormat(1); // MetalRough

        m_pipelines[channel] = context.psoManager->GetOrCreate(desc, outError);
        if (!m_pipelines[channel].IsValid())
        {
            return false;
        }
    }

    return true;
}

bool EnhancedDecalPass::PrepareFrame(const EnhancedFrameContext& context, std::string& outError)
{
    m_width = context.width;
    m_height = context.height;
    m_instances.clear();
    m_batches.clear();
    m_visibilityFrame.reset();
    m_visibilitySpheres.clear();
    m_gpuVisibilityEnabled = context.resources && context.resources->GetIndirectDrawCapabilities().nonIndexedDraw;
    m_lastDecalCount = 0;
    m_lastBatchCount = 0;

    if (nullptr != context.camera)
    {
        // 스냅샷이 역행렬을 이미 들고 있다 — 패스마다 다시 구하지 않는다.
        m_inverseView = context.camera->inverseView;
        m_inverseProjection = context.camera->inverseProjection;
        m_viewProjection = context.camera->view * context.camera->projection;
    }
    else
    {
        // 카메라가 빠진 프레임에 직전 프레임의 밀봉 값이 남지 않게 한다.
        m_inverseView = math::matrix4x4::identity();
        m_inverseProjection = math::matrix4x4::identity();
        m_viewProjection = math::matrix4x4::identity();
    }

    if (m_decals.empty())
    {
        return true;
    }

    if (m_decals.size() > (std::numeric_limits<uint32_t>::max)())
    {
        outError = "Decal instance count exceeds 32-bit addressing.";
        return false;
    }
    m_visibilitySpheres.reserve(m_decals.size());

    // 텍스처 하나를 올리고 배치가 쓸 형태로 돌려준다. 없는 슬롯은 null이
    // 그대로 남고, Record가 그 자리에 null 디스크립터를 만든다 — 셰이더가
    // useFlags로 그 슬롯을 읽지 않으므로 값은 상관없지만, 테이블에 빈 칸을
    // 두면 검증 레이어가 잡는다.
    //
    // 캐시가 없으면 올리지 않고 넘어간다. 배칭은 원본 포인터로 판단하므로
    // 그래도 성립하고, 그리지 않고 묶음만 확인하는 검증이 이 경로를 쓴다.
    const auto upload = [&](const Texture* texture, RHITextureHandle& outResource, RHIFormat& outFormat,
                            uint32_t& outMips) -> bool {
        if (nullptr == texture)
        {
            return true;
        }
        if (nullptr == context.textureCache)
        {
            return false;
        }

        std::string textureError;
        const auto entry = context.textureCache->GetOrUpload(texture, context.TextureImage(texture), textureError);
        if (!entry.IsValid() || !textureError.empty())
        {
            return false;
        }

        outResource = entry.handle;
        outFormat = entry.format;
        outMips = entry.mipLevels;
        return true;
    };

    m_instances.reserve(m_decals.size());

    for (const auto& decal : m_decals)
    {
        const uint32_t channel = (nullptr != decal.diffuse ? kChannelDiffuse : 0u) |
                                 (nullptr != decal.normal ? kChannelNormal : 0u) |
                                 (nullptr != decal.occRoughMetal ? kChannelOrm : 0u);

        // 텍스처가 하나도 없으면 그릴 것이 없다.
        if (kChannelNone == channel)
        {
            continue;
        }

        Batch candidate{};
        candidate.channel = channel;
        candidate.texturePinIndices = decal.texturePinIndices;
        candidate.textureIds[0] = TextureFramePins::Identity(decal.diffuse);
        candidate.textureIds[1] = TextureFramePins::Identity(decal.normal);
        candidate.textureIds[2] = TextureFramePins::Identity(decal.occRoughMetal);
        if (!upload(decal.diffuse, candidate.textures[0], candidate.formats[0], candidate.mipLevels[0]) ||
            !upload(decal.normal, candidate.textures[1], candidate.formats[1], candidate.mipLevels[1]) ||
            !upload(decal.occRoughMetal, candidate.textures[2], candidate.formats[2], candidate.mipLevels[2]))
        {
            // 한 장이 실패해도 프레임을 세우지 않는다 — 그 데칼만 빠진다.
            continue;
        }

        // ★ 정렬하지 않고 '연속한 것'만 묶는다.
        //
        // 데칼은 블렌드하므로 겹친 둘의 순서가 바뀌면 결과가 달라진다.
        // 배치를 키우려고 큐를 정렬하면 그림이 바뀌는데, 그 차이는 겹치는
        // 데칼에서만 나타나 '가끔 다르다'로만 드러난다. 순서를 지킨다.
        const bool sameAsPrevious = !m_batches.empty() && m_batches.back().channel == candidate.channel &&
                                    m_batches.back().textureIds[0] == candidate.textureIds[0] &&
                                    m_batches.back().textureIds[1] == candidate.textureIds[1] &&
                                    m_batches.back().textureIds[2] == candidate.textureIds[2];

        if (!sameAsPrevious)
        {
            candidate.firstInstance = static_cast<uint32_t>(m_instances.size());
            candidate.instanceCount = 0;
            m_batches.push_back(candidate);
        }
        ++m_batches.back().instanceCount;

        InstanceData instance{};
        instance.world = math::transpose(decal.worldMatrix);
        instance.inverseWorld = math::transpose(math::inverse(decal.worldMatrix));
        instance.useFlags = channel;
        if (candidate.formats[1] == RHIFormat::BC5Unorm)
        {
            instance.useFlags |= kNormalBc5;
        }
        // Native sRGB reads and explicitly locked cooked Linear reads are
        // already linear samples. Only unlocked legacy UNORM keeps its pow path.
        const auto diffuseImage = context.TextureImage(decal.diffuse);
        if (IsSrgbTexture(candidate.formats[0]) || (diffuseImage && diffuseImage->colorSpaceLocked))
        {
            instance.useFlags |= kDiffuseLinearSample;
        }
        instance.sliceX = (std::max)(1u, decal.sliceX);
        instance.sliceY = (std::max)(1u, decal.sliceY);
        instance.sliceNum = decal.sliceNum;
        m_instances.push_back(instance);
        EnhancedDrawItem bounds{};
        bounds.worldMatrix = decal.worldMatrix;
        // Encloses [-.5,.5]^3, rounded upward. WorldBounds includes shear,
        // float arithmetic error and unknown/nonaffine keep-visible handling.
        bounds.boundRadius = 0.866026f;
        const auto sphere = shadow_math::WorldBounds(bounds);
        m_visibilitySpheres.push_back({sphere.center.x, sphere.center.y, sphere.center.z, sphere.radius});
    }

    m_lastDecalCount = static_cast<uint32_t>(m_instances.size());
    m_lastBatchCount = static_cast<uint32_t>(m_batches.size());
    return true;
}

bool EnhancedDecalPass::PrepareGpuVisibility(const EnhancedFrameContext& context, std::string& outError)
{
    m_visibilityFrame.reset();
    if (!m_gpuVisibilityEnabled || m_batches.empty())
    {
        outError.clear();
        return true;
    }
    if (!context.resources || !context.resources->GetIndirectDrawCapabilities().nonIndexedDraw ||
        m_visibilitySpheres.size() != m_instances.size())
    {
        outError = "Decal GPU visibility lost its prepared geometry contract.";
        return false;
    }
    if (!m_visibility.PreparePipelines(context, outError))
    {
        return false;
    }
    std::vector<GpuGeometryVisibility::Candidate> candidates;
    std::vector<GpuGeometryVisibility::Bin> bins;
    candidates.reserve(m_instances.size());
    bins.reserve(m_batches.size());
    uint64_t outputOffset = 0;
    for (const auto& batch : m_batches)
    {
        const uint64_t count = batch.instanceCount;
        if (!count || uint64_t(batch.firstInstance) + count > m_instances.size() ||
            outputOffset + count > (std::numeric_limits<uint32_t>::max)())
        {
            outError = "Decal GPU visibility exceeds valid instance/output ranges.";
            return false;
        }
        const uint32_t bin = static_cast<uint32_t>(bins.size());
        // Deliberate 16B nonindexed prefix of a 20B GPU-produced indexed record.
        // firstIndex and baseVertex MUST both remain zero (RHI ABI assertions).
        // Preserve the entire contiguous batch if any instance is visible; GPU
        // compaction may not reorder blending or change original instance IDs.
        bins.push_back({36u, 0u, 0, batch.instanceCount});
        for (uint32_t local = 0; local < batch.instanceCount; ++local)
        {
            const auto sphere = m_visibilitySpheres[batch.firstInstance + local];
            const uint32_t flags = sphere.w > 0.f ? 0u : GpuGeometryVisibility::kConservative;
            candidates.push_back({sphere, bin, local, static_cast<uint32_t>(outputOffset), flags});
        }
        outputOffset += count;
        outputOffset = (outputOffset + GpuGeometryVisibility::kOutputAlignment - 1u) &
            ~(uint64_t(GpuGeometryVisibility::kOutputAlignment) - 1u);
    }
    if (!m_visibility.Prepare(context, m_viewProjection, candidates, bins, m_visibilityFrame, outError))
    {
        return false;
    }
    if (!m_visibilityFrame)
    {
        outError = "Decal GPU visibility did not produce a supported indirect frame.";
        return false;
    }
    return true;
}

void EnhancedDecalPass::Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context)
{
    m_outputs = m_inputs;
    const bool versioned = graph.GetSchedulingMode() == RGSchedulingMode::ExplicitVersioned;
    const bool explicitAccess = graph.GetSchedulingMode() != RGSchedulingMode::DeclarationOrder;
    const auto readAccess = explicitAccess ? RGAccessMode::Read : RGAccessMode::LegacyState;
    const auto writeAccess = explicitAccess ? RGAccessMode::Write : RGAccessMode::LegacyState;
    const auto modifyAccess = explicitAccess ? RGAccessMode::ReadWrite : RGAccessMode::LegacyState;
    m_copiedDiffuse = RGHandle{};
    m_copiedNormal = RGHandle{};
    m_copiedOrm = RGHandle{};

    if (0 == m_width || 0 == m_height)
    {
        return;
    }
    if (!m_inputs.diffuse.IsValid() || !m_inputs.normal.IsValid() || !m_inputs.metalRough.IsValid() ||
        !m_inputs.depth.IsValid())
    {
        return;
    }

    // 그릴 것이 없으면 사본도 뜨지 않는다. DX11은 데칼이 0개여도 화면 크기
    // 복사 넷을 매 프레임 했다 — 데칼을 쓰지 않는 씬이 그 값을 치를 이유가 없다.
    if (m_batches.empty())
    {
        return;
    }

    const auto visibility = m_visibilityFrame;
    if (m_gpuVisibilityEnabled && !visibility)
    {
        throw std::runtime_error("Decal GPU visibility must be prepared before graph declaration.");
    }
    if (visibility)
    {
        visibility->Declare(graph);
    }

    RGTextureDesc desc{};
    desc.width = m_width;
    desc.height = m_height;

    desc.format = EnhancedGBufferPass::GetRenderTargetFormat(0);
    desc.name = "Decal.CopiedDiffuse";
    m_copiedDiffuse = graph.CreateTexture(desc);

    desc.format = EnhancedGBufferPass::GetRenderTargetFormat(2);
    desc.name = "Decal.CopiedNormal";
    m_copiedNormal = graph.CreateTexture(desc);

    desc.format = EnhancedGBufferPass::GetRenderTargetFormat(1);
    desc.name = "Decal.CopiedOrm";
    m_copiedOrm = graph.CreateTexture(desc);
    if (versioned)
    {
        m_copiedDiffuse = graph.Write(m_copiedDiffuse);
        m_copiedNormal = graph.Write(m_copiedNormal);
        m_copiedOrm = graph.Write(m_copiedOrm);
    }
    const auto incoming = m_inputs;
    const auto baseline = GetBaseline();

    // ── 사본 뜨기 ──
    //
    // 셋은 정말로 읽으면서 쓰는 대상이라 피할 수 없다. 셰이더가 바탕색과
    // 바탕 노멀을 읽어 섞은 결과를 같은 타깃에 블렌드하기 때문이다.
    //
    // 깊이는 여기 없다 — 읽기 전용 DSV로 테스트하면서 SRV로도 읽으므로
    // 사본이 필요 없다. DX11이 뜨던 넷 중 하나가 이렇게 빠진다.
    graph.AddPass("Decal.Snapshot",
                  {
                      {incoming.diffuse, RHIResourceState::CopySource, readAccess},
                      {incoming.normal, RHIResourceState::CopySource, readAccess},
                      {incoming.metalRough, RHIResourceState::CopySource, readAccess},
                      {m_copiedDiffuse, RHIResourceState::CopyDest, writeAccess},
                      {m_copiedNormal, RHIResourceState::CopyDest, writeAccess},
                      {m_copiedOrm, RHIResourceState::CopyDest, writeAccess},
                  },
                  [incoming, baseline](const EnhancedRenderGraph::ExecuteContext& executeContext) {
                      const auto copyOne = [&](RGHandle source, RGHandle destination) {
                          executeContext.encoder->CopyResource(executeContext.ResolveHandle(destination),
                                                               executeContext.ResolveHandle(source));
                      };

                      copyOne(incoming.diffuse, baseline[0]);
                      copyOne(incoming.normal, baseline[2]);
                      copyOne(incoming.metalRough, baseline[1]);
                  });

    if (versioned)
    {
        m_outputs.diffuse = graph.Modify(incoming.diffuse);
        m_outputs.normal = graph.Modify(incoming.normal);
        m_outputs.metalRough = graph.Modify(incoming.metalRough);
    }
    const auto outputs = m_outputs;

    // ── 덧칠 ──
    std::vector<EnhancedRenderGraph::RGPassUsage> applyUses{
        {m_copiedDiffuse, RHIResourceState::ShaderResource, readAccess},
        {m_copiedNormal, RHIResourceState::ShaderResource, readAccess},
        {m_copiedOrm, RHIResourceState::ShaderResource, readAccess},
        {outputs.depth, RHIResourceState::DepthReadShaderResource, readAccess},
        {outputs.diffuse, RHIResourceState::RenderTarget, modifyAccess},
        {outputs.normal, RHIResourceState::RenderTarget, modifyAccess},
        {outputs.metalRough, RHIResourceState::RenderTarget, modifyAccess},
    };
    if (m_inputs.bitmask.IsValid())
    {
        applyUses.push_back({outputs.bitmask, RHIResourceState::PixelShaderResource, readAccess});
    }
    if (explicitAccess)
    {
        for (const auto& batch : m_batches)
        {
            for (const auto texture : batch.textures)
            {
                if (!texture.IsValid())
                {
                    continue;
                }
                auto handle = graph.FindImportedTexture(texture);
                if (!handle.IsValid())
                {
                    handle = graph.ImportTexture(texture, RHIResourceState::PixelShaderResource, "Decal.Texture");
                }
                if (std::none_of(applyUses.begin(), applyUses.end(), [handle](const auto& use) {
                        return use.handle.index == handle.index && use.handle.version == handle.version;
                    }))
                {
                    applyUses.push_back({handle, RHIResourceState::PixelShaderResource, readAccess});
                }
            }
        }
    }
    if (visibility)
    {
        visibility->AddReadUsages(graph, applyUses);
    }
    graph.AddPass(
        "Decal.Apply", applyUses,
        [this, outputs, baseline, visibility, &context](const EnhancedRenderGraph::ExecuteContext& executeContext) {
            RHIEncoder& encoder = *executeContext.encoder;

            // 타깃 순서는 셰이더 출력 순서다(확산·노멀·ORM). GBuffer의
            // 저장 순서(확산·ORM·노멀)와 다르므로 여기서 맞춰 건다.
            const RHITextureHandle colors[3] = {
                executeContext.ResolveHandle(outputs.diffuse),
                executeContext.ResolveHandle(outputs.normal),
                executeContext.ResolveHandle(outputs.metalRough),
            };

            // ★ 읽기 전용 깊이. 이것이 없으면 같은 리소스를 SRV로도 읽는 것이
            // 불법이 된다 — 깊이 사본을 없앤 근거가 이 한 줄이다.
            const auto depthDesc = RHIDepthTargetDesc::DepthReadOnly(executeContext.ResolveHandle(outputs.depth),
                                                                     EnhancedGBufferPass::kDepthFormat);

            const auto targets = context.resources->CreateRenderTargets(colors, &depthDesc);
            if (!targets.IsValid())
            {
                if (visibility)
                {
                    throw std::runtime_error("Decal render-target binding failed on the prepared GPU route.");
                }
                return;
            }

            encoder.SetViewportAndScissor(m_width, m_height);
            encoder.BindRenderTargets(targets);

            // 프레임 상수 — 데칼 전체가 공유한다.
            DecalFrameConstants constants{};
            constants.inverseView = math::transpose(m_inverseView);
            constants.inverseProjection = math::transpose(m_inverseProjection);
            constants.viewProjection = math::transpose(m_viewProjection);
            constants.screenDimensions[0] = static_cast<float>(m_width);
            constants.screenDimensions[1] = static_cast<float>(m_height);
            constants.hasOwners = outputs.bitmask.IsValid();

            // 데칼 배열 — 데칼마다 상수 버퍼를 갱신하던 것을 한 번의 업로드로.
            const auto instanceBuffer = context.resources->AllocateUpload(RHIUploadRequest{
                sizeof(InstanceData) * m_instances.size(), RHIUploadUsage::BufferCopy, 256});
            if (!instanceBuffer.IsValid())
            {
                if (visibility)
                {
                    throw std::runtime_error("Decal instance upload failed on the prepared GPU route.");
                }
                return;
            }
            memcpy(instanceBuffer.cpuAddress, m_instances.data(), sizeof(InstanceData) * m_instances.size());

            // GBuffer 사본 넷(깊이 + 확산·노멀·ORM). 프레임에 한 번만 자른다.
            const RHIBindingDesc gbufferSrvs[] = {
                RHIBindingDesc::SrvDepth(executeContext.ResolveHandle(outputs.depth)),
                RHIBindingDesc::Srv(executeContext.ResolveHandle(baseline[0])),
                RHIBindingDesc::Srv(executeContext.ResolveHandle(baseline[2])),
                RHIBindingDesc::Srv(executeContext.ResolveHandle(baseline[1])),
            };
            const RHIBindingTable gbufferSrv = context.resources->CreateBindings(gbufferSrvs);
            if (!gbufferSrv.IsValid())
            {
                if (visibility)
                {
                    throw std::runtime_error("Decal GBuffer bindings failed on the prepared GPU route.");
                }
                return;
            }
            const RHIBindingDesc ownerDesc =
                RHIBindingDesc::Srv2D(constants.hasOwners ? executeContext.ResolveHandle(outputs.bitmask)
                                                          : RHITextureHandle{},
                                      RHIFormat::R32Uint)
                    .OrNull();
            const auto owners = context.resources->CreateBindings({&ownerDesc, 1});
            if (!owners.IsValid())
            {
                if (visibility)
                {
                    throw std::runtime_error("Decal owner bindings failed on the prepared GPU route.");
                }
                return;
            }

            // ★ 예전에는 여기서 `SetPipeline(..., nullptr, m_rootSignature)` 로
            //   **루트 시그니처만** 걸었다. A-1 이후로 표현 불가능하다 — 핸들
            //   하나가 짝을 들므로 "파이프라인 없이 레이아웃만"이 계약에 없다.
            //
            //   ★ 없어진 것이 맞다. `RHIEncoder` ③이 "루트 시그니처를 안 걸고
            //     루트를 건드린다"를 막았는데 그 이웃에 "파이프라인 없이 루트만
            //     건다"가 남아 있었다. Vulkan 에는 후자를 표현할 방법이 아예
            //     없다 — 디스크립터 셋을 걸려면 파이프라인이 먼저 걸려 있어야
            //     하고, 레이아웃도 그 파이프라인에서 따라온다.
            //
            //   대신 실제 파이프라인 하나를 건다. 채널 넷이 레이아웃을
            //   공유하므로 어느 것을 걸어도 루트 시그니처는 같고, 루프가
            //   배치마다 PSO를 갈 때 인코더가 루트 시그니처 중복을 걸러 낸다.
            RHIPipelineHandle rootBinder{};
            for (const auto& batch : m_batches)
            {
                if (m_pipelines[batch.channel].IsValid())
                {
                    rootBinder = m_pipelines[batch.channel];
                    break;
                }
            }
            if (!rootBinder.IsValid())
            {
                if (visibility)
                {
                    throw std::runtime_error("Decal graphics binding failed on the prepared GPU route.");
                }
                return; // 그릴 수 있는 배치가 없다
            }

            encoder.SetPipeline(RHIBindPoint::Graphics, rootBinder);
            encoder.SetRootBuffer(RHIBindPoint::Graphics, 1, instanceBuffer);
            encoder.SetBindings(RHIBindPoint::Graphics, 2, gbufferSrv);
            encoder.SetBindings(RHIBindPoint::Graphics, 4, owners);
            encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);

            for (uint32_t batchIndex = 0; batchIndex < m_batches.size(); ++batchIndex)
            {
                const auto& batch = m_batches[batchIndex];
                if (!m_pipelines[batch.channel].IsValid())
                {
                    if (visibility)
                    {
                        throw std::runtime_error("Decal lost a prepared graphics pipeline.");
                    }
                    continue;
                }

                // 없는 슬롯에는 널 디스크립터를 깐다(OrNull). 셰이더가 useFlags로
                // 읽지 않지만, 테이블에 빈 칸을 두면 검증 레이어가 잡는다.
                const auto decalSlot = [&batch](uint32_t i) {
                    const bool has = batch.textures[i].IsValid();
                    return RHIBindingDesc::Srv2D(batch.textures[i], has ? batch.formats[i] : RHIFormat::RGBA8Unorm, 0,
                                                 has ? (std::max)(1u, batch.mipLevels[i]) : 1)
                        .OrNull();
                };
                const RHIBindingDesc decalSrvs[] = {decalSlot(0), decalSlot(1), decalSlot(2)};
                const RHIBindingTable decalSrv = context.resources->CreateBindings(decalSrvs);
                if (!decalSrv.IsValid())
                {
                    if (visibility)
                    {
                        throw std::runtime_error("Decal texture bindings failed on the prepared GPU route.");
                    }
                    break; // 링이 찼다 — 남은 배치도 마찬가지다
                }

                encoder.SetPipeline(RHIBindPoint::Graphics, m_pipelines[batch.channel]);
                encoder.SetBindings(RHIBindPoint::Graphics, 3, decalSrv);

                constants.instanceBase = batch.firstInstance;
                const auto frameCb = context.resources->UploadConstants(&constants, sizeof(constants));
                if (!frameCb.IsValid())
                {
                    throw std::runtime_error("Decal batch constant upload failed.");
                }
                encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0, frameCb);
                // Preserve procedural vertices 0..35 and original instance order.
                // GPU firstInstance is zero; the shader explicitly adds the base.
                if (visibility)
                {
                    if (!encoder.DrawIndirect(visibility->Arguments(), visibility->ArgsOffset(batchIndex)))
                    {
                        throw std::runtime_error("Decal nonindexed indirect submission failed.");
                    }
                }
                else
                {
                    encoder.Draw(36, batch.instanceCount);
                }
            }
        },
        m_keepAlive);
}

void EnhancedDecalPass::Shutdown()
{
    m_visibilityFrame.reset();
    m_visibility.ShutdownAfterIdle();
    m_gpuVisibilityEnabled = false;
    m_visibilitySpheres.clear();
    m_width = 0;
    m_height = 0;
    m_decals.clear();
    m_instances.clear();
    m_batches.clear();
    m_texturePins.reset();
    for (auto& pipeline : m_pipelines)
    {
        pipeline = {};
    }
}
