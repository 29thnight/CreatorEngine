#pragma once
#include "../RHIFormat.h"
#ifndef DYNAMICCPP_EXPORTS
#include <array>
#include <cstdint>
#include <wrl/client.h>

#include "EnhancedRenderPass.h"

// 화면 공간 전역 조명 (PHASE 3-6, 신규 작성).
//
// ── 왜 이식이 아니라 새로 쓰는가 ──
//
// 기존 DX11 SSGIPass를 옮기지 않는다. 그쪽 구조가 비용을 쓰는 자리가
// 지금 하려는 것과 어긋나 있어서다. 코드를 읽고 확인한 것:
//
//   ① 전 해상도로 계산한다. Dispatch가 viewWidth/viewHeight 기준인데,
//      GI는 저주파 신호라 픽셀마다 풀 수를 뽑을 이유가 없다.
//   ② 샘플마다 텍스처를 세 번 판다(깊이·노멀·라이트). 슬라이스 Nd ×
//      샘플 Ns의 이중 루프이므로 페치 수가 그대로 곱해진다.
//   ③ 시간축을 쓰지 않는다. 매 프레임 처음부터 뽑고, 그렇게 생긴 노이즈를
//      dual filtering 여러 단계로 지운다 — 만든 노이즈를 다시 지우는 데
//      두 번 비용을 쓴다.
//   ④ 화면 공간 행진이 균등 스텝이다. 빈 공간도 물체가 빽빽한 곳과 같은
//      값을 치르고, UV가 크게 뛰면 텍스처 캐시도 놓친다.
//
// 옮기면 이 넷이 그대로 따라온다. 그래서 새로 쓴다.
//
// ── 새 구조 ──
//
//   1/2 해상도 → Hi-Z 행진 → 프레임당 소수 샘플(프레임마다 회전)
//              → 시간적 재투영·누적 → bilateral 한 번 → 업샘플 합성
//
// 넷을 각각 다르게 한다:
//
//   1/2 해상도 — 비용이 1/4이 된다. GI가 저주파라 잃는 것이 거의 없고,
//     경계는 마지막 업샘플에서 깊이·노멀을 보고 되살린다.
//
//   Hi-Z 행진 — 깊이 밉 피라미드를 타고 빈 공간을 큰 걸음으로 건넌다.
//     균등 스텝은 '아무것도 없는 구간'에 같은 값을 치르는데, 화면 공간
//     행진에서 그 구간이 대부분이다.
//
//   프레임당 소수 샘플 + 회전 — 시간축이 샘플 수를 대신한다. 프레임마다
//     노이즈 패턴을 돌리면 여러 프레임에 걸쳐 표본이 쌓인다.
//
//   시간적 누적 — 지난 프레임 결과를 재투영해 섞는다. 이것이 가장 크게
//     아끼는 자리다. 노이즈가 줄어드는 만큼 뒤의 공간 필터도 가벼워진다
//     (dual filtering 다단계 → bilateral 한 번).
//
// ── 판정 ──
//
// 위의 '아낀다'는 전부 근거 있는 추정이지 측정이 아니다. 그래서 패스마다
// GPU 타임스탬프를 처음부터 붙이고, 같은 씬에서 기존 DX11 SSGI와 시간을
// 나란히 잰다. Release로 잰다 — 이 프로젝트에서 Debug 측정이 결론을
// 뒤집은 적이 있다.
//
// 개선이 확인되지 않으면 그 항목은 되돌린다. 구조가 새롭다는 것은
// 빨라졌다는 뜻이 아니다.
class EnhancedSSGIPass : public EnhancedRenderPass
{
public:
    /// GI를 계산하는 해상도의 분모. 1/2 해상도.
    static constexpr uint32_t kResolutionDivisor = 2;

    /// 히스토리 버퍼 수. 재투영이 지난 프레임 하나만 보면 되므로 둘이면 된다
    /// (읽는 것과 쓰는 것을 번갈아 쓴다).
    static constexpr uint32_t kHistoryCount = 2;

