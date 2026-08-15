#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedSSSPass.h"
#include "../../Graph/EnhancedRenderGraph.h"
#include "../../../RHI/RHIEncoder.h"
#include "../../../RHI/DX12/DX12ShaderCompiler.h"

#include <cstring>
#include <string>
#include <vector>

namespace
{
    // DX11 SSS.ps.hlsl의 이식. 25샘플 커널과 표면 추종 수식을 그대로 옮겼다 —
    // 그림의 기준선이므로 상수 하나도 건드리지 않는다.
    //
    // 바꾼 것은 셋뿐이다:
    //   · 정점을 SV_VertexID 풀스크린 삼각형으로(DX11은 Fullscreen.vs + Draw(4))
    //   · MetalRough(t2) 선언 제거 — 원본이 읽지 않는다
    //   · direction을 상수로 받되 호출부가 축을 고정한다(원본과 같은 동작)
    constexpr const char* kSSSShaderFile = "Sss.hlsl";

    struct SSSConstants
    {
        float direction[2]{};
        float strength{ 0.f };
        float width{ 0.f };
        float cameraFov{ 0.f };
        float padding[3]{};
    };

    bool CompileSSSShader(const char* entry, const char* target,
        RHIShaderBlob& outBlob, std::string& outError)
    {
        return DX12ShaderCompiler::CompileFile(kSSSShaderFile, entry, target, outBlob, outError);
    }
}

bool EnhancedSSSPass::Initialize(const EnhancedFrameContext& context, std::string& outError)
{
    if (nullptr == context.resources || nullptr == context.psoManager ||
        nullptr == context.rootSignatures)
    {
        outError = "SSS 패스 컨텍스트가 불완전하다";
        return false;
    }

    return CreatePipelines(context, outError);
}

bool EnhancedSSSPass::CreatePipelines(const EnhancedFrameContext& context, std::string& outError)
{
    // b0 상수 · t0~t1 테이블(깊이·색) · s0 선형 클램프.
    //
    // 클램프가 중요하다 — 커널이 화면 밖을 짚을 때 WRAP이면 반대편 색이
    // 딸려 와 가장자리에 엉뚱한 번짐이 생긴다.
    const RHIPipelineLayoutParam params[] = {
        RHILayout::Cbv(0, RHIShaderVisibility::Pixel),
        RHILayout::SrvTable(2, 0, RHIShaderVisibility::Pixel),
    };

    const RHIStaticSamplerDesc samplers[] = {
        { RHISampler::Linear(RHIAddressMode::Clamp), 0, RHIShaderVisibility::Pixel },
    };

    RHIPipelineLayoutDesc rootDesc{};
    rootDesc.params = params;
    rootDesc.staticSamplers = samplers;

    const auto root = context.rootSignatures->GetOrCreate(rootDesc, outError);
    if (!root.IsValid()) return false;

    RHIShaderBlob vsBlob;
    RHIShaderBlob psBlob;
    if (!CompileSSSShader("VSMain", "vs_5_0", vsBlob, outError)) return false;
    if (!CompileSSSShader("PSMain", "ps_5_0", psBlob, outError)) return false;

    RHIGraphicsPipelineDesc desc{};
    desc.vsBytecode = vsBlob.Data();
    desc.vsSize = vsBlob.Size();
    desc.psBytecode = psBlob.Data();
    desc.psSize = psBlob.Size();
    desc.layout = root;
    desc.inputElements = nullptr;
    desc.inputElementCount = 0;
    desc.topologyType = RHITopologyType::Triangle;

    // ★ 숨어 있던 암묵 상태를 명시한다.
    //
    // DX11은 이 패스에서 블렌드·깊이 상태를 세우지 않고 앞 패스가 남긴
    // 것에 얹혀 갔다. DX12는 PSO에 박아야 하므로 결정해야 하고, 블러
    // 결과는 덮어쓰는 것이 맞다(깊이도 안 본다 — 풀스크린이다).
    desc.depthEnable = false;
    desc.blendEnable = false;
    desc.cullMode = RHICullMode::None;
    desc.numRenderTargets = 1;
    desc.rtvFormats[0] = kOutputFormat;

    m_pso = context.psoManager->GetOrCreate(desc, outError);
    if (!m_pso.IsValid()) return false;

    return true;
}

bool EnhancedSSSPass::PrepareFrame(const EnhancedFrameContext& context, std::string& outError)
{
    (void)outError;

    m_width = context.width;
    m_height = context.height;

    // FOV는 도로 넘긴다 — 셰이더가 그렇게 받는다(원본 그대로).
    m_cameraFov = (nullptr != context.camera)
        ? XMConvertToDegrees(context.camera->fov) : 60.f;

    return true;
}

