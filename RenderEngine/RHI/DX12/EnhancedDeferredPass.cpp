#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedDeferredPass.h"
#include "DX12DeviceResources.h"
#include "DX12PSOManager.h"
#include "DX12RootSignatureCache.h"

#include <d3dcompiler.h>
#include <sstream>

#pragma comment(lib, "d3dcompiler.lib")

namespace
{
    // 유니티 빌드에서 익명 네임스페이스가 파일 간 합쳐지므로 이름을 고유하게 둔다.
    constexpr const char* kDeferredShader = R"(
Texture2D    gDiffuse    : register(t0);
Texture2D    gMetalRough : register(t1);
Texture2D    gNormal     : register(t2);
Texture2D    gEmissive   : register(t3);
SamplerState gSampler    : register(s0);

struct VSOut
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
};

VSOut VSMain(uint id : SV_VertexID)
{
    // 정점 버퍼 없는 풀스크린 삼각형. 쿼드보다 낫다 — 대각선에서 픽셀이
    // 두 번 셰이딩되는 일이 없고, 정점도 셋뿐이다.
    VSOut output;
    output.uv = float2((id << 1) & 2, id & 2);
    output.position = float4(output.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return output;
}

float4 PSMain(VSOut input) : SV_TARGET
{
    const float3 albedo   = gDiffuse.Sample(gSampler, input.uv).rgb;
    const float3 normal   = gNormal.Sample(gSampler, input.uv).rgb * 2.0f - 1.0f;
    const float3 emissive = gEmissive.Sample(gSampler, input.uv).rgb;
    const float  rough    = gMetalRough.Sample(gSampler, input.uv).g;

    // 실제 광원은 다음 슬라이스에서 붙는다. 지금은 GBuffer 값이 실제로 읽히는지가
    // 목적이므로 고정 방향광 하나로 조합한다 — 네 타깃이 전부 결과에 기여해야
    // 픽셀 검증이 '읽기가 되는가'를 볼 수 있다.
    const float3 lightDirection = normalize(float3(0.3f, 0.6f, -0.7f));
    const float  ndotl = saturate(dot(normalize(normal), lightDirection));

    return float4(albedo * ndotl * (1.0f - rough * 0.5f) + emissive, 1.0f);
}
)";

    bool CompileDeferredShader(const char* entry, const char* target,
        Microsoft::WRL::ComPtr<ID3DBlob>& outBlob, std::string& outError)
    {
        Microsoft::WRL::ComPtr<ID3DBlob> errors;
        const HRESULT hr = D3DCompile(kDeferredShader, strlen(kDeferredShader), nullptr,
            nullptr, nullptr, entry, target, 0, 0, &outBlob, &errors);
        if (FAILED(hr))
        {
            outError = std::string("Deferred 셰이더 컴파일 실패(") + entry + "): ";
            if (errors) outError += static_cast<const char*>(errors->GetBufferPointer());
            return false;
        }
        return true;
    }
}

bool EnhancedDeferredPass::Initialize(const EnhancedFrameContext& context, std::string& outError)
{
    if (nullptr == context.resources || nullptr == context.psoManager ||
        nullptr == context.rootSignatures)
    {
        outError = "Deferred 패스 컨텍스트가 불완전하다";
        return false;
    }

    auto* device = context.resources->GetDevice();

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.NumDescriptors = 1;
    if (FAILED(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap))))
    {
        outError = "Deferred RTV 힙 생성 실패";
        return false;
    }

    ComPtr<ID3DBlob> vsBlob;
    ComPtr<ID3DBlob> psBlob;
    if (!CompileDeferredShader("VSMain", "vs_5_0", vsBlob, outError)) return false;
    if (!CompileDeferredShader("PSMain", "ps_5_0", psBlob, outError)) return false;

    // SRV 4개를 한 테이블로 묶는다. 디스크립터 링에서 연속으로 잘라 쓰므로
    // 테이블 하나면 충분하고, 바인딩 호출도 한 번이다(상태 변경 최소화).
    D3D12_DESCRIPTOR_RANGE srvRange{};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 4;

    D3D12_DESCRIPTOR_RANGE samplerRange{};
    samplerRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    samplerRange.NumDescriptors = 1;

    D3D12_ROOT_PARAMETER params[2]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[0].DescriptorTable.NumDescriptorRanges = 1;
    params[0].DescriptorTable.pDescriptorRanges = &srvRange;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &samplerRange;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = _countof(params);
    rootDesc.pParameters = params;

    const auto root = context.rootSignatures->GetOrCreate(rootDesc, outError);
    if (!root.IsValid()) return false;
    m_rootSignature = root.signature;

    DX12GraphicsPipelineDesc desc{};
    desc.vsBytecode = vsBlob->GetBufferPointer();
    desc.vsSize = vsBlob->GetBufferSize();
    desc.psBytecode = psBlob->GetBufferPointer();
    desc.psSize = psBlob->GetBufferSize();
    desc.rootSignature = root.signature;
    desc.rootSignatureId = root.id;
    desc.numRenderTargets = 1;
    desc.rtvFormats[0] = kOutputFormat;

    m_pso = context.psoManager->GetOrCreate(desc, outError);
    if (nullptr == m_pso) return false;

    D3D12_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;

    m_sampler = context.resources->GetSamplerHeap().GetOrCreate(sampler);
    if (0 == m_sampler.ptr)
    {
        outError = "Deferred 샘플러 생성 실패";
        return false;
    }

    return true;
}

