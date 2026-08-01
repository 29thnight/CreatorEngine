#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedGBufferPass.h"
#include "DX12DeviceResources.h"
#include "DX12PSOManager.h"
#include "DX12RootSignatureCache.h"
#include "DX12MeshCache.h"
#include "../../Mesh.h"

#include <d3dcompiler.h>
#include <sstream>

#pragma comment(lib, "d3dcompiler.lib")

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
    constexpr const char* kGBufferShader = R"(
cbuffer PerFrame : register(b0)
{
    float4x4 gViewProjection;
};

cbuffer PerDraw : register(b1)
{
    float4x4 gWorld;
};

struct VSIn
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
};

struct VSOut
{
    float4 position : SV_POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
};

VSOut VSMain(VSIn input)
{
    VSOut output;
    const float4 worldPosition = mul(float4(input.position, 1.0f), gWorld);
    output.position = mul(worldPosition, gViewProjection);
    // 법선은 회전만 적용한다. 비균등 스케일은 역전치 행렬이 필요하고,
    // 그건 재질 연결 슬라이스에서 상수에 함께 넣는다.
    output.normal   = mul(input.normal, (float3x3)gWorld);
    output.uv       = input.uv;
    return output;
}

struct PSOut
{
    float4 diffuse    : SV_TARGET0;
    float4 metalRough : SV_TARGET1;
    float4 normal     : SV_TARGET2;
    float4 emissive   : SV_TARGET3;
    uint   bitmask    : SV_TARGET4;
};

PSOut PSMain(VSOut input)
{
    PSOut output;
    output.diffuse    = float4(input.uv, 0.0f, 1.0f);
    output.metalRough = float4(0.25f, 0.75f, 0.0f, 1.0f);
    output.normal     = float4(normalize(input.normal) * 0.5f + 0.5f, 1.0f);
    output.emissive   = float4(0.0f, 0.5f, 1.0f, 1.0f);
    output.bitmask    = 0xABCDu;
    return output;
}
)";

    bool CompileGBufferShader(const char* entry, const char* target,
        Microsoft::WRL::ComPtr<ID3DBlob>& outBlob, std::string& outError)
    {
        Microsoft::WRL::ComPtr<ID3DBlob> errors;
        const HRESULT hr = D3DCompile(kGBufferShader, strlen(kGBufferShader), nullptr,
            nullptr, nullptr, entry, target, 0, 0, &outBlob, &errors);
        if (FAILED(hr))
        {
            outError = std::string("GBuffer 셰이더 컴파일 실패(") + entry + "): ";
            if (errors) outError += static_cast<const char*>(errors->GetBufferPointer());
            return false;
        }
        return true;
    }
}

DXGI_FORMAT EnhancedGBufferPass::GetRenderTargetFormat(uint32_t index)
{
    // DX11 쪽 구성과 같아야 대조가 성립한다.
    switch (index)
    {
    case 0: return DXGI_FORMAT_R16G16B16A16_FLOAT;  // Diffuse
    case 1: return DXGI_FORMAT_R16G16B16A16_FLOAT;  // MetalRough
    case 2: return DXGI_FORMAT_R16G16B16A16_FLOAT;  // Normal
    case 3: return DXGI_FORMAT_R16G16B16A16_FLOAT;  // Emissive
    case 4: return DXGI_FORMAT_R32_UINT;            // Bitmask
    default: return DXGI_FORMAT_UNKNOWN;
    }
}

bool EnhancedGBufferPass::PrepareFrame(const EnhancedFrameContext& context, std::string& outError)
{
    m_drawGeometry.clear();
    m_lastDrawCount = 0;

    // 프레임 밀봉된 카메라에서 뷰·투영을 만든다. 스냅샷이 없으면 항등으로 두는데,
    // 그러면 클립 공간에 바로 그리게 되므로 '카메라가 안 붙었다'가 화면에 드러난다.
    m_frameViewProjection = (nullptr != context.camera)
        ? XMMatrixMultiply(context.camera->view, context.camera->projection)
        : XMMatrixIdentity();

    if (nullptr == context.draws || nullptr == context.meshCache) return true;

    // 이번 프레임에 그릴 메시를 미리 올린다. 같은 메시가 여러 번 나와도 캐시가
    // 한 번만 올린다.
    for (const auto& draw : *context.draws)
    {
        if (nullptr == draw.mesh) continue;
        if (m_drawGeometry.find(draw.mesh) != m_drawGeometry.end()) continue;

        std::string uploadError;
        const auto entry = context.meshCache->GetOrUpload(draw.mesh, uploadError);
        if (!entry.IsValid())
        {
            // 빈 메시는 그냥 건너뛴다. 업로드 실패는 알린다 — 조용히 안 그리면
            // '왜 이 오브젝트만 안 보이지'가 된다.
            if (!uploadError.empty()) outError = uploadError;
            continue;
        }

        m_drawGeometry.emplace(draw.mesh, entry);
        ++m_lastDrawCount;
    }

    return true;
}

