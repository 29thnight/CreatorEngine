#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedGizmoLinePass.h"
#include "DX12DeviceResources.h"
#include "DX12PSOManager.h"
#include "DX12RootSignatureCache.h"
#include "EnhancedRenderGraph.h"

#include <d3dcompiler.h>
#include <cmath>
#include <cstring>
#include <sstream>
#include <string>

#pragma comment(lib, "d3dcompiler.lib")

namespace
{
    // 유니티 빌드에서 익명 네임스페이스가 합쳐지므로 이름을 고유하게 둔다.
    std::string GizmoLineHrToString(HRESULT hr)
    {
        std::ostringstream oss;
        oss << "HRESULT 0x" << std::hex << static_cast<unsigned long>(hr);
        return oss.str();
    }

    // DX11 Gizmo_Line.vs/ps의 이식. 정점이 위치·색뿐이라 셰이더도 그만큼이다.
    // 행렬 규약만 GBuffer·Grid와 맞췄다(전치 업로드 + mul(v, M)).
    constexpr const char* kGizmoLineShader = R"(
cbuffer GizmoCamera : register(b0)
{
    float4x4 gViewProjection;
    float4   gEyePosition;
};

struct VSIn
{
    float3 position : POSITION;
    float4 color    : COLOR;
};

struct VSOut
{
    float4 position : SV_POSITION;
    float4 color    : COLOR;
};

VSOut VSMain(VSIn input)
{
    VSOut output;
    output.position = mul(float4(input.position, 1.0f), gViewProjection);
    output.color = input.color;
    return output;
}

float4 PSMain(VSOut input) : SV_TARGET
{
    return input.color;
}
)";

    struct GizmoCameraConstants
    {
        Mathf::Matrix  viewProjection{};   // 전치해서 넣는다
        Mathf::Vector4 eyePosition{};
    };

    bool CompileGizmoLineShader(const char* entry, const char* target,
        Microsoft::WRL::ComPtr<ID3DBlob>& outBlob, std::string& outError)
    {
        Microsoft::WRL::ComPtr<ID3DBlob> errors;
        const HRESULT hr = D3DCompile(kGizmoLineShader, strlen(kGizmoLineShader),
            nullptr, nullptr, nullptr, entry, target, 0, 0, &outBlob, &errors);
        if (FAILED(hr))
        {
            outError = std::string("기즈모 라인 셰이더 컴파일 실패(") + entry + "): ";
            if (errors) outError += static_cast<const char*>(errors->GetBufferPointer());
            else        outError += GizmoLineHrToString(hr);
            return false;
        }
        return true;
    }
}

// ── 도형 → 선 (DX11 GizmoLinePass의 이식 — 세그먼트 수까지 그대로) ──

void EnhancedGizmoLinePass::AddLine(const Mathf::Vector3& p0, const Mathf::Vector3& p1,
    const Mathf::Color4& color)
{
    m_vertices.push_back({ p0, color });
    m_vertices.push_back({ p1, color });
}

void EnhancedGizmoLinePass::AddWireCircle(const Mathf::Vector3& center, float radius,
    const Mathf::Vector3& up, const Mathf::Color4& color)
{
    using namespace Mathf;
    const int segmentCount = 64;

    Vector3 right = XMVector3Normalize(XMVector3Cross(up, Vector3(0, 1, 0)));
    if (XMVectorGetX(XMVector3LengthSq(right)) < 1e-5f)
        right = XMVector3Normalize(XMVector3Cross(up, Vector3(1, 0, 0)));
    const Vector3 forward = XMVector3Normalize(XMVector3Cross(right, up));

    for (int i = 0; i < segmentCount; ++i)
    {
        const float angle0 = XM_2PI * (i / (float)segmentCount);
        const float angle1 = XM_2PI * ((i + 1) / (float)segmentCount);

        const Vector3 p0 = center + radius * (cosf(angle0) * right + sinf(angle0) * forward);
        const Vector3 p1 = center + radius * (cosf(angle1) * right + sinf(angle1) * forward);

        AddLine(p0, p1, color);
    }
}

