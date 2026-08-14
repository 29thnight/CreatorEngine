#pragma once
#include "../../../RHI/RHIFormat.h"
#ifndef DYNAMICCPP_EXPORTS
#include <cstdint>
#include <wrl/client.h>

#include "../../Graph/EnhancedRenderPass.h"

// SSAO 패스 (PHASE 3-6, 신규 작성).
//
// ── 기존 DX11 SSAO가 하는 일과 그 값 ──
//
// 반구 커널 64개를 뽑아 픽셀마다 이렇게 돈다:
//
//   for (i = 0; i < 64; ++i)
//       samplePosW = posW + mul(TBN, kernel[i]) * radius
//       sampleClip = mul(viewProjection, samplePosW)        ← 행렬곱 1
//       depth      = gDepthTex.Sample(sampleUV)             ← 텍스처 페치
//       scenePosW  = mul(inverseViewProjection, ...)        ← 행렬곱 2
//       occlusion += step(...) * rangeCheck
//
// 전 해상도에서 픽셀마다 페치 64회 + 4x4 행렬곱 128회다. 그리고 결과를
// 흐리지 않는다 — 픽셀마다 커널을 무작위로 회전시켜 놓고 디노이즈가 없으니
// 그 잡음이 그대로 화면에 남는다.
//
// ── 다시 쓰면서 바꾸는 것 ──
//
//   1. 월드 왕복을 없앤다. 전부 뷰 공간에서 하고, 표본은 화면 공간에서
//      행진해 얻는다 — 표본 UV가 2D 덧셈 하나로 나오므로 표본당 행렬곱이
//      0이 된다. 깊이에서 뷰 위치를 되돌리는 것만 남는다.
//
//   2. 반해상도(1/2)에서 계산한다. AO는 저주파라 반해상도로 잃는 것이
//      거의 없고, 픽셀 수가 1/4이 된다.
//
//   3. 가시성 비트마스크로 센다. 방향마다 32비트 마스크를 두고 표본이
//      가리는 각도 구간의 비트를 세운 뒤 countbits로 한 번에 센다.
//      가중치를 누적하는 방식과 달리 같은 각도를 두 번 세지 않는다 —
//      겹쳐 있는 가림막이 AO를 과대평가하는 것이 이 방식으로 사라진다.
//
//   4. 디노이즈를 넣는다. 방향을 픽셀·프레임마다 돌려 잡음을 만들고
//      교차 양방향 필터로 걷어낸다. 잡음을 만들고 걷어내는 쪽이, 잡음을
//      안 만들려고 표본을 늘리는 쪽보다 싸다.
//
// 표본 수 비교(전 해상도 픽셀 하나 기준):
//   기존  페치 64 · 행렬곱 128
//   신규  페치 (2방향 x 8스텝 x 양쪽 2) / 4 = 8 · 행렬곱 0
//
// ★ 처음에는 '페치 4'라고 적었다. 양쪽을 다 본다는 것(x2)을 계산에서
//   빼먹은 탓이다. 계산을 근거로 삼지 말자고 적어 두고도 그 계산이
//   틀렸으니, 실측이 아니면 적지 않는 편이 낫다는 쪽에 무게가 더 실린다.
//
// ── 단계 ──
//
//   1. 반해상도 AO 컴퓨트 + 자가 검증(알려진 배치로 단정)
//   2. 디노이즈·업샘플
//   3. 실제 씬 연결 + 기존 SSAO와 시간 비교
//
// ── 3단계 실측 (dx12.ssaoscale) ──
//
//   해상도       신규(반해상도)   참조(커널 64, 전 해상도)   배수
//    640x360        0.026 ms            0.101 ms            4.0
//   1280x720        0.084 ms            0.367 ms            4.4
//   1920x1080       0.196 ms            0.810 ms            4.1
//
//   ★ 4배쯤이다. 계산상 페치 비는 8배(픽셀 1/4 x 페치 64/32)인데 실측은
//     그 절반이다. 새 경로가 표본마다 asin 둘과 비트 연산을 더 하고,
//     참조 경로는 화면 밖·범위 밖 표본을 continue로 일찍 버려 페치를 다
//     쓰지 않기 때문으로 보인다. '왜 8이 아니라 4인가'는 더 파지 않았다 —
//     4배면 이 슬라이스의 목적에는 충분하고, 더 파려면 표본별 페치 수를
//     따로 세는 계측을 넣어야 한다.
//
//   ★ 배수가 세 해상도에서 4.0~4.4로 안정적이다. 한 해상도에서만 쟀으면
//     '그 해상도에서만 그렇다'와 구분되지 않았을 것이다.
//
//   실제 DX11 패스와 직접 재지 않았다. 그러면 API·드라이버·프레임 구조
//   차이가 수에 섞여 알고리즘 덕인지 DX12 덕인지 알 수 없다. 같은 디바이스·
//   같은 입력 위에 옛 알고리즘을 그대로 올려 두고 잰다.
//
// ── 1·2단계 실측 (dx12.ssao, 256x256 → 반해상도 128x128) ──
//
//   평평한 곳 AO 0.969 · 계단 안쪽 0.373 · 필터가 이웃 차이를 51.7% 줄임
//
// ★ 첫 실행은 평평한 곳이 0.590으로 나왔다. 비트마스크를 법선 기준 0~pi에
//   걸어 둔 탓이다 — 같은 평면 위의 이웃 표본은 법선과 정확히 pi/2를
//   이루므로 그 위쪽 절반(16비트)이 통째로 켜져 가림 0.5가 된다.
//   실측 1-0.590 = 0.41이 그 계산과 맞아떨어져 원인이 한 번에 잡혔다.
//
//   반구는 접평면 기준 0~pi/2다. 고도(asin)로 재면 같은 평면 위의 표본은
//   고도 0, 뒷면은 음수라 구간이 [0,0]으로 접혀 비트가 0이 된다 —
//   '평면은 자기 자신을 가리지 않는다'가 식에서 저절로 나온다.
//
//   이 배치를 고른 이유가 여기서 값을 했다. 평평한 곳을 단정하지 않았으면
//   계단이 어두워진 것만 보고 통과시켰을 것이고, 화면 전체가 반쯤 탁한 채로
//   씬에 붙었을 것이다.
//
// SSGI·Forward+에서 배운 규율을 그대로 쓴다: 셰이더는 부르는 자리가 생겨야
// 오류가 드러나므로 자가 검증을 먼저, 리드백 없이는 '도는 것처럼 보이는'
// 상태를 구분할 수 없으므로 진단을 처음부터, 상수는 실측으로.
class EnhancedSSAOPass : public EnhancedRenderPass
{
public:
    /// 계산 해상도의 분모. AO는 저주파라 반해상도에서 잃는 것이 거의 없다.
    /// 넷으로 나누면 얇은 물체의 접촉 그림자가 뭉개지기 시작한다 —
    /// 바꿀 근거가 생기면 실측으로 바꾼다.
    static constexpr uint32_t kResolutionDivisor = 2;