bool EnhancedGBufferPass::CreatePipeline(const EnhancedFrameContext& context, std::string& outError)
{
    ComPtr<ID3DBlob> vsBlob;
    ComPtr<ID3DBlob> psBlob;
    if (!CompileGBufferShader("VSMain", "vs_5_0", vsBlob, outError)) return false;
    if (!CompileGBufferShader("PSMain", "ps_5_0", psBlob, outError)) return false;

    // 루트 시그니처는 캐시가 식별자를 준다 — 손번호를 붙이지 않는 것이 3-4의 계약이다.
    //
    // 상수는 디스크립터 테이블이 아니라 루트 CBV로 넘긴다. 업로드 링에서 자른
    // 조각의 GPU 주소를 그대로 꽂으면 되므로 디스크립터를 만들 필요가 없고,
    // 드로우마다 바뀌는 값에는 이쪽이 싸다(테이블은 디스크립터 힙을 거친다).
    D3D12_ROOT_PARAMETER params[2]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;   // b0 — 프레임 상수
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[1].Descriptor.ShaderRegister = 1;   // b1 — 드로우 상수
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = _countof(params);
    rootDesc.pParameters = params;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    const auto root = context.rootSignatures->GetOrCreate(rootDesc, outError);
    if (!root.IsValid()) return false;
    m_rootSignature = root.signature;

    // 입력 레이아웃. 정점 구조체와 순서가 맞아야 하고, 어긋나면 검증 레이어가
    // 잡아 주지 않는 경우도 있어 화면이 조용히 이상해진다.
    static const D3D12_INPUT_ELEMENT_DESC kInputElements[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    DX12GraphicsPipelineDesc desc{};
    desc.inputElements = kInputElements;
    desc.inputElementCount = _countof(kInputElements);
    desc.vsBytecode = vsBlob->GetBufferPointer();
    desc.vsSize = vsBlob->GetBufferSize();
    desc.psBytecode = psBlob->GetBufferPointer();
    desc.psSize = psBlob->GetBufferSize();
    desc.rootSignature = root.signature;
    desc.rootSignatureId = root.id;
    desc.depthEnable = true;
    desc.cullMode = D3D12_CULL_MODE_NONE;
    desc.numRenderTargets = kRenderTargetCount;
    for (uint32_t i = 0; i < kRenderTargetCount; ++i)
    {
        desc.rtvFormats[i] = GetRenderTargetFormat(i);
    }
    desc.dsvFormat = kDepthFormat;

    m_pso = context.psoManager->GetOrCreate(desc, outError);
    return nullptr != m_pso;
}

bool EnhancedGBufferPass::Initialize(const EnhancedFrameContext& context, std::string& outError)
{
    if (nullptr == context.resources || nullptr == context.psoManager ||
        nullptr == context.rootSignatures)
    {
        outError = "GBuffer 패스 컨텍스트가 불완전하다";
        return false;
    }

    auto* device = context.resources->GetDevice();

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.NumDescriptors = kRenderTargetCount;
    if (FAILED(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap))))
    {
        outError = "GBuffer RTV 힙 생성 실패";
        return false;
    }
    m_rtvIncrement = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.NumDescriptors = 1;
    if (FAILED(device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap))))
    {
        outError = "GBuffer DSV 힙 생성 실패";
        return false;
    }

    if (!CreatePipeline(context, outError)) return false;

    return true;
}