void EnhancedGizmoLinePass::AddWireCircleWithDirectionLines(const Mathf::Vector3& center,
    float radius, const Mathf::Vector3& up, const Mathf::Vector3& direction,
    const Mathf::Color4& color)
{
    using namespace Mathf;
    const int segmentCount = 9;
    const float lineLength = radius * 3.f;

    Vector3 right = XMVector3Normalize(XMVector3Cross(up, Vector3(0, 1, 0)));
    if (XMVectorGetX(XMVector3LengthSq(right)) < 1e-5f)
        right = XMVector3Normalize(XMVector3Cross(up, Vector3(1, 0, 0)));
    const Vector3 forward = XMVector3Normalize(XMVector3Cross(right, up));

    Vector3 dirNormalized = direction;
    dirNormalized.Normalize();

    for (int i = 0; i < segmentCount; ++i)
    {
        const float angle0 = XM_2PI * (i / (float)segmentCount);
        const float angle1 = XM_2PI * ((i + 1) / (float)segmentCount);

        const Vector3 p0 = center + radius * (cosf(angle0) * right + sinf(angle0) * forward);
        const Vector3 p1 = center + radius * (cosf(angle1) * right + sinf(angle1) * forward);

        AddLine(p0, p1, color);
        AddLine(p0, p0 + dirNormalized * lineLength, color);
    }
}

void EnhancedGizmoLinePass::AddWireSphere(const Mathf::Vector3& center, float radius,
    const Mathf::Color4& color)
{
    AddWireCircle(center, radius, Mathf::Vector3(0, 1, 0), color); // XZ
    AddWireCircle(center, radius, Mathf::Vector3(1, 0, 0), color); // YZ
    AddWireCircle(center, radius, Mathf::Vector3(0, 0, 1), color); // XY
}

void EnhancedGizmoLinePass::AddWireBox(const Mathf::Matrix& transform,
    const Mathf::Vector3& extents, const Mathf::Color4& color)
{
    Mathf::Vector3 corners[8] = {
        { -extents.x, -extents.y, -extents.z },
        {  extents.x, -extents.y, -extents.z },
        {  extents.x,  extents.y, -extents.z },
        { -extents.x,  extents.y, -extents.z },
        { -extents.x, -extents.y,  extents.z },
        {  extents.x, -extents.y,  extents.z },
        {  extents.x,  extents.y,  extents.z },
        { -extents.x,  extents.y,  extents.z },
    };

    for (auto& corner : corners)
    {
        corner = XMVector3TransformCoord(corner, transform);
    }

    constexpr uint32_t indices[24] = {
        0,1, 1,2, 2,3, 3,0,
        4,5, 5,6, 6,7, 7,4,
        0,4, 1,5, 2,6, 3,7
    };

    for (size_t i = 0; i < 24; i += 2)
    {
        AddLine(corners[indices[i]], corners[indices[i + 1]], color);
    }
}

void EnhancedGizmoLinePass::AddWireCapsule(const Mathf::Matrix& transform,
    float radius, float height, const Mathf::Color4& color)
{
    using namespace Mathf;
    const int segmentCount = 16;

    Vector3 up = transform.Up();       up.Normalize();
    Vector3 right = transform.Right(); right.Normalize();
    Vector3 forward = transform.Forward(); forward.Normalize();

    const float halfHeight = height * 0.5f;
    const Vector3 center = transform.Translation();
    const Vector3 topCenter = center + up * halfHeight;
    const Vector3 bottomCenter = center - up * halfHeight;

    for (int i = 0; i < segmentCount; ++i)
    {
        const float angle = XM_2PI * (static_cast<float>(i) / segmentCount);
        const Vector3 dir = cosf(angle) * right + sinf(angle) * forward;
        AddLine(bottomCenter + dir * radius, topCenter + dir * radius, color);
    }

    AddWireSphere(topCenter, radius, color);
    AddWireSphere(bottomCenter, radius, color);
    AddWireCircle(topCenter, radius, up, color);
    AddWireCircle(bottomCenter, radius, up, color);
}

