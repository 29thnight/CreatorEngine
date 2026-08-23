#include "EnhancedSSGIPass.h"
#include "../../../RHI/DX12/DX12DeviceResources.h"
#include "../../../RHI/DX12/DX12PSOManager.h"
#include "../../../RHI/DX12/DX12RootSignatureCache.h"
#include "../../Graph/EnhancedRenderGraph.h"
#include "../../../RHI/RHIEncoder.h"
#include "../../Scene/EnhancedSceneRenderer.h"
#include "EnhancedSSGIShaders.h"

#include <algorithm>
#include <sstream>
#include "../../../RHI/RHIShaderCompiler.h"

// 남은 단계(순서대로 채운다):
//   [v] 1. Hi-Z 피라미드 빌드 — 깊이 밉을 min으로 줄여 간다
//   [v] 2. 행진(트레이스) — Hi-Z를 타고 1/2 해상도로
//   [v] 3. 리졸브 — 지난 프레임을 재투영해 누적
//   [v] 4. 필터 — bilateral 한 번
//   [v] 5. 합성 — 업샘플 + 라이팅에 더하기
//
// 다섯 단계가 다 돈다(dx12.ssgi: 선언 14 · 실행 14 · 배리어 26).
//
// 아직 남은 것:
//   - 실제 씬에 붙이지 않았다. 검증은 합성 깊이로 도는 것만 확인했고,
//     GBuffer·라이팅을 물려 그림이 맞는지는 보지 않았다.
//   - 기존 DX11 SSGI와 시간을 대조하지 않았다. '아낀다'는 아직 추정이다.
//   - 상수 셋(누적 허용 깊이차 0.01, 필터 시그마 0.01·노멀 지수 16,
//     추적 거리 8·두께 0.5)이 전부 눈대중이다. 실측으로 조여야 한다.

namespace
{
    // 유니티 빌드에서 익명 네임스페이스가 파일 간 합쳐지므로 이름을 고유하게 둔다.
    std::string SsgiHrToString(HRESULT hr)
    {
        std::ostringstream oss;
        oss << "HRESULT 0x" << std::hex << static_cast<unsigned long>(hr);
        return oss.str();
    }

    // ── Hi-Z 빌드 ──
    //
    // 깊이 한 밉에서 다음 밉을 만든다. 2x2를 min으로 줄인다 — 화면 공간
    // 행진에서 '이 사각형 안에 가장 가까운 표면이 어디인가'를 물으므로
    // 최솟값이라야 건너뛰어도 안전하다. 평균이나 최댓값으로 줄이면 실제로는
    // 막혀 있는 구간을 비어 있다고 판단해 광선이 물체를 통과한다.
    //
    // 홀수 크기에서 한 텍셀이 빠지는 것을 막으려고 오른쪽·아래를 한 번 더
    // 본다. 빠뜨리면 그 줄만 낡은 값이 남고, 증상이 '가끔 광선이 샌다'라서
    // 찾기 어렵다.
    constexpr const char* kHiZBuildShaderFile = "SsgiHiZBuild.hlsl";

    // ── 트레이스 ──
    //
    // 화면 공간 행진을 Hi-Z 위에서 한다. 기존 방식과 다른 점이 여기다:
    // 균등 스텝은 빈 공간도 물체가 빽빽한 곳과 같은 값을 치르는데, 화면
    // 공간에서 그 빈 구간이 대부분이다. Hi-Z는 거친 밉에서 '이 큰 사각형이
    // 통째로 비었나'를 한 번에 물어 건너뛴다.
    //
    // 알고리즘:
    //   거친 밉에서 시작 → 셀 경계까지 전진 → 그 셀의 최소 깊이보다
    //   광선이 앞이면 비었다는 뜻이니 밉을 올려 더 크게 건넌다.
    //   광선이 뒤로 가면 무언가 있다는 뜻이니 밉을 내려 정밀하게 본다.
    //   밉 0에서 교차하면 그것이 히트다.
    //
    // 프레임당 슬라이스는 적게(kSlicesPerFrame) 쓰고 프레임마다 방향을
    // 돌린다. 나머지는 시간축이 맡는다 — 그것이 이 설계의 전제이고,
    // 리졸브(3단계)가 붙기 전까지는 노이즈가 그대로 보인다.
    constexpr const char* kTraceShaderFile = "SsgiTrace.hlsl";

    struct ResolveParams
    {
        Mathf::Matrix inverseProjection{};
        Mathf::Matrix inverseView{};
        Mathf::Matrix previousViewProjection{};
        uint32_t      width{ 0 };
        uint32_t      height{ 0 };
        uint32_t      hasHistory{ 0 };
        uint32_t      maxAccum{ 0 };
        float         depthTolerance{ 0.f };
        float         pad0{ 0.f };
        float         pad1[2]{};
    };

    struct FilterParams
    {
        uint32_t width{ 0 };
        uint32_t height{ 0 };
        float    depthSigma{ 0.f };
        float    normalPower{ 0.f };
    };

    struct CompositeParams
    {
        uint32_t outputWidth{ 0 };
        uint32_t outputHeight{ 0 };
        uint32_t giWidth{ 0 };
        uint32_t giHeight{ 0 };
        float    intensity{ 0.f };
        float    depthSigma{ 0.f };

        // 0이면 AO 입력이 없다는 뜻이다. 슬롯 수를 조건부로 바꾸는 대신
        // 값으로 구분한다 — 슬롯이 밀리면 레지스터가 어긋나고, 그 증상은
        // '결과가 조용히 이상해진다'라서 잡기 어렵다.
        uint32_t aoWidth{ 0 };
        uint32_t aoHeight{ 0 };
    };

