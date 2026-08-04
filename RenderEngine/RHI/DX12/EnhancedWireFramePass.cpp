#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedWireFramePass.h"
#include "DX12DeviceResources.h"
#include "DX12PSOManager.h"
#include "DX12RootSignatureCache.h"
#include "EnhancedRenderGraph.h"
#include "../../Mesh.h"

#include <d3dcompiler.h>
#include <cstring>
#include <map>
#include <sstream>
#include <string>

#pragma comment(lib, "d3dcompiler.lib")

namespace
{
    // 유니티 빌드에서 익명 네임스페이스가 합쳐지므로 이름을 고유하게 둔다.
    std::string WireHrToString(HRESULT hr)
    {
        std::ostringstream oss;
        oss << "HRESULT 0x" << std::hex << static_cast<unsigned long>(hr);
        return oss.str();
    }

    // DX11 WireFrame.vs/ps의 이식. 원본 VS는 본 스키닝·카메라 방향까지
    // 계산하지만 PS가 실제로 쓰는 것은 상수 색 하나다 — 위치만 남긴다.
    // 색 (0,1,0,1)은 원본 그대로. 스키닝은 EnhancedDrawItem이 본 행렬을
    // 나르게 될 때 GBuffer 애니메이티드 경로와 함께 붙는다.
    constexpr const char* kWireFrameShader = R"(
cbuffer WireFrameFrame : register(b0)
{
    float4x4 gViewProjection;
};

struct WireInstance
{
    float4x4 world;
};

StructuredBuffer<WireInstance> gInstances : register(t0);

struct VSOut
{
    float4 position : SV_POSITION;
};

VSOut VSMain(float3 position : POSITION, uint instanceId : SV_InstanceID)
{
    const float4 worldPosition = mul(float4(position, 1.0f), gInstances[instanceId].world);

    VSOut output;
    output.position = mul(worldPosition, gViewProjection);
    return output;
}

float4 PSMain() : SV_TARGET
{
    return float4(0.0f, 1.0f, 0.0f, 1.0f);
}
)";

    struct WireFrameConstants
    {
        Mathf::Matrix viewProjection{};   // 전치해서 넣는다
    };

    bool CompileWireFrameShader(const char* entry, const char* target,
        Microsoft::WRL::ComPtr<ID3DBlob>& outBlob, std::string& outError)
    {
        Microsoft::WRL::ComPtr<ID3DBlob> errors;
        const HRESULT hr = D3DCompile(kWireFrameShader, strlen(kWireFrameShader),
            nullptr, nullptr, nullptr, entry, target, 0, 0, &outBlob, &errors);
        if (FAILED(hr))
        {
            outError = std::string("와이어프레임 셰이더 컴파일 실패(") + entry + "): ";
            if (errors) outError += static_cast<const char*>(errors->GetBufferPointer());
            else        outError += WireHrToString(hr);
            return false;
        }
        return true;
    }
}

bool EnhancedWireFramePass::Initialize(const EnhancedFrameContext& context,
    std::string& outError)
{
    if (nullptr == context.resources || nullptr == context.psoManager ||
        nullptr == context.rootSignatures || nullptr == context.meshCache)
    {
        outError = "와이어프레임 패스 컨텍스트가 불완전하다";
        return false;
    }

    return CreatePipelines(context, outError);
}

