#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedGizmoIconPass.h"
#include "DX12DeviceResources.h"
#include "DX12PSOManager.h"
#include "DX12RootSignatureCache.h"
#include "DX12TextureCache.h"
#include "EnhancedRenderGraph.h"
#include "RHIEncoder.h"

#include <d3dcompiler.h>
#include <cstring>
#include <sstream>
#include <string>

#pragma comment(lib, "d3dcompiler.lib")

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
    constexpr const char* kGizmoIconShader = R"(
struct IconInstance
{
    float4 centerSize;   // xyz 중심 · w 크기
};

StructuredBuffer<IconInstance> gIcons : register(t0);
Texture2D                      gTexture : register(t1);
SamplerState                   gSampler : register(s0);

cbuffer GizmoCamera : register(b0)
{
    float4x4 gViewProjection;
    float4   gEyePosition;
};

struct VSOut
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
};

VSOut VSMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    const IconInstance icon = gIcons[instanceId];
    const float3 center = icon.centerSize.xyz;
    const float  size = icon.centerSize.w;
    const float  halfWidth = size * 0.5f;

    float3 planeNormal = normalize(center - gEyePosition.xyz);
    float3 rightVector = normalize(cross(planeNormal, float3(0.0f, 1.0f, 0.0f))) * halfWidth;
    const float3 upVector = float3(0.0f, size, 0.0f);

    // GS와 같은 스트립 순서: C-r · C+r · C-r+up · C+r+up
    const float sideSign = (vertexId & 1u) ? 1.0f : -1.0f;
    const float upAmount = (vertexId & 2u) ? 1.0f : 0.0f;
    const float3 world = center + sideSign * rightVector + upAmount * upVector;

    // GS와 같은 UV: (0,1) (1,1) (0,0) (1,0)
    VSOut output;
    output.position = mul(float4(world, 1.0f), gViewProjection);
    output.uv = float2((vertexId & 1u) ? 1.0f : 0.0f, upAmount > 0.5f ? 0.0f : 1.0f);
    return output;
}

float4 PSMain(VSOut input) : SV_TARGET
{
    float4 color = gTexture.Sample(gSampler, input.uv);
    float alpha = min(color.a, 0.5f);
    return float4(color.rgb, alpha);
}
)";

    struct GizmoIconConstants
    {
        Mathf::Matrix  viewProjection{};   // 전치해서 넣는다
        Mathf::Vector4 eyePosition{};
    };

    bool CompileGizmoIconShader(const char* entry, const char* target,
        Microsoft::WRL::ComPtr<ID3DBlob>& outBlob, std::string& outError)
    {
        Microsoft::WRL::ComPtr<ID3DBlob> errors;
        const HRESULT hr = D3DCompile(kGizmoIconShader, strlen(kGizmoIconShader),
            nullptr, nullptr, nullptr, entry, target, 0, 0, &outBlob, &errors);
        if (FAILED(hr))
        {
            outError = std::string("기즈모 아이콘 셰이더 컴파일 실패(") + entry + "): ";
            if (errors) outError += static_cast<const char*>(errors->GetBufferPointer());
            else        outError += GizmoIconHrToString(hr);
            return false;
        }
        return true;
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
    D3D12_DESCRIPTOR_RANGE textureRange{};
    textureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    textureRange.NumDescriptors = 1;
    textureRange.BaseShaderRegister = 1;

    D3D12_ROOT_PARAMETER params[3]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[1].Descriptor.ShaderRegister = 0;
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].DescriptorTable.NumDescriptorRanges = 1;
    params[2].DescriptorTable.pDescriptorRanges = &textureRange;

    // DX11 원본이 LinearSampler(WRAP)로 아이콘을 찍는다.
    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = _countof(params);
    rootDesc.pParameters = params;
    rootDesc.NumStaticSamplers = 1;
    rootDesc.pStaticSamplers = &sampler;

    const auto root = context.rootSignatures->GetOrCreate(rootDesc, outError);
    if (!root.IsValid()) return false;
    m_rootSignature = root.signature;

    ComPtr<ID3DBlob> vsBlob;
    ComPtr<ID3DBlob> psBlob;
    if (!CompileGizmoIconShader("VSMain", "vs_5_0", vsBlob, outError)) return false;
    if (!CompileGizmoIconShader("PSMain", "ps_5_0", psBlob, outError)) return false;

    DX12GraphicsPipelineDesc desc{};
    desc.vsBytecode = vsBlob->GetBufferPointer();
    desc.vsSize = vsBlob->GetBufferSize();
    desc.psBytecode = psBlob->GetBufferPointer();
    desc.psSize = psBlob->GetBufferSize();
    desc.rootSignature = root.signature;
    desc.rootSignatureId = root.id;

    // 정점을 셰이더가 만든다 — 입력 레이아웃이 없다.
    desc.inputElements = nullptr;
    desc.inputElementCount = 0;
    desc.topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

    // 깊이를 안 본다(DSV 미바인딩이 원본 동작) · 반투명이라 블렌딩.
    desc.depthEnable = false;
    desc.blendEnable = true;
    desc.cullMode = D3D12_CULL_MODE_NONE;
    desc.numRenderTargets = 1;
    desc.rtvFormats[0] = ToDXGI(m_outputFormat);

    m_pso = context.psoManager->GetOrCreate(desc, outError);
    if (nullptr == m_pso) return false;

    return true;
}