    struct HiZParams
    {
        uint32_t targetWidth{ 0 };
        uint32_t targetHeight{ 0 };
        uint32_t sourceWidth{ 0 };
        uint32_t sourceHeight{ 0 };
    };

    struct TraceParams
    {
        Mathf::Matrix inverseProjection{};
        Mathf::Matrix projection{};
        uint32_t      outputWidth{ 0 };
        uint32_t      outputHeight{ 0 };
        uint32_t      depthWidth{ 0 };
        uint32_t      depthHeight{ 0 };
        uint32_t      mipCount{ 0 };
        uint32_t      frameIndex{ 0 };
        uint32_t      lightingWidth{ 0 };
        uint32_t      lightingHeight{ 0 };
        uint32_t      normalWidth{ 0 };
        uint32_t      normalHeight{ 0 };
        float         maxDistance{ 0.f };
        float         thickness{ 0.f };
    };

    bool CompileSsgiShader(const char* file, const RHIShaderDefine* defines,
        RHIShaderBlob& outBlob, std::string& outError)
    {
        return RHIShaderCompiler::CompileFile(file, "CSMain", "cs_5_0", defines,
            outBlob, outError);
    }
}

bool EnhancedSSGIPass::Initialize(const EnhancedFrameContext& context, std::string& outError)
{
    if (nullptr == context.resources || nullptr == context.psoManager ||
        nullptr == context.rootSignatures)
    {
        outError = "SSGI 패스 컨텍스트가 불완전하다";
        return false;
    }

    return CreatePipelines(context, outError);
}

bool EnhancedSSGIPass::CreatePipelines(const EnhancedFrameContext& context, std::string& outError)
{
    // ── 루트 시그니처 ──
    //
    // 두 패스가 같은 것을 쓴다. Hi-Z 빌드는 SRV 1·UAV 1만 쓰지만, 트레이스가
    // 요구하는 넓은 테이블에 얹어도 비용이 없다(안 쓰는 슬롯은 바인딩만 안
    // 하면 된다). 시그니처를 둘로 나누면 캐시에 둘이 남고, 패스 사이에서
    // 루트 시그니처를 바꾸는 비용이 더 든다.
    // 샘플러는 두지 않는다. 두 셰이더 모두 Load로 읽으므로 필요가 없고,
    // 안 쓰는 루트 파라미터는 바인딩을 잊었을 때 조용히 통과하는 자리가 된다.
    const RHIPipelineLayoutParam params[] = {
        RHILayout::Cbv(0),
        RHILayout::SrvTable(kMaxHiZMips + 2, 0),   // Hi-Z 밉들 + 노멀 + 라이팅
        RHILayout::UavTable(1, 0),
    };

    RHIPipelineLayoutDesc rootDesc{};
    rootDesc.params = params;

    const auto root = context.rootSignatures->GetOrCreate(rootDesc, outError);
    if (!root.IsValid()) return false;

    // ── Hi-Z 빌드 PSO ──
    RHIShaderBlob hiZBlob;
    if (!CompileSsgiShader(kHiZBuildShaderFile, nullptr, hiZBlob, outError)) return false;

    RHIComputePipelineDesc hiZDesc{};
    hiZDesc.csBytecode = hiZBlob.Data();
    hiZDesc.csSize = hiZBlob.Size();
    hiZDesc.layout = root;

    m_hiZBuildPSO = context.psoManager->GetOrCreateCompute(hiZDesc, outError);
    if (!m_hiZBuildPSO.IsValid()) return false;

    // ── 트레이스 PSO ──
    //
    // 슬라이스 수를 매크로로 넘긴다. 상수 버퍼로 넘기면 루프가 동적이 되고,
    // 그러면 컴파일러가 펼치지 못해 레지스터 압박이 늘어난다.
    const std::string slices = std::to_string(kSlicesPerFrame);
    const RHIShaderDefine traceDefines[] = {
        { "SLICES_PER_FRAME", slices.c_str() },
        { nullptr, nullptr }
    };

    RHIShaderBlob traceBlob;
    if (!CompileSsgiShader(kTraceShaderFile, traceDefines, traceBlob, outError)) return false;

    RHIComputePipelineDesc traceDesc{};
    traceDesc.csBytecode = traceBlob.Data();
    traceDesc.csSize = traceBlob.Size();
    traceDesc.layout = root;

    m_tracePSO = context.psoManager->GetOrCreateCompute(traceDesc, outError);
    if (!m_tracePSO.IsValid()) return false;

    // ── 리졸브·필터·합성 PSO ──
    //
    // 셋 다 같은 루트 시그니처를 쓴다. 요구하는 SRV 수가 다르지만 넓은
    // 테이블에 얹어도 비용이 없고, 시그니처를 나누면 패스 사이에서 바꾸는
    // 비용이 더 든다.
    struct StagePSO
    {
        const char*           source;
        RHIPipelineHandle* target;
    };

    const StagePSO stages[] = {
        { SsgiShaders::kResolveFile,   &m_resolvePSO },
        { SsgiShaders::kFilterFile,    &m_filterPSO },
        { SsgiShaders::kCompositeFile, &m_compositePSO },
    };

    for (const StagePSO& stage : stages)
    {
        RHIShaderBlob blob;
        if (!CompileSsgiShader(stage.source, nullptr, blob, outError)) return false;

        RHIComputePipelineDesc desc{};
        desc.csBytecode = blob.Data();
        desc.csSize = blob.Size();
        desc.layout = root;

        *stage.target = context.psoManager->GetOrCreateCompute(desc, outError);
        if (!stage.target->IsValid()) return false;
    }

    return true;
}