bool EnhancedWireFramePass::CreatePipelines(const EnhancedFrameContext& context,
    std::string& outError)
{
    // b0 상수(뷰투영) · t0 인스턴스 월드(루트 SRV). 텍스처가 없다.
    D3D12_ROOT_PARAMETER params[2]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[1].Descriptor.ShaderRegister = 0;

    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = _countof(params);
    rootDesc.pParameters = params;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    const auto root = context.rootSignatures->GetOrCreate(rootDesc, outError);
    if (!root.IsValid()) return false;
    m_rootSignature = root.signature;

    ComPtr<ID3DBlob> vsBlob;
    ComPtr<ID3DBlob> psBlob;
    if (!CompileWireFrameShader("VSMain", "vs_5_0", vsBlob, outError)) return false;
    if (!CompileWireFrameShader("PSMain", "ps_5_0", psBlob, outError)) return false;

    // 엔진 Vertex에서 위치만 읽는다. 스트라이드는 정점 버퍼 뷰가 정하므로
    // 요소를 다 선언할 필요가 없다(GBuffer도 부분 선언이다).
    static const D3D12_INPUT_ELEMENT_DESC kInputElements[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
    static_assert(offsetof(Vertex, position) == 0, "Vertex 레이아웃이 바뀌었다");

    DX12GraphicsPipelineDesc desc{};
    desc.vsBytecode = vsBlob->GetBufferPointer();
    desc.vsSize = vsBlob->GetBufferSize();
    desc.psBytecode = psBlob->GetBufferPointer();
    desc.psSize = psBlob->GetBufferSize();
    desc.rootSignature = root.signature;
    desc.rootSignatureId = root.id;
    desc.inputElements = kInputElements;
    desc.inputElementCount = _countof(kInputElements);
    desc.topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

    // ★ 이 패스의 요점 — 채우기 대신 선을 긋는다.
    desc.fillMode = D3D12_FILL_MODE_WIREFRAME;

    // 원본과 같이 깊이는 보고(가려짐), 블렌딩은 없다(불투명 선).
    desc.depthEnable = true;
    desc.blendEnable = false;
    desc.cullMode = D3D12_CULL_MODE_NONE;
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
        outError = "와이어프레임 RTV 힙 생성 실패: " + WireHrToString(hr);
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.NumDescriptors = 1;
    hr = context.resources->GetDevice()->CreateDescriptorHeap(
        &dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap));
    if (FAILED(hr))
    {
        outError = "와이어프레임 DSV 힙 생성 실패: " + WireHrToString(hr);
        return false;
    }

    return true;
}

void EnhancedWireFramePass::CollectDraws(const std::vector<EnhancedDrawItem>* draws)
{
    if (nullptr == draws) return;

    for (const EnhancedDrawItem& draw : *draws)
    {
        if (nullptr == draw.mesh) continue;

        ++m_lastDrawItemCount;

        // 배치는 (메시)로만 가른다 — 재질은 이 패스에 없다.
        Batch* batch = nullptr;
        for (Batch& existing : m_batches)
        {
            if (existing.mesh == draw.mesh) { batch = &existing; break; }
        }

        if (nullptr == batch)
        {
            m_batches.push_back(Batch{ draw.mesh, 0, 0 });
            batch = &m_batches.back();
        }
        ++batch->count;
    }
}

