#pragma once
#include "../../../RHI/RHIFormat.h"
#ifndef DYNAMICCPP_EXPORTS
#include <array>
#include <cstdint>

#include "../../Graph/EnhancedRenderPass.h"

// 포스트 체인 (PHASE 3-6, 신규 작성).
//
// ── 기존 DX11 포스트 체인이 하는 일과 그 값 ──
//
// 다섯 패스가 줄지어 돈다:
//
//   Bloom → AA → ToneMap → Vignette → ColorGrading
//
// 각각이 전체 화면을 읽고 전체 화면을 쓴다. 1920x1080 RGBA16F가 한 장에
// 약 16MB이므로, 다섯 단계면 읽기·쓰기 왕복 10회 = 약 160MB의 대역폭이
// 화면 한 장에 든다. 이 중 넷은 픽셀 하나만 보고 끝나는 연산인데도
// 그때마다 화면을 통째로 오간다.
//
// ── 다시 쓰면서 바꾸는 것 ──
//
//   1. 픽셀 국소 연산을 한 패스로 합친다.
//
//      블룸 합성 · 톤맵 · 비네트 · 컬러그레이딩은 전부 '그 픽셀 하나만
//      보고 답이 나오는' 연산이다. 이웃을 안 보므로 순서대로 합쳐도 결과가
//      같고, 화면 왕복이 넷에서 하나가 된다. 이 합친 패스를 Uber라 부른다.
//
//      Bloom과 FXAA는 못 합친다 — 둘 다 이웃을 본다.
//
//   2. FXAA를 톤맵 뒤로 옮긴다.
//
//      ★ 이건 성능이 아니라 정확성 문제다. FXAA는 휘도 대비로 경계를
//        찾는데, 기존 순서에서는 톤맵 전(HDR)에서 돈다. HDR에서는 밝은
//        곳의 대비가 실제 화면보다 훨씬 크게 나와서, 눈에 보이지도 않는
//        경계를 흐리고 정작 보이는 경계는 놓친다. LDR에서 도는 것이 맞다.
//
//   3. 블룸은 저해상도 체인으로 만든다.
//
//      다운샘플 체인을 만들고 업샘플하며 더한다. 넓은 번짐을 큰 반경의
//      가우시안으로 만들면 표본이 폭발하는데, 저해상도에서 작은 반경으로
//      같은 넓이를 얻을 수 있다.
//
// ── 실측 (dx12.postscale) ──
//
// 픽셀 국소 연산 넷을 합친 것과, 그것을 옛 방식대로 넷으로 나눈 것:
//
//   해상도       합친 것    나눈 것(4패스)   배수
//    640x360    0.004 ms      0.018 ms      4.50
//   1280x720    0.012 ms      0.083 ms      6.75
//   1920x1080   0.024 ms      0.146 ms      6.22
//
//   ★ 6배쯤이다. 왕복 횟수 비(4:1)보다 크게 나왔는데, 나눈 쪽은 중간
//     버퍼를 세 장 더 만들고 그때마다 디스패치·배리어·디스크립터를 다시
//     세우기 때문으로 보인다. 왕복만이 비용의 전부는 아니었다.
//
//   ★ 640x360에서 배수가 낮은 것(4.50)은 그 크기에서는 고정 비용
//     (디스패치 자체)이 상대적으로 커서다. 해상도가 오르면 대역폭 차이가
//     드러나 6배대로 올라간다.
//
//   참조 경로는 같은 셰이더에 플래그만 하나씩 켜서 네 번 돌린다 —
//   갈리는 것이 화면 왕복 횟수 하나뿐이라야 나온 수가 무엇을 뜻하는지
//   분명해진다. 실제 DX11 체인과 재면 API 차이가 수에 섞인다.
//
// 실제 씬(dx12.scene, 256x256):
//   BloomThreshold 0.0020 · BloomDown(x4) 0.0092 · BloomUp(x4) 0.0031 ·
//   Uber 0.0031 · FXAA 0.0031 ms
//
// ── 단계 ──
//
//   1. 블룸 체인(다운·업샘플) + Uber + FXAA + 자가 검증
//   2. 실제 씬 연결 + 기존 체인과 시간 비교
//
// ── 톤매퍼 실측 (dx12.post) ──
//
// 밝고 채도 높은 빨강 (4,0,0)을 넣으면:
//
//   ACES (0.608 0.000 0.000) — 채도 1.000
//   AgX  (0.624 0.341 0.341) — 채도 0.453
//
// ACES는 채널마다 따로 곡선을 먹이므로 R만 포화하고 G·B가 0에 남는다.
// 화면에서는 밝은 색이 형광빛으로 뜨고 색상이 밀린다.
// AgX는 곡선 전에 채널을 섞으므로 G·B가 같이 올라가 흰색으로 수렴한다 —
// 필름이 하던 일이고, 밝은 부분에서 색이 무너지지 않는 이유다.
//
// SSGI·Forward+·SSAO에서 배운 규율을 그대로 쓴다: 자가 검증을 먼저(셰이더는
// 부르는 자리가 생겨야 오류가 드러난다), 결과를 읽는 패스를 반드시(아무도
// 안 읽으면 그래프가 걷어내고 '0 ms'가 '빠른 것'으로 읽힌다), 상수는 실측으로.
class EnhancedPostChainPass : public EnhancedRenderPass
{
public:
    /// 블룸 체인의 최대 단수. 1080p에서 여섯이면 가장 거친 밉이 약 30x17이라
    /// 화면 폭의 절반을 덮는 번짐이 나온다. 더 내려가도 눈에 띄는 변화가
    /// 없고 디스패치 수만 는다 — 바꿀 근거가 생기면 실측으로 바꾼다.
    static constexpr uint32_t kMaxBloomMips = 6;

