#include "EnhancedSSRPass.h"
#include "../../Graph/EnhancedRenderGraph.h"
#include "../../../RHI/RHIEncoder.h"
#include "../../../RHI/RHIShaderCompiler.h"

#include <cstring>
#include <string>
#include <vector>

namespace
{
    // DX11 SSR.ps.hlsl의 이식.
    //
    // 광선 행진·잡음·두께 판정·가중치·최종 혼합을 그대로 옮겼다. 상수도
    // 그대로다. 바꾼 것은 넷이다:
    //
    //   · 정점을 SV_VertexID 풀스크린 삼각형으로. 원본은 Fullscreen.vs의
    //     4정점 트라이앵글 스트립인데 UV 대응이 같다(왼쪽 위 (0,0)).
    //   · prevSSR(t4) 선언과 두 번째 렌더 타깃을 뺀다 — 원본이 읽지 않는다.
    //     그 자리를 비트플래그가 메워 t4로 한 칸 당겨졌다.
    //   · 행렬 규약을 DX12 쪽에 맞춘다(CPU가 전치, mul(v, M)).
    //   · 주석 처리된 죽은 줄들을 옮기지 않는다.
    //
    // ★ 옮기지 '않은' 것은 없다. 아래 셋은 원본의 결함이지만 그대로 둔다:
    //   depth>=1 분기에 return이 없는 것, reflectFactor·edgeFade를 구해
    //   놓고 안 쓰는 것, screenSize가 (0,0)이라 비트플래그가 텍셀 (0,0)만
    //   보는 것. 고치면 그림이 바뀌고, 기준선은 DX11이다.
    constexpr const char* kSSRShaderFile = "Ssr.hlsl";

    struct SSRConstants
    {
        Mathf::Matrix  inverseProjection{};
        Mathf::Matrix  inverseView{};
        Mathf::Matrix  viewProjection{};
        Mathf::Vector4 cameraPosition{};
        float          stepSize{ 0.f };
        float          maxThickness{ 0.f };
        float          time{ 0.f };
        int32_t        maxRayCount{ 0 };

        // ★ DX11이 채우지 않는 자리다. Mathf::Vector2가 SimpleMath라 기본
        // 생성자가 (0,0)을 넣고, 그 값이 그대로 셰이더로 간다. 여기서도
        // 0으로 둬야 같은 그림이 나온다 — 화면 크기를 넣으면 비트플래그
        // 게이트가 원본과 다르게 동작한다(그쪽이 옳은 동작이지만, 그것은
        // DX11을 고치는 일이지 이식하는 일이 아니다).
        float          screenSize[2]{ 0.f, 0.f };
        float          padding[2]{};
    };

    bool CompileSSRShader(const char* entry, const char* target,
        RHIShaderBlob& outBlob, std::string& outError)
    {
        return RHIShaderCompiler::CompileFile(kSSRShaderFile, entry, target, outBlob, outError);
    }
}

bool EnhancedSSRPass::Initialize(const EnhancedFrameContext& context, std::string& outError)
{
    if (nullptr == context.resources || nullptr == context.psoManager ||
        nullptr == context.rootSignatures)
    {
        outError = "SSR 패스 컨텍스트가 불완전하다";
        return false;
    }

    return CreatePipelines(context, outError);
}

bool EnhancedSSRPass::CreatePipelines(const EnhancedFrameContext& context, std::string& outError)
{
    // b0 상수 · t0~t4 테이블(깊이·색·금속거칠기·노멀·비트마스크) ·
    // s0 선형 클램프 · s1 포인트 클램프.
    const RHIPipelineLayoutParam params[] = {
        RHILayout::Cbv(0, RHIShaderVisibility::Pixel),
        RHILayout::SrvTable(5, 0, RHIShaderVisibility::Pixel),
    };

    const RHIStaticSamplerDesc samplers[] = {
        { RHISampler::Linear(RHIAddressMode::Clamp), 0, RHIShaderVisibility::Pixel },
        { RHISampler::Point(RHIAddressMode::Clamp),  1, RHIShaderVisibility::Pixel },
    };

    RHIPipelineLayoutDesc rootDesc{};
    rootDesc.params = params;
    rootDesc.staticSamplers = samplers;

    const auto root = context.rootSignatures->GetOrCreate(rootDesc, outError);
    if (!root.IsValid()) return false;

    RHIShaderBlob vsBlob;
    RHIShaderBlob psBlob;
    if (!CompileSSRShader("VSMain", "vs_5_0", vsBlob, outError)) return false;
    if (!CompileSSRShader("PSMain", "ps_5_0", psBlob, outError)) return false;

    RHIGraphicsPipelineDesc desc{};
    desc.vsBytecode = vsBlob.Data();
    desc.vsSize = vsBlob.Size();
    desc.psBytecode = psBlob.Data();
    desc.psSize = psBlob.Size();
    desc.layout = root;
    desc.inputElements = nullptr;
    desc.inputElementCount = 0;
    desc.topologyType = RHITopologyType::Triangle;

    // 숨어 있던 암묵 상태를 명시한다(SSS·Decal과 같은 부류). 결과는
    // 덮어쓰는 것이 맞고, 풀스크린이라 깊이도 보지 않는다.
    desc.depthEnable = false;
    desc.blendEnable = false;
    desc.cullMode = RHICullMode::None;
    desc.numRenderTargets = 1;
    desc.rtvFormats[0] = kOutputFormat;

    m_pso = context.psoManager->GetOrCreate(desc, outError);
    if (!m_pso.IsValid()) return false;

    return true;
}

