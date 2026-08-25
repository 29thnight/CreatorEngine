#include "EnhancedGizmoLinePass.h"
#include "Render/Graph/EnhancedRenderGraph.h"
#include "RHI/RHIEncoder.h"

#include <cstddef>
#include <cmath>
#include <cstring>
#include <string>
#include "RHI/RHIShaderCompiler.h"

namespace
{
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
        return RHIShaderCompiler::CompileFile(kGizmoLineShaderFile, entry, target, outBlob, outError);
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
          static_cast<uint32_t>(offsetof(Vertex, position)), 0 },
        { "COLOR", 0, RHIFormat::RGBA32Float, 0,
          static_cast<uint32_t>(offsetof(Vertex, color)), 0 },
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
    m_lastVertexCount = static_cast<uint32_t>(m_lines.GetVertices().size());
    m_lastDrawCount = 0;

    if (nullptr != context.camera)
    {
        m_viewProjection = MathematicsInterop::ToDirectX(
            context.camera->view * context.camera->projection);
        const math::vector3& eye = context.camera->eyePosition;
        m_eyePosition = Mathf::Vector4{ eye.x, eye.y, eye.z, 1.f };
    }
    else
    {
        m_viewProjection = DirectX::XMMatrixIdentity();
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

            const std::vector<Vertex>& vertices = m_lines.GetVertices();
            if (vertices.empty()) return;

            GizmoCameraConstants constants{};
            constants.viewProjection = DirectX::XMMatrixTranspose(m_viewProjection);
            constants.eyePosition = m_eyePosition;

            const auto cb = context.resources->UploadConstants(
                &constants, sizeof(GizmoCameraConstants));
            if (!cb.IsValid()) return;
            // 프레임의 모든 선을 한 번에 올린다. DX11은 도형마다 Map과
            // 드로우가 나갔다 — 그 차이가 이 패스를 다시 쓴 이유다.
            const uint64_t vertexBytes =
                sizeof(Vertex) * static_cast<uint64_t>(vertices.size());
            const auto vertexUpload = context.resources->AllocateUpload(
                RHIUploadRequest{ vertexBytes, RHIUploadUsage::VertexData, 16 });
            if (!vertexUpload.IsValid()) return;
            memcpy(vertexUpload.cpuAddress, vertices.data(),
                static_cast<size_t>(vertexBytes));


            encoder.SetPipeline(RHIBindPoint::Graphics, m_pso);
            encoder.SetPrimitiveTopology(RHIPrimitiveTopology::LineList);
            encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0, cb);
            encoder.SetVertexBuffer(vertexUpload, sizeof(Vertex));

            encoder.Draw(static_cast<uint32_t>(vertices.size()), 1);
            ++m_lastDrawCount;
        },
        m_keepAlive);
}

void EnhancedGizmoLinePass::Shutdown()
{
    m_lines.Reset();
    m_lastVertexCount = 0;
    m_lastDrawCount = 0;
    m_width = 0;
    m_height = 0;

    m_pso = {};
}
