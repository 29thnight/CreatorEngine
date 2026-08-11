#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedPostChainPass.h"
#include "EnhancedPostChainShaders.h"
#include "DX12DeviceResources.h"
#include "DX12PSOManager.h"
#include "DX12RootSignatureCache.h"
#include "EnhancedRenderGraph.h"
#include "../RHIEncoder.h"

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>
#include "DX12ShaderCompiler.h"

// 단계(순서대로 채운다):
//   [v] 1. 블룸 체인 + Uber + FXAA + 자가 검증
//   [v] 2. 실제 씬 연결 + 기존 체인과 시간 비교
//
// 자가 검증(dx12.post, 256x256) 실측:
//   밝은 곳 0.957(ACES(3.0)=0.954에 비네트) · 번짐 0.373 · 배경 0.216 ·
//   구석 0.000 · FXAA 계단 이웃 차이 45.1% 감소 · 블룸 5단
//
// 톤매퍼 대조 — 밝고 채도 높은 빨강(4,0,0)에서:
//   ACES (0.608 0.000 0.000) 채도 1.000  ← 채널별 곡선이라 R만 포화
//   AgX  (0.624 0.341 0.341) 채도 0.453  ← 채널을 섞어 흰색으로 수렴
//   두 톤매퍼가 다른 픽셀 60612/65536

namespace
{
    // 유니티 빌드에서 익명 네임스페이스가 합쳐지므로 이름을 고유하게 둔다.
    std::string PostHrToString(HRESULT hr)
    {
        std::ostringstream oss;
        oss << "HRESULT 0x" << std::hex << static_cast<unsigned long>(hr);
        return oss.str();
    }

    struct PostParams
    {
        uint32_t srcWidth{ 0 };
        uint32_t srcHeight{ 0 };
        uint32_t dstWidth{ 0 };
        uint32_t dstHeight{ 0 };

        float bloomThreshold{ 0.f };
        float bloomKnee{ 0.f };
        float bloomIntensity{ 0.f };
        float exposure{ 0.f };

        float vignetteRadius{ 0.f };
        float vignetteSoftness{ 0.f };
        float saturation{ 0.f };
        float contrast{ 0.f };

        float    fxaaBias{ 0.f };
        float    fxaaBiasMin{ 0.f };
        float    fxaaSpanMax{ 0.f };
        uint32_t flags{ 0 };

        // 뒤에 붙인 이유: 중간에 끼우면 HLSL cbuffer의 레지스터 배치가
        // 전부 밀린다. 추가는 항상 꼬리에, 16바이트 정렬을 채워서.
        float vignetteIntensity{ 0.f };
        float padding0{ 0.f };
        float padding1{ 0.f };
        float padding2{ 0.f };
    };

    constexpr uint32_t kFlagBloom = 1u;
    constexpr uint32_t kFlagToneMap = 2u;
    constexpr uint32_t kFlagVignette = 4u;
    constexpr uint32_t kFlagGrading = 8u;
    constexpr uint32_t kFlagAgX = 16u;

    bool CompilePostShader(const char* file,
        RHIShaderBlob& outBlob, std::string& outError)
    {
        // 공통 조각은 셰이더가 #include "PostChainCommon.hlsli" 로 직접 당긴다.
        return DX12ShaderCompiler::CompileFile(file, "CSMain", "cs_5_0", outBlob, outError);
    }
}

bool EnhancedPostChainPass::Initialize(const EnhancedFrameContext& context, std::string& outError)
{
    if (nullptr == context.resources || nullptr == context.psoManager ||
        nullptr == context.rootSignatures)
    {
        outError = "포스트 체인 컨텍스트가 불완전하다";
        return false;
    }

    return CreatePipelines(context, outError);
}

