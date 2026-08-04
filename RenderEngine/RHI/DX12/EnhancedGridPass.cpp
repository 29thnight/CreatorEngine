#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedGridPass.h"
#include "DX12DeviceResources.h"
#include "DX12PSOManager.h"
#include "DX12RootSignatureCache.h"
#include "EnhancedRenderGraph.h"

#include <d3dcompiler.h>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "d3dcompiler.lib")

namespace
{
    // 유니티 빌드에서 익명 네임스페이스가 합쳐지므로 이름을 고유하게 둔다.
    std::string GridHrToString(HRESULT hr)
    {
        std::ostringstream oss;
        oss << "HRESULT 0x" << std::hex << static_cast<unsigned long>(hr);
        return oss.str();
    }

    // ── 그리드 셰이더 ──
    //
    // 로직은 DX11 Grid.vs/ps.hlsl의 이식이다. 옮기며 바꾼 것은 셋뿐이다:
    //   · 정점을 SV_VertexID로 만든다(위치는 DX11 쿼드와 동일한 ±10000)
    //   · 상수를 cbuffer 하나로 합쳤다(루트 CBV 하나면 되므로)
    //   · 행렬을 mul(v, M)으로 곱한다(GBuffer와 같은 규약 — 전치 업로드)
    //
    // 옮기며 바꾸지 않은 것: 카메라 위치를 int3로 잘라 그리드를 재중심하는
    // quirk(비정수 위치에서 서브픽셀 이동이 사라진다), 페이드가 fadeStart/
    // fadeEnd와 별개로 (1 - dist/100)을 한 번 더 곱하는 것. 픽셀 대조의
    // 기준선이 그 동작이므로 여기서 고치면 대조가 성립하지 않는다.
    constexpr const char* kGridShader = R"(
cbuffer GridConstants : register(b0)
{
    float4x4 gViewProjection;
    float4   gCameraPos;        // xyz = eye
    float4   gGridColor;
    float4   gCheckerColor;
    float4   gFadeUnitSub;      // x fadeStart · y fadeEnd · z unitSize · w subdivisions
    float4   gCenterOffset;     // xyz centerOffset · w majorLineThickness
    float4   gMinorParams;      // x minorLineThickness · y minorLineAlpha
};

struct VSOut
{
    float4 position : SV_POSITION;
    float3 worldPos : TEXCOORD0;
};

VSOut VSMain(uint vertexId : SV_VertexID)
{
    // 0,1,2,3 -> (0,0) (1,0) (0,1) (1,1). 삼각형 스트립 순서다.
    const float2 corner = float2(float(vertexId & 1u), float((vertexId >> 1u) & 1u));
    const float3 pos = float3(
        lerp(-10000.0f, 10000.0f, corner.x),
        0.0f,
        lerp(-10000.0f, 10000.0f, corner.y));

    // DX11 quirk 그대로: 카메라 위치를 정수로 잘라 쿼드를 재중심한다.
    int3 cPos = int3(gCameraPos.xyz);
    cPos.y = 0;

    const float3 worldPos = pos + float3(cPos);

    VSOut output;
    output.worldPos = worldPos;
    output.position = mul(float4(worldPos, 1.0f), gViewProjection);
    return output;
}

float2 fwidth2(float2 v)
{
    return abs(ddx(v)) + abs(ddy(v));
}

float grid_mask(float2 posAbs, float unit, float thickness)
{
    unit = max(unit, 1e-6);
    const float2 fw = fwidth2(posAbs);
    const float2 threshold = fw * thickness * 0.5f / unit;

    const float2 coord = posAbs / unit;
    const float2 fracP = frac(coord);
    const float2 fracN = frac(-coord);

    float2 hit;
    hit.x = ((fracP.x < threshold.x) ? 1.0f : 0.0f) + ((fracN.x < threshold.x) ? 1.0f : 0.0f);
    hit.y = ((fracP.y < threshold.y) ? 1.0f : 0.0f) + ((fracN.y < threshold.y) ? 1.0f : 0.0f);

    return saturate(max(hit.x, hit.y));
}

float4 PSMain(VSOut input) : SV_TARGET
{
    const float2 posAbs = input.worldPos.xz - gCenterOffset.xz;

    const float fSubs = max(gFadeUnitSub.w, 1.0f);
    const float minorUnit = gFadeUnitSub.z / fSubs;

    const float major = grid_mask(posAbs, gFadeUnitSub.z, gCenterOffset.w);
    const float minor = grid_mask(posAbs, minorUnit, gMinorParams.x) * gMinorParams.y;
    const float lineMask = saturate(major + minor);

    const float distPlanar = length(input.worldPos.xz - gCameraPos.xz);
    const float denom = max(gFadeUnitSub.y - gFadeUnitSub.x, 1e-5f);
    float fadeFactor = 1.0f - saturate((distPlanar - gFadeUnitSub.x) / denom);

    // DX11 원본의 두 번째 페이드. 100 이후를 통째로 끈다 — 그대로 둔다.
    fadeFactor *= (1.0f - saturate(distPlanar / 100.0f));

    const float alphaGrid = lineMask * gGridColor.a;
    const float alpha = saturate(alphaGrid) * fadeFactor;

    const float3 color = lerp(gCheckerColor.rgb, gGridColor.rgb, lineMask);

    return float4(color, alpha);
}
)";

    // HLSL 쪽 cbuffer와 정확히 같은 배치.
    struct GridConstants
    {
        Mathf::Matrix  viewProjection{};   // 전치해서 넣는다
        Mathf::Vector4 cameraPos{};
        Mathf::Color4  gridColor{};
        Mathf::Color4  checkerColor{};
        Mathf::Vector4 fadeUnitSub{};
        Mathf::Vector4 centerOffsetMajor{};
        Mathf::Vector4 minorParams{};
    };

    bool CompileGridShader(const char* entry, const char* target,
        Microsoft::WRL::ComPtr<ID3DBlob>& outBlob, std::string& outError)
    {
        Microsoft::WRL::ComPtr<ID3DBlob> errors;
        const HRESULT hr = D3DCompile(kGridShader, strlen(kGridShader), nullptr, nullptr,
            nullptr, entry, target, 0, 0, &outBlob, &errors);
        if (FAILED(hr))
        {
            outError = std::string("그리드 셰이더 컴파일 실패(") + entry + "): ";
            if (errors) outError += static_cast<const char*>(errors->GetBufferPointer());
            else        outError += GridHrToString(hr);
            return false;
        }
        return true;
    }
}