void EnhancedGizmoLinePass::AddWireCone(const Mathf::Vector3& apex,
    const Mathf::Vector3& direction, float height, float outerConeAngleDegrees,
    const Mathf::Color4& color)
{
    using namespace Mathf;
    const int segmentCount = 32;

    Vector3 dir = direction;
    dir.Normalize();

    Vector3 up = Vector3(0, 1, 0);
    if (fabs(XMVectorGetX(XMVector3Dot(up, dir))) > 0.99f)
        up = Vector3(1, 0, 0);

    const Vector3 right = XMVector3Normalize(XMVector3Cross(dir, up));
    const Vector3 forward = XMVector3Normalize(XMVector3Cross(right, dir));

    const float radius = height * tanf(XMConvertToRadians(outerConeAngleDegrees) * 0.5f);

    for (int i = 0; i < segmentCount; ++i)
    {
        const float angle0 = XM_2PI * (i / (float)segmentCount);
        const float angle1 = XM_2PI * ((i + 1) / (float)segmentCount);

        const Vector3 p0 = apex + dir * height
            + radius * (cosf(angle0) * right + sinf(angle0) * forward);
        const Vector3 p1 = apex + dir * height
            + radius * (cosf(angle1) * right + sinf(angle1) * forward);

        AddLine(p0, p1, color);
        AddLine(apex, p0, color);
    }
}

void EnhancedGizmoLinePass::AddBoundingFrustum(const DirectX::BoundingFrustum& frustum,
    const Mathf::Color4& color)
{
    DirectX::XMFLOAT3 corners[DirectX::BoundingFrustum::CORNER_COUNT];
    frustum.GetCorners(corners);

    constexpr uint32_t indices[24] = {
        0,1, 1,2, 2,3, 3,0,     // 근평면
        0,4, 1,5, 2,6, 3,7,     // 모서리
        4,5, 5,6, 6,7, 7,4      // 원평면
    };

    for (size_t i = 0; i < 24; i += 2)
    {
        AddLine(corners[indices[i]], corners[indices[i + 1]], color);
    }
}

// ── 패스 ──

bool EnhancedGizmoLinePass::Initialize(const EnhancedFrameContext& context,
    std::string& outError)
{
    if (nullptr == context.resources || nullptr == context.psoManager ||
        nullptr == context.rootSignatures)
    {
        outError = "기즈모 라인 패스 컨텍스트가 불완전하다";
        return false;
    }

    return CreatePipelines(context, outError);
}

bool EnhancedGizmoLinePass::CreatePipelines(const EnhancedFrameContext& context,
    std::string& outError)
{
    // 루트 CBV 하나. 색이 정점에 있으므로 상수는 카메라뿐이다.
    D3D12_ROOT_PARAMETER params[1]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;

    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = _countof(params);
    rootDesc.pParameters = params;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    const auto root = context.rootSignatures->GetOrCreate(rootDesc, outError);
    if (!root.IsValid()) return false;
    m_rootSignature = root.signature;

    ComPtr<ID3DBlob> vsBlob;
    ComPtr<ID3DBlob> psBlob;
    if (!CompileGizmoLineShader("VSMain", "vs_5_0", vsBlob, outError)) return false;
    if (!CompileGizmoLineShader("PSMain", "ps_5_0", psBlob, outError)) return false;

    // DX11과 같은 정점 배치.
    const D3D12_INPUT_ELEMENT_DESC inputElements[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
          D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
          D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    DX12GraphicsPipelineDesc desc{};
    desc.vsBytecode = vsBlob->GetBufferPointer();
    desc.vsSize = vsBlob->GetBufferSize();
    desc.psBytecode = psBlob->GetBufferPointer();
    desc.psSize = psBlob->GetBufferSize();
    desc.rootSignature = root.signature;
    desc.rootSignatureId = root.id;
    desc.inputElements = inputElements;
    desc.inputElementCount = _countof(inputElements);
    desc.topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;

    // 깊이를 안 본다 — DX11 원본이 DSV를 바인딩하지 않는다. 기즈모는 물체
    // 뒤에서도 보이는 것이 의도된 동작이다.
    desc.depthEnable = false;
    desc.blendEnable = true;
    desc.cullMode = D3D12_CULL_MODE_NONE;
    desc.numRenderTargets = 1;
    desc.rtvFormats[0] = kOutputFormat;

    m_pso = context.psoManager->GetOrCreate(desc, outError);
    if (nullptr == m_pso) return false;

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.NumDescriptors = 1;
    const HRESULT hr = context.resources->GetDevice()->CreateDescriptorHeap(
        &rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap));
    if (FAILED(hr))
    {
        outError = "기즈모 라인 RTV 힙 생성 실패: " + GizmoLineHrToString(hr);
        return false;
    }

    return true;
}