void EnhancedSSSPass::Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context)
{
    // 꺼져 있으면 입력을 그대로 흘린다. 뒤 패스가 '켜졌나'를 따지지 않고
    // GetOutput()만 이으면 되도록 — SSR과 같은 규약이다.
    if (!m_enabled)
    {
        m_horizontal = RGHandle{};
        m_output = m_inputs.color;
        return;
    }

    m_output = RGHandle{};
    m_horizontal = RGHandle{};

    if (!m_pso.IsValid() || 0 == m_width || 0 == m_height) return;
    if (!m_inputs.color.IsValid() || !m_inputs.depth.IsValid()) return;

    // ★ 복사가 사라진 자리.
    //
    // DX11은 읽으면서 쓸 수 없어 매 축마다 씬 컬러를 통째로 복사했다
    // (화면 크기 CopyResource 2회). 여기서는 가로 블러가 transient에 쓰고
    // 세로 블러가 그것을 읽으므로 복사가 필요 없다.
    RGTextureDesc desc{};
    desc.width = m_width;
    desc.height = m_height;
    desc.format = kOutputFormat;
    desc.allowRenderTarget = true;

    desc.name = "SSS.Horizontal";
    m_horizontal = graph.CreateTexture(desc);

    desc.name = "SSS.Output";
    m_output = graph.CreateTexture(desc);

    // 두 축을 각각 선언한다. 그래프가 사이의 전이 배리어를 만들어 준다 —
    // 가로가 쓴 것을 세로가 읽으므로 RENDER_TARGET → SHADER_RESOURCE다.
    // isFinal은 예전에 rtvIndex가 겸하던 판단이다 — 힙 슬롯 번호가 '마지막
    // 축인가'까지 뜻하고 있었다. R2b가 슬롯을 걷어내면서 그 겸직이 드러나
    // 뜻하는 바를 그대로 적었다.
    const auto declareAxis = [&](RGHandle source, RGHandle target,
        bool isFinal, float dirX, float dirY, const char* name)
    {
        const std::vector<EnhancedRenderGraph::RGPassUsage> usages = {
            { source,         RHIResourceState::ShaderResource },
            { m_inputs.depth, RHIResourceState::ShaderResource },
            { target,         RHIResourceState::RenderTarget },
        };

        graph.AddPass(name, usages,
            [this, &context, source, target, dirX, dirY](
                const EnhancedRenderGraph::ExecuteContext& executeContext)
            {
                RHIEncoder& encoder = *executeContext.encoder;

                const RHITextureHandle colors[] = { executeContext.ResolveHandle(target) };
                const auto targets = context.resources->CreateRenderTargets(colors);
                if (!targets.IsValid()) return;

                encoder.SetViewportAndScissor(m_width, m_height);
                encoder.BindRenderTargets(targets);

                // 테이블 하나로 잘라 받는다(R2). 깊이는 D32_FLOAT 리소스를
                // SRV로 읽는 것이라 포맷을 R32_FLOAT로 명시해야 하고,
                // 색은 리소스가 아는 대로 보면 된다.
                const RHIBindingDesc bindings[] = {
                    RHIBindingDesc::Srv2D(executeContext.ResolveHandle(m_inputs.depth),
                        RHIFormat::R32Float),
                    RHIBindingDesc::Srv(executeContext.ResolveHandle(source)),
                };
                const RHIBindingTable srvTable = context.resources->CreateBindings(bindings);
                if (!srvTable.IsValid()) return;

                SSSConstants constants{};
                constants.direction[0] = dirX;
                constants.direction[1] = dirY;
                constants.strength = m_tuning.strength;
                constants.width = m_tuning.width;
                constants.cameraFov = m_cameraFov;

                const auto cb = context.resources->UploadConstants(
                    &constants, sizeof(SSSConstants));
                if (!cb.IsValid()) return;
                encoder.SetPipeline(RHIBindPoint::Graphics, m_pso);
                encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0, cb);
                encoder.SetBindings(RHIBindPoint::Graphics, 1, srvTable);

                encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
                encoder.Draw(3, 1);
            },
            // 가로는 세로가 읽으므로 뿌리가 아니어도 살아남는다. 세로는
            // 소비자가 붙기 전까지 호출부가 정한다.
            isFinal ? m_keepAlive : false);
    };

    // 축은 고정이다. DX11의 direction 슬라이더는 코드가 항상 덮어써
    // 죽어 있었고, 분리 블러는 축이 고정이어야 맞다.
    declareAxis(m_inputs.color, m_horizontal, false, 1.f, 0.f, "SSS.Horizontal");
    declareAxis(m_horizontal, m_output, true, 0.f, 1.f, "SSS.Vertical");
}

void EnhancedSSSPass::Shutdown()
{
    m_width = 0;
    m_height = 0;
    m_pso = {};
}

#endif
