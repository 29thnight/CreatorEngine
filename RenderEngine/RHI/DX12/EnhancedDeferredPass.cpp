#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedDeferredPass.h"
#include "EnhancedShadowPass.h"
#include "DX12DeviceResources.h"
#include "DX12PSOManager.h"
#include "DX12RootSignatureCache.h"
#include "RHIEncoder.h"

#include <sstream>
#include "DX12ShaderCompiler.h"

namespace
{
    // 유니티 빌드에서 익명 네임스페이스가 파일 간 합쳐지므로 이름을 고유하게 둔다.
    constexpr const char* kDeferredShaderFile = "Deferred.hlsl";

    bool CompileDeferredShader(const char* entry, const char* target,
        RHIShaderBlob& outBlob, std::string& outError)
    {
        return DX12ShaderCompiler::CompileFile(kDeferredShaderFile, entry, target, outBlob, outError);
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

    RHIShaderBlob vsBlob;
    RHIShaderBlob psBlob;
    if (!CompileDeferredShader("VSMain", "vs_5_0", vsBlob, outError)) return false;
    if (!CompileDeferredShader("PSMain", "ps_5_0", psBlob, outError)) return false;

    // SRV를 한 테이블로 묶는다. 디스크립터 링에서 연속으로 잘라 쓰므로
    // 테이블 하나면 충분하고, 바인딩 호출도 한 번이다(상태 변경 최소화).
    // 광원 상수는 루트 CBV로 넘긴다 — 업로드 링 주소를 그대로 꽂으면 되고,
    // 프레임마다 한 번이라 디스크립터를 만들 이유가 없다.
    const RHIPipelineLayoutParam params[] = {
        // diffuse · metalRough · normal · emissive · depth · shadow
        // + IBL 셋(조도 · 프리필터 · LUT)
        RHILayout::SrvTable(9, 0, RHIShaderVisibility::Pixel),
        RHILayout::SamplerTable(3, 0, RHIShaderVisibility::Pixel),   // 일반 · 비교(그림자) · IBL 선형
        RHILayout::Cbv(0, RHIShaderVisibility::Pixel),
    };

    RHIPipelineLayoutDesc rootDesc{};
    rootDesc.params = params;

    const auto root = context.rootSignatures->GetOrCreate(rootDesc, outError);
    if (!root.IsValid()) return false;

    RHIGraphicsPipelineDesc desc{};
    desc.vsBytecode = vsBlob.Data();
    desc.vsSize = vsBlob.Size();
    desc.psBytecode = psBlob.Data();
    desc.psSize = psBlob.Size();
    desc.layout = root;
    desc.numRenderTargets = 1;
    desc.rtvFormats[0] = kOutputFormat;

    m_pso = context.psoManager->GetOrCreate(desc, outError);
    if (!m_pso.IsValid()) return false;

    // 샘플러들을 연속으로 만든다 — 테이블은 연속이어야 하므로 따로 만들어
    // 인접을 기대하면 안 된다.
    const RHISamplerDesc samplers[] = {
        RHISampler::Point(RHIAddressMode::Clamp),

        // 그림자 비교 샘플러. 경계 색을 흰색으로 두어 맵 밖이 '빛을 받음'이 되게 한다.
        RHISampler::Comparison(RHICompareOp::LessEqual, RHIAddressMode::Border,
            RHIBorderColor::OpaqueWhite),

        // IBL용 선형 클램프. 프리필터 밉 사이 보간(TRILINEAR)까지 필요하다 —
        // 거칠기가 밉 좌표라서 포인트로 읽으면 거칠기 단차가 띠로 보인다.
        RHISampler::Linear(RHIAddressMode::Clamp),
    };

    m_sampler = RHISamplerTable{ context.resources->GetSamplerHeap().CreateRange(samplers) };
    if (!m_sampler.IsValid())
    {
        outError = "Deferred 샘플러 생성 실패";
        return false;
    }

    return true;
}

bool EnhancedDeferredPass::PrepareFrame(const EnhancedFrameContext& context, std::string& outError)
{
    m_frameLights.clear();
    m_droppedLights = 0;

    if (nullptr != context.camera)
    {
        const Mathf::xMatrix viewProjection =
            XMMatrixMultiply(context.camera->view, context.camera->projection);

        // HLSL이 행 우선으로 읽으므로 전치해서 넣는다. 역행렬을 여기서 구해 두면
        // 픽셀마다 다시 구하지 않는다.
        m_inverseViewProjection = XMMatrixTranspose(XMMatrixInverse(nullptr, viewProjection));
        m_eyePosition = context.camera->eyePosition;
    }
    else
    {
        m_inverseViewProjection = XMMatrixIdentity();
        m_eyePosition = Mathf::Vector4{};
    }

    if (nullptr == context.lights) return true;

    // ★ 앞에서부터 채운다 — 뷰가 이미 기여도 순으로 세워 보냈으므로
    //   (EnhancedLightPacking.h의 SelectLightsForView) "앞의 kMaxLights개"가
    //   곧 "가장 중요한 kMaxLights개"다. 예전에는 이 자리가 등록 순서로
    //   잘랐고, 그래서 씬을 밝히는 태양이 나중에 등록됐다는 이유로 빠질 수
    //   있었다.
    for (const auto& light : *context.lights)
    {
        if (m_frameLights.size() >= kMaxLights)
        {
            // 세기는 하되 오류로 올리지는 않는다. 뷰가 고른 결과를 한도만큼
            // 받는 것은 설계된 동작이고, 매 프레임 오류 문자열을 세우면
            // 진짜 실패가 그 안에 묻힌다. 잘린 수는 status가 낸다.
            ++m_droppedLights;
            continue;
        }
        m_frameLights.push_back(light);
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
        { m_inputs.diffuse,    RHIResourceState::ShaderResource },
        { m_inputs.metalRough, RHIResourceState::ShaderResource },
        { m_inputs.normal,     RHIResourceState::ShaderResource },
        { m_inputs.emissive,   RHIResourceState::ShaderResource },
        // 깊이는 DEPTH_WRITE에서 읽기 상태로 넘어와야 한다. 그래프가 알아서
        // 전이를 만들지만, 여기서 선언하지 않으면 만들지 않는다.
        { m_inputs.depth,      RHIResourceState::ShaderResource },
        { m_shadowMap,         RHIResourceState::ShaderResource },   // 캐스케이드 배열
        { m_output,            RHIResourceState::RenderTarget },
    };

    graph.AddPass(GetName(), usages,
        [this, &context](const EnhancedRenderGraph::ExecuteContext& executeContext)
        {
            RHIEncoder& encoder = *executeContext.encoder;

            const RHITextureHandle colors[] = { executeContext.ResolveHandle(m_output) };
            const auto targets = context.resources->CreateRenderTargets(colors);
            if (!targets.IsValid()) return;

            // IBL 셋(t6~t8)은 셋이 다 있을 때만 건다. 하나라도 없으면 널
            // 디스크립터를 깔고 셰이더가 hasIbl로 분기한다.
            const bool hasIbl = m_iblIrradiance.IsValid()
                && m_iblPrefiltered.IsValid() && m_iblBrdfLut.IsValid();

            constexpr RHIFormat kIblFormat = RHIFormat::RGBA16Float;

            // t0~t8을 테이블 하나로 잘라 받는다(R2).
            const RHIBindingDesc srvs[] = {
                RHIBindingDesc::Srv(executeContext.ResolveHandle(m_inputs.diffuse)),
                RHIBindingDesc::Srv(executeContext.ResolveHandle(m_inputs.metalRough)),
                RHIBindingDesc::Srv(executeContext.ResolveHandle(m_inputs.normal)),
                RHIBindingDesc::Srv(executeContext.ResolveHandle(m_inputs.emissive)),
                RHIBindingDesc::SrvDepth(executeContext.ResolveHandle(m_inputs.depth)),
                // 그림자 맵도 깊이지만 배열이라 차원까지 바꿔 봐야 한다.
                RHIBindingDesc::SrvArray(executeContext.ResolveHandle(m_shadowMap),
                    RHIFormat::R32Float, kShadowCascadeCount),
                RHIBindingDesc::SrvCube(hasIbl ? m_iblIrradiance : RHITextureHandle{},
                    kIblFormat, 1).OrNull(),
                RHIBindingDesc::SrvCube(hasIbl ? m_iblPrefiltered : RHITextureHandle{},
                    kIblFormat, hasIbl ? m_iblPrefilterMips : 1).OrNull(),
                RHIBindingDesc::Srv2D(hasIbl ? m_iblBrdfLut : RHITextureHandle{},
                    kIblFormat).OrNull(),
            };
            const RHIBindingTable srvTable = context.resources->CreateBindings(srvs);
            if (!srvTable.IsValid()) return;

            // 광원 상수를 업로드 링에 올린다.
            LightingConstants constants{};
            constants.inverseViewProjection = m_inverseViewProjection;
            for (uint32_t i = 0; i < kShadowCascadeCount; ++i)
            {
                constants.lightViewProjection[i] =
                    XMMatrixTranspose(m_shadowData.lightViewProjection[i]);
            }
            constants.eyePosition = m_eyePosition;
            constants.cameraForward = m_shadowData.cameraForward;
            constants.cascadeSplits = m_shadowData.splitDepths;
            constants.shadowBias = m_shadowData.bias;
            constants.lightCount = static_cast<uint32_t>(m_frameLights.size());
            constants.hasShadow = m_shadowData.enabled ? 1u : 0u;
            constants.cascadeBlendBand = m_shadowData.cascadeBlendBand;
            constants.hasIbl = hasIbl ? 1u : 0u;
            for (size_t i = 0; i < m_frameLights.size(); ++i)
            {
                constants.lights[i] = m_frameLights[i];
            }

            const auto lightConstants = context.resources->GetUploadRing().Allocate(
                sizeof(LightingConstants), DX12UploadRing::kConstantBufferAlignment);
            if (!lightConstants.IsValid()) return;
            memcpy(lightConstants.cpuAddress, &constants, sizeof(constants));

            encoder.SetViewportAndScissor(context.width, context.height);
            encoder.BindRenderTargets(targets);

            encoder.SetPipeline(RHIBindPoint::Graphics, m_pso);
            encoder.SetBindings(RHIBindPoint::Graphics, 0, srvTable);
            encoder.SetSamplers(RHIBindPoint::Graphics, 1, m_sampler);
            encoder.SetConstantBuffer(RHIBindPoint::Graphics, 2, lightConstants.gpuAddress);

            encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
            encoder.Draw(3, 1);
        },
        // 이 패스의 결과가 최종 출력이다. GBuffer는 이 패스가 읽으므로
        // 뿌리 표시 없이도 컬링에서 살아남는다 — 그것이 3-5 컬링의 실전 확인이다.
        true);
}

void EnhancedDeferredPass::Shutdown()
{
    m_pso = {};
    m_iblIrradiance = {};
    m_iblPrefiltered = {};
    m_iblBrdfLut = {};
    m_iblPrefilterMips = 1;
}

#endif