bool EnhancedGridPass::Initialize(const EnhancedFrameContext& context, std::string& outError)
{
    if (nullptr == context.resources || nullptr == context.psoManager ||
        nullptr == context.rootSignatures)
    {
        outError = "그리드 패스 컨텍스트가 불완전하다";
        return false;
    }

    return CreatePipelines(context, outError);
}

bool EnhancedGridPass::CreatePipelines(const EnhancedFrameContext& context, std::string& outError)
{
    // 루트 CBV 하나. 그리드는 텍스처를 읽지 않으므로 디스크립터 테이블이
    // 필요 없다 — 프레임마다 업로드 링에서 자른 조각의 주소만 꽂는다.
    D3D12_ROOT_PARAMETER params[1]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;

    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = _countof(params);
    rootDesc.pParameters = params;

    const auto root = context.rootSignatures->GetOrCreate(rootDesc, outError);
    if (!root.IsValid()) return false;
    m_rootSignature = root.signature;

    ComPtr<ID3DBlob> vsBlob;
    ComPtr<ID3DBlob> psBlob;
    if (!CompileGridShader("VSMain", "vs_5_0", vsBlob, outError)) return false;
    if (!CompileGridShader("PSMain", "ps_5_0", psBlob, outError)) return false;

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

    // 깊이를 켠다. DX11 그리드는 씬 깊이에 가려지고 자기 깊이도 쓴다.
    desc.depthEnable = true;

    // 선 사이가 투명해야 아래 그림이 비친다 — 알파 블렌딩이 그리드의 본질이다.
    desc.blendEnable = true;
    desc.cullMode = D3D12_CULL_MODE_NONE;   // DX11도 CULL_NONE이다(아래에서 봐도 그린다)
    desc.numRenderTargets = 1;
    desc.rtvFormats[0] = kOutputFormat;
    desc.dsvFormat = kDepthFormat;

    m_pso = context.psoManager->GetOrCreate(desc, outError);
    if (nullptr == m_pso) return false;

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.NumDescriptors = 1;
    HRESULT hr = context.resources->GetDevice()->CreateDescriptorHeap(
        &rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap));
    if (FAILED(hr))
    {
        outError = "그리드 RTV 힙 생성 실패: " + GridHrToString(hr);
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.NumDescriptors = 1;
    hr = context.resources->GetDevice()->CreateDescriptorHeap(
        &dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap));
    if (FAILED(hr))
    {
        outError = "그리드 DSV 힙 생성 실패: " + GridHrToString(hr);
        return false;
    }

    return true;
}