bool EnhancedSSGIPass::EnsureHistory(const EnhancedFrameContext& context,
    std::string& outError)
{
    // 크기가 그대로면 다시 만들지 않는다. 매 프레임 만들면 그것만으로
    // 프레임 예산을 먹고, 히스토리가 매번 비어 누적이 성립하지 않는다.
    if (m_history[0].IsValid())
    {
        const RHITextureInfo existing = context.resources->DescribeTexture(m_history[0]);
        if (existing.width == m_giWidth && existing.height == m_giHeight) return true;
    }

    // 크기가 바뀌었다 — 히스토리를 버린다. 낡은 크기의 값을 새 크기에
    // 섞으면 화면이 어긋난 채로 번진다.
    m_historyValid = false;

    // 새 리소스는 아래 initialState로 만들어진다. 상태 멤버가 옛 리소스의
    // 끝 상태를 들고 있으면 다음 Import의 첫 배리어가 틀린 before로 나간다.
    m_historyState.fill(RHIResourceState::ShaderResource);
    m_historyDepthState.fill(RHIResourceState::ShaderResource);

    RHITextureDesc desc{};
    desc.width = m_giWidth;
    desc.height = m_giHeight;
    desc.allowUnorderedAccess = true;

    // 히스토리는 만들자마자 다음 프레임의 셰이더가 읽는다 — Common에서
    // 출발시키면 첫 읽기 앞에 전이가 하나 더 붙는다.
    //
    // ★ 위 m_historyState와 같은 값이어야 한다(예전엔 NON_PIXEL만 쓰다가
    //   히스토리가 그래프에 들어오면서 R4-2b에서 맞췄다). 그래프가 아는 상태와
    //   실제 상태가 어긋나면 첫 배리어의 before가 틀린다.
    //
    //   A-2 전에는 이 줄이 D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE였고,
    //   그것이 위의 ShaderResource와 같은 값이라는 것을 주석으로 적어 둬야
    //   했다. 이제 같은 어휘라 두 줄을 눈으로 대조할 수 있다.
    desc.initialState = RHIResourceState::ShaderResource;

    for (uint32_t i = 0; i < kHistoryCount; ++i)
    {
        desc.format = kGIFormat;
        desc.debugName = (0 == i) ? L"SSGI.History0" : L"SSGI.History1";
        if (!context.resources->CreateTexture(desc, m_history[i], outError))
        {
            outError = "SSGI 히스토리 — " + outError;
            return false;
        }

        desc.format = kHiZFormat;
        desc.debugName = (0 == i) ? L"SSGI.HistoryDepth0" : L"SSGI.HistoryDepth1";
        if (!context.resources->CreateTexture(desc, m_historyDepth[i], outError))
        {
            outError = "SSGI 히스토리 깊이 — " + outError;
            return false;
        }
    }

    return true;
}

bool EnhancedSSGIPass::PrepareFrame(const EnhancedFrameContext& context, std::string& outError)
{
    (void)outError;

    // GI 해상도는 화면의 1/2. 0이 되지 않게 하한을 둔다 — 창을 아주 작게
    // 줄이면 Dispatch가 0이 되고, 그러면 조용히 아무것도 안 그린다.
    m_giWidth = (std::max)(1u, context.width / kResolutionDivisor);
    m_giHeight = (std::max)(1u, context.height / kResolutionDivisor);

    // Hi-Z 밉 수. 가장 작은 변이 1이 될 때까지 반으로 줄인다.
    m_hiZMipCount = 1;
    uint32_t w = m_giWidth;
    uint32_t h = m_giHeight;
    while (w > 1 && h > 1 && m_hiZMipCount < kMaxHiZMips)
    {
        w = (std::max)(1u, w / 2);
        h = (std::max)(1u, h / 2);
        ++m_hiZMipCount;
    }

    if (!EnsureHistory(context, outError)) return false;

    // 프레임마다 노이즈를 돌리기 위한 인덱스. 시간축이 샘플 수를 대신하려면
    // 프레임마다 다른 방향을 봐야 한다.
    ++m_frameIndex;

    // 히스토리 슬롯을 번갈아 쓴다. 이번 프레임이 쓰는 것과 지난 프레임이
    // 쓴 것이 달라야 한다 — 같으면 읽으면서 쓰게 된다.
    m_historyIndex = (m_historyIndex + 1) % kHistoryCount;

    // ★ 이전 프레임 행렬은 여기서 갱신하지 않는다.
    //
    // 처음에는 PrepareFrame에서 현재 행렬을 m_previousViewProjection에
    // 넣었다. 그러면 리졸브가 '지난 프레임'이라며 이번 프레임 행렬을 쓰게
    // 되고, 재투영이 늘 제자리를 가리켜 카메라가 움직여도 히스토리를
    // 버리지 않는다 — 움직이면 번지는데 원인이 안 보인다.
    //
    // 갱신은 Declare가 끝난 뒤(이번 프레임 값을 다 쓴 뒤)에 한다.

    return true;
}

void EnhancedSSGIPass::ResetHistory()
{
    m_historyValid = false;
    m_hasPreviousFrame = false;
    m_historyIndex = 0;
    m_frameIndex = 0;
    m_lastAccumFrames = 0;
    m_lastRejectRatio = 0.f;
    m_previousViewProjection = {};
}

