#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedSkyBoxPass.h"
#include "DX12DeviceResources.h"
#include "DX12PSOManager.h"
#include "DX12RootSignatureCache.h"
#include "EnhancedRenderGraph.h"
#include "RHIEncoder.h"

#include <d3dcompiler.h>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "d3dcompiler.lib")

namespace
{
    // 유니티 빌드에서 익명 네임스페이스가 합쳐지므로 이름을 고유하게 둔다.
    std::string SkyBoxHrToString(HRESULT hr)
    {
        std::ostringstream oss;
        oss << "HRESULT 0x" << std::hex << static_cast<unsigned long>(hr);
        return oss.str();
    }

    // DX11 Skybox.vs/ps의 이식. 바꾼 것은 정점 공급(SV_VertexID 테이블)과
    // 행렬 규약(전치 업로드 + mul(v, M))뿐이다.
    //
    // 유지한 quirk:
    //   · 클립 z = w x 0.99999 — 하늘을 원평면 직전에 두어 깊이 테스트가
    //     씬이 안 그린 곳에만 하늘을 남긴다. z = w로 두면 z/w = 1.0이
    //     LESS를 통과하지 못해 하늘이 통째로 사라진다.
    //   · texCoord = 큐브 로컬 정점(스케일·이동 전) — 샘플 방향이 된다.
    //     정규화하지 않는다(큐브맵 샘플은 방향의 크기를 무시한다).
    constexpr const char* kSkyBoxShader = R"(
cbuffer SkyBoxConstants : register(b0)
{
    float4x4 gViewProjection;
    float4   gEyePosition;   // w = 스케일
};

TextureCube  gCubeMap : register(t0);
SamplerState gSampler : register(s0);

struct VSOut
{
    float4 position : SV_POSITION;
    float3 texCoord : TEXCOORD0;
};

VSOut VSMain(uint vertexId : SV_VertexID)
{
    // 단위 큐브 36정점. 눈이 항상 중심이라 픽셀마다 면이 하나뿐이어서
    // 감김 방향은 그림에 영향이 없다(컬링도 끈다).
    const float3 kCorners[8] = {
        float3(-1, -1, -1), float3( 1, -1, -1), float3( 1,  1, -1), float3(-1,  1, -1),
        float3(-1, -1,  1), float3( 1, -1,  1), float3( 1,  1,  1), float3(-1,  1,  1),
    };
    const uint kIndices[36] = {
        0, 1, 2,  0, 2, 3,     // -Z
        5, 4, 7,  5, 7, 6,     // +Z
        4, 0, 3,  4, 3, 7,     // -X
        1, 5, 6,  1, 6, 2,     // +X
        3, 2, 6,  3, 6, 7,     // +Y
        4, 5, 1,  4, 1, 0,     // -Y
    };

    const float3 local = kCorners[kIndices[vertexId]];
    const float3 world = local * gEyePosition.w + gEyePosition.xyz;

    VSOut output;
    output.position = mul(float4(world, 1.0f), gViewProjection);
    output.position.z = output.position.w * 0.99999f;
    output.texCoord = local;
    return output;
}

float4 PSMain(VSOut input) : SV_TARGET
{
    return float4(gCubeMap.SampleLevel(gSampler, input.texCoord, 0.0f).rgb, 1.0f);
}
)";

    struct SkyBoxConstants
    {
        Mathf::Matrix  viewProjection{};   // 전치해서 넣는다
        Mathf::Vector4 eyePositionScale{};
    };

    bool CompileSkyBoxShader(const char* entry, const char* target,
        Microsoft::WRL::ComPtr<ID3DBlob>& outBlob, std::string& outError)
    {
        Microsoft::WRL::ComPtr<ID3DBlob> errors;
        const HRESULT hr = D3DCompile(kSkyBoxShader, strlen(kSkyBoxShader),
            nullptr, nullptr, nullptr, entry, target, 0, 0, &outBlob, &errors);
        if (FAILED(hr))
        {
            outError = std::string("스카이박스 셰이더 컴파일 실패(") + entry + "): ";
            if (errors) outError += static_cast<const char*>(errors->GetBufferPointer());
            else        outError += SkyBoxHrToString(hr);
            return false;
        }
        return true;
    }
}

bool EnhancedSkyBoxPass::Initialize(const EnhancedFrameContext& context, std::string& outError)
{
    if (nullptr == context.resources || nullptr == context.psoManager ||
        nullptr == context.rootSignatures)
    {
        outError = "스카이박스 패스 컨텍스트가 불완전하다";
        return false;
    }

    return CreatePipelines(context, outError);
}

