#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedDecalPass.h"
#include "DX12DeviceResources.h"
#include "DX12PSOManager.h"
#include "DX12RootSignatureCache.h"
#include "DX12TextureCache.h"
#include "EnhancedRenderGraph.h"
#include "RHIEncoder.h"

#include <cstring>
#include <sstream>
#include <string>
#include <vector>
#include "DX12ShaderCompiler.h"

namespace
{
    // 유니티 빌드에서 익명 네임스페이스가 합쳐지므로 이름을 고유하게 둔다.
    std::string DecalHrToString(HRESULT hr)
    {
        std::ostringstream oss;
        oss << "HRESULT 0x" << std::hex << static_cast<unsigned long>(hr);
        return oss.str();
    }

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
    constexpr const char* kDecalShaderFile = "Decal.hlsl";

    struct DecalFrameConstants
    {
        Mathf::Matrix inverseView{};
        Mathf::Matrix inverseProjection{};
        Mathf::Matrix viewProjection{};
        float         screenDimensions[2]{};
        float         padding[2]{};
    };

    bool CompileDecalShader(const char* entry, const char* target,
        RHIShaderBlob& outBlob, std::string& outError)
    {
        return DX12ShaderCompiler::CompileFile(kDecalShaderFile, entry, target, outBlob, outError);
    }

    /// 한 렌더 타깃의 블렌드. DX11 DecalPass가 채널마다 세우던 것 그대로다.
    ///
    /// 꺼진 채널은 BlendEnable을 끄는 것에 더해 쓰기 마스크를 0으로 둔다.
    /// 마스크가 0이면 타깃이 바인딩돼 있어도 건드리지 않으므로, 채널을 끄려고
    /// 타깃을 떼었다 붙였다 할 필요가 없다.
    D3D12_RENDER_TARGET_BLEND_DESC MakeDecalBlend(bool enabled)
    {
        D3D12_RENDER_TARGET_BLEND_DESC blend{};
        if (!enabled)
        {
            blend.BlendEnable = FALSE;
            blend.RenderTargetWriteMask = 0;
            return blend;
        }

        blend.BlendEnable = TRUE;
        blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        blend.BlendOp = D3D12_BLEND_OP_ADD;
        blend.SrcBlendAlpha = D3D12_BLEND_ONE;
        blend.DestBlendAlpha = D3D12_BLEND_ZERO;
        blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        return blend;
    }
}

bool EnhancedDecalPass::Initialize(const EnhancedFrameContext& context, std::string& outError)
{
    if (nullptr == context.resources || nullptr == context.psoManager ||
        nullptr == context.rootSignatures)
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
        RHILayout::Cbv(0, RHIShaderVisibility::All),
        RHILayout::Srv(7, RHIShaderVisibility::All),
        RHILayout::SrvTable(4, 0, RHIShaderVisibility::Pixel),   // G버퍼
        RHILayout::SrvTable(3, 4, RHIShaderVisibility::Pixel),   // 데칼 텍스처
    };

    const RHIStaticSamplerDesc samplers[] = {
        { RHISampler::Linear(RHIAddressMode::Clamp), 0, RHIShaderVisibility::Pixel },
        { RHISampler::Point(RHIAddressMode::Clamp),  1, RHIShaderVisibility::Pixel },
    };

    RHIPipelineLayoutDesc rootDesc{};
    rootDesc.params = params;
    rootDesc.staticSamplers = samplers;

    const auto root = context.rootSignatures->GetOrCreate(rootDesc, outError);
    if (!root.IsValid()) return false;
    m_rootSignature = root.signature;

    RHIShaderBlob vsBlob;
    RHIShaderBlob psBlob;
    if (!CompileDecalShader("VSMain", "vs_5_0", vsBlob, outError)) return false;
    if (!CompileDecalShader("PSMain", "ps_5_0", psBlob, outError)) return false;

    // 채널 조합마다 PSO 하나. 조합 0은 텍스처가 하나도 없다는 뜻이라
    // 큐에 들어오지 않는다(DX11도 프록시 단계에서 걸러 낸다).
    for (uint32_t channel = 1; channel < kChannelCount; ++channel)
    {
        DX12GraphicsPipelineDesc desc{};
        desc.vsBytecode = vsBlob.Data();
        desc.vsSize = vsBlob.Size();
        desc.psBytecode = psBlob.Data();
        desc.psSize = psBlob.Size();
        desc.rootSignature = root.signature;
        desc.rootSignatureId = root.id;
        desc.inputElements = nullptr;
        desc.inputElementCount = 0;
        desc.topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

        // 원본은 CD3D11_DEFAULT 래스터라이저다 — 뒷면 컬링.
        desc.cullMode = D3D12_CULL_MODE_BACK;

        // 깊이는 보되 쓰지 않는다. 데칼 상자가 깊이를 덮어쓰면 뒤따르는
        // 패스가 상자의 깊이를 표면의 깊이로 착각한다.
        desc.depthEnable = true;
        desc.depthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        desc.depthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        desc.dsvFormat = ToDXGI(EnhancedGBufferPass::kDepthFormat);

        desc.independentBlend = true;
        desc.renderTargetBlend[0] = MakeDecalBlend(0 != (channel & kChannelDiffuse));
        desc.renderTargetBlend[1] = MakeDecalBlend(0 != (channel & kChannelNormal));
        desc.renderTargetBlend[2] = MakeDecalBlend(0 != (channel & kChannelOrm));

        desc.numRenderTargets = 3;
        desc.rtvFormats[0] = EnhancedGBufferPass::GetRenderTargetFormat(0);   // Diffuse
        desc.rtvFormats[1] = EnhancedGBufferPass::GetRenderTargetFormat(2);   // Normal
        desc.rtvFormats[2] = EnhancedGBufferPass::GetRenderTargetFormat(1);   // MetalRough

        m_pipelines[channel] = context.psoManager->GetOrCreate(desc, outError);
        if (nullptr == m_pipelines[channel]) return false;
    }

    return true;
}