    /// 체인이 시작하는 해상도의 분모. 블룸은 애초에 저주파라 절반에서
    /// 시작해도 잃는 것이 없다.
    static constexpr uint32_t kBloomStartDivisor = 2;

    static constexpr RHIFormat kHDRFormat = RHIFormat::RGBA16Float;

    /// 최종 출력. 톤맵을 거친 뒤라 LDR이고, FXAA가 이 위에서 돈다.
    static constexpr RHIFormat kLDRFormat = RHIFormat::RGBA8Unorm;

    /// 톤맵 곡선.
    enum class ToneMapper : uint32_t
    {
        ACES = 0,   ///< 채널별 곡선. 밝고 채도 높은 색에서 색상이 밀린다.
        AgX  = 1,   ///< 채널을 섞은 뒤 로그 공간에서 곡선. 흰색으로 수렴한다.
    };

    /// 기존 설정과 같은 이름·같은 뜻으로 둔다. 값이 다르면 '새 체인이
    /// 다르게 보인다'가 이식 실수인지 설정 차이인지 구분되지 않는다.
    struct Tuning
    {
        // ── 블룸 ──
        bool  bloomEnabled{ true };
        /// 이 밝기를 넘는 부분만 번진다.
        float bloomThreshold{ 1.f };
        /// 임계 근처를 부드럽게 넘긴다. 0이면 경계에서 딱 끊겨 계단이 보인다.
        float bloomKnee{ 0.5f };
        float bloomIntensity{ 0.05f };

        // ── 톤맵 ──
        bool  toneMapEnabled{ true };

        /// 어느 곡선을 쓸지.
        ///
        /// ACES는 채널마다 따로 곡선을 먹인다. 그래서 채도가 높은 밝은 색이
        /// 들어오면 한 채널만 먼저 포화하고 나머지가 따라가지 못해 색이
        /// 밀린다 — 밝은 주황이 노랗게, 밝은 빨강이 형광빛으로 뜨는 것이
        /// 그 증상이다.
        ///
        /// AgX는 먼저 채널을 섞는 행렬을 통과시킨 뒤 로그 공간에서 곡선을
        /// 먹인다. 그래서 밝아질수록 자연스럽게 흰색으로 수렴하고 색상이
        /// 유지된다. 필름이 하던 일에 가깝다.
        ///
        /// 둘 다 두는 이유는 취향이 갈리기 때문이다. AgX는 전반적으로
        /// 대비가 낮게 느껴져서, 이미 ACES에 맞춰 그린 씬이 있으면 그쪽이
        /// 나을 수 있다. 기본은 AgX로 둔다.
        ToneMapper toneMapper{ ToneMapper::AgX };
        /// 노출. 기존은 조리개·셔터·ISO로 계산하는데, 그 세 값이 하는 일은
        /// 결국 이 배율 하나다. 자동 노출이 붙기 전까지는 이것만 둔다.
        /// 기본 0.7 — 1.0은 puresky 계열 HDR에서 하늘이 통째로 날아갔다
        /// (2026-08-07 그림 비교로 정함).
        float exposure{ 0.7f };

