#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedGizmoIconPass.h"
#include "../../../RHI/DX12/DX12DeviceResources.h"
#include "../../../RHI/DX12/DX12PSOManager.h"
#include "../../../RHI/DX12/DX12RootSignatureCache.h"
#include "../../../RHI/DX12/DX12TextureCache.h"
#include "../../Graph/EnhancedRenderGraph.h"
#include "../../../RHI/RHIEncoder.h"

#include <cstring>
#include <sstream>
#include <string>
#include "../../../RHI/DX12/DX12ShaderCompiler.h"

namespace
{
    // 유니티 빌드에서 익명 네임스페이스가 합쳐지므로 이름을 고유하게 둔다.
    std::string GizmoIconHrToString(HRESULT hr)
    {
        std::ostringstream oss;
        oss << "HRESULT 0x" << std::hex << static_cast<unsigned long>(hr);
        return oss.str();
    }

    // ── 빌보드 셰이더 ──
    //
    // DX11 Gizmo_billboard.vs/gs/ps의 이식. GS의 쿼드 확장을 VS로 옮겼다 —
    // 확장 수식(center-eye 노멀, cross 기반 right, up=(0,Size,0))과
    // 스트립 꼭짓점 순서·UV 배치는 GS의 것 그대로다.
    //
    // planeNormal이 (0,1,0)과 평행하면(정수직 시점) cross가 0이 되어
    // 빌보드가 사라진다 — DX11도 같은 동작이라 그대로 둔다.
    //
    // PS의 alpha = min(a, 0.5)도 원본의 의도된 그림이라 유지한다.
    constexpr const char* kGizmoIconShaderFile = "GizmoIcon.hlsl";

    struct GizmoIconConstants
    {
        Mathf::Matrix  viewProjection{};   // 전치해서 넣는다
        Mathf::Vector4 eyePosition{};
    };

    bool CompileGizmoIconShader(const char* entry, const char* target,
        RHIShaderBlob& outBlob, std::string& outError)
    {
        return DX12ShaderCompiler::CompileFile(kGizmoIconShaderFile, entry, target, outBlob, outError);
    }
}

bool EnhancedGizmoIconPass::Initialize(const EnhancedFrameContext& context,
    std::string& outError)
{
    if (nullptr == context.resources || nullptr == context.psoManager ||
        nullptr == context.rootSignatures)
    {
        outError = "기즈모 아이콘 패스 컨텍스트가 불완전하다";
        return false;
    }

    return CreatePipelines(context, outError);
}

bool EnhancedGizmoIconPass::CreatePipelines(const EnhancedFrameContext& context,
    std::string& outError)
{
    // b0 상수 · t0 인스턴스(루트 SRV) · t1 텍스처(테이블) — UI 패스와 같은
    // 구성이고 이유도 같다: 인스턴스는 배치마다 주소만 바뀌므로 루트 SRV,
    // 텍스처만 배치마다 디스크립터를 자른다.
    const RHIPipelineLayoutParam params[] = {
        RHILayout::Cbv(0),
        RHILayout::Srv(0),
        RHILayout::SrvTable(1, 1),
    };

    // DX11 원본이 LinearSampler(WRAP)로 아이콘을 찍는다.
    const RHIStaticSamplerDesc samplers[] = {
        { RHISampler::Linear(RHIAddressMode::Wrap), 0, RHIShaderVisibility::Pixel },
    };

    RHIPipelineLayoutDesc rootDesc{};
    rootDesc.params = params;
    rootDesc.staticSamplers = samplers;

    const auto root = context.rootSignatures->GetOrCreate(rootDesc, outError);
    if (!root.IsValid()) return false;

    RHIShaderBlob vsBlob;
    RHIShaderBlob psBlob;
    if (!CompileGizmoIconShader("VSMain", "vs_5_0", vsBlob, outError)) return false;
    if (!CompileGizmoIconShader("PSMain", "ps_5_0", psBlob, outError)) return false;

    RHIGraphicsPipelineDesc desc{};
    desc.vsBytecode = vsBlob.Data();
    desc.vsSize = vsBlob.Size();
    desc.psBytecode = psBlob.Data();
    desc.psSize = psBlob.Size();
    desc.layout = root;

    // 정점을 셰이더가 만든다 — 입력 레이아웃이 없다.
    desc.inputElements = nullptr;
    desc.inputElementCount = 0;
    desc.topologyType = RHITopologyType::Triangle;

    // 깊이를 안 본다(DSV 미바인딩이 원본 동작) · 반투명이라 블렌딩.
    desc.depthEnable = false;
    desc.blendEnable = true;
    desc.cullMode = RHICullMode::None;
    desc.numRenderTargets = 1;
    desc.rtvFormats[0] = m_outputFormat;

    m_pso = context.psoManager->GetOrCreate(desc, outError);
    if (!m_pso.IsValid()) return false;

    return true;
}