bool EnhancedDecalPass::PrepareFrame(const EnhancedFrameContext& context, std::string& outError)
{
    m_width = context.width;
    m_height = context.height;
    m_instances.clear();
    m_batches.clear();
    m_lastDecalCount = 0;
    m_lastBatchCount = 0;

    if (nullptr != context.camera)
    {
        // 스냅샷이 역행렬을 이미 들고 있다 — 패스마다 다시 구하지 않는다.
        m_inverseView = XMMatrixTranspose(context.camera->inverseView);
        m_inverseProjection = XMMatrixTranspose(context.camera->inverseProjection);
    }

    if (m_decals.empty()) return true;

    // 텍스처 하나를 올리고 배치가 쓸 형태로 돌려준다. 없는 슬롯은 null이
    // 그대로 남고, Record가 그 자리에 null 디스크립터를 만든다 — 셰이더가
    // useFlags로 그 슬롯을 읽지 않으므로 값은 상관없지만, 테이블에 빈 칸을
    // 두면 검증 레이어가 잡는다.
    //
    // 캐시가 없으면 올리지 않고 넘어간다. 배칭은 원본 포인터로 판단하므로
    // 그래도 성립하고, 그리지 않고 묶음만 확인하는 검증이 이 경로를 쓴다.
    const auto upload = [&](Texture* texture, RHITextureHandle& outResource,
        RHIFormat& outFormat, uint32_t& outMips) -> bool
    {
        if (nullptr == texture || nullptr == context.textureCache) return true;

        std::string textureError;
        const auto entry = context.textureCache->GetOrUpload(texture, textureError);
        if (!entry.IsValid()) return false;

        outResource = entry.handle;
        outFormat = FromDXGI(entry.format);
        outMips = entry.mipLevels;
        return true;
    };

    m_instances.reserve(m_decals.size());

    for (const auto& decal : m_decals)
    {
        const uint32_t channel =
            (nullptr != decal.diffuse ? kChannelDiffuse : 0u) |
            (nullptr != decal.normal ? kChannelNormal : 0u) |
            (nullptr != decal.occRoughMetal ? kChannelOrm : 0u);

        // 텍스처가 하나도 없으면 그릴 것이 없다.
        if (kChannelNone == channel) continue;

        Batch candidate{};
        candidate.channel = channel;
        candidate.sources[0] = decal.diffuse;
        candidate.sources[1] = decal.normal;
        candidate.sources[2] = decal.occRoughMetal;
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
        const bool sameAsPrevious = !m_batches.empty() &&
            m_batches.back().channel == candidate.channel &&
            m_batches.back().sources[0] == candidate.sources[0] &&
            m_batches.back().sources[1] == candidate.sources[1] &&
            m_batches.back().sources[2] == candidate.sources[2];

        if (!sameAsPrevious)
        {
            candidate.firstInstance = static_cast<uint32_t>(m_instances.size());
            candidate.instanceCount = 0;
            m_batches.push_back(candidate);
        }
        ++m_batches.back().instanceCount;

        InstanceData instance{};
        instance.world = XMMatrixTranspose(decal.worldMatrix);
        instance.inverseWorld = XMMatrixTranspose(
            XMMatrixInverse(nullptr, decal.worldMatrix));
        instance.useFlags = channel;
        instance.sliceX = (std::max)(1u, decal.sliceX);
        instance.sliceY = (std::max)(1u, decal.sliceY);
        instance.sliceNum = decal.sliceNum;
        m_instances.push_back(instance);
    }

    m_lastDecalCount = static_cast<uint32_t>(m_instances.size());
    m_lastBatchCount = static_cast<uint32_t>(m_batches.size());
    return true;
}