        // ── 비네트 ──
        bool  vignetteEnabled{ true };
        float vignetteRadius{ 0.75f };
        float vignetteSoftness{ 0.5f };
        /// 감광의 상한. smoothstep 결과 v가 0(완전 검정)까지 떨어져도
        /// 실제 감광은 lerp(1, v, intensity)로 묶인다 — 0.3이면 코너가
        /// 최대 30% 어두워진다. 기존(=1.0 상당)은 가장자리 중간에서 이미
        /// 84% 감광이라 터널처럼 보였다(2026-08-07 그림 비교로 정함).
        float vignetteIntensity{ 0.3f };

        // ── 컬러 그레이딩 ──
        /// LUT 텍스처가 붙기 전까지는 채도·대비만 둔다. LUT는 텍스처
        /// 파이프라인이 필요해 이 슬라이스 범위 밖이다.
        bool  gradingEnabled{ true };
        float saturation{ 1.f };
        float contrast{ 1.f };

        // ── FXAA ──
        bool  fxaaEnabled{ true };
        float fxaaBias{ 0.688f };
        float fxaaBiasMin{ 0.021f };
        float fxaaSpanMax{ 8.f };
    };

    void SetTuning(const Tuning& tuning) { m_tuning = tuning; }
    const Tuning& GetTuning() const { return m_tuning; }

    const char* GetName() const override { return "PostChain"; }

    bool Initialize(const EnhancedFrameContext& context, std::string& outError) override;
    bool PrepareFrame(const EnhancedFrameContext& context, std::string& outError) override;
    void Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context) override;
    void Shutdown() override;

    struct Inputs
    {
        RGHandle color;   // 라이팅(+SSGI) 결과 — HDR
    };

    void SetInputs(const Inputs& inputs) { m_inputs = inputs; }

    /// 참조 경로: 픽셀 국소 연산 넷을 합치지 않고 옛 방식대로 네 패스로
    /// 나눠 돈다(블룸 합성 → 톤맵 → 비네트 → 그레이딩).
    ///
    /// 성능 비교의 기준선이다. 실제 DX11 체인과 재면 API 차이가 수에 섞여
    /// '무엇 때문에 빠른가'를 알 수 없으므로, 같은 셰이더에 플래그만 하나씩
    /// 켜서 네 번 돌린다 — 갈리는 것이 화면 왕복 횟수 하나뿐이라야
    /// 그 수가 무엇을 뜻하는지 분명해진다.
    void SetUseSeparatePasses(bool use) { m_useSeparatePasses = use; }
    bool IsUsingSeparatePasses() const { return m_useSeparatePasses; }

    /// 최종 LDR. FXAA까지 끝난 것이다.
    RGHandle GetOutput() const { return m_output; }

    /// FXAA 전의 톤맵 결과. FXAA가 실제로 무언가 했는지 대조하는 데 쓴다 —
    /// 전후가 같으면 FXAA가 죽은 것이고, 최종 그림만 봐서는 알 수 없다.
    RGHandle GetPreAAOutput() const { return m_toneMapped; }

    /// 블룸 체인의 가장 거친 밉. 블룸이 실제로 만들어졌는지 본다.
    RGHandle GetBloomOutput() const { return m_bloomChainValid ? m_bloomMips[0] : RGHandle{}; }

    uint32_t GetBloomMipCount() const { return m_bloomMipCount; }

private:
    bool CreatePipelines(const EnhancedFrameContext& context, std::string& outError);

    Inputs   m_inputs{};
    RGHandle m_output;
    RGHandle m_toneMapped;

    std::array<RGHandle, kMaxBloomMips> m_bloomMips{};
    uint32_t m_bloomMipCount{ 0 };
    bool     m_bloomChainValid{ false };

    Tuning   m_tuning{};
    bool     m_useSeparatePasses{ false };

    uint32_t m_width{ 0 };
    uint32_t m_height{ 0 };

    RHIPipelineHandle m_thresholdPSO;
    RHIPipelineHandle m_downsamplePSO;
    RHIPipelineHandle m_upsamplePSO;
    RHIPipelineHandle m_uberPSO;
    RHIPipelineHandle m_uberHDRPSO;
    RHIPipelineHandle m_fxaaPSO;
};

#endif