void EnhancedSSGIPass::Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context)
{
    if (!m_inputs.depth.IsValid() || !m_tracePSO.IsValid())
    {
        // 입력이 없으면 선언하지 않는다. 빈 패스를 넣으면 배리어와 컬링이
        // 그것을 진짜로 취급한다.
        m_output = RGHandle{};
        return;
    }

    // ── 히스토리를 그래프에 들인다 (R4-2b) ──
    //
    // 프레임을 넘겨 살아야 해서 transient가 될 수 없지만, 그래프 밖 리소스는
    // 아니다. 매 프레임 Import하면 리졸브의 읽기(ShaderResource)와
    // StoreHistory의 쓰기(CopyDest) 사이 전이를 그래프가 만들고, 끝 상태는
    // writeback이 멤버에 적어 다음 프레임 Import가 맞는 before로 시작한다.
    //
    // 둘 다 넣는다 — 이번 프레임의 읽는 쪽과 쓰는 쪽이 핑퐁으로 갈리므로
    // 한쪽만 들이면 나머지가 그래프 밖에 남아 같은 문제가 반복된다.
    for (uint32_t i = 0; i < kHistoryCount; ++i)
    {
        m_historyHandle[i] = graph.ImportTexture(m_history[i],
            m_historyState[i], "SSGI.History" + std::to_string(i), &m_historyState[i]);
        m_historyDepthHandle[i] = graph.ImportTexture(m_historyDepth[i],
            m_historyDepthState[i], "SSGI.HistoryDepth" + std::to_string(i),
            &m_historyDepthState[i]);
    }

    // ── Hi-Z 밉 체인 선언 ──
    //
    // 밉마다 별도 텍스처다. 그래프가 밉 체인을 한 리소스로 다루지 않기
    // 때문이고, 그래서 UAV도 밉마다 따로 만든다.
    for (uint32_t mip = 0; mip < m_hiZMipCount; ++mip)
    {
        RGTextureDesc desc{};
        desc.width = (std::max)(1u, m_giWidth >> mip);
        desc.height = (std::max)(1u, m_giHeight >> mip);
        desc.format = kHiZFormat;
        desc.allowUnorderedAccess = true;
        desc.name = "SSGI.HiZ." + std::to_string(mip);
        m_hiZMips[mip] = graph.CreateTexture(desc);
    }

    RGTextureDesc giDesc{};
    giDesc.width = m_giWidth;
    giDesc.height = m_giHeight;
    giDesc.format = kGIFormat;
    giDesc.allowUnorderedAccess = true;

    giDesc.name = "SSGI.Trace";
    m_traceResult = graph.CreateTexture(giDesc);

    giDesc.name = "SSGI.Resolved";
    m_resolved = graph.CreateTexture(giDesc);

    giDesc.name = "SSGI.Filtered";
    m_filtered = graph.CreateTexture(giDesc);

    // 합성은 전 해상도다. 라이팅에 간접광을 더한 결과를 뒤 패스가 읽는다.
    //
    // RTV도 허용한다. 이 텍스처가 라이브 배선의 '라이팅 결과'이고, 투명
    // (Forward+)이 그 위에 직접 알파 블렌딩으로 그린다 — UAV 전용으로 두면
    // RTV를 못 만들어 별도 타깃 + 합성 패스가 필요해진다. 플래그 하나로
    // 전 화면 패스 하나와 HDR 타깃 하나를 아낀다.
    RGTextureDesc outDesc{};
    outDesc.width = context.width;
    outDesc.height = context.height;
    outDesc.format = kGIFormat;
    outDesc.allowUnorderedAccess = true;
    outDesc.allowRenderTarget = true;
    outDesc.name = "SSGI.Output";
    m_output = graph.CreateTexture(outDesc);

    // ── 1단계: Hi-Z 빌드 ──
    //
    // 밉마다 패스를 하나씩 선언한다. 한 패스로 묶으면 밉 사이의 배리어를
    // 손으로 넣어야 하는데, 그래프에 맡기면 선언만으로 해결된다 — 각 밉이
    // 앞 밉을 SRV로 읽고 자기를 UAV로 쓴다고 적으면 된다.
    for (uint32_t mip = 0; mip < m_hiZMipCount; ++mip)
    {
        std::vector<EnhancedRenderGraph::RGPassUsage> usages;
        if (0 == mip)
        {
            usages.push_back({ m_inputs.depth, RHIResourceState::ShaderResource });
        }
        else
        {
            usages.push_back({ m_hiZMips[mip - 1], RHIResourceState::ShaderResource });
        }
        usages.push_back({ m_hiZMips[mip], RHIResourceState::UnorderedAccess });

        graph.AddPass("SSGI.HiZ." + std::to_string(mip), usages,
            [this, &context, mip](const EnhancedRenderGraph::ExecuteContext& executeContext)
            {
                const uint32_t targetWidth = (std::max)(1u, m_giWidth >> mip);
                const uint32_t targetHeight = (std::max)(1u, m_giHeight >> mip);
                const uint32_t sourceWidth = (0 == mip) ? context.width
                    : (std::max)(1u, m_giWidth >> (mip - 1));
                const uint32_t sourceHeight = (0 == mip) ? context.height
                    : (std::max)(1u, m_giHeight >> (mip - 1));

                HiZParams params{};
                params.targetWidth = targetWidth;
                params.targetHeight = targetHeight;
                params.sourceWidth = sourceWidth;
                params.sourceHeight = sourceHeight;

                const auto cb = context.resources->UploadConstants(
                    &params, sizeof(HiZParams));
                if (!cb.IsValid()) return;
                const RHITextureHandle source = (0 == mip)
                    ? executeContext.ResolveHandle(m_inputs.depth)
                    : executeContext.ResolveHandle(m_hiZMips[mip - 1]);

                // SRV 테이블은 시그니처 크기만큼 잡는다. 안 쓰는 슬롯도
                // 디스크립터가 있어야 검증 레이어가 조용하다 — 그래서 전부
                // 같은 것으로 채운다.
                std::array<RHIBindingDesc, kMaxHiZMips + 2> srvs{};
                srvs.fill((0 == mip)
                    ? RHIBindingDesc::SrvDepth(source)
                    : RHIBindingDesc::Srv2D(source, kHiZFormat));
                const RHIBindingDesc uavs[] = {
                    RHIBindingDesc::Uav2D(executeContext.ResolveHandle(m_hiZMips[mip]),
                        kHiZFormat),
                };
                const RHIBindingTable srvTable = context.resources->CreateBindings(srvs);
                const RHIBindingTable uavTable = context.resources->CreateBindings(uavs);
                if (!srvTable.IsValid() || !uavTable.IsValid()) return;

                // 힙 바인딩은 인코더가 한다(R4-1c). 이 자리에서 그것을 손으로
                // 부르다 빠뜨려 SetComputeRootDescriptorTable에서 죽은 적이
                // 있는데, 그 사연은 RHIEncoder.h에 옮겨 적었다.
                RHIEncoder& encoder = *executeContext.encoder;
                encoder.SetPipeline(RHIBindPoint::Compute, m_hiZBuildPSO);
                encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, cb);
                encoder.SetBindings(RHIBindPoint::Compute, 1, srvTable);
                encoder.SetBindings(RHIBindPoint::Compute, 2, uavTable);

                encoder.Dispatch((targetWidth + 7) / 8, (targetHeight + 7) / 8, 1);
            });
    }

    // ── 2단계: 트레이스 ──
    //
    // 뿌리로 표시하지 않는다. 뒤 단계(리졸브·합성)가 읽으면 컬링이 살리고,
    // 아직 아무도 안 읽으면 걷어내는 것이 맞다 — 결과를 안 쓰는데 도는
    // 패스가 남아 있으면 프레임 시간만 먹는다.
    {
        std::vector<EnhancedRenderGraph::RGPassUsage> usages;
        for (uint32_t mip = 0; mip < m_hiZMipCount; ++mip)
        {
            usages.push_back({ m_hiZMips[mip], RHIResourceState::ShaderResource });
        }
        if (m_inputs.normal.IsValid())
        {
            usages.push_back({ m_inputs.normal, RHIResourceState::ShaderResource });
        }
        if (m_inputs.lighting.IsValid())
        {
            usages.push_back({ m_inputs.lighting, RHIResourceState::ShaderResource });
        }
        usages.push_back({ m_traceResult, RHIResourceState::UnorderedAccess });

        graph.AddPass("SSGI.Trace", usages,
            [this, &context](const EnhancedRenderGraph::ExecuteContext& executeContext)
            {
                TraceParams params{};
                if (nullptr != context.camera)
                {
                    const Mathf::xMatrix projection = context.camera->projection;
                    params.projection = XMMatrixTranspose(projection);
                    params.inverseProjection = XMMatrixTranspose(
                        XMMatrixInverse(nullptr, projection));
                }
                params.outputWidth = m_giWidth;
                params.outputHeight = m_giHeight;
                params.depthWidth = m_giWidth;
                params.depthHeight = m_giHeight;
                params.mipCount = m_hiZMipCount;
                params.frameIndex = m_frameIndex;
                // 라이팅이 없으면 대체물(Hi-Z 0)이 꽂히므로 그 해상도를 준다.
                params.lightingWidth = m_inputs.lighting.IsValid()
                    ? context.width : m_giWidth;
                params.lightingHeight = m_inputs.lighting.IsValid()
                    ? context.height : m_giHeight;
                params.normalWidth = m_inputs.normal.IsValid()
                    ? context.width : m_giWidth;
                params.normalHeight = m_inputs.normal.IsValid()
                    ? context.height : m_giHeight;
                // 실측으로 정할 값들이다. 지금은 눈으로 볼 수 있는 범위를
                // 잡아 두고, 리졸브가 붙은 뒤 씬에 맞춰 조인다.
                params.maxDistance = m_tuning.traceDistance;
                params.thickness = m_tuning.traceThickness;

                const auto cb = context.resources->UploadConstants(
                    &params, sizeof(TraceParams));
                if (!cb.IsValid()) return;
                // ★ 없어도 디스크립터는 반드시 만든다.
                //
                // 처음에는 nullptr이면 건너뛰었다. 그러자 그 슬롯이 초기화되지
                // 않은 채 테이블에 남았고, GPU가 그것을 읽어 그 자리에서
                // 죽었다(그래프 Execute에서 크래시). 디스크립터 힙은 쓰레기
                // 메모리이지 '비어 있음'이 아니다.
                //
                // 입력이 없으면 Hi-Z 0을 꽂는다. 값은 뜻이 없지만 유효한
                // 디스크립터이고, 셰이더가 그 값을 쓰더라도 검은 결과가 나올
                // 뿐 죽지는 않는다.
                const RHITextureHandle fallback = executeContext.ResolveHandle(m_hiZMips[0]);
                constexpr RHIFormat kColorFormat = RHIFormat::RGBA16Float;

                std::array<RHIBindingDesc, kMaxHiZMips + 2> srvs{};
                for (uint32_t i = 0; i < kMaxHiZMips; ++i)
                {
                    // 밉이 모자라면 마지막 것으로 채운다. 셰이더가 gMipCount
                    // 안에서만 읽으므로 값은 안 쓰이지만, 디스크립터가 비어
                    // 있으면 검증 레이어가 잡는다.
                    const uint32_t index = (i < m_hiZMipCount) ? i : (m_hiZMipCount - 1);
                    srvs[i] = RHIBindingDesc::Srv2D(
                        executeContext.ResolveHandle(m_hiZMips[index]), kHiZFormat);
                }
                srvs[kMaxHiZMips] = m_inputs.normal.IsValid()
                    ? RHIBindingDesc::Srv2D(
                        executeContext.ResolveHandle(m_inputs.normal), kColorFormat)
                    : RHIBindingDesc::Srv2D(fallback, kHiZFormat);
                srvs[kMaxHiZMips + 1] = m_inputs.lighting.IsValid()
                    ? RHIBindingDesc::Srv2D(
                        executeContext.ResolveHandle(m_inputs.lighting), kColorFormat)
                    : RHIBindingDesc::Srv2D(fallback, kHiZFormat);

                const RHIBindingDesc uavs[] = {
                    RHIBindingDesc::Uav2D(executeContext.ResolveHandle(m_traceResult), kGIFormat),
                };
                const RHIBindingTable srvTable = context.resources->CreateBindings(srvs);
                const RHIBindingTable uavTable = context.resources->CreateBindings(uavs);
                if (!srvTable.IsValid() || !uavTable.IsValid()) return;

                RHIEncoder& encoder = *executeContext.encoder;
                encoder.SetPipeline(RHIBindPoint::Compute, m_tracePSO);
                encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, cb);
                encoder.SetBindings(RHIBindPoint::Compute, 1, srvTable);
                encoder.SetBindings(RHIBindPoint::Compute, 2, uavTable);

                encoder.Dispatch((m_giWidth + 7) / 8, (m_giHeight + 7) / 8, 1);
            });
    }

    // ── 3~5단계 공통 ──
    //
    // 세 패스가 같은 모양이라 헬퍼로 묶는다: 상수 올리기, SRV 테이블 채우기,
    // UAV 하나, Dispatch. 손으로 세 번 쓰면 한 곳만 고치고 나머지를 잊는
    // 종류의 실수가 난다 — 이 작업에서 이미 두 번 겪었다(대상 선택 계산,
    // 경계 측정).
    const auto declareStage = [this, &graph, &context](
        const std::string& name,
        const std::vector<EnhancedRenderGraph::RGPassUsage>& usages,
        RHIPipelineHandle pso,
        RGHandle target,
        const std::vector<RGHandle>& srvHandles,
        const std::vector<RHITextureHandle>& externalSrvs,
        const void* constants, size_t constantBytes,
        uint32_t dispatchWidth, uint32_t dispatchHeight,
        bool hasSideEffect)
    {
        // 상수는 값으로 복사해 둔다. 람다가 프레임 뒤에 실행되므로 호출부의
        // 지역 변수를 가리키면 그때는 이미 사라져 있다.
        std::vector<uint8_t> constantCopy(constantBytes);
        memcpy(constantCopy.data(), constants, constantBytes);

        graph.AddPass(name, usages,
            [this, &context, pso, target, srvHandles, externalSrvs, constantCopy,
             dispatchWidth, dispatchHeight]
            (const EnhancedRenderGraph::ExecuteContext& executeContext)
            {

                const auto cb = context.resources->AllocateUpload(
                    RHIUploadRequest{ constantCopy.size(),
                        RHIUploadUsage::ConstantBuffer, 1 });
                if (!cb.IsValid()) return;
                memcpy(cb.cpuAddress, constantCopy.data(), constantCopy.size());

                // 그래프 리소스와 외부 리소스를 순서대로 꽂는다.
                std::vector<RHITextureHandle> sources;
                for (const RGHandle handle : srvHandles)
                {
                    sources.push_back(executeContext.ResolveHandle(handle));
                }
                for (RHITextureHandle external : externalSrvs)
                {
                    sources.push_back(external);
                }

                // ★ 남는 슬롯도 반드시 채운다. 비워 두면 GPU가 초기화되지
                //   않은 디스크립터를 읽고 그 자리에서 죽는다 — 트레이스에서
                //   이미 겪었다.
                const RHITextureHandle fallback = sources.empty()
                    ? executeContext.ResolveHandle(m_hiZMips[0]) : sources.front();

                std::array<RHIBindingDesc, kMaxHiZMips + 2> srvs{};
                for (uint32_t i = 0; i < kMaxHiZMips + 2; ++i)
                {
                    const RHITextureHandle resource = (i < sources.size() && sources[i].IsValid())
                        ? sources[i] : fallback;
                    // 깊이면 색 포맷으로 갈아 보고, 아니면 리소스 포맷 그대로다.
                    srvs[i] = RHIBindingDesc::SrvDepth(resource);
                }

                const RHITextureHandle targetResource = executeContext.ResolveHandle(target);
                // 포맷은 리소스가 안다 — 핸들만 있으므로 서비스에 되묻는다.
                // 기준선과 같은 값이어야 하므로 UNKNOWN으로 뭉개지 않는다.
                //
                // ★ 되묻는 길이 5c-1 에서 생겼다. 예전에는 포인터로 풀어
                //   `GetDesc()` 를 읽었고, 그것이 이 패스가 인터페이스 경유로
                //   DX12 를 만지던 마지막 자리 중 하나였다.
                const RHIBindingDesc uavs[] = {
                    RHIBindingDesc::Uav2D(targetResource,
                        context.resources->DescribeTexture(targetResource).format),
                };
                const RHIBindingTable srvTable = context.resources->CreateBindings(srvs);
                const RHIBindingTable uavTable = context.resources->CreateBindings(uavs);
                if (!srvTable.IsValid() || !uavTable.IsValid()) return;

                RHIEncoder& encoder = *executeContext.encoder;
                encoder.SetPipeline(RHIBindPoint::Compute, pso);
                encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, cb);
                encoder.SetBindings(RHIBindPoint::Compute, 1, srvTable);
                encoder.SetBindings(RHIBindPoint::Compute, 2, uavTable);

                encoder.Dispatch((dispatchWidth + 7) / 8, (dispatchHeight + 7) / 8, 1);
            }, hasSideEffect);
    };

    // ── 3단계: 리졸브(재투영 + 누적) ──
    {
        ResolveParams params{};
        if (nullptr != context.camera)
        {
            const Mathf::xMatrix projection = context.camera->projection;
            params.inverseProjection = XMMatrixTranspose(
                XMMatrixInverse(nullptr, projection));
            params.inverseView = XMMatrixTranspose(
                XMMatrixInverse(nullptr, context.camera->view));
        }
        params.previousViewProjection = XMMatrixTranspose(m_previousViewProjection);
        params.width = m_giWidth;
        params.height = m_giHeight;
        params.hasHistory = (m_historyValid && m_hasPreviousFrame) ? 1u : 0u;
        params.maxAccum = kMaxAccumFrames;
        // 실측으로 조일 값이다. 너무 크면 다른 표면을 같은 것으로 보고,
        // 너무 작으면 정지 상태에서도 히스토리를 버린다.
        params.depthTolerance = m_tuning.accumDepthTolerance;

        const uint32_t readIndex = (m_historyIndex + kHistoryCount - 1) % kHistoryCount;

        std::vector<EnhancedRenderGraph::RGPassUsage> usages;
        usages.push_back({ m_traceResult, RHIResourceState::ShaderResource });
        usages.push_back({ m_hiZMips[0], RHIResourceState::ShaderResource });
        // 지난 프레임 히스토리를 읽는다. 예전에는 이 읽기가 그래프에 안
        // 보였고, 상태가 늘 셰이더 자원으로 고정돼 있어서 우연히 맞았다.
        usages.push_back({ m_historyHandle[readIndex], RHIResourceState::ShaderResource });
        usages.push_back({ m_historyDepthHandle[readIndex], RHIResourceState::ShaderResource });
        if (m_inputs.normal.IsValid())
        {
            usages.push_back({ m_inputs.normal, RHIResourceState::ShaderResource });
        }
        usages.push_back({ m_resolved, RHIResourceState::UnorderedAccess });

        // ★ 슬롯 순서가 셰이더 선언과 맞아야 한다.
        //   t0 트레이스 · t1 히스토리 · t2 히스토리깊이 · t3 깊이 · t4 노멀
        //
        // 그런데 헬퍼는 그래프 핸들을 먼저 꽂고 외부 리소스를 뒤에 꽂는다.
        // 히스토리(외부)가 t1·t2에 와야 하므로 이 순서로는 맞출 수 없다 —
        // 리졸브만 SRV를 직접 만든다면 헬퍼를 쓰는 뜻이 없어지므로,
        // 셰이더 쪽 레지스터를 헬퍼 순서에 맞춘다.
        //
        // 실제 순서: t0 트레이스 · t1 깊이 · t2 노멀 · t3 히스토리 · t4 히스토리깊이
        //
        // ★ 입력이 없어도 자리를 비우지 않는다.
        //
        // 처음에는 normal이 유효할 때만 push_back했다. 그러자 검증(노멀
        // 미설정)에서 슬롯이 한 칸씩 밀려 t3에 히스토리 깊이가, t4에 엉뚱한
        // 것이 들어갔다. 누적이 2에서 멈췄고 — 히스토리의 a를 못 읽으니
        // 늘 1로 봤다 — depthTolerance를 100까지 키워도 그대로였다.
        //
        // 조건부로 슬롯 수가 바뀌는 것 자체가 위험하다. 자리는 고정하고
        // 없는 것만 대체물로 채운다.
        std::vector<RGHandle> ordered{ m_traceResult, m_hiZMips[0] };
        ordered.push_back(m_inputs.normal.IsValid() ? m_inputs.normal : m_hiZMips[0]);

        std::vector<RHITextureHandle> orderedExternal{
            m_history[readIndex], m_historyDepth[readIndex] };

        declareStage("SSGI.Resolve", usages, m_resolvePSO, m_resolved,
            ordered, orderedExternal, &params, sizeof(params),
            m_giWidth, m_giHeight, false);
    }

    // ── 4단계: bilateral 필터 ──
    {
        FilterParams params{};
        params.width = m_giWidth;
        params.height = m_giHeight;
        params.depthSigma = m_tuning.filterDepthSigma;
        params.normalPower = m_tuning.filterNormalPower;

        std::vector<EnhancedRenderGraph::RGPassUsage> usages;
        usages.push_back({ m_resolved, RHIResourceState::ShaderResource });
        usages.push_back({ m_hiZMips[0], RHIResourceState::ShaderResource });
        if (m_inputs.normal.IsValid())
        {
            usages.push_back({ m_inputs.normal, RHIResourceState::ShaderResource });
        }
        usages.push_back({ m_filtered, RHIResourceState::UnorderedAccess });

        // 자리를 고정한다(리졸브와 같은 이유).
        std::vector<RGHandle> srvHandles{ m_resolved, m_hiZMips[0] };
        srvHandles.push_back(m_inputs.normal.IsValid() ? m_inputs.normal : m_hiZMips[0]);

        declareStage("SSGI.Filter", usages, m_filterPSO, m_filtered,
            srvHandles, {}, &params, sizeof(params),
            m_giWidth, m_giHeight, false);
    }

    // ── 5단계: 업샘플 + 합성 ──
    {
        CompositeParams params{};
        params.outputWidth = context.width;
        params.outputHeight = context.height;
        params.giWidth = m_giWidth;
        params.giHeight = m_giHeight;
        params.intensity = m_tuning.intensity;
        params.depthSigma = m_tuning.compositeDepthSigma;

        std::vector<EnhancedRenderGraph::RGPassUsage> usages;
        usages.push_back({ m_filtered, RHIResourceState::ShaderResource });
        usages.push_back({ m_hiZMips[0], RHIResourceState::ShaderResource });
        if (m_inputs.lighting.IsValid())
        {
            usages.push_back({ m_inputs.lighting, RHIResourceState::ShaderResource });
        }
        usages.push_back({ m_inputs.depth, RHIResourceState::ShaderResource });
        if (m_inputs.diffuse.IsValid())
        {
            usages.push_back({ m_inputs.diffuse, RHIResourceState::ShaderResource });
        }
        if (m_inputs.ambientOcclusion.IsValid())
        {
            usages.push_back({ m_inputs.ambientOcclusion, RHIResourceState::ShaderResource });
        }
        usages.push_back({ m_output, RHIResourceState::UnorderedAccess });

        // t0 GI · t1 GI깊이 · t2 라이팅 · t3 깊이 · t4 디퓨즈 · t5 AO
        // 자리를 고정한다(리졸브와 같은 이유).
        std::vector<RGHandle> srvHandles{ m_filtered, m_hiZMips[0] };
        srvHandles.push_back(m_inputs.lighting.IsValid() ? m_inputs.lighting : m_hiZMips[0]);
        srvHandles.push_back(m_inputs.depth);
        srvHandles.push_back(m_inputs.diffuse.IsValid() ? m_inputs.diffuse : m_hiZMips[0]);
        srvHandles.push_back(m_inputs.ambientOcclusion.IsValid()
            ? m_inputs.ambientOcclusion : m_hiZMips[0]);

        if (m_inputs.ambientOcclusion.IsValid())
        {
            params.aoWidth = (context.width + 1) / 2;
            params.aoHeight = (context.height + 1) / 2;
        }

        // 합성 결과는 그래프 밖으로 나간다 — 뿌리로 표시한다.
        declareStage("SSGI.Composite", usages, m_compositePSO, m_output,
            srvHandles, {}, &params, sizeof(params),
            context.width, context.height, true);
    }

    // ── 히스토리 갱신 ──
    //
    // 이번 프레임 결과를 다음 프레임이 읽도록 복사한다. 그래프의 transient는
    // 프레임이 끝나면 사라지므로 영속 리소스로 옮겨야 한다.
    {
        // ★ 저장하는 것은 필터 결과가 아니라 리졸브 결과다.
        //
        // 처음에는 필터 결과를 넣었다. 그러자 누적이 2에서 멈췄다(여덟
        // 프레임을 돌려도 평균 2.00). 두 가지가 겹친 탓이다:
        //   ① 필터가 float4 전체를 이웃과 가중 평균하므로 a 채널(누적
        //      프레임 수)도 함께 뭉개진다.
        //   ② 설계상으로도 틀렸다 — 필터 결과를 히스토리에 넣으면 블러가
        //      매 프레임 다시 블러되어 계속 번진다.
        //
        // 누적은 리졸브 결과를 이어가고, 필터는 화면에 낼 때만 쓴다.
        std::vector<EnhancedRenderGraph::RGPassUsage> usages;
        usages.push_back({ m_resolved, RHIResourceState::CopySource });
        usages.push_back({ m_hiZMips[0], RHIResourceState::CopySource });
        // 쓰는 쪽도 usage로 선언한다(R4-2b). 예전에는 여기서 손으로
        // SRV → COPY_DEST → SRV 전이를 걸었고, 그 앞뒤 상태를 단정하고 있었다.
        usages.push_back({ m_historyHandle[m_historyIndex], RHIResourceState::CopyDest });
        usages.push_back({ m_historyDepthHandle[m_historyIndex], RHIResourceState::CopyDest });

        const RGHandle historyTarget = m_historyHandle[m_historyIndex];
        const RGHandle historyDepthTarget = m_historyDepthHandle[m_historyIndex];

        graph.AddPass("SSGI.StoreHistory", usages,
            [this, historyTarget, historyDepthTarget]
            (const EnhancedRenderGraph::ExecuteContext& executeContext)
            {
                RHIEncoder& encoder = *executeContext.encoder;

                encoder.CopyResource(executeContext.ResolveHandle(historyTarget),
                    executeContext.ResolveHandle(m_resolved));
                encoder.CopyResource(executeContext.ResolveHandle(historyDepthTarget),
                    executeContext.ResolveHandle(m_hiZMips[0]));
            }, true);
    }

    // 이번 프레임 행렬을 다음 프레임의 '이전'으로 남긴다. Declare가 끝난
    // 뒤라야 이번 프레임 리졸브가 진짜 지난 프레임 값을 쓴다.
    if (nullptr != context.camera)
    {
        m_previousViewProjection = XMMatrixMultiply(context.camera->view,
            context.camera->projection);
        m_hasPreviousFrame = true;
    }
    m_historyValid = true;
}

void EnhancedSSGIPass::Shutdown()
{
    m_history.fill(RHITextureHandle{});
    m_historyDepth.fill(RHITextureHandle{});

    m_historyHandle.fill(RGHandle{});
    m_historyDepthHandle.fill(RGHandle{});
    m_historyState.fill(RHIResourceState::ShaderResource);
    m_historyDepthState.fill(RHIResourceState::ShaderResource);

    m_historyValid = false;
    m_hasPreviousFrame = false;
    m_hiZMipCount = 0;
    m_frameIndex = 0;

    m_hiZBuildPSO = {};
    m_tracePSO = {};
    m_resolvePSO = {};
    m_filterPSO = {};
    m_compositePSO = {};
}

