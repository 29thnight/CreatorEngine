#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include <array>
#include <cstdint>
#include <wrl/client.h>

#include "EnhancedRenderPass.h"

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
// 화면 왕복 비교(전 해상도 기준):
//   기존  10회 (다섯 패스 x 읽기·쓰기)
//   신규   4회 (Uber 읽기·쓰기 + FXAA 읽기·쓰기) + 저해상도 블룸 체인
//
// ★ 위 수는 계산이지 실측이 아니다. SSAO에서 계산으로 적은 수가 틀렸던 적이
//   있으므로(양쪽 표본을 빼먹어 '페치 4'라고 적었는데 실제로는 8이었다),
//   실측이 나오기 전까지 이 주석을 근거로 삼지 않는다.
//
// ── 단계 ──
//
//   1. 블룸 체인(다운·업샘플) + Uber + FXAA + 자가 검증
//   2. 실제 씬 연결 + 기존 체인과 시간 비교
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

    static constexpr DXGI_FORMAT kHDRFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

    /// 최종 출력. 톤맵을 거친 뒤라 LDR이고, FXAA가 이 위에서 돈다.
    static constexpr DXGI_FORMAT kLDRFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

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
        /// 노출. 기존은 조리개·셔터·ISO로 계산하는데, 그 세 값이 하는 일은
        /// 결국 이 배율 하나다. 자동 노출이 붙기 전까지는 이것만 둔다.
        float exposure{ 1.f };

        // ── 비네트 ──
        bool  vignetteEnabled{ true };
        float vignetteRadius{ 0.75f };
        float vignetteSoftness{ 0.5f };

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

    /// 최종 LDR. FXAA까지 끝난 것이다.
    RGHandle GetOutput() const { return m_output; }

    /// FXAA 전의 톤맵 결과. FXAA가 실제로 무언가 했는지 대조하는 데 쓴다 —
    /// 전후가 같으면 FXAA가 죽은 것이고, 최종 그림만 봐서는 알 수 없다.
    RGHandle GetPreAAOutput() const { return m_toneMapped; }

    /// 블룸 체인의 가장 거친 밉. 블룸이 실제로 만들어졌는지 본다.
    RGHandle GetBloomOutput() const { return m_bloomChainValid ? m_bloomMips[0] : RGHandle{}; }

    uint32_t GetBloomMipCount() const { return m_bloomMipCount; }

private:
    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    bool CreatePipelines(const EnhancedFrameContext& context, std::string& outError);

    Inputs   m_inputs{};
    RGHandle m_output;
    RGHandle m_toneMapped;

    std::array<RGHandle, kMaxBloomMips> m_bloomMips{};
    uint32_t m_bloomMipCount{ 0 };
    bool     m_bloomChainValid{ false };

    Tuning   m_tuning{};

    uint32_t m_width{ 0 };
    uint32_t m_height{ 0 };

    ID3D12PipelineState* m_thresholdPSO{ nullptr };
    ID3D12PipelineState* m_downsamplePSO{ nullptr };
    ID3D12PipelineState* m_upsamplePSO{ nullptr };
    ID3D12PipelineState* m_uberPSO{ nullptr };
    ID3D12PipelineState* m_fxaaPSO{ nullptr };
    ID3D12RootSignature* m_rootSignature{ nullptr };
};

#endif