bool EnhancedGizmoLinePass::PrepareFrame(const EnhancedFrameContext& context,
    std::string& outError)
{
    (void)outError;

    m_width = context.width;
    m_height = context.height;
    m_lastVertexCount = static_cast<uint32_t>(m_vertices.size());
    m_lastDrawCount = 0;

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

void EnhancedGizmoLinePass::Declare(EnhancedRenderGraph& graph,
    const EnhancedFrameContext& context)
{
    m_output = RGHandle{};

    if (nullptr == m_pso || nullptr == m_rtvHeap || 0 == m_width || 0 == m_height)
    {
        return;
    }

    const bool ownsColor = !m_inputs.color.IsValid();

    if (ownsColor)
    {
        RGTextureDesc desc{};
        desc.width = m_width;
        desc.height = m_height;
        desc.format = kOutputFormat;
        desc.allowRenderTarget = true;
        desc.name = "GizmoLine.Output";
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
            auto* commandList = executeContext.commandList;
            auto* device = context.resources->GetDevice();

            const auto rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
            device->CreateRenderTargetView(executeContext.Resolve(m_output), nullptr, rtvHandle);

            const D3D12_VIEWPORT viewport{ 0.f, 0.f,
                static_cast<float>(m_width), static_cast<float>(m_height), 0.f, 1.f };
            const D3D12_RECT scissor{ 0, 0,
                static_cast<LONG>(m_width), static_cast<LONG>(m_height) };
            commandList->RSSetViewports(1, &viewport);
            commandList->RSSetScissorRects(1, &scissor);
            commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

            if (ownsColor)
            {
                constexpr float kClear[4] = { 0.f, 0.f, 0.f, 0.f };
                commandList->ClearRenderTargetView(rtvHandle, kClear, 0, nullptr);
            }

            if (m_vertices.empty()) return;

            GizmoCameraConstants constants{};
            constants.viewProjection = XMMatrixTranspose(m_viewProjection);
            constants.eyePosition = m_eyePosition;

            const auto cb = context.resources->GetUploadRing().Allocate(
                sizeof(GizmoCameraConstants), DX12UploadRing::kConstantBufferAlignment);
            if (!cb.IsValid()) return;
            memcpy(cb.cpuAddress, &constants, sizeof(constants));

            // 프레임의 모든 선을 한 번에 올린다. DX11은 도형마다 Map과
            // 드로우가 나갔다 — 그 차이가 이 패스를 다시 쓴 이유다.
            const uint64_t vertexBytes =
                sizeof(Vertex) * static_cast<uint64_t>(m_vertices.size());
            const auto vertexUpload = context.resources->GetUploadRing().Allocate(
                vertexBytes, 16);
            if (!vertexUpload.IsValid()) return;
            memcpy(vertexUpload.cpuAddress, m_vertices.data(),
                static_cast<size_t>(vertexBytes));

            D3D12_VERTEX_BUFFER_VIEW vertexView{};
            vertexView.BufferLocation = vertexUpload.gpuAddress;
            vertexView.SizeInBytes = static_cast<UINT>(vertexBytes);
            vertexView.StrideInBytes = sizeof(Vertex);

            commandList->SetGraphicsRootSignature(m_rootSignature);
            commandList->SetPipelineState(m_pso);
            commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
            commandList->SetGraphicsRootConstantBufferView(0, cb.gpuAddress);
            commandList->IASetVertexBuffers(0, 1, &vertexView);

            commandList->DrawInstanced(static_cast<UINT>(m_vertices.size()), 1, 0, 0);
            ++m_lastDrawCount;
        },
        m_keepAlive);
}

void EnhancedGizmoLinePass::Shutdown()
{
    m_vertices.clear();
    m_lastVertexCount = 0;
    m_lastDrawCount = 0;
    m_width = 0;
    m_height = 0;

    m_rtvHeap.Reset();
    m_pso = nullptr;
    m_rootSignature = nullptr;
}

#endif