    /// Hi-Z 피라미드의 최대 밉 수. 1/2 해상도에서 시작하므로 이 정도면
    /// 화면 전체를 몇 걸음에 건널 수 있다.
    static constexpr uint32_t kMaxHiZMips = 8;

    /// 프레임당 슬라이스 수. 기존은 Nd를 통째로 돌았지만 여기서는 시간축이
    /// 나머지를 맡는다. 이 값을 올리면 프레임당 비용이 그대로 비례한다.
    static constexpr uint32_t kSlicesPerFrame = 2;

    /// 시간적 누적의 최대 프레임 수. 너무 길면 움직이는 것이 번지고,
    /// 짧으면 노이즈가 남는다. 실측으로 정할 값이라 상수로 박아 두되
    /// 근거가 생기면 고친다.
    static constexpr uint32_t kMaxAccumFrames = 32;

    static constexpr RHIFormat kGIFormat = RHIFormat::RGBA16Float;
    static constexpr RHIFormat kHiZFormat = RHIFormat::R32Float;

    const char* GetName() const override { return "SSGI"; }

    bool Initialize(const EnhancedFrameContext& context, std::string& outError) override;
    bool PrepareFrame(const EnhancedFrameContext& context, std::string& outError) override;
    void Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context) override;
    void Shutdown() override;

    /// GBuffer·라이팅 입력. Declare 전에 넣어 준다.
    struct Inputs
    {
        RGHandle depth;
        RGHandle normal;
        RGHandle diffuse;
        RGHandle metalRough;
        RGHandle lighting;

        /// SSAO 결과(반해상도, x = AO). 간접광에 곱한다.
        ///
        /// 직접광이 아니라 간접광에 곱하는 것이 요점이다 — AO가 모형화하는
        /// 것은 '주변에서 오는 빛이 얼마나 막히는가'이고, 그 주변광이 곧
        /// 여기의 GI다. 직접광에 곱하면 광원이 실제로 보이는 곳까지
        /// 어두워져 그림자가 두 번 진 것처럼 보인다.
        ///
        /// 비어 있으면 AO 없이(=1) 합성한다.
        RGHandle ambientOcclusion;   // 직접광 결과 — 간접광의 광원이 된다
    };

    void SetInputs(const Inputs& inputs) { m_inputs = inputs; }

    /// 조율 가능한 상수.
    ///
    /// 헤더에 박아 두지 않고 밖에서 바꿀 수 있게 한다. 값을 정하려면
    /// 여러 값으로 돌려 봐야 하는데, 상수로 두면 재빌드가 한 번씩 든다.
    /// 기본값은 눈대중이고 실측으로 조인다.
    struct Tuning
    {
        // 추적 거리는 결과를 실제로 바꾼다(실측):
        //   거리  2 → 히트 0.2354 · 8 → 0.1592 · 32 → 0.1359
        // 멀리 볼수록 히트가 주는 것은 같은 스텝 수로 넓은 범위를 훑어
        // 해상도가 떨어지기 때문이다. 다만 히트 비율이 높다고 좋은 것은
        // 아니다 — 가까운 것만 맞추면 GI가 국소적이 된다. 어느 값이 옳은지는
        // 그림을 봐야 정할 수 있고, 8은 아직 눈대중이다.
        //
        // ★ 노멀을 넣고 다시 재니 거리 의존성이 사라졌다:
        //   2 → 0.5511 · 8 → 0.5507 · 32 → 0.5503
        // 앞선 측정(0.2354 / 0.1592 / 0.1359)은 노멀 자리에 깊이가 꽂혀
        // 반구 방향이 엉망일 때의 값이었다. 제대로 된 노멀에서는 대부분
        // 가까이서 맞으므로 멀리 볼 이유가 줄어든다 — 이 씬 기준이고,
        // 넓은 실내처럼 먼 반사가 중요한 씬에서는 다시 재야 한다.
        float traceDistance{ 8.f };

        // ★ 두께는 현재 거의 아무 일도 하지 않는다(실측).
        //
        //   0.0002 → 0.1587 · 0.002 → 0.1597 · 0.02 → 0.1596 · 0.5 → 0.1592
        //   100배를 바꿔도 0.6% 안에서 움직인다.
        //
        // 두 가지가 겹쳐 있다:
        //   ① 단위가 안 맞았다. '뷰 공간 두께'라고 적어 놓고 셰이더는 클립
        //      깊이(0~1 비선형)와 비교한다. 0.5는 클립 깊이에서 거대한 값이라
        //      검사가 늘 통과했다.
        //   ② 작은 값으로 바꿔도 안 걸린다. Hi-Z 행진이 밉 0에서 교차를
        //      판정하는 순간 rayDepth - cellDepth가 이미 매우 작기 때문이다.
        //
        // 그래서 이 값은 지금 '조일 상수'가 아니다. 두께 검사를 살리려면
        // 클립 깊이를 뷰 공간으로 되돌려 비교하도록 셰이더를 고쳐야 하고,
        // 그 전에는 어떤 값을 넣어도 같다.
        float traceThickness{ 0.5f };
        float accumDepthTolerance{ 0.01f };

        // 필터 상수는 결과를 바꾸지만 폭이 작다(실측, 이웃 차이 감소율):
        //   시그마 0.0001 → 2.7% · 0.01 → 3.6% · 1.0 → 4.0%
        //   지수   1 → 5.0% · 16 → 3.6% · 64 → 3.1%
        //
        // 느슨할수록(시그마↑ 지수↓) 더 지운다. 당연한 방향이고, 죽어 있지
        // 않다는 것은 확인됐다. 다만 최대 5%뿐이다 — 8프레임 누적이 앞에서
        // 이미 노이즈를 지워 필터가 할 일이 적다는 뜻이고, 그것은 '누적이
        // 일하는 만큼 공간 필터가 가벼워진다'는 설계 의도와 맞는다.
        //
        // 그래서 값을 더 조일 실익이 크지 않다. 느슨하게 하면 조금 더 지우는
        // 대신 물체 경계에서 배경 GI가 새어 들어온다(후광). 지금 값은 그
        // 절충의 안전한 쪽이고, 경계가 실제로 새는지는 그림을 봐야 안다.
        float filterDepthSigma{ 0.01f };
        float filterNormalPower{ 16.f };
        float compositeDepthSigma{ 0.01f };

        // 그림 비교로 정했다(FT_Material, 노출 0.35 PNG):
        //   0.5 — 차이가 거의 안 보인다 · 1.0 — 색 번짐이 보이되 과하지 않다
        //   2.0 — 바닥이 씻겨 나간다(과노출처럼 보임)
        // 1.0을 유지한다. 거리(2/8/32)는 세 그림이 사실상 동일했다 — 이 씬은
        // 기하가 가까워 거리 값이 그림을 못 가른다. 8을 중간값으로 두되,
        // 먼 반사가 중요한 씬이 생기면 그림으로 다시 정한다.
        float intensity{ 1.f };
    };

    void SetTuning(const Tuning& tuning) { m_tuning = tuning; }
    const Tuning& GetTuning() const { return m_tuning; }

    /// 합성까지 끝난 결과. 뒤 패스가 읽는다.
    RGHandle GetOutput() const { return m_output; }

    /// 단계별 중간 결과. 검증과 디버그 뷰가 본다.
    RGHandle GetTraceResult() const { return m_traceResult; }
    RGHandle GetResolvedResult() const { return m_resolved; }
    RGHandle GetFilteredResult() const { return m_filtered; }

    /// 시간적 누적이 실제로 도는가.
    ///
    /// 이 값이 늘 1이면 재투영이 죽은 것이고, 그러면 '시간축이 샘플 수를
    /// 대신한다'는 전제가 무너진 채로 프레임당 소수 샘플만 쓰게 된다 —
    /// 조용히 품질만 나빠진다. 그래서 밖에서 볼 수 있게 둔다.
    uint32_t GetLastAccumFrames() const { return m_lastAccumFrames; }

    /// 히스토리를 버린 픽셀의 비율(0~1). 카메라가 크게 움직이면 올라가는
    /// 것이 정상이고, 정지 상태에서 높으면 재투영이 어긋난 것이다.
    float GetLastRejectRatio() const { return m_lastRejectRatio; }