bool EnhancedSkyBoxPass::CreatePipelines(const EnhancedFrameContext& context,
    std::string& outError)
{
    // b0 상수 · t0 큐브맵(테이블) · s0 선형 샘플러(DX11 LinearSampler와 동일).
    D3D12_DESCRIPTOR_RANGE cubeRange{};
    cubeRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    cubeRange.NumDescriptors = 1;
    cubeRange.BaseShaderRegister = 0;

    D3D12_ROOT_PARAMETER params[2]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &cubeRange;

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
    if (!CompileSkyBoxShader("VSMain", "vs_5_0", vsBlob, outError)) return false;
    if (!CompileSkyBoxShader("PSMain", "ps_5_0", psBlob, outError)) return false;

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

    // 깊이 테스트를 켠다 — z = w x 0.99999와 짝을 이뤄 씬이 안 그린 곳에만
    // 하늘이 남는다. 블렌딩은 없다(하늘은 불투명 배경이다).
    desc.depthEnable = true;
    desc.blendEnable = false;
    desc.cullMode = D3D12_CULL_MODE_NONE;
    desc.numRenderTargets = 1;
    desc.rtvFormats[0] = kOutputFormat;
    desc.dsvFormat = kDepthFormat;

    m_pso = context.psoManager->GetOrCreate(desc, outError);
    if (nullptr == m_pso) return false;

    return true;
}

bool EnhancedSkyBoxPass::PrepareFrame(const EnhancedFrameContext& context,
    std::string& outError)
{
    (void)outError;

    m_width = context.width;
    m_height = context.height;

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

    return true;
}

void EnhancedSkyBoxPass::Declare(EnhancedRenderGraph& graph,
    const EnhancedFrameContext& context)
{
    m_output = RGHandle{};
    m_depth = RGHandle{};

    if (nullptr == m_pso ||
        0 == m_width || 0 == m_height)
    {
        return;
    }

    const bool ownsColor = !m_inputs.color.IsValid();
    const bool ownsDepth = !m_inputs.depth.IsValid();

    if (ownsColor)
    {
        RGTextureDesc desc{};
        desc.width = m_width;
        desc.height = m_height;
        desc.format = kOutputFormat;
        desc.allowRenderTarget = true;
        desc.name = "SkyBox.Output";
        m_output = graph.CreateTexture(desc);
    }
    else
    {
        m_output = m_inputs.color;
    }

    if (ownsDepth)
    {
        RGTextureDesc desc{};
        desc.width = m_width;
        desc.height = m_height;
        desc.format = kDepthFormat;
        desc.allowDepthStencil = true;
        desc.name = "SkyBox.Depth";
        m_depth = graph.CreateTexture(desc);
    }
    else
    {
        m_depth = m_inputs.depth;
    }

    std::vector<EnhancedRenderGraph::RGPassUsage> usages;
    usages.push_back({ m_output, RGResourceState::RenderTarget });
    usages.push_back({ m_depth, RGResourceState::DepthWrite });

    graph.AddPass(GetName(), usages,
        [this, &context, ownsColor, ownsDepth](
            const EnhancedRenderGraph::ExecuteContext& executeContext)
        {
            RHIEncoder& encoder = *executeContext.encoder;
            auto* commandList = executeContext.commandList;

            ID3D12Resource* const colors[] = { executeContext.Resolve(m_output) };
            const auto depthDesc = RHIDepthTargetDesc::Depth(
                executeContext.Resolve(m_depth), kDepthFormat);
            const auto targets = context.resources->CreateRenderTargets(colors, &depthDesc);
            if (!targets.IsValid()) return;

            encoder.SetViewportAndScissor(m_width, m_height);
            encoder.BindRenderTargets(targets);

            if (ownsColor)
            {
                constexpr float kClear[4] = { 0.f, 0.f, 0.f, 0.f };
                encoder.ClearRenderTargets(targets, kClear);
            }
            if (ownsDepth)
            {
                encoder.ClearDepthTarget(targets, 1.f);
            }

            // ★ 자원이 없으면 그리지 않는다 — 디스크립터 없이 그리면 힙의
            // 쓰레기를 읽는다(SSGI에서 그것으로 GPU가 죽었다).
            if (nullptr == m_cubeMap) return;

            SkyBoxConstants constants{};
            constants.viewProjection = XMMatrixTranspose(m_viewProjection);
            constants.eyePositionScale = Mathf::Vector4(
                m_eyePosition.x, m_eyePosition.y, m_eyePosition.z, m_scale);

            const auto cb = context.resources->GetUploadRing().Allocate(
                sizeof(SkyBoxConstants), DX12UploadRing::kConstantBufferAlignment);
            if (!cb.IsValid()) return;
            memcpy(cb.cpuAddress, &constants, sizeof(constants));

            const RHIBindingDesc cube[] = {
                RHIBindingDesc::SrvCube(m_cubeMap, m_cubeMapFormat, m_cubeMapMips),
            };
            const RHIBindingTable cubeTable = context.resources->CreateBindings(cube);
            if (!cubeTable.IsValid()) return;

            encoder.BindDescriptorHeaps();

            encoder.SetPipeline(RHIBindPoint::Graphics, m_pso, m_rootSignature);
            encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
            encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0, cb.gpuAddress);
            encoder.SetBindings(RHIBindPoint::Graphics, 1, cubeTable);

            encoder.Draw(36, 1);
        },
        m_keepAlive);
}

void EnhancedSkyBoxPass::Shutdown()
{
    m_width = 0;
    m_height = 0;
    m_cubeMap = nullptr;

    m_pso = nullptr;
    m_rootSignature = nullptr;
}

#endif