bool EnhancedPostChainPass::CreatePipelines(const EnhancedFrameContext& context,
    std::string& outError)
{
    // 다섯 셰이더가 같은 시그니처를 쓴다. SRV 둘·UAV 하나면 전부 담긴다 —
    // 시그니처를 나누면 패스마다 그것을 바꾸는 비용이 더 든다.
    const RHIPipelineLayoutParam params[] = {
        RHILayout::Cbv(0),
        RHILayout::SrvTable(2, 0),
        RHILayout::UavTable(1, 0),
    };

    // 선형 샘플러. FXAA가 픽셀 사이(1/3·2/3 지점)를 읽어야 해서 필요하다 —
    // 정수 Load로는 그 소수 오프셋이 잘려 나가 FXAA가 아무 일도 안 한다.
    const RHIStaticSamplerDesc samplers[] = {
        { RHISampler::Linear(RHIAddressMode::Clamp), 0, RHIShaderVisibility::All },
    };

    RHIPipelineLayoutDesc rootDesc{};
    rootDesc.params = params;
    rootDesc.staticSamplers = samplers;

    const auto root = context.rootSignatures->GetOrCreate(rootDesc, outError);
    if (!root.IsValid()) return false;

    struct Stage
    {
        const char*           file;
        RHIPipelineHandle* target;
    };
    const Stage stages[] = {
        { PostChainShaders::kThresholdFile,  &m_thresholdPSO },
        { PostChainShaders::kDownsampleFile, &m_downsamplePSO },
        { PostChainShaders::kUpsampleFile,   &m_upsamplePSO },
        { PostChainShaders::kUberFile,       &m_uberPSO },
        { PostChainShaders::kFxaaFile,       &m_fxaaPSO },
    };

    for (const Stage& stage : stages)
    {
        RHIShaderBlob blob;
        if (!CompilePostShader(stage.file, blob, outError)) return false;

        RHIComputePipelineDesc desc{};
        desc.csBytecode = blob.Data();
        desc.csSize = blob.Size();
        desc.layout = root;

        *stage.target = context.psoManager->GetOrCreateCompute(desc, outError);
        if (!stage.target->IsValid()) return false;
    }

    return true;
}

bool EnhancedPostChainPass::PrepareFrame(const EnhancedFrameContext& context,
    std::string& outError)
{
    (void)outError;

    m_width = context.width;
    m_height = context.height;

    // 블룸 단수는 화면 크기가 정한다. 8픽셀보다 작아지면 멈춘다 —
    // 그보다 작은 밉은 스레드 그룹 하나도 못 채우면서 디스패치만 는다.
    m_bloomMipCount = 0;
    uint32_t w = m_width / kBloomStartDivisor;
    uint32_t h = m_height / kBloomStartDivisor;
    while (m_bloomMipCount < kMaxBloomMips && w >= 8 && h >= 8)
    {
        ++m_bloomMipCount;
        w /= 2;
        h /= 2;
    }
    return true;
}