void EnhancedGBufferPass::Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context)
{
    // 타깃을 그래프에 선언한다. 실제 생성과 배리어는 그래프가 맡는다 —
    // 이 패스는 무엇을 어떤 상태로 쓸지만 말한다.
    static const char* kNames[kRenderTargetCount] = {
        "GBuffer.Diffuse", "GBuffer.MetalRough", "GBuffer.Normal",
        "GBuffer.Emissive", "GBuffer.Bitmask" };

    RGHandle targets[kRenderTargetCount]{};
    for (uint32_t i = 0; i < kRenderTargetCount; ++i)
    {
        RGTextureDesc desc{};
        desc.width = context.width;
        desc.height = context.height;
        desc.format = GetRenderTargetFormat(i);
        desc.allowRenderTarget = true;
        desc.name = kNames[i];
        targets[i] = graph.CreateTexture(desc);
    }

    RGTextureDesc depthDesc{};
    depthDesc.width = context.width;
    depthDesc.height = context.height;
    depthDesc.format = kDepthFormat;
    depthDesc.allowDepthStencil = true;
    depthDesc.name = "GBuffer.Depth";
    const RGHandle depth = graph.CreateTexture(depthDesc);

    m_outputs.diffuse = targets[0];
    m_outputs.metalRough = targets[1];
    m_outputs.normal = targets[2];
    m_outputs.emissive = targets[3];
    m_outputs.bitmask = targets[4];
    m_outputs.depth = depth;

    std::vector<EnhancedRenderGraph::RGPassUsage> usages;
    usages.reserve(kRenderTargetCount + 1);
    for (uint32_t i = 0; i < kRenderTargetCount; ++i)
    {
        usages.push_back({ targets[i], RGResourceState::RenderTarget });
    }
    usages.push_back({ depth, RGResourceState::DepthWrite });

    // 지금은 이 패스의 출력을 읽는 패스가 없어서 컬링에 걸린다. 뿌리로 표시해
    // 살려 둔다 — Deferred가 붙으면 그쪽이 읽으므로 이 표시는 빼야 한다.
    const bool keepAlive = true;

    graph.AddPass(GetName(), usages,
        [this, &context, targets, depth](const EnhancedRenderGraph::ExecuteContext& executeContext)
        {
            auto* commandList = executeContext.commandList;
            auto* device = context.resources->GetDevice();

            // 뷰는 매 프레임 만든다. 그래프가 리소스를 프레임마다 다르게 줄 수
            // 있으므로(컬링·앨리어싱) 캐시하면 어긋난다.
            D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[kRenderTargetCount]{};
            const auto rtvBase = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
            for (uint32_t i = 0; i < kRenderTargetCount; ++i)
            {
                rtvHandles[i] = rtvBase;
                rtvHandles[i].ptr += static_cast<SIZE_T>(i) * m_rtvIncrement;
                device->CreateRenderTargetView(executeContext.Resolve(targets[i]), nullptr,
                    rtvHandles[i]);
            }

            const auto dsvHandle = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
            D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
            dsvDesc.Format = kDepthFormat;
            dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
            device->CreateDepthStencilView(executeContext.Resolve(depth), &dsvDesc, dsvHandle);

            const D3D12_VIEWPORT viewport{ 0.f, 0.f,
                static_cast<float>(context.width), static_cast<float>(context.height), 0.f, 1.f };
            const D3D12_RECT scissor{ 0, 0,
                static_cast<LONG>(context.width), static_cast<LONG>(context.height) };
            commandList->RSSetViewports(1, &viewport);
            commandList->RSSetScissorRects(1, &scissor);

            commandList->OMSetRenderTargets(kRenderTargetCount, rtvHandles, FALSE, &dsvHandle);

            // 클리어 값은 0으로 둔다. 그려진 곳과 안 그려진 곳이 값으로 구분되어야
            // 픽셀 검증이 '다섯 타깃 각각이 실제로 기록됐는가'를 볼 수 있다.
            constexpr float kZero[4] = { 0.f, 0.f, 0.f, 0.f };
            for (uint32_t i = 0; i < kRenderTargetCount; ++i)
            {
                commandList->ClearRenderTargetView(rtvHandles[i], kZero, 0, nullptr);
            }
            commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.f, 0, 0, nullptr);

            commandList->SetGraphicsRootSignature(m_rootSignature);
            commandList->SetPipelineState(m_pso);
            commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            // 프레임 상수는 한 번만 올린다. 드로우마다 올리면 같은 값을 수백 번
            // 복사하는 꼴이고, 그건 CE 단계를 늘리는 방향이다.
            //
            // HLSL은 행 우선으로 읽으므로 전치해서 넣는다.
            const Mathf::Matrix viewProjection = XMMatrixTranspose(m_frameViewProjection);
            const auto frameConstants = context.resources->GetUploadRing().Allocate(
                sizeof(Mathf::Matrix), DX12UploadRing::kConstantBufferAlignment);
            if (!frameConstants.IsValid()) return;
            memcpy(frameConstants.cpuAddress, &viewProjection, sizeof(viewProjection));
            commandList->SetGraphicsRootConstantBufferView(0, frameConstants.gpuAddress);

            // 그릴 것이 없으면 클리어만 하고 끝난다 — 빈 씬도 정상 경로다.
            if (nullptr == context.draws) return;

            for (const auto& draw : *context.draws)
            {
                const auto mesh = m_drawGeometry.find(draw.mesh);
                if (mesh == m_drawGeometry.end() || !mesh->second.IsValid()) continue;

                const Mathf::Matrix world = XMMatrixTranspose(draw.worldMatrix);
                const auto drawConstants = context.resources->GetUploadRing().Allocate(
                    sizeof(Mathf::Matrix), DX12UploadRing::kConstantBufferAlignment);
                if (!drawConstants.IsValid()) break;   // 구간이 찼다 — 남은 드로우는 다음 프레임

                memcpy(drawConstants.cpuAddress, &world, sizeof(world));
                commandList->SetGraphicsRootConstantBufferView(1, drawConstants.gpuAddress);

                commandList->IASetVertexBuffers(0, 1, &mesh->second.vertexView);
                commandList->IASetIndexBuffer(&mesh->second.indexView);
                commandList->DrawIndexedInstanced(mesh->second.indexCount, 1, 0, 0, 0);
            }
        },
        keepAlive);
}

void EnhancedGBufferPass::Shutdown()
{
    m_drawGeometry.clear();
    m_rtvHeap.Reset();
    m_dsvHeap.Reset();
    m_pso = nullptr;
    m_rootSignature = nullptr;
}

#endif
