#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedSSGIPass.h"
#include "DX12DeviceResources.h"
#include "DX12PSOManager.h"
#include "DX12RootSignatureCache.h"
#include "EnhancedRenderGraph.h"
#include "RHIEncoder.h"
#include "EnhancedSceneRenderer.h"
#include "EnhancedSSGIShaders.h"

#include <algorithm>
#include <sstream>
#include "DX12ShaderCompiler.h"

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
        float         maxDistance{ 0.f };
        float         thickness{ 0.f };
    };

    bool CompileSsgiShader(const char* file, const D3D_SHADER_MACRO* defines,
        RHIShaderBlob& outBlob, std::string& outError)
    {
        return DX12ShaderCompiler::CompileFile(file, "CSMain", "cs_5_0", defines,
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
    const D3D_SHADER_MACRO traceDefines[] = {
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
        D3D12_RESOURCE_DESC existing = context.resources->Resolve(m_history[0])->GetDesc();
        if (existing.Width == m_giWidth && existing.Height == m_giHeight) return true;
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
                    constantCopy.size(), DX12UploadRing::kConstantBufferAlignment);
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
                ID3D12Resource* const targetNative = context.resources->Resolve(targetResource);
                const RHIBindingDesc uavs[] = {
                    RHIBindingDesc::Uav2D(targetResource,
                        (nullptr != targetNative) ? FromDXGI(targetNative->GetDesc().Format)
                                                  : RHIFormat::Unknown),
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

// ── 자가 검증 ──
//
// 셰이더는 런타임 컴파일이라 C++ 빌드로는 HLSL 오류가 안 잡힌다. 실제로
// 선언하지 않은 샘플러를 쓰는 코드가 빌드를 통과했다 — 부르는 곳이 없으면
// 컴파일 자체가 안 돈다. 그래서 부르는 자리를 만든다.
bool EnhancedSceneRenderer::RunSSGITest(std::string& outLog)
{
    using Microsoft::WRL::ComPtr;

    constexpr uint32_t kWidth = 256;
    constexpr uint32_t kHeight = 256;

    outLog += "── SSGI 검증 (PHASE 3-6) ──\n";

    std::string error;

    DX12DeviceResources resources;
    if (!resources.Initialize(kWidth, kHeight, error))
    {
        outLog += "[1/3] DX12 초기화 실패: " + error + "\n";
        return false;
    }

    DX12PSOManager psoManager;
    DX12RootSignatureCache rootSignatures;
    if (!psoManager.Initialize(&resources, L"dx12_ssgi.cache", error) ||
        !rootSignatures.Initialize(&resources, error))
    {
        outLog += "[1/3] 캐시 초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }

    EnhancedFrameContext frameContext{};
    frameContext.resources = &resources;
    frameContext.psoManager = &psoManager;
    frameContext.rootSignatures = &rootSignatures;
    frameContext.width = kWidth;
    frameContext.height = kHeight;

    // ★ 카메라를 넣어야 재투영이 성립한다.
    //
    // 처음에는 width/height만 넣었다. 그러자 리졸브가 이전 프레임 행렬을
    // 못 얻어 히스토리를 통째로 버렸고, 여덟 프레임을 돌려도 누적이 1에
    // 머물렀다(평균 1.00 · 최소 1 · 최대 1). 셰이더도 그래프도 멀쩡한데
    // 검증 쪽 입력이 빠져 있었던 것이다.
    //
    // 카메라를 고정해 둔다 — 정지 상태에서 누적이 쌓이는지가 질문이므로
    // 움직이면 답이 흐려진다.
    FrameCameraSnapshot camera{};
    camera.view = XMMatrixLookAtLH(
        XMVectorSet(0.f, 1.f, -3.f, 1.f),
        XMVectorSet(0.f, 0.f, 0.f, 1.f),
        XMVectorSet(0.f, 1.f, 0.f, 0.f));
    camera.projection = XMMatrixPerspectiveFovLH(
        XM_PIDIV4, static_cast<float>(kWidth) / static_cast<float>(kHeight), 0.1f, 100.f);
    camera.inverseView = XMMatrixInverse(nullptr, camera.view);
    camera.inverseProjection = XMMatrixInverse(nullptr, camera.projection);
    camera.nearPlane = 0.1f;
    camera.farPlane = 100.f;

    frameContext.camera = &camera;

    // ── [1/3] 셰이더 컴파일과 PSO 생성 ──
    EnhancedSSGIPass ssgi;
    if (!ssgi.Initialize(frameContext, error))
    {
        outLog += "[1/3] SSGI 초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }
    outLog += "[1/3] 셰이더 컴파일·PSO 생성 통과(Hi-Z·트레이스·리졸브·필터·합성)\n";

    // ── [2/3] 밉 수 산정 ──
    if (!ssgi.PrepareFrame(frameContext, error))
    {
        outLog += "[2/3] PrepareFrame 실패: " + error + "\n";
        ssgi.Shutdown();
        resources.Shutdown();
        return false;
    }

    // 256을 1/2로 줄이면 128이고, 1이 될 때까지 반이면 128→64→…→1로
    // 여덟 단계다. 상한(kMaxHiZMips)에 걸린다.
    const uint32_t expectedMips = EnhancedSSGIPass::kMaxHiZMips;
    const uint32_t giWidth = kWidth / EnhancedSSGIPass::kResolutionDivisor;

    {
        char line[192]{};
        std::snprintf(line, sizeof(line),
            "[2/3] GI 해상도 %ux%u · Hi-Z 밉 %u(기대 %u)\n",
            giWidth, kHeight / EnhancedSSGIPass::kResolutionDivisor,
            expectedMips, expectedMips);
        outLog += line;
    }

    // ── [3/3] 실제 렌더 ──
    //
    // 깊이를 그래프 밖에서 만들어 임포트한다. 여기서 확인하려는 것은
    // Hi-Z 체인과 트레이스가 도는가이지 GBuffer가 아니다.
    bool passed = true;
    {
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC depthDesc{};
        depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        depthDesc.Width = kWidth;
        depthDesc.Height = kHeight;
        depthDesc.DepthOrArraySize = 1;
        depthDesc.MipLevels = 1;
        depthDesc.Format = DXGI_FORMAT_R32_FLOAT;
        depthDesc.SampleDesc.Count = 1;
        depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        ComPtr<ID3D12Resource> depth;
        if (FAILED(resources.GetDevice()->CreateCommittedResource(&heap,
            D3D12_HEAP_FLAG_NONE, &depthDesc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&depth))))
        {
            outLog += "[3/3] 깊이 텍스처 생성 실패\n";
            ssgi.Shutdown();
            resources.Shutdown();
            return false;
        }

        // 노멀도 만든다.
        //
        // 필터가 노멀 가중을 쓰므로 없으면 그 항을 잴 수 없다. 대체물(깊이
        // 텍스처)을 꽂으면 깊이값을 노멀로 해석하게 되고, 그러면
        // filterNormalPower를 스윕해도 무엇을 재는지 알 수 없다.
        D3D12_RESOURCE_DESC normalDesc = depthDesc;
        normalDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;

        ComPtr<ID3D12Resource> testNormal;
        if (FAILED(resources.GetDevice()->CreateCommittedResource(&heap,
            D3D12_HEAP_FLAG_NONE, &normalDesc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&testNormal))))
        {
            outLog += "[3/3] 테스트 노멀 생성 실패\n";
            ssgi.Shutdown();
            resources.Shutdown();
            return false;
        }

        // ★ 깊이를 실제로 채운다.
        //
        // 만들기만 하고 두면 초기화되지 않은 메모리를 깊이로 읽는다. 그
        // 상태로는 히트 비율 같은 숫자가 뜻을 잃고, 무엇을 재는지 모르는 채
        // 상수를 조이게 된다. 바닥 평면과 구 하나를 절차적으로 그린다.
        {
            RHIShaderBlob depthBlob;
            if (!CompileSsgiShader(SsgiShaders::kTestDepthFile, nullptr, depthBlob, error))
            {
                outLog += "[3/3] 테스트 깊이 셰이더 컴파일 실패: " + error + "\n";
                ssgi.Shutdown();
                resources.Shutdown();
                return false;
            }

            const RHIPipelineLayoutParam depthParams[] = {
                RHILayout::Cbv(0),
                RHILayout::UavTable(2, 0),   // 깊이 + 노멀
            };

            RHIPipelineLayoutDesc depthRootDesc{};
            depthRootDesc.params = depthParams;

            const auto depthRoot = rootSignatures.GetOrCreate(depthRootDesc, error);
            if (!depthRoot.IsValid())
            {
                outLog += "[3/3] 테스트 깊이 루트 시그니처 실패: " + error + "\n";
                ssgi.Shutdown();
                resources.Shutdown();
                return false;
            }

            RHIComputePipelineDesc depthPsoDesc{};
            depthPsoDesc.csBytecode = depthBlob.Data();
            depthPsoDesc.csSize = depthBlob.Size();
            depthPsoDesc.layout = depthRoot;

            const RHIPipelineHandle depthPso = psoManager.GetOrCreateCompute(depthPsoDesc, error);
            if (!depthPso.IsValid())
            {
                outLog += "[3/3] 테스트 깊이 PSO 실패: " + error + "\n";
                ssgi.Shutdown();
                resources.Shutdown();
                return false;
            }

            if (!resources.BeginFrame(error))
            {
                outLog += "[3/3] 깊이 채우기 BeginFrame 실패: " + error + "\n";
                ssgi.Shutdown();
                resources.Shutdown();
                return false;
            }

            struct { uint32_t w, h; float nearP, farP; } depthCb{ kWidth, kHeight, 0.1f, 100.f };

            const auto cb = resources.AllocateUpload(
                sizeof(depthCb), DX12UploadRing::kConstantBufferAlignment);
            const RHIBindingDesc depthUavs[] = {
                RHIBindingDesc::Uav2D(resources.RegisterExternalTexture(depth.Get()),
                    RHIFormat::R32Float),
                RHIBindingDesc::Uav2D(resources.RegisterExternalTexture(testNormal.Get()),
                    RHIFormat::RGBA16Float),
            };
            const RHIBindingTable uavTable = resources.CreateBindings(depthUavs);
            if (cb.IsValid() && uavTable.IsValid())
            {
                memcpy(cb.cpuAddress, &depthCb, sizeof(depthCb));

                auto* cmd = resources.GetCommandList();
                resources.BindDescriptorHeaps(cmd);
                // ' + chr(0x2605) + ' 이 검사는 인코더를 안 타고 원시 경로를 쓴다 — 그것이 검사의
                //   목적이라 그대로 두고, 핸들만 스스로 푼다.
                const DX12PipelineEntry depthEntry = resources.Resolve(depthPso);
                cmd->SetComputeRootSignature(depthEntry.signature);
                cmd->SetPipelineState(depthEntry.pipeline);
                cmd->SetComputeRootConstantBufferView(0,
                    resources.Resolve(cb.buffer)->GetGPUVirtualAddress() + cb.offset);
                cmd->SetComputeRootDescriptorTable(1, DX12ToGpuHandle(uavTable.backend));
                cmd->Dispatch((kWidth + 7) / 8, (kHeight + 7) / 8, 1);

                // SSGI가 SRV로 읽으므로 전이한다.
                // ★ 여기가 NON_PIXEL 로 전이하면서 그래프에는 ShaderResource
                //   (=ALL)라고 말하고 있었다. 그래프의 첫 usage 도
                //   ShaderResource 라 전이가 안 나와서 드러나지 않던 불일치다 —
                //   배리어가 한 번이라도 나왔으면 before 가 실제와 어긋난다.
                //   중립 어휘로 옮기면서 선언을 참으로 만든다(V3-c).
                const RHITransition both[] = {
                    { resources.RegisterExternalTexture(depth.Get()),
                      RHIResourceState::UnorderedAccess, RHIResourceState::ShaderResource },
                    { resources.RegisterExternalTexture(testNormal.Get()),
                      RHIResourceState::UnorderedAccess, RHIResourceState::ShaderResource } };
                resources.TransitionResources(both);
            }

            if (!resources.EndFrame(error))
            {
                outLog += "[3/3] 깊이 채우기 EndFrame 실패: " + error + "\n";
                ssgi.Shutdown();
                resources.Shutdown();
                return false;
            }
            resources.WaitForGpu();
        }

        // ★ 결과를 읽는 패스를 붙인다.
        //
        // 없으면 트레이스가 컬링돼 Dispatch가 한 번도 안 돈다. 실제로 첫
        // 실행이 '선언 9 · 실행 0'이었다 — 셰이더 컴파일만 확인하고 넘어갈
        // 뻔했다. 컬링이 도는 것은 옳지만, 도는지 보려면 읽는 쪽이 있어야 한다.
        // ★ 리드백 크기는 읽는 리소스에 맞춘다.
        //
        // 처음에는 GI 해상도(1/2)로 잡았다. 합성이 붙으면서 출력이 전 해상도가
        // 됐는데 리드백은 그대로였고, CopyTextureRegion이 "목적지 경계를\n// 넘는다"로 실패했다. 그 실패가 커맨드 리스트를 무효로 만들어
        // EndFrame의 Close가 E_INVALIDARG를 냈다 — 증상이 나온 자리와
        // 원인이 있는 자리가 달랐다.
        // ★ 리드백 대상은 리졸브 결과다.
        //
        // 합성 결과가 아니라 리졸브를 읽는 이유: a 채널에 누적 프레임 수가
        // 들어 있다. 그것이 '시간축이 샘플 수를 대신한다'는 전제가 실제로
        // 도는지 보여 주는 유일한 숫자다. 합성 결과에는 그 정보가 없다.
        const uint32_t giW = kWidth / EnhancedSSGIPass::kResolutionDivisor;
        const uint32_t giH = kHeight / EnhancedSSGIPass::kResolutionDivisor;
        RHIReadback readback{};
        std::string readbackError;
        if (!resources.CreateReadback(giW, giH, EnhancedSSGIPass::kGIFormat, 1,
            readback, readbackError))
        {
            outLog += "[3/3] 리드백 버퍼 생성 실패\n";
            passed = false;
        }

        std::vector<double> sweepHits;

        // ── 여러 프레임 돌린다 ──
        //
        // 한 프레임만 돌면 누적이 늘 1이고, 그러면 '시간축이 샘플 수를
        // 대신한다'는 이 설계의 전제를 확인할 수 없다. 카메라를 고정한 채
        // 여러 프레임 돌려 누적이 실제로 쌓이는지 본다.
        //
        // 정지 상태에서 쌓이지 않으면 재투영이 어긋난 것이고, 그때는
        // depthTolerance가 너무 빡빡하다는 뜻이다.
        constexpr uint32_t kTestFrames = 8;
        EnhancedRenderGraph::Stats stats{};

        for (uint32_t frameIndex = 0; frameIndex < kTestFrames && passed; ++frameIndex)
        {
        if (!resources.BeginFrame(error))
        {
            outLog += "[3/3] BeginFrame 실패: " + error + "\n";
            ssgi.Shutdown();
            resources.Shutdown();
            return false;
        }

        if (!ssgi.PrepareFrame(frameContext, error))
        {
            outLog += "[3/3] PrepareFrame 실패: " + error + "\n";
            passed = false;
            break;
        }

        EnhancedRenderGraph graph(resources);

        EnhancedSSGIPass::Inputs inputs{};
        inputs.depth = graph.ImportTexture(depth.Get(),
            RHIResourceState::ShaderResource, "SSGI.TestDepth");
        inputs.normal = graph.ImportTexture(testNormal.Get(),
            RHIResourceState::ShaderResource, "SSGI.TestNormal");
        ssgi.SetInputs(inputs);

        ssgi.Declare(graph, frameContext);

        const auto output = ssgi.GetOutput();
        if (!output.IsValid())
        {
            outLog += "[3/3] 출력 핸들이 비었다 — Declare가 패스를 선언하지 않았다\n";
            passed = false;
        }

        if (passed && output.IsValid())
        {
            graph.AddPass("SSGI.Readback",
                { { ssgi.GetResolvedResult(), RHIResourceState::CopySource } },
                [&](const EnhancedRenderGraph::ExecuteContext& executeContext)
                {
                    executeContext.encoder->CopyToReadback( readback,
                        executeContext.ResolveHandle(ssgi.GetResolvedResult()));
                }, true);
        }

        if (passed && !graph.Compile(resources.GetDevice(), error))
        {
            outLog += "[3/3] 그래프 Compile 실패: " + error + "\n";
            passed = false;
        }

        if (passed && !graph.Execute(resources.GetCommandList(), error))
        {
            outLog += "[3/3] 그래프 Execute 실패: " + error + "\n";
            passed = false;
        }

        stats = graph.GetStats();

        if (!resources.EndFrame(error))
        {
            outLog += "[3/3] EndFrame 실패: " + error + "\n";
            passed = false;
        }
        else
        {
            resources.WaitForGpu();
        }
        }   // 프레임 루프 끝

        {
            char line[224]{};
            std::snprintf(line, sizeof(line),
                "[3/3] 그래프 — 선언 %u · 실행 %u · 컬링 %u · 배리어 %u(%u번에)\n",
                stats.passesDeclared, stats.passesExecuted, stats.passesCulled,
                stats.barriersEmitted, stats.barrierBatches);
            outLog += line;
        }

        // ── 누적이 실제로 쌓이는가 ──
        //
        // 리졸브 결과의 a 채널에 누적 프레임 수가 들어 있다. 카메라를
        // 고정하고 여덟 프레임 돌렸으므로, 재투영이 맞으면 8까지 올라가야
        // 한다. 1에 머물면 매 프레임 히스토리를 버리는 것이고, 그러면
        // 프레임당 슬라이스 둘만 쓰는 셈이라 품질만 나빠진다.
        {
            RHIReadbackImage captured{};
            std::string mapError;
            if (resources.MapReadback(readback, captured, mapError))
            {
                double accumSum = 0.0;
                float  accumMin = 1e9f;
                float  accumMax = 0.f;
                size_t counted = 0;

                // a 채널이 누적 수다. 디코드는 캡처가 한다(R2c-b2).
                for (uint32_t y = 0; y < giH; ++y)
                    for (uint32_t x = 0; x < giW; ++x)
                    {
                        const float value = captured.At(x, y, 3);
                        if (value <= 0.f) continue;   // 하늘은 0이다

                        accumSum += value;
                        accumMin = (std::min)(accumMin, value);
                        accumMax = (std::max)(accumMax, value);
                        ++counted;
                    }

                if (0 != counted)
                {
                    const double average = accumSum / static_cast<double>(counted);

                    char line[256]{};
                    std::snprintf(line, sizeof(line),
                        "[3/3] 누적 — %u프레임 뒤 평균 %.2f · 최소 %.0f · 최대 %.0f"
                        " (픽셀 %zu)\n",
                        kTestFrames, average, accumMin, accumMax, counted);
                    outLog += line;

                    // ★ 정지 상태에서 누적이 안 쌓이면 전제가 무너진 것이다.
                    //   여덟 프레임을 돌았으니 평균이 절반은 넘어야 한다.
                    if (average < kTestFrames * 0.5)
                    {
                        outLog += "누적이 쌓이지 않는다 — 재투영이 매 프레임"
                            " 히스토리를 버리고 있다(depthTolerance를 볼 것)\n";
                        passed = false;
                    }
                }
                else
                {
                    outLog += "[3/3] 누적을 잴 픽셀이 없다 — 전부 하늘로 판정됐다\n";
                    passed = false;
                }
            }
        }

        // ── 상수 스윕 ──
        //
        // 값을 정하기 전에 그 값이 결과를 실제로 바꾸는지부터 본다.
        // 바꿔도 숫자가 그대로면 그 상수는 지금 아무 일도 안 하는 것이고,
        // 그때는 값을 고르는 것이 아니라 배선을 봐야 한다 — 누적에서
        // 이미 그렇게 세 번 틀렸다.
        //
        // 히트 비율은 트레이스 출력의 a 채널이다. 추적 거리와 두께가
        // 그것을 좌우한다.
        {
            struct SweepCase { float distance; float thickness; };
            const SweepCase cases[] = {
                { 2.f,  0.5f }, { 8.f,  0.5f }, { 32.f, 0.5f },
                // 두께 검사가 뷰 공간으로 바뀌었으므로 뷰 단위 눈금으로 본다.
                // 0.05는 얇은 판, 0.5는 사람 몸통, 5는 벽 두께쯤 된다.
                { 8.f,  0.05f }, { 8.f,  5.0f },
            };

            outLog += "[3/3] 추적 스윕 — 거리/두께 → 히트 비율\n";

            for (const SweepCase& sweepCase : cases)
            {
                EnhancedSSGIPass::Tuning tuning = ssgi.GetTuning();
                tuning.traceDistance = sweepCase.distance;
                tuning.traceThickness = sweepCase.thickness;
                ssgi.SetTuning(tuning);

                if (!resources.BeginFrame(error)) break;
                if (!ssgi.PrepareFrame(frameContext, error)) break;

                EnhancedRenderGraph sweepGraph(resources);

                // 깊이는 그래프마다 새로 임포트한다. 핸들은 그래프에 매인
                // 것이라 앞 그래프의 것을 넘겨 쓰면 다른 리소스를 가리킨다.
                EnhancedSSGIPass::Inputs sweepInputs{};
                sweepInputs.depth = sweepGraph.ImportTexture(depth.Get(),
                    RHIResourceState::ShaderResource, "SSGI.SweepDepth");
                sweepInputs.normal = sweepGraph.ImportTexture(testNormal.Get(),
                    RHIResourceState::ShaderResource, "SSGI.SweepNormal");
                ssgi.SetInputs(sweepInputs);

                ssgi.Declare(sweepGraph, frameContext);

                sweepGraph.AddPass("SSGI.SweepReadback",
                    { { ssgi.GetTraceResult(), RHIResourceState::CopySource } },
                    [&](const EnhancedRenderGraph::ExecuteContext& executeContext)
                    {
                        executeContext.encoder->CopyToReadback( readback,
                            executeContext.ResolveHandle(ssgi.GetTraceResult()));
                    }, true);

                if (!sweepGraph.Compile(resources.GetDevice(), error)) break;
                if (!sweepGraph.Execute(resources.GetCommandList(), error)) break;
                if (!resources.EndFrame(error)) break;
                resources.WaitForGpu();

                RHIReadbackImage captured{};
                std::string mapError;
                if (!resources.MapReadback(readback, captured, mapError)) break;

                double hitSum = 0.0;
                size_t counted = 0;

                for (uint32_t y = 0; y < giH; ++y)
                    for (uint32_t x = 0; x < giW; ++x)
                    {
                        hitSum += captured.At(x, y, 3);
                        ++counted;
                    }

                char line[192]{};
                std::snprintf(line, sizeof(line),
                    "        거리 %5.1f · 두께 %4.2f → 히트 %.4f\n",
                    sweepCase.distance, sweepCase.thickness,
                    (0 != counted) ? (hitSum / static_cast<double>(counted)) : 0.0);
                outLog += line;

                sweepHits.push_back((0 != counted)
                    ? (hitSum / static_cast<double>(counted)) : 0.0);
            }

            // ★ 값을 바꿔도 결과가 그대로면 그 상수는 지금 안 쓰이는 것이다.
            if (sweepHits.size() >= 3)
            {
                const double spread = *std::max_element(sweepHits.begin(), sweepHits.end())
                    - *std::min_element(sweepHits.begin(), sweepHits.end());
                if (spread < 1e-4)
                {
                    outLog += "추적 상수를 바꿔도 히트 비율이 그대로다"
                        " — 그 값이 셰이더에 닿지 않는다\n";
                    passed = false;
                }
            }
        }

        // ── 필터 상수 스윕 ──
        //
        // 필터가 하는 일은 노이즈를 줄이는 것이다. 그러니 지표는 분산이다 —
        // 리졸브 결과와 필터 결과의 표준편차를 나란히 보면 실제로 일하는지
        // 알 수 있다. 값을 바꿔도 감소율이 그대로면 그 상수는 안 닿는 것이다.
        //
        // 깊이 시그마: 작을수록 깊이 차이에 민감해져 덜 섞는다.
        // 노멀 지수: 클수록 노멀이 다른 이웃을 강하게 배제한다.
        {
            struct FilterCase { float depthSigma; float normalPower; };
            const FilterCase cases[] = {
                { 0.0001f, 16.f }, { 0.01f, 16.f }, { 1.0f, 16.f },
                { 0.01f,    1.f }, { 0.01f, 64.f },
            };

            outLog += "[3/3] 필터 스윕 — 시그마/지수 → 이웃 차이(리졸브 → 필터)\n";

            std::vector<double> reductions;

            for (const FilterCase& filterCase : cases)
            {
                EnhancedSSGIPass::Tuning tuning = ssgi.GetTuning();
                tuning.filterDepthSigma = filterCase.depthSigma;
                tuning.filterNormalPower = filterCase.normalPower;
                ssgi.SetTuning(tuning);

                double sigmaBefore = 0.0;
                double sigmaAfter = 0.0;

                // 리졸브와 필터를 각각 읽는다. 한 프레임에 둘 다 읽으려면
                // 리드백 버퍼가 둘이어야 하는데, 두 번 도는 편이 단순하다.
                for (uint32_t stage = 0; stage < 2; ++stage)
                {
                    if (!resources.BeginFrame(error)) break;
                    if (!ssgi.PrepareFrame(frameContext, error)) break;

                    EnhancedRenderGraph filterGraph(resources);

                    EnhancedSSGIPass::Inputs filterInputs{};
                    filterInputs.depth = filterGraph.ImportTexture(depth.Get(),
                        RHIResourceState::ShaderResource, "SSGI.FilterDepth");
                    filterInputs.normal = filterGraph.ImportTexture(testNormal.Get(),
                        RHIResourceState::ShaderResource, "SSGI.FilterNormal");
                    ssgi.SetInputs(filterInputs);

                    ssgi.Declare(filterGraph, frameContext);

                    const RGHandle target = (0 == stage)
                        ? ssgi.GetResolvedResult() : ssgi.GetOutput();
                    const RGHandle readSource = (0 == stage)
                        ? ssgi.GetResolvedResult() : ssgi.GetFilteredResult();
                    (void)target;

                    filterGraph.AddPass("SSGI.FilterReadback",
                        { { readSource, RHIResourceState::CopySource } },
                        [&](const EnhancedRenderGraph::ExecuteContext& executeContext)
                        {
                            executeContext.encoder->CopyToReadback( readback,
                                executeContext.ResolveHandle(readSource));
                        }, true);

                    if (!filterGraph.Compile(resources.GetDevice(), error)) break;
                    if (!filterGraph.Execute(resources.GetCommandList(), error)) break;
                    if (!resources.EndFrame(error)) break;
                    resources.WaitForGpu();

                    RHIReadbackImage captured{};
                    std::string mapError;
                    if (!resources.MapReadback(readback, captured, mapError)) break;

                    // ★ 표준편차가 아니라 '이웃 간 차이'를 잰다.
                    //
                    // 처음에는 전체 표준편차를 썼다. 필터 전후로 0.079422 →
                    // 0.079416, 감소 0.0%가 나왔다. 필터가 죽은 것처럼 보였지만
                    // 지표가 틀린 것이었다 — 표준편차는 공간 구조(밝은 곳과
                    // 어두운 곳의 차이)를 포함하고, 필터는 그것을 지우면 안 된다.
                    //
                    // 노이즈는 이웃 픽셀 사이에서 튀고 구조는 완만하다. 그래서
                    // 오른쪽·아래 이웃과의 차이 평균을 본다. 필터가 일하면
                    // 이 값이 줄고, 구조를 지우는지는 별개로 봐야 한다.
                    double diffSum = 0.0;
                    size_t counted = 0;

                    const auto lumaAt = [&](uint32_t x, uint32_t y) -> double
                    {
                        return 0.2126 * captured.At(x, y, 0)
                            + 0.7152 * captured.At(x, y, 1)
                            + 0.0722 * captured.At(x, y, 2);
                    };

                    for (uint32_t y = 0; y + 1 < giH; ++y)
                    {
                        for (uint32_t x = 0; x + 1 < giW; ++x)
                        {
                            const double here = lumaAt(x, y);
                            diffSum += std::abs(lumaAt(x + 1, y) - here);
                            diffSum += std::abs(lumaAt(x, y + 1) - here);
                            counted += 2;
                        }
                    }

                    if (0 != counted)
                    {
                        const double roughness = diffSum / static_cast<double>(counted);
                        if (0 == stage) sigmaBefore = roughness;
                        else            sigmaAfter = roughness;
                    }
                }

                const double reduction = (sigmaBefore > 1e-9)
                    ? (1.0 - sigmaAfter / sigmaBefore) : 0.0;
                reductions.push_back(reduction);

                char line[224]{};
                std::snprintf(line, sizeof(line),
                    "        시그마 %7.4f · 지수 %5.1f → %.6f → %.6f (감소 %.1f%%)\n",
                    filterCase.depthSigma, filterCase.normalPower,
                    sigmaBefore, sigmaAfter, reduction * 100.0);
                outLog += line;
            }

            // ★ 값을 바꿔도 감소율이 그대로면 그 상수는 안 닿는 것이다.
            if (reductions.size() >= 3)
            {
                const double spread =
                    *std::max_element(reductions.begin(), reductions.end())
                    - *std::min_element(reductions.begin(), reductions.end());

                char line[192]{};
                std::snprintf(line, sizeof(line),
                    "        감소율 폭 %.1f%%p — %s\n",
                    spread * 100.0,
                    (spread < 0.01) ? "상수가 결과를 거의 안 바꾼다"
                                    : "상수가 결과를 바꾼다");
                outLog += line;
            }
        }

        // Hi-Z 밉마다 하나 + 트레이스 + 리졸브 + 필터 + 합성
        // + 히스토리 저장 + 리드백.
        const uint32_t expectedPasses = expectedMips + 6;
        if (stats.passesDeclared != expectedPasses)
        {
            outLog += "선언된 패스가 " + std::to_string(stats.passesDeclared)
                + "개인데 " + std::to_string(expectedPasses) + "개여야 한다\n";
            passed = false;
        }

        // ★ 리드백이 결과를 읽으므로 전부 살아나야 한다.
        //
        // 이 단정이 없던 첫 실행이 '선언 9 · 실행 0'이었다 — 아무도 결과를
        // 안 읽어 전부 컬링됐고, 셰이더 컴파일만 확인한 채 '통과'가 나왔다.
        // 컬링이 도는 것은 옳지만 그 상태로는 Hi-Z 체인과 행진이 실제로
        // 도는지 알 수 없다. 그래서 리드백을 붙이고 실행 수를 단정한다.
        if (stats.passesExecuted != expectedPasses)
        {
            outLog += "실행된 패스가 " + std::to_string(stats.passesExecuted)
                + "개다 — Hi-Z 체인의 의존이 끊겼다\n";
            passed = false;
        }

        // 밉이 앞 밉을 SRV로 읽으므로 UAV→SRV 전이가 밉 수만큼은 나와야 한다.
        // 0이면 배리어 유도가 죽은 것이고, 그러면 GPU가 덜 쓴 것을 읽는다.
        if (stats.barriersEmitted < expectedMips)
        {
            outLog += "배리어가 " + std::to_string(stats.barriersEmitted)
                + "건뿐이다 — UAV→SRV 전이가 빠졌다\n";
            passed = false;
        }
    }

    std::string validation;
    const uint32_t problems = resources.DrainDebugMessages(validation);
    if (0 != problems)
    {
        passed = false;
        outLog += "검증 레이어 문제 " + std::to_string(problems) + "건\n" + validation;
    }

    ssgi.Shutdown();
    rootSignatures.Shutdown();
    psoManager.Shutdown();
    resources.Shutdown();

    outLog += passed ? "SSGI 검증 통과\n" : "SSGI 검증 실패\n";
    return passed;
}

#endif