private:
    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    bool CreatePipelines(const EnhancedFrameContext& context, std::string& outError);

    Inputs   m_inputs{};
    Tuning   m_tuning{};
    RGHandle m_output;
    RGHandle m_traceResult;   // 행진 원본(노이즈가 살아 있다)

    // Hi-Z 피라미드. 밉마다 UAV가 필요해 핸들을 따로 든다.
    std::array<RGHandle, kMaxHiZMips> m_hiZMips{};
    uint32_t m_hiZMipCount{ 0 };

    // 히스토리는 그래프의 transient가 아니다 — 프레임을 넘겨 살아야 한다.
    //
    // GI와 깊이를 함께 든다. 깊이가 없으면 '재투영한 자리가 정말 같은
    // 표면인가'를 물을 수 없고, 그러면 카메라가 움직일 때 엉뚱한 픽셀의
    // 값을 섞게 된다 — 증상이 '움직이면 번진다'라서 노이즈로 오해하기 쉽다.
    std::array<RHITextureHandle, kHistoryCount> m_history;
    std::array<RHITextureHandle, kHistoryCount> m_historyDepth;
    uint32_t m_historyIndex{ 0 };
    bool     m_historyValid{ false };

    /// 히스토리도 그래프에 들인다(R4-2b). 프레임을 넘겨 살지만 그래프 밖
    /// 리소스는 아니다 — Declare가 매 프레임 Import하고 전이는 그래프가 만든다.
    ///
    /// ★ 초기 상태가 NON_PIXEL_SHADER_RESOURCE인 것은 CreateTexture에
    ///   그렇게 주기 때문이다(만들자마자 다음 프레임 셰이더가 읽으므로
    ///   COMMON에서 출발시키면 첫 읽기 앞에 전이가 하나 더 붙는다).
    ///   EnsureHistory가 다시 만들면 여기도 되돌려야 한다.
    std::array<RGHandle, kHistoryCount> m_historyHandle;
    std::array<RGHandle, kHistoryCount> m_historyDepthHandle;
    std::array<RHIResourceState, kHistoryCount> m_historyState{
        RHIResourceState::ShaderResource, RHIResourceState::ShaderResource };
    std::array<RHIResourceState, kHistoryCount> m_historyDepthState{
        RHIResourceState::ShaderResource, RHIResourceState::ShaderResource };

    bool EnsureHistory(const EnhancedFrameContext& context, std::string& outError);

    RGHandle m_filtered;      // bilateral 결과(1/2 해상도)
    RGHandle m_resolved;      // 누적 결과(1/2 해상도)

    // 재투영에 쓸 지난 프레임 뷰·투영.
    Mathf::Matrix m_previousViewProjection{};
    bool          m_hasPreviousFrame{ false };

    uint32_t m_giWidth{ 0 };
    uint32_t m_giHeight{ 0 };

    uint32_t m_lastAccumFrames{ 0 };
    float    m_lastRejectRatio{ 0.f };
    uint32_t m_frameIndex{ 0 };

    RHIPipelineHandle m_hiZBuildPSO;
    RHIPipelineHandle m_tracePSO;
    RHIPipelineHandle m_resolvePSO;   // 재투영 + 누적
    RHIPipelineHandle m_filterPSO;    // bilateral
    RHIPipelineHandle m_compositePSO; // 업샘플 + 합성
};

#endif