void EnhancedDecalPass::Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context)
{
    m_copiedDiffuse = RGHandle{};
    m_copiedNormal = RGHandle{};
    m_copiedOrm = RGHandle{};

    if (0 == m_width || 0 == m_height) return;
    if (!m_inputs.diffuse.IsValid() || !m_inputs.normal.IsValid() ||
        !m_inputs.metalRough.IsValid() || !m_inputs.depth.IsValid()) return;

    // 그릴 것이 없으면 사본도 뜨지 않는다. DX11은 데칼이 0개여도 화면 크기
    // 복사 넷을 매 프레임 했다 — 데칼을 쓰지 않는 씬이 그 값을 치를 이유가 없다.
    if (m_batches.empty()) return;

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

    // ── 사본 뜨기 ──
    //
    // 셋은 정말로 읽으면서 쓰는 대상이라 피할 수 없다. 셰이더가 바탕색과
    // 바탕 노멀을 읽어 섞은 결과를 같은 타깃에 블렌드하기 때문이다.
    //
    // 깊이는 여기 없다 — 읽기 전용 DSV로 테스트하면서 SRV로도 읽으므로
    // 사본이 필요 없다. DX11이 뜨던 넷 중 하나가 이렇게 빠진다.
    graph.AddPass("Decal.Snapshot",
        {
            { m_inputs.diffuse,    RHIResourceState::CopySource },
            { m_inputs.normal,     RHIResourceState::CopySource },
            { m_inputs.metalRough, RHIResourceState::CopySource },
            { m_copiedDiffuse,     RHIResourceState::CopyDest },
            { m_copiedNormal,      RHIResourceState::CopyDest },
            { m_copiedOrm,         RHIResourceState::CopyDest },
        },
        [this](const EnhancedRenderGraph::ExecuteContext& executeContext)
        {
            const auto copyOne = [&](RGHandle source, RGHandle destination)
            {
                executeContext.encoder->CopyResource(
                    executeContext.Resolve(destination), executeContext.Resolve(source));
            };

            copyOne(m_inputs.diffuse, m_copiedDiffuse);
            copyOne(m_inputs.normal, m_copiedNormal);
            copyOne(m_inputs.metalRough, m_copiedOrm);
        });

    // ── 덧칠 ──
    graph.AddPass("Decal.Apply",
        {
            { m_copiedDiffuse,     RHIResourceState::ShaderResource },
            { m_copiedNormal,      RHIResourceState::ShaderResource },
            { m_copiedOrm,         RHIResourceState::ShaderResource },
            { m_inputs.depth,      RHIResourceState::DepthReadShaderResource },
            { m_inputs.diffuse,    RHIResourceState::RenderTarget },
            { m_inputs.normal,     RHIResourceState::RenderTarget },
            { m_inputs.metalRough, RHIResourceState::RenderTarget },
        },
        [this, &context](const EnhancedRenderGraph::ExecuteContext& executeContext)
        {
            RHIEncoder& encoder = *executeContext.encoder;

            // 타깃 순서는 셰이더 출력 순서다(확산·노멀·ORM). GBuffer의
            // 저장 순서(확산·ORM·노멀)와 다르므로 여기서 맞춰 건다.
            const RHITextureHandle colors[3] = {
                executeContext.ResolveHandle(m_inputs.diffuse),
                executeContext.ResolveHandle(m_inputs.normal),
                executeContext.ResolveHandle(m_inputs.metalRough),
            };

            // ★ 읽기 전용 깊이. 이것이 없으면 같은 리소스를 SRV로도 읽는 것이
            // 불법이 된다 — 깊이 사본을 없앤 근거가 이 한 줄이다.
            const auto depthDesc = RHIDepthTargetDesc::DepthReadOnly(
                executeContext.ResolveHandle(m_inputs.depth), EnhancedGBufferPass::kDepthFormat);

            const auto targets = context.resources->CreateRenderTargets(colors, &depthDesc);
            if (!targets.IsValid()) return;

            encoder.SetViewportAndScissor(m_width, m_height);
            encoder.BindRenderTargets(targets);

            // 프레임 상수 — 데칼 전체가 공유한다.
            DecalFrameConstants constants{};
            constants.inverseView = m_inverseView;
            constants.inverseProjection = m_inverseProjection;
            constants.viewProjection = (nullptr != context.camera)
                ? XMMatrixTranspose(context.camera->view * context.camera->projection)
                : XMMatrixIdentity();
            constants.screenDimensions[0] = static_cast<float>(m_width);
            constants.screenDimensions[1] = static_cast<float>(m_height);

            const auto frameCb = context.resources->GetUploadRing().Allocate(
                sizeof(DecalFrameConstants), DX12UploadRing::kConstantBufferAlignment);
            if (!frameCb.IsValid()) return;
            memcpy(frameCb.cpuAddress, &constants, sizeof(constants));

            // 데칼 배열 — 데칼마다 상수 버퍼를 갱신하던 것을 한 번의 업로드로.
            const auto instanceBuffer = context.resources->GetUploadRing().Allocate(
                sizeof(InstanceData) * m_instances.size(), sizeof(InstanceData));
            if (!instanceBuffer.IsValid()) return;
            memcpy(instanceBuffer.cpuAddress, m_instances.data(),
                sizeof(InstanceData) * m_instances.size());

            // GBuffer 사본 넷(깊이 + 확산·노멀·ORM). 프레임에 한 번만 자른다.
            const RHIBindingDesc gbufferSrvs[] = {
                RHIBindingDesc::SrvDepth(executeContext.ResolveHandle(m_inputs.depth)),
                RHIBindingDesc::Srv(executeContext.ResolveHandle(m_copiedDiffuse)),
                RHIBindingDesc::Srv(executeContext.ResolveHandle(m_copiedNormal)),
                RHIBindingDesc::Srv(executeContext.ResolveHandle(m_copiedOrm)),
            };
            const RHIBindingTable gbufferSrv = context.resources->CreateBindings(gbufferSrvs);
            if (!gbufferSrv.IsValid()) return;

            // ★ PSO는 배치마다 다르므로(채널별 블렌드) 여기서는 루트 시그니처만
            //   건다. 아래 루프가 같은 시그니처로 SetPipeline을 다시 부르지만
            //   인코더가 중복을 걸러 내므로, 방금 건 프레임 상수와 테이블이
            //   루프 내내 그대로 남는다.
            encoder.SetPipeline(RHIBindPoint::Graphics, nullptr, m_rootSignature);
            encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0, frameCb.gpuAddress);
            encoder.SetRootBuffer(RHIBindPoint::Graphics, 1, instanceBuffer.gpuAddress);
            encoder.SetBindings(RHIBindPoint::Graphics, 2, gbufferSrv);
            encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);

            for (const auto& batch : m_batches)
            {
                if (nullptr == m_pipelines[batch.channel]) continue;

                // 없는 슬롯에는 널 디스크립터를 깐다(OrNull). 셰이더가 useFlags로
                // 읽지 않지만, 테이블에 빈 칸을 두면 검증 레이어가 잡는다.
                const auto decalSlot = [&batch](uint32_t i)
                {
                    const bool has = batch.textures[i].IsValid();
                    return RHIBindingDesc::Srv2D(batch.textures[i],
                        has ? batch.formats[i] : RHIFormat::RGBA8Unorm,
                        0, has ? (std::max)(1u, batch.mipLevels[i]) : 1).OrNull();
                };
                const RHIBindingDesc decalSrvs[] = {
                    decalSlot(0), decalSlot(1), decalSlot(2) };
                const RHIBindingTable decalSrv = context.resources->CreateBindings(decalSrvs);
                if (!decalSrv.IsValid()) break;   // 링이 찼다 — 남은 배치도 마찬가지다

                encoder.SetPipeline(RHIBindPoint::Graphics,
                    m_pipelines[batch.channel], m_rootSignature);
                encoder.SetBindings(RHIBindPoint::Graphics, 3, decalSrv);

                // 상자 하나가 36정점이다. 정점·인덱스 버퍼 없이 SV_VertexID로 짚는다.
                encoder.Draw(36, batch.instanceCount, 0, batch.firstInstance);
            }
        },
        m_keepAlive);
}

void EnhancedDecalPass::Shutdown()
{
    m_width = 0;
    m_height = 0;
    m_decals.clear();
    m_instances.clear();
    m_batches.clear();
    for (auto*& pipeline : m_pipelines) pipeline = nullptr;
    m_rootSignature = nullptr;
}

#endif