    /// 한 픽셀이 도는 방향 수. 적게 돌고 디노이즈로 메우는 쪽을 택했다.
    /// 방향은 픽셀 위치와 프레임 번호로 돌려 이웃끼리 다른 각을 보게 한다.
    static constexpr uint32_t kDirectionsPerPixel = 2;

    /// 방향 하나가 걷는 발자국 수. 등비로 늘려 가까운 곳을 촘촘히 본다 —
    /// 접촉 그림자가 AO에서 눈에 띄는 부분이고 그건 가까운 곳에서 나온다.
    static constexpr uint32_t kStepsPerDirection = 8;

    /// 반구를 나눈 비트 수. 32비트 정수 하나에 담기는 것이 상한이자
    /// countbits 한 번으로 세지는 크기다.
    static constexpr uint32_t kBitmaskBits = 32;

    /// x = AO(0~1) · y = 뷰 깊이.
    ///
    /// 깊이를 함께 담는 이유는 디노이즈가 그것을 필요로 하기 때문이다.
    /// 따로 두면 필터가 깊이 텍스처를 다시 읽고 뷰 공간으로 되돌려야 하는데,
    /// 그건 이미 여기서 한 계산을 한 번 더 하는 것이다.
    static constexpr RHIFormat kAOFormat = RHIFormat::RG16Float;

    /// 실측으로 정할 상수들. 지금 값은 출발점이고 근거가 아니다 —
    /// 근거가 생기는 대로 이 주석에 실측을 적는다.
    struct Tuning
    {
        /// 가림을 볼 최대 거리(뷰 공간 단위). 크면 넓은 그늘, 작으면
        /// 접촉 그림자만 남는다.
        float radius{ 0.5f };