bool EnhancedGizmoIconPass::PrepareFrame(const EnhancedFrameContext& context,
    std::string& outError)
{
    m_width = context.width;
    m_height = context.height;

    m_instances.clear();
    m_batches.clear();
    m_lastIconCount = 0;
    m_lastBatchCount = 0;

    if (nullptr != context.camera)
    {
        m_viewProjection = XMMatrixMultiply(context.camera->view, context.camera->projection);
        m_eyePosition = Mathf::Vector4(context.camera->eyePosition);
    }
    else
    {
        m_viewProjection = XMMatrixIdentity();
        m_eyePosition = Mathf::Vector4(0.f, 0.f, 0.f, 0.f);
    }

    if (nullptr == m_icons || m_icons->empty()) return true;

    // 순서는 목록 순서 그대로 두고, 연속해서 같은 텍스처인 구간만 묶는다.
    // 아이콘은 반투명(알파 상한 0.5)이라 그리는 순서가 그림을 바꾼다 —
    // UI와 같은 이유로 전역 정렬을 하지 않는다.
    m_instances.reserve(m_icons->size());
    for (const Icon& icon : *m_icons)
    {
        IconInstance instance{};
        instance.centerSize = Mathf::Vector4(
            icon.position.x, icon.position.y, icon.position.z, icon.size);
        m_instances.push_back(instance);

        if (!m_batches.empty() && m_batches.back().texture == icon.texture)
        {
            ++m_batches.back().count;
        }
        else
        {
            Batch batch{};
            batch.first = static_cast<uint32_t>(m_instances.size() - 1);
            batch.count = 1;
            batch.texture = icon.texture;
            m_batches.push_back(batch);
        }
    }

    m_lastIconCount = static_cast<uint32_t>(m_instances.size());
    m_lastBatchCount = static_cast<uint32_t>(m_batches.size());

    // 텍스처 생성·복사·전이는 그래프 실행 전에 끝낸다. DX12에서는 실행
    // 중 업로드도 우연히 가능했지만, Vulkan의 이미지 배리어와 복사는 동적
    // 렌더링 안에서 기록할 수 없다. 배치가 캐시 소유 핸들만 보관하므로
    // 프레임 디스크립터보다 오래 살고, 캐시보다 오래 살지는 않는다.
    if (nullptr != context.textureCache)
    {
        for (Batch& batch : m_batches)
        {
            std::string uploadError;
            batch.uploaded = context.textureCache->GetOrUpload(batch.texture, uploadError);
            if (!batch.uploaded.IsValid() && !uploadError.empty())
            {
                outError = "기즈모 아이콘 텍스처 업로드 실패: " + uploadError;
                return false;
            }
        }
    }
    return true;
}

