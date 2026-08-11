#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedGizmoLinePass.h"
#include "DX12DeviceResources.h"
#include "DX12PSOManager.h"
#include "DX12RootSignatureCache.h"
#include "EnhancedRenderGraph.h"
#include "RHIEncoder.h"

#include <cmath>
#include <cstring>
#include <sstream>
#include <string>
#include "DX12ShaderCompiler.h"

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
    constexpr const char* kGizmoLineShaderFile = "GizmoLine.hlsl";

    struct GizmoCameraConstants
    {
        Mathf::Matrix  viewProjection{};   // 전치해서 넣는다
        Mathf::Vector4 eyePosition{};
    };

    bool CompileGizmoLineShader(const char* entry, const char* target,
        RHIShaderBlob& outBlob, std::string& outError)
    {
        return DX12ShaderCompiler::CompileFile(kGizmoLineShaderFile, entry, target, outBlob, outError);
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
    const RHIPipelineLayoutParam params[] = { RHILayout::Cbv(0) };

    RHIPipelineLayoutDesc rootDesc{};
    rootDesc.params = params;
    rootDesc.allowInputAssembler = true;

    const auto root = context.rootSignatures->GetOrCreate(rootDesc, outError);
    if (!root.IsValid()) return false;

    RHIShaderBlob vsBlob;
    RHIShaderBlob psBlob;
    if (!CompileGizmoLineShader("VSMain", "vs_5_0", vsBlob, outError)) return false;
    if (!CompileGizmoLineShader("PSMain", "ps_5_0", psBlob, outError)) return false;

    // DX11과 같은 정점 배치.
    const RHIInputElement inputElements[] = {
        { "POSITION", 0, RHIFormat::RGB32Float, 0,
          D3D12_APPEND_ALIGNED_ELEMENT, 0 },
        { "COLOR", 0, RHIFormat::RGBA32Float, 0,
          D3D12_APPEND_ALIGNED_ELEMENT, 0 },
    };

    RHIGraphicsPipelineDesc desc{};
    desc.vsBytecode = vsBlob.Data();
    desc.vsSize = vsBlob.Size();
    desc.psBytecode = psBlob.Data();
    desc.psSize = psBlob.Size();
    desc.layout = root;
    desc.inputElements = inputElements;
    desc.inputElementCount = _countof(inputElements);
    desc.topologyType = RHITopologyType::Line;

    // 깊이를 안 본다 — DX11 원본이 DSV를 바인딩하지 않는다. 기즈모는 물체
    // 뒤에서도 보이는 것이 의도된 동작이다.
    desc.depthEnable = false;
    desc.blendEnable = true;
    desc.cullMode = RHICullMode::None;
    desc.numRenderTargets = 1;
    desc.rtvFormats[0] = m_outputFormat;

    m_pso = context.psoManager->GetOrCreate(desc, outError);
    if (!m_pso.IsValid()) return false;

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
        desc.name = "GizmoLine.Output";
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

            encoder.SetPipeline(RHIBindPoint::Graphics, m_pso);
            encoder.SetPrimitiveTopology(RHIPrimitiveTopology::LineList);
            encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0, cb.gpuAddress);
            encoder.SetVertexBuffer(vertexView);

            encoder.Draw(static_cast<uint32_t>(m_vertices.size()), 1);
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

    m_pso = {};
}

#endif