        /// 표면 두께 가정. 화면 공간에서는 물체 뒤쪽을 볼 수 없어서,
        /// 깊이가 크게 다른 표본을 '무한히 두꺼운 가림막'으로 보면
        /// 배경이 통째로 가려진다. 그 깊이를 이 값으로 자른다.
        float thickness{ 0.25f };

        /// 결과에 거는 세기. 1이 물리적으로 맞는 값이고, 그림이 약하다고
        /// 느껴 올리는 것은 화가의 선택이라 기본값은 1로 둔다.
        float intensity{ 1.f };

        /// 디노이즈의 깊이 민감도. 작을수록 경계를 잘 지키고 잡음이 남는다.
        float filterDepthSigma{ 0.05f };
    };

    void SetTuning(const Tuning& tuning) { m_tuning = tuning; }
    const Tuning& GetTuning() const { return m_tuning; }

    const char* GetName() const override { return "SSAO"; }

    bool Initialize(const EnhancedFrameContext& context, std::string& outError) override;
    bool PrepareFrame(const EnhancedFrameContext& context, std::string& outError) override;
    void Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context) override;
    void Shutdown() override;

    struct Inputs
    {
        RGHandle depth;    // GBuffer 깊이
        RGHandle normal;   // GBuffer 노멀 — 반구의 축
    };

    void SetInputs(const Inputs& inputs) { m_inputs = inputs; }

    /// 결과 소비자가 아직 없는 이식 단계에서만 최종 필터 패스를 side effect로
    /// 살려 둔다. 정상 라이브 경로에서는 SSGI가 AO를 읽으므로 false가 맞다.
    void SetKeepAlive(bool keepAlive) { m_keepAlive = keepAlive; }

    /// 디노이즈까지 끝난 반해상도 AO. 조명 패스가 이것을 업샘플해 읽는다.
    RGHandle GetOutput() const { return m_output; }

    /// 필터를 거치기 전의 날 것. 필터가 실제로 무언가를 했는지 대조하는 데
    /// 쓴다 — 필터 전후가 같으면 필터가 죽은 것이고, 그것은 결과만 봐서는
    /// 알 수 없다(SSGI 필터 스윕에서 전 구간 0.0%가 나왔던 일이 그 예다).
    RGHandle GetRawOutput() const { return m_rawOutput; }

    /// 프레임 번호. 방향 회전에 쓴다 — 고정하면 잡음이 화면에 박혀서
    /// 디노이즈가 지우지 못한다.
    void SetFrameIndex(uint32_t index) { m_frameIndex = index; }

    /// 참조 경로(기존 반구 커널 64개)로 계산한다.
    ///
    /// 성능 비교의 기준선이다. 실제 DX11 패스와 재면 API 차이가 수에 섞여
    /// '무엇 때문에 빠른가'를 알 수 없으므로, 같은 하네스 위에 옛 알고리즘을
    /// 그대로 올려 두고 그것과 잰다 — Forward+에서 쓴 것과 같은 방법이다.
    ///
    /// 참조 경로는 전 해상도로 돈다. 반해상도는 새 방식이 가져가는 이득의
    /// 일부이므로, 기준선에까지 적용하면 그 이득이 수에서 사라진다.
    void SetUseReferencePath(bool use) { m_useReferencePath = use; }
    bool IsUsingReferencePath() const { return m_useReferencePath; }

private:
    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    bool CreatePipelines(const EnhancedFrameContext& context, std::string& outError);

    Inputs   m_inputs{};
    RGHandle m_output;
    RGHandle m_rawOutput;

    Tuning   m_tuning{};
    uint32_t m_frameIndex{ 0 };
    bool     m_useReferencePath{ false };
    bool     m_keepAlive{ false };

    uint32_t m_width{ 0 };    // 반해상도 폭
    uint32_t m_height{ 0 };

    RHIPipelineHandle m_aoPSO;

    // 참조 경로(반구 커널 64개, 전 해상도) PSO. 성능 비교의 기준선이다.
    RHIPipelineHandle m_referencePSO;
    RHIPipelineHandle m_filterPSO;
};

#endif