bool EnhancedWireFramePass::PrepareFrame(const EnhancedFrameContext& context,
    std::string& outError)
{
    m_width = context.width;
    m_height = context.height;

    m_batches.clear();
    m_instanceWorlds.clear();
    m_geometry.clear();
    m_lastDrawItemCount = 0;
    m_lastBatchCount = 0;

    if (nullptr != context.camera)
    {
        m_viewProjection = XMMatrixMultiply(context.camera->view, context.camera->projection);
    }
    else
    {
        m_viewProjection = XMMatrixIdentity();
    }

    // 두 큐를 다 그린다 — 와이어프레임은 deferred/forward 구분이 없다.
    CollectDraws(context.draws);
    CollectDraws(context.forwardDraws);

    if (m_batches.empty()) return true;

    // 배치별 시작 위치를 정하고, 월드 행렬을 배치 순서로 연속 배치한다.
    uint32_t offset = 0;
    for (Batch& batch : m_batches)
    {
        batch.first = offset;
        offset += batch.count;
        batch.count = 0;   // 아래 채우기에서 다시 센다
    }
    m_instanceWorlds.resize(offset);

    const auto place = [&](const std::vector<EnhancedDrawItem>* draws)
    {
        if (nullptr == draws) return;
        for (const EnhancedDrawItem& draw : *draws)
        {
            if (nullptr == draw.mesh) continue;
            for (Batch& batch : m_batches)
            {
                if (batch.mesh != draw.mesh) continue;
                m_instanceWorlds[batch.first + batch.count] =
                    XMMatrixTranspose(draw.worldMatrix);
                ++batch.count;
                break;
            }
        }
    };
    place(context.draws);
    place(context.forwardDraws);

    // 메시 업로드는 여기서 한다 — Record는 리소스를 만들지 않는다(3-6 규약).
    // GBuffer가 이미 올린 메시면 캐시 히트라 업로드 0회다.
    for (const Batch& batch : m_batches)
    {
        std::string uploadError;
        const auto entry = context.meshCache->GetOrUpload(batch.mesh, uploadError);
        if (!entry.IsValid())
        {
            if (!uploadError.empty())
            {
                outError = "와이어프레임 메시 업로드 실패: " + uploadError;
                return false;
            }
            continue;   // 빈 메시는 건너뛴다
        }
        m_geometry[batch.mesh] = entry;
    }

    m_lastBatchCount = static_cast<uint32_t>(m_batches.size());
    return true;
}

void EnhancedWireFramePass::Declare(EnhancedRenderGraph& graph,
    const EnhancedFrameContext& context)
{
    m_output = RGHandle{};
    m_depth = RGHandle{};

    if (nullptr == m_pso || nullptr == m_rtvHeap || nullptr == m_dsvHeap ||
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
        desc.name = "WireFrame.Output";
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
        desc.name = "WireFrame.Depth";
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

            if (m_batches.empty() || m_instanceWorlds.empty()) return;

            WireFrameConstants constants{};
            constants.viewProjection = XMMatrixTranspose(m_viewProjection);

            const auto cb = context.resources->GetUploadRing().Allocate(
                sizeof(WireFrameConstants), DX12UploadRing::kConstantBufferAlignment);
            if (!cb.IsValid()) return;
            memcpy(cb.cpuAddress, &constants, sizeof(constants));

            const uint64_t instanceBytes =
                sizeof(Mathf::Matrix) * static_cast<uint64_t>(m_instanceWorlds.size());
            const auto instanceUpload = context.resources->GetUploadRing().Allocate(
                instanceBytes, sizeof(Mathf::Matrix));
            if (!instanceUpload.IsValid()) return;
            memcpy(instanceUpload.cpuAddress, m_instanceWorlds.data(),
                static_cast<size_t>(instanceBytes));

            commandList->SetGraphicsRootSignature(m_rootSignature);
            commandList->SetPipelineState(m_pso);
            commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            commandList->SetGraphicsRootConstantBufferView(0, cb.gpuAddress);

            for (const Batch& batch : m_batches)
            {
                const auto geometry = m_geometry.find(batch.mesh);
                if (geometry == m_geometry.end()) continue;

                commandList->SetGraphicsRootShaderResourceView(1,
                    instanceUpload.gpuAddress
                    + static_cast<uint64_t>(batch.first) * sizeof(Mathf::Matrix));

                commandList->IASetVertexBuffers(0, 1, &geometry->second.vertexView);
                commandList->IASetIndexBuffer(&geometry->second.indexView);
                commandList->DrawIndexedInstanced(geometry->second.indexCount,
                    batch.count, 0, 0, 0);
            }
        },
        m_keepAlive);
}

void EnhancedWireFramePass::Shutdown()
{
    m_batches.clear();
    m_instanceWorlds.clear();
    m_geometry.clear();
    m_lastDrawItemCount = 0;
    m_lastBatchCount = 0;
    m_width = 0;
    m_height = 0;

    m_rtvHeap.Reset();
    m_dsvHeap.Reset();
    m_pso = nullptr;
    m_rootSignature = nullptr;
}

#endif