bool EnhancedSSRPass::PrepareFrame(const EnhancedFrameContext& context, std::string& outError)
{
    (void)outError;

    m_width = context.width;
    m_height = context.height;

    if (nullptr != context.camera)
    {
        m_inverseProjection = MathematicsInterop::ToDirectX(
            math::transpose(context.camera->inverseProjection));
        m_inverseView = MathematicsInterop::ToDirectX(
            math::transpose(context.camera->inverseView));
        m_viewProjection = MathematicsInterop::ToDirectX(
            math::transpose(context.camera->view * context.camera->projection));
        const math::vector3& eye = context.camera->eyePosition;
        m_cameraPosition = Mathf::Vector4{ eye.x, eye.y, eye.z, 1.f };
    }

    return true;
}

void EnhancedSSRPass::Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context)
{
    // 꺼져 있으면 입력을 그대로 흘린다. 뒤 패스가 '켜졌나'를 따지지 않고
    // GetOutput()만 이으면 되도록 — DX11은 여기서 return해 씬 컬러가
    // 손대지 않은 채 남는데, 핸들을 그대로 넘기는 것이 같은 뜻이다.
    if (!m_enabled)
    {
        m_output = m_inputs.color;
        return;
    }

    m_output = RGHandle{};

    if (!m_pso.IsValid() || 0 == m_width || 0 == m_height) return;
    if (!m_inputs.color.IsValid() || !m_inputs.depth.IsValid() ||
        !m_inputs.metalRough.IsValid() || !m_inputs.normal.IsValid() ||
        !m_inputs.bitmask.IsValid())
    {
        // 입력이 모자라면 켜져 있어도 그릴 수 없다. 입력을 흘려보내
        // 체인이 끊기지 않게 한다.
        m_output = m_inputs.color;
        return;
    }

    // ★ 씬 컬러 복사가 사라진 자리.
    //
    // DX11은 제자리에 쓰느라 씬 컬러를 통째로 복사했다. 여기서는 입력을
    // 읽어 새 transient에 쓴다 — 출력이 입력 이미지만으로 정해지므로
    // 같은 픽셀이 나오고, 읽는 것과 쓰는 것이 갈려 복사가 필요 없다.
    RGTextureDesc desc{};
    desc.width = m_width;
    desc.height = m_height;
    desc.format = kOutputFormat;
    desc.allowRenderTarget = true;
    desc.name = "SSR.Output";
    m_output = graph.CreateTexture(desc);

    const std::vector<EnhancedRenderGraph::RGPassUsage> usages = {
        { m_inputs.color,      RHIResourceState::ShaderResource },
        { m_inputs.depth,      RHIResourceState::ShaderResource },
        { m_inputs.metalRough, RHIResourceState::ShaderResource },
        { m_inputs.normal,     RHIResourceState::ShaderResource },
        { m_inputs.bitmask,    RHIResourceState::ShaderResource },
        { m_output,            RHIResourceState::RenderTarget },
    };

    graph.AddPass(GetName(), usages,
        [this, &context](const EnhancedRenderGraph::ExecuteContext& executeContext)
        {
            RHIEncoder& encoder = *executeContext.encoder;

            const RHITextureHandle colors[] = { executeContext.ResolveHandle(m_output) };
            const auto targets = context.resources->CreateRenderTargets(colors);
            if (!targets.IsValid()) return;

            encoder.SetViewportAndScissor(m_width, m_height);
            encoder.BindRenderTargets(targets);

            // 테이블 하나로 잘라 받는다(R2). 깊이만 포맷을 명시한다 —
            // D32_FLOAT 리소스를 SRV로 읽으려면 R32_FLOAT로 봐야 한다.
            const RHIBindingDesc bindings[] = {
                RHIBindingDesc::Srv2D(executeContext.ResolveHandle(m_inputs.depth),
                    RHIFormat::R32Float),
                RHIBindingDesc::Srv(executeContext.ResolveHandle(m_inputs.color)),
                RHIBindingDesc::Srv(executeContext.ResolveHandle(m_inputs.metalRough)),
                RHIBindingDesc::Srv(executeContext.ResolveHandle(m_inputs.normal)),
                RHIBindingDesc::Srv(executeContext.ResolveHandle(m_inputs.bitmask)),
            };
            const RHIBindingTable srvTable = context.resources->CreateBindings(bindings);
            if (!srvTable.IsValid()) return;

            SSRConstants constants{};
            constants.inverseProjection = m_inverseProjection;
            constants.inverseView = m_inverseView;
            constants.viewProjection = m_viewProjection;
            constants.cameraPosition = m_cameraPosition;
            constants.stepSize = m_tuning.stepSize;
            constants.maxThickness = m_tuning.maxThickness;
            constants.time = m_time;
            constants.maxRayCount = m_tuning.maxRayCount;
            // screenSize는 채우지 않는다 — 위 구조체 주석 참고.

            const auto cb = context.resources->UploadConstants(
                &constants, sizeof(SSRConstants));
            if (!cb.IsValid()) return;
            encoder.SetPipeline(RHIBindPoint::Graphics, m_pso);
            encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0, cb);
            encoder.SetBindings(RHIBindPoint::Graphics, 1, srvTable);

            encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
            encoder.Draw(3, 1);
        },
        m_keepAlive);
}

void EnhancedSSRPass::Shutdown()
{
    m_width = 0;
    m_height = 0;
    m_pso = {};
}