bool EnhancedGizmoIconPass::PrepareFrame(const EnhancedFrameContext& context,
    std::string& outError)
{
    (void)outError;

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
    return true;
}

void EnhancedGizmoIconPass::Declare(EnhancedRenderGraph& graph,
    const EnhancedFrameContext& context)
{
    m_output = RGHandle{};

    if (nullptr == m_pso || 0 == m_width || 0 == m_height)
    {
        return;
    }

    const bool ownsColor = !m_inputs.color.IsValid();

    if (ownsColor)
    {
        RGTextureDesc desc{};
        desc.width = m_width;
        desc.height = m_height;
        desc.format = ToDXGI(m_outputFormat);
        desc.allowRenderTarget = true;
        desc.name = "GizmoIcon.Output";
        m_output = graph.CreateTexture(desc);
    }
    else
    {
        m_output = m_inputs.color;
    }

    std::vector<EnhancedRenderGraph::RGPassUsage> usages;
    usages.push_back({ m_output, RGResourceState::RenderTarget });

    graph.AddPass(GetName(), usages,
        [this, &context, ownsColor](const EnhancedRenderGraph::ExecuteContext& executeContext)
        {
            RHIEncoder& encoder = *executeContext.encoder;

            ID3D12Resource* const colors[] = { executeContext.Resolve(m_output) };
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

            const auto cb = context.resources->GetUploadRing().Allocate(
                sizeof(GizmoIconConstants), DX12UploadRing::kConstantBufferAlignment);
            if (!cb.IsValid()) return;
            memcpy(cb.cpuAddress, &constants, sizeof(constants));

            // 인스턴스는 한 번에 올린다 — 배치는 주소만 옮긴다.
            const uint64_t instanceBytes =
                sizeof(IconInstance) * static_cast<uint64_t>(m_instances.size());
            const auto instanceUpload = context.resources->GetUploadRing().Allocate(
                instanceBytes, sizeof(IconInstance));
            if (!instanceUpload.IsValid()) return;
            memcpy(instanceUpload.cpuAddress, m_instances.data(),
                static_cast<size_t>(instanceBytes));

            encoder.SetPipeline(RHIBindPoint::Graphics, m_pso, m_rootSignature);
            encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleStrip);
            encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0, cb.gpuAddress);

            for (const Batch& batch : m_batches)
            {
                ID3D12Resource* resource = nullptr;
                DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
                uint32_t mipLevels = 1;

                if (nullptr != context.textureCache)
                {
                    std::string uploadError;
                    // nullptr이면 캐시가 1x1 흰색을 돌려준다.
                    const auto entry =
                        context.textureCache->GetOrUpload(batch.texture, uploadError);
                    if (entry.IsValid())
                    {
                        resource = entry.resource;
                        format = entry.format;
                        mipLevels = entry.mipLevels;
                    }
                }

                // ★ 자원이 없으면 그리지 않는다 — 디스크립터 없이 그리면
                // 힙의 쓰레기를 읽는다(SSGI에서 그것으로 GPU가 죽었다).
                //
                // CreateBindings도 널 리소스에 invalid를 돌려주지만, 여기서
                // 먼저 끊는 것을 남긴다 — 이쪽은 '이 배치만 건너뛴다'이고
                // 링 고갈은 '더 그릴 수 없다'라 처리가 다르다.
                if (nullptr == resource) continue;

                const RHIBindingDesc bindings[] = {
                    RHIBindingDesc::Srv2D(resource, format, 0, mipLevels),
                };
                const RHIBindingTable textureTable =
                    context.resources->CreateBindings(bindings);
                if (!textureTable.IsValid()) break;

                encoder.SetRootBuffer(RHIBindPoint::Graphics, 1,
                    instanceUpload.gpuAddress
                    + static_cast<uint64_t>(batch.first) * sizeof(IconInstance));
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

    m_pso = nullptr;
    m_rootSignature = nullptr;
}

#endif