void EnhancedDeferredPass::Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context)
{
    RGTextureDesc outputDesc{};
    outputDesc.width = context.width;
    outputDesc.height = context.height;
    outputDesc.format = kOutputFormat;
    outputDesc.allowRenderTarget = true;
    outputDesc.name = "Deferred.Lighting";
    m_output = graph.CreateTexture(outputDesc);

    // GBuffer 넷을 읽고 하나에 쓴다. 이 선언만으로 그래프가
    // RENDER_TARGET → PIXEL_SHADER_RESOURCE 전이를 만들어 준다.
    const std::vector<EnhancedRenderGraph::RGPassUsage> usages = {
        { m_inputs.diffuse,    RGResourceState::ShaderResource },
        { m_inputs.metalRough, RGResourceState::ShaderResource },
        { m_inputs.normal,     RGResourceState::ShaderResource },
        { m_inputs.emissive,   RGResourceState::ShaderResource },
        { m_output,            RGResourceState::RenderTarget },
    };

    graph.AddPass(GetName(), usages,
        [this, &context](const EnhancedRenderGraph::ExecuteContext& executeContext)
        {
            auto* commandList = executeContext.commandList;
            auto* device = context.resources->GetDevice();

            const auto rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
            device->CreateRenderTargetView(executeContext.Resolve(m_output), nullptr, rtv);

            // SRV 넷을 디스크립터 링에서 연속으로 자른다 — 테이블은 연속이어야 한다.
            const auto srvRange = context.resources->GetDescriptorRing().Allocate(4);
            if (!srvRange.IsValid()) return;

            const RGHandle sources[4] = {
                m_inputs.diffuse, m_inputs.metalRough, m_inputs.normal, m_inputs.emissive };
            for (uint32_t i = 0; i < 4; ++i)
            {
                device->CreateShaderResourceView(executeContext.Resolve(sources[i]), nullptr,
                    srvRange.CpuAt(i));
            }

            const D3D12_VIEWPORT viewport{ 0.f, 0.f,
                static_cast<float>(context.width), static_cast<float>(context.height), 0.f, 1.f };
            const D3D12_RECT scissor{ 0, 0,
                static_cast<LONG>(context.width), static_cast<LONG>(context.height) };
            commandList->RSSetViewports(1, &viewport);
            commandList->RSSetScissorRects(1, &scissor);
            commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

            ID3D12DescriptorHeap* heaps[] = {
                context.resources->GetDescriptorRing().GetHeap(),
                context.resources->GetSamplerHeap().GetHeap() };
            commandList->SetDescriptorHeaps(2, heaps);

            commandList->SetGraphicsRootSignature(m_rootSignature);
            commandList->SetPipelineState(m_pso);
            commandList->SetGraphicsRootDescriptorTable(0, srvRange.gpu);
            commandList->SetGraphicsRootDescriptorTable(1, m_sampler);

            commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            commandList->DrawInstanced(3, 1, 0, 0);
        },
        // 이 패스의 결과가 최종 출력이다. GBuffer는 이 패스가 읽으므로
        // 뿌리 표시 없이도 컬링에서 살아남는다 — 그것이 3-5 컬링의 실전 확인이다.
        true);
}

void EnhancedDeferredPass::Shutdown()
{
    m_rtvHeap.Reset();
    m_pso = nullptr;
    m_rootSignature = nullptr;
}

#endif