void EnhancedGizmoIconPass::Declare(EnhancedRenderGraph& graph,
    const EnhancedFrameContext& context)
{
    m_output = RGHandle{};

    if (!m_pso.IsValid() || 0 == m_width || 0 == m_height)
    {
        return;
    }

    const bool ownsColor = !m_inputs.color.IsValid();

    if (ownsColor)
    {
        RGTextureDesc desc{};
        desc.width = m_width;
        desc.height = m_height;
        desc.format = m_outputFormat;
        desc.allowRenderTarget = true;
        desc.name = "GizmoIcon.Output";
        m_output = graph.CreateTexture(desc);
    }
    else
    {
        m_output = m_inputs.color;
    }

    std::vector<EnhancedRenderGraph::RGPassUsage> usages;
    usages.push_back({ m_output, RHIResourceState::RenderTarget });

    graph.AddPass(GetName(), usages,
        [this, &context, ownsColor](const EnhancedRenderGraph::ExecuteContext& executeContext)
        {
            RHIEncoder& encoder = *executeContext.encoder;

            const RHITextureHandle colors[] = { executeContext.ResolveHandle(m_output) };
            const auto targets = context.resources->CreateRenderTargets(colors);
            if (!targets.IsValid()) return;

            encoder.SetViewportAndScissor(m_width, m_height);
            encoder.BindRenderTargets(targets);

            if (ownsColor)
            {
                constexpr float kClear[4] = { 0.f, 0.f, 0.f, 0.f };
                encoder.ClearRenderTargets(targets, kClear);
            }

            if (m_instances.empty()) return;

            GizmoIconConstants constants{};
            constants.viewProjection = XMMatrixTranspose(m_viewProjection);
            constants.eyePosition = m_eyePosition;

            const auto cb = context.resources->UploadConstants(
                &constants, sizeof(GizmoIconConstants));
            if (!cb.IsValid()) return;
            // 인스턴스는 한 번에 올린다 — 배치는 주소만 옮긴다.
            const uint64_t instanceBytes =
                sizeof(IconInstance) * static_cast<uint64_t>(m_instances.size());
            const auto instanceUpload = context.resources->AllocateUpload(
                RHIUploadRequest{ instanceBytes, RHIUploadUsage::BufferCopy,
                    sizeof(IconInstance) });
            if (!instanceUpload.IsValid()) return;
            memcpy(instanceUpload.cpuAddress, m_instances.data(),
                static_cast<size_t>(instanceBytes));

            encoder.SetPipeline(RHIBindPoint::Graphics, m_pso);
            encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleStrip);
            encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0, cb);

            for (const Batch& batch : m_batches)
            {
                const RHITextureHandle resource = batch.uploaded.handle;
                const RHIFormat format = batch.uploaded.format;
                const uint32_t mipLevels = batch.uploaded.mipLevels;

                // ★ 자원이 없으면 그리지 않는다 — 디스크립터 없이 그리면
                // 힙의 쓰레기를 읽는다(SSGI에서 그것으로 GPU가 죽었다).
                //
                // CreateBindings도 널 리소스에 invalid를 돌려주지만, 여기서
                // 먼저 끊는 것을 남긴다 — 이쪽은 '이 배치만 건너뛴다'이고
                // 링 고갈은 '더 그릴 수 없다'라 처리가 다르다.
                if (!resource.IsValid()) continue;

                const RHIBindingDesc bindings[] = {
                    RHIBindingDesc::Srv2D(resource, format, 0, mipLevels),
                };
                const RHIBindingTable textureTable =
                    context.resources->CreateBindings(bindings);
                if (!textureTable.IsValid()) break;

                encoder.SetRootBuffer(RHIBindPoint::Graphics, 1,
                    instanceUpload.SubRange(
                        static_cast<uint64_t>(batch.first) * sizeof(IconInstance),
                        static_cast<uint64_t>(batch.count) * sizeof(IconInstance)));
                encoder.SetBindings(RHIBindPoint::Graphics, 2, textureTable);

                encoder.Draw(4, batch.count);
            }
        },
        m_keepAlive);
}

void EnhancedGizmoIconPass::Shutdown()
{
    m_instances.clear();
    m_batches.clear();
    m_lastIconCount = 0;
    m_lastBatchCount = 0;
    m_width = 0;
    m_height = 0;

    m_pso = {};
}

#endif