void EnhancedPostChainPass::Declare(EnhancedRenderGraph& graph,
    const EnhancedFrameContext& context)
{
    m_output = RGHandle{};
    m_toneMapped = RGHandle{};
    m_bloomChainValid = false;
    m_bloomMips = {};

    if (!m_inputs.color.IsValid() || !m_uberPSO.IsValid() || !m_fxaaPSO.IsValid() ||
        0 == m_width || 0 == m_height)
    {
        return;
    }

    // 상수를 채우는 곳을 하나로 둔다. 패스마다 따로 채우면 크기가 갈릴
    // 자리가 늘고, 그 어긋남은 '조금 흐리다'로만 드러난다.
    const auto makeParams = [this](uint32_t srcW, uint32_t srcH,
        uint32_t dstW, uint32_t dstH) -> PostParams
    {
        PostParams params{};
        params.srcWidth = srcW;
        params.srcHeight = srcH;
        params.dstWidth = dstW;
        params.dstHeight = dstH;
        params.bloomThreshold = m_tuning.bloomThreshold;
        params.bloomKnee = m_tuning.bloomKnee;
        params.bloomIntensity = m_tuning.bloomIntensity;
        params.exposure = m_tuning.exposure;
        params.vignetteRadius = m_tuning.vignetteRadius;
        params.vignetteSoftness = m_tuning.vignetteSoftness;
        params.vignetteIntensity = m_tuning.vignetteIntensity;
        params.saturation = m_tuning.saturation;
        params.contrast = m_tuning.contrast;
        params.fxaaBias = m_tuning.fxaaBias;
        params.fxaaBiasMin = m_tuning.fxaaBiasMin;
        params.fxaaSpanMax = m_tuning.fxaaSpanMax;
        return params;
    };

    // 컴퓨트 한 단계를 선언한다. 다섯 패스가 배선이 같아서(상수 하나 ·
    // SRV 둘 · UAV 하나) 함수로 뺐다 — 같은 배선을 다섯 번 적으면
    // 한 곳만 고치고 나머지를 잊는 부류의 버그가 생긴다.
    const auto declareStage = [this, &graph, &context](
        const char* name, RHIPipelineHandle pso,
        RGHandle srcA, RGHandle srcB, RGHandle dst,
        const PostParams& params, uint32_t dispatchW, uint32_t dispatchH,
        bool accumulate)
    {
        std::vector<EnhancedRenderGraph::RGPassUsage> usages;
        usages.push_back({ srcA, RHIResourceState::ShaderResource });
        if (srcB.IsValid() && srcB.index != srcA.index)
        {
            usages.push_back({ srcB, RHIResourceState::ShaderResource });
        }
        usages.push_back({ dst, RHIResourceState::UnorderedAccess });

        graph.AddPass(name, usages,
            [this, &context, pso, srcA, srcB, dst, params, dispatchW, dispatchH]
            (const EnhancedRenderGraph::ExecuteContext& executeContext)
            {
                // device를 더 들지 않는다 — 뷰 생성이 CreateBindings로 넘어가면서
                // 이 패스가 디바이스에 닿을 이유가 사라졌다(R2가 노리는 것이 이것이다).

                const auto cb = context.resources->UploadConstants(
                    &params, sizeof(PostParams));
                if (!cb.IsValid()) return;
                // ★ SRV는 늘 둘을 자른다.
                //
                // 두 번째를 안 쓰는 단계에서도 같은 수를 잡고 첫 번째로
                // 채운다. 조건에 따라 슬롯 수가 바뀌면 레지스터가 밀리는데,
                // SSGI에서 그것으로 누적이 조용히 죽은 적이 있다.
                const RHITextureHandle resourceA = executeContext.ResolveHandle(srcA);
                const RHITextureHandle resourceB = executeContext.ResolveHandle(srcB.IsValid() ? srcB : srcA);
                const RHITextureHandle dstResource = executeContext.ResolveHandle(dst);
                if (!resourceA.IsValid() || !resourceB.IsValid() || !dstResource.IsValid()) return;

                // 포맷은 리소스가 안다. 핸들만 있으므로 서비스에 물어본다.
                //
                // ★ 5c-1 에서 되묻는 길이 중립이 됐다 — 예전에는 포인터로
                //   풀어 `GetDesc()` 를 읽었다. (V4 에서 사라진다고 적어
                //   두었으나 사라진 것은 왕복이 아니라 **DX12 왕복**이다.)
                const auto formatOf = [&](RHITextureHandle h)
                { return context.resources->DescribeTexture(h).format; };

                // 포맷을 리소스에서 그대로 읽어 명시한다. 이 패스는 밉 하나짜리
                // 2D만 다루므로 Default(nullptr 설명)로도 같지만, 원래 코드가
                // 명시하던 것을 그대로 옮긴다 — 기준선은 지금 그림이다.
                const RHIBindingDesc srvBindings[] = {
                    RHIBindingDesc::Srv2D(resourceA, formatOf(resourceA)),
                    RHIBindingDesc::Srv2D(resourceB, formatOf(resourceB)),
                };
                const RHIBindingDesc uavBindings[] = {
                    RHIBindingDesc::Uav2D(dstResource, formatOf(dstResource)),
                };

                const RHIBindingTable srvTable = context.resources->CreateBindings(srvBindings);
                const RHIBindingTable uavTable = context.resources->CreateBindings(uavBindings);
                if (!srvTable.IsValid() || !uavTable.IsValid()) return;

                RHIEncoder& encoder = *executeContext.encoder;
                // 컴퓨트 바인드 포인트(R3). DX12는 그래픽스와 컴퓨트의 루트
                // 상태가 완전히 별개라 슬롯 번호만으로는 어디에 거는지 정해지지
                // 않는다 — 인코더가 그것을 인자로 받는 이유다.
                encoder.SetPipeline(RHIBindPoint::Compute, pso);
                encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, cb);
                encoder.SetBindings(RHIBindPoint::Compute, 1, srvTable);
                encoder.SetBindings(RHIBindPoint::Compute, 2, uavTable);

                encoder.Dispatch((dispatchW + 7) / 8, (dispatchH + 7) / 8, 1);
            });
        (void)accumulate;
    };

    // ── 블룸 체인 ──
    RGHandle bloomResult;
    if (m_tuning.bloomEnabled && m_bloomMipCount > 0)
    {
        uint32_t mipW = m_width / kBloomStartDivisor;
        uint32_t mipH = m_height / kBloomStartDivisor;

        std::vector<uint32_t> widths;
        std::vector<uint32_t> heights;

        for (uint32_t i = 0; i < m_bloomMipCount; ++i)
        {
            RGTextureDesc desc{};
            desc.width = mipW;
            desc.height = mipH;
            desc.format = kHDRFormat;
            desc.allowUnorderedAccess = true;
            desc.name = "PostChain.Bloom";
            m_bloomMips[i] = graph.CreateTexture(desc);

            widths.push_back(mipW);
            heights.push_back(mipH);
            mipW /= 2;
            mipH /= 2;
        }

        // 임계: 원본 → 밉 0
        declareStage("PostChain.BloomThreshold", m_thresholdPSO,
            m_inputs.color, RGHandle{}, m_bloomMips[0],
            makeParams(m_width, m_height, widths[0], heights[0]),
            widths[0], heights[0], false);

        // 다운샘플: 밉 i → 밉 i+1
        for (uint32_t i = 0; i + 1 < m_bloomMipCount; ++i)
        {
            declareStage("PostChain.BloomDown", m_downsamplePSO,
                m_bloomMips[i], RGHandle{}, m_bloomMips[i + 1],
                makeParams(widths[i], heights[i], widths[i + 1], heights[i + 1]),
                widths[i + 1], heights[i + 1], false);
        }

        // 업샘플: 거친 것에서 고운 것으로 더해 올린다.
        for (uint32_t i = m_bloomMipCount - 1; i > 0; --i)
        {
            declareStage("PostChain.BloomUp", m_upsamplePSO,
                m_bloomMips[i], RGHandle{}, m_bloomMips[i - 1],
                makeParams(widths[i], heights[i], widths[i - 1], heights[i - 1]),
                widths[i - 1], heights[i - 1], true);
        }

        bloomResult = m_bloomMips[0];
        m_bloomChainValid = true;
    }

    // ── Uber ──
    RGTextureDesc ldrDesc{};
    ldrDesc.width = m_width;
    ldrDesc.height = m_height;
    ldrDesc.format = kLDRFormat;
    ldrDesc.allowUnorderedAccess = true;
    // ★ 렌더 타깃 자격도 준다.
    //
    //   체인 자체는 컴퓨트로 쓰므로 UAV만 있으면 되지만, 이 결과가 최종
    //   그림이라 그 위에 무언가 더 그리는 소비자가 생긴다 — 상시 러너의
    //   기즈모 체인이 그렇다. 플래그가 없으면 RTV 생성 자체가 불법이라
    //   커맨드 리스트가 그 자리에서 무효가 되고, 증상은 Close 실패
    //   (E_INVALIDARG)에 이은 디바이스 제거다(실측으로 겪었다 — 포맷을
    //   맞춰도 용도가 없으면 같은 결과다).
    ldrDesc.allowRenderTarget = true;
    ldrDesc.name = "PostChain.ToneMapped";
    m_toneMapped = graph.CreateTexture(ldrDesc);

    {
        // 블룸 크기를 src에 담는다 — Uber가 그 크기로 블룸을 읽는다.
        const uint32_t bloomW = m_bloomChainValid
            ? (m_width / kBloomStartDivisor) : m_width;
        const uint32_t bloomH = m_bloomChainValid
            ? (m_height / kBloomStartDivisor) : m_height;

        uint32_t toneFlags = 0;
        if (m_tuning.toneMapEnabled) toneFlags |= kFlagToneMap;
        if (EnhancedPostChainPass::ToneMapper::AgX == m_tuning.toneMapper)
        {
            toneFlags |= kFlagAgX;
        }

        if (!m_useSeparatePasses)
        {
            PostParams params = makeParams(bloomW, bloomH, m_width, m_height);
            if (m_bloomChainValid)        params.flags |= kFlagBloom;
            params.flags |= toneFlags;
            if (m_tuning.vignetteEnabled) params.flags |= kFlagVignette;
            if (m_tuning.gradingEnabled)  params.flags |= kFlagGrading;

            declareStage("PostChain.Uber", m_uberPSO,
                m_inputs.color, m_bloomChainValid ? bloomResult : m_inputs.color,
                m_toneMapped, params, m_width, m_height, false);
        }
        else
        {
            // ── 참조 경로: 넷을 나눠 돈다 ──
            //
            // 옛 체인의 구조를 그대로 흉내 낸다. 셰이더는 같은 것을 쓰고
            // 플래그만 하나씩 켠다 — 그래야 갈리는 것이 화면 왕복 횟수
            // 하나뿐이고, 나온 수가 무엇을 뜻하는지 분명해진다.
            //
            // 톤맵 앞은 HDR, 뒤는 LDR이다. 뒤까지 HDR로 두면 참조 경로가
            // 대역폭에서 네 배 불리해져 비교가 공정하지 않다.
            RGTextureDesc hdrDesc{};
            hdrDesc.width = m_width;
            hdrDesc.height = m_height;
            hdrDesc.format = kHDRFormat;
            hdrDesc.allowUnorderedAccess = true;
            hdrDesc.name = "PostChain.RefBloomed";
            const RGHandle bloomed = graph.CreateTexture(hdrDesc);

            RGTextureDesc stepDesc = ldrDesc;
            stepDesc.name = "PostChain.RefToned";
            const RGHandle toned = graph.CreateTexture(stepDesc);
            stepDesc.name = "PostChain.RefVignetted";
            const RGHandle vignetted = graph.CreateTexture(stepDesc);

            // ① 블룸 합성 (HDR → HDR)
            PostParams bloomParams = makeParams(bloomW, bloomH, m_width, m_height);
            if (m_bloomChainValid) bloomParams.flags |= kFlagBloom;
            declareStage("PostChain.RefBloom", m_uberPSO,
                m_inputs.color, m_bloomChainValid ? bloomResult : m_inputs.color,
                bloomed, bloomParams, m_width, m_height, false);

            // ② 톤맵 (HDR → LDR)
            PostParams toneParams = makeParams(m_width, m_height, m_width, m_height);
            toneParams.flags = toneFlags;
            declareStage("PostChain.RefToneMap", m_uberPSO,
                bloomed, RGHandle{}, toned, toneParams, m_width, m_height, false);

            // ③ 비네트 (LDR → LDR)
            PostParams vignetteParams = makeParams(m_width, m_height, m_width, m_height);
            if (m_tuning.vignetteEnabled) vignetteParams.flags |= kFlagVignette;
            declareStage("PostChain.RefVignette", m_uberPSO,
                toned, RGHandle{}, vignetted, vignetteParams, m_width, m_height, false);

            // ④ 그레이딩 (LDR → LDR)
            PostParams gradeParams = makeParams(m_width, m_height, m_width, m_height);
            if (m_tuning.gradingEnabled) gradeParams.flags |= kFlagGrading;
            declareStage("PostChain.RefGrading", m_uberPSO,
                vignetted, RGHandle{}, m_toneMapped, gradeParams,
                m_width, m_height, false);
        }
    }

    // ── FXAA ──
    //
    // 톤맵 뒤(LDR)다. 끄면 Uber 결과가 그대로 최종이 된다.
    if (!m_tuning.fxaaEnabled)
    {
        m_output = m_toneMapped;
        return;
    }

    RGTextureDesc aaDesc = ldrDesc;
    aaDesc.name = "PostChain.Output";
    m_output = graph.CreateTexture(aaDesc);

    declareStage("PostChain.FXAA", m_fxaaPSO,
        m_toneMapped, RGHandle{}, m_output,
        makeParams(m_width, m_height, m_width, m_height),
        m_width, m_height, false);
}

void EnhancedPostChainPass::Shutdown()
{
    m_width = 0;
    m_height = 0;
    m_bloomMipCount = 0;
    m_bloomChainValid = false;

    m_useSeparatePasses = false;
    m_thresholdPSO = {};
    m_downsamplePSO = {};
    m_upsamplePSO = {};
    m_uberPSO = {};
    m_fxaaPSO = {};
}

#endif
