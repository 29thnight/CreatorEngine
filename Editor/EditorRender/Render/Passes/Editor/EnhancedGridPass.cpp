#include "EnhancedGridPass.h"
#include "Render/Graph/EnhancedRenderGraph.h"
#include "RHI/RHIEncoder.h"

#include <cstring>
#include <sstream>
#include <string>
#include <vector>
#include "RHI/RHIShaderCompiler.h"

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
    constexpr const char* kGridShaderFile = "Grid.hlsl";

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
        RHIShaderBlob& outBlob, std::string& outError)
    {
        return RHIShaderCompiler::CompileFile(kGridShaderFile, entry, target, outBlob, outError);
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
    const RHIPipelineLayoutParam params[] = { RHILayout::Cbv(0) };

    RHIPipelineLayoutDesc rootDesc{};
    rootDesc.params = params;

    const auto root = context.rootSignatures->GetOrCreate(rootDesc, outError);
    if (!root.IsValid()) return false;

    RHIShaderBlob vsBlob;
    RHIShaderBlob psBlob;
    if (!CompileGridShader("VSMain", "vs_5_0", vsBlob, outError)) return false;
    if (!CompileGridShader("PSMain", "ps_5_0", psBlob, outError)) return false;

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

    // 깊이를 켠다. DX11 그리드는 씬 깊이에 가려지고 자기 깊이도 쓴다.
    desc.depthEnable = true;

    // 선 사이가 투명해야 아래 그림이 비친다 — 알파 블렌딩이 그리드의 본질이다.
    //
    // 공용 blendEnable 경로의 알파식은 ONE/ZERO라서 destination alpha를
    // source alpha로 교체한다. 그리드 셀 내부의 source alpha는 0이므로,
    // 포스트 체인의 불투명 출력까지 투명해져 최종 합성에서 검게 보인다.
    // RGB는 기존 SRC_ALPHA/INV_SRC_ALPHA를 유지하고, 알파만 straight-alpha
    // 누적식 As + Ad*(1-As)을 써서 불투명 배경은 1로 보존한다.
    desc.independentBlend = true;
    auto& blend = desc.renderTargetBlend[0];
    blend.enable = true;
    blend.srcColor = RHIBlendFactor::SrcAlpha;
    blend.dstColor = RHIBlendFactor::InvSrcAlpha;
    blend.colorOp = RHIBlendOp::Add;
    blend.srcAlpha = RHIBlendFactor::One;
    blend.dstAlpha = RHIBlendFactor::InvSrcAlpha;
    blend.alphaOp = RHIBlendOp::Add;
    blend.writeMask = 0xF;
    desc.cullMode = RHICullMode::None;   // DX11도 CULL_NONE이다(아래에서 봐도 그린다)
    desc.numRenderTargets = 1;
    desc.rtvFormats[0] = m_outputFormat;
    desc.dsvFormat = kDepthFormat;

    m_pso = context.psoManager->GetOrCreate(desc, outError);
    if (!m_pso.IsValid()) return false;

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

    if (!m_pso.IsValid() ||
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
        desc.format = m_outputFormat;
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
    usages.push_back({ m_output, RHIResourceState::RenderTarget });
    usages.push_back({ m_depth, RHIResourceState::DepthWrite });

    graph.AddPass(GetName(), usages,
        [this, &context, ownsColor, ownsDepth](
            const EnhancedRenderGraph::ExecuteContext& executeContext)
        {
            // 커맨드는 인코더에만 적는다(R3). commandList가 아직 남은 것은
            // 렌더 타깃 바인딩 셋뿐이고, 그쪽은 R2b가 만든 서비스가 커맨드
            // 리스트를 받는 형태라 R3에서 함께 정리한다.
            RHIEncoder& encoder = *executeContext.encoder;

            // 뷰는 매 프레임 만든다. 그래프가 리소스를 프레임마다 다르게 줄 수
            // 있으므로(컬링·앨리어싱) 캐시하면 어긋난다.
            const RHITextureHandle colors[] = { executeContext.ResolveHandle(m_output) };
            const auto depthDesc = RHIDepthTargetDesc::Depth(
                executeContext.ResolveHandle(m_depth), kDepthFormat);
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

            const auto cb = context.resources->UploadConstants(
                &constants, sizeof(GridConstants));
            if (!cb.IsValid()) return;
            // 파이프라인과 루트 시그니처를 한 번에 건다 — 둘을 따로 거는
            // 자리가 없어야 "루트 시그니처를 안 걸고 루트를 건드린다"가
            // 표현 불가능해진다(RHIEncoder.h ③).
            encoder.SetPipeline(RHIBindPoint::Graphics, m_pso);
            encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleStrip);
            encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0, cb);

            encoder.Draw(4, 1);
        },
        m_keepAlive);
}

void EnhancedGridPass::Shutdown()
{
    m_width = 0;
    m_height = 0;

    m_pso = {};
}