bool EnhancedGridPass::PrepareFrame(const EnhancedFrameContext& context, std::string& outError)
{
    (void)outError;

    m_width = context.width;
    m_height = context.height;

    // 프레임 밀봉. Record가 스냅샷을 직접 읽지 않고 여기서 복사한 값을 쓴다.
    if (nullptr != context.camera)
    {
        m_viewProjection = XMMatrixMultiply(context.camera->view, context.camera->projection);
        m_cameraPos = Mathf::Vector4(context.camera->eyePosition);
    }
    else
    {
        m_viewProjection = XMMatrixIdentity();
        m_cameraPos = Mathf::Vector4(0.f, 0.f, 0.f, 0.f);
    }

    return true;
}

void EnhancedGridPass::Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context)
{
    m_output = RGHandle{};
    m_depth = RGHandle{};

    if (nullptr == m_pso || nullptr == m_rtvHeap || nullptr == m_dsvHeap ||
        0 == m_width || 0 == m_height)
    {
        return;
    }

    // 입력이 있으면 그 위에 그리고, 없으면 자체 transient를 만든다.
    // 자체 생성일 때만 지운다 — 남의 그림 위에 지우면 그림이 사라진다.
    const bool ownsColor = !m_inputs.color.IsValid();
    const bool ownsDepth = !m_inputs.depth.IsValid();

    if (ownsColor)
    {
        RGTextureDesc desc{};
        desc.width = m_width;
        desc.height = m_height;
        desc.format = kOutputFormat;
        desc.allowRenderTarget = true;
        desc.name = "Grid.Output";
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
        desc.name = "Grid.Depth";
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
            auto* commandList = executeContext.commandList;
            auto* device = context.resources->GetDevice();

            // 뷰는 매 프레임 만든다. 그래프가 리소스를 프레임마다 다르게 줄 수
            // 있으므로(컬링·앨리어싱) 캐시하면 어긋난다.
            const auto rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
            device->CreateRenderTargetView(executeContext.Resolve(m_output), nullptr, rtvHandle);

            const auto dsvHandle = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
            D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
            dsvDesc.Format = kDepthFormat;
            dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
            device->CreateDepthStencilView(executeContext.Resolve(m_depth), &dsvDesc, dsvHandle);

            const D3D12_VIEWPORT viewport{ 0.f, 0.f,
                static_cast<float>(m_width), static_cast<float>(m_height), 0.f, 1.f };
            const D3D12_RECT scissor{ 0, 0,
                static_cast<LONG>(m_width), static_cast<LONG>(m_height) };
            commandList->RSSetViewports(1, &viewport);
            commandList->RSSetScissorRects(1, &scissor);
            commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

            if (ownsColor)
            {
                constexpr float kClear[4] = { 0.f, 0.f, 0.f, 0.f };
                commandList->ClearRenderTargetView(rtvHandle, kClear, 0, nullptr);
            }
            if (ownsDepth)
            {
                commandList->ClearDepthStencilView(dsvHandle,
                    D3D12_CLEAR_FLAG_DEPTH, 1.f, 0, 0, nullptr);
            }

            GridConstants constants{};
            constants.viewProjection = XMMatrixTranspose(m_viewProjection);
            constants.cameraPos = m_cameraPos;
            constants.gridColor = m_style.gridColor;
            constants.checkerColor = m_style.checkerColor;
            constants.fadeUnitSub = Mathf::Vector4(
                m_style.fadeStart, m_style.fadeEnd, m_style.unitSize,
                static_cast<float>(m_style.subdivisions));
            constants.centerOffsetMajor = Mathf::Vector4(
                m_style.centerOffset.x, m_style.centerOffset.y, m_style.centerOffset.z,
                m_style.majorLineThickness);
            constants.minorParams = Mathf::Vector4(
                m_style.minorLineThickness, m_style.minorLineAlpha, 0.f, 0.f);

            const auto cb = context.resources->GetUploadRing().Allocate(
                sizeof(GridConstants), DX12UploadRing::kConstantBufferAlignment);
            if (!cb.IsValid()) return;
            memcpy(cb.cpuAddress, &constants, sizeof(constants));

            commandList->SetGraphicsRootSignature(m_rootSignature);
            commandList->SetPipelineState(m_pso);
            commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
            commandList->SetGraphicsRootConstantBufferView(0, cb.gpuAddress);

            commandList->DrawInstanced(4, 1, 0, 0);
        },
        m_keepAlive);
}

void EnhancedGridPass::Shutdown()
{
    m_width = 0;
    m_height = 0;

    m_rtvHeap.Reset();
    m_dsvHeap.Reset();
    m_pso = nullptr;
    m_rootSignature = nullptr;
}

#endif
