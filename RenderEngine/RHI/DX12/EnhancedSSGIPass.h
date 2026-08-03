#pragma once
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

    static constexpr DXGI_FORMAT kGIFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    static constexpr DXGI_FORMAT kHiZFormat = DXGI_FORMAT_R32_FLOAT;

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
        RGHandle lighting;   // 직접광 결과 — 간접광의 광원이 된다
    };

    void SetInputs(const Inputs& inputs) { m_inputs = inputs; }

    /// 합성까지 끝난 결과. 뒤 패스가 읽는다.
    RGHandle GetOutput() const { return m_output; }

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
    RGHandle m_output;

    // Hi-Z 피라미드. 밉마다 UAV가 필요해 핸들을 따로 든다.
    std::array<RGHandle, kMaxHiZMips> m_hiZMips{};
    uint32_t m_hiZMipCount{ 0 };

    // 히스토리는 그래프의 transient가 아니다 — 프레임을 넘겨 살아야 한다.
    std::array<ComPtr<ID3D12Resource>, kHistoryCount> m_history;
    uint32_t m_historyIndex{ 0 };
    bool     m_historyValid{ false };

    // 재투영에 쓸 지난 프레임 뷰·투영.
    Mathf::Matrix m_previousViewProjection{};
    bool          m_hasPreviousFrame{ false };

    uint32_t m_giWidth{ 0 };
    uint32_t m_giHeight{ 0 };

    uint32_t m_lastAccumFrames{ 0 };
    float    m_lastRejectRatio{ 0.f };
    uint32_t m_frameIndex{ 0 };

    ID3D12PipelineState* m_hiZBuildPSO{ nullptr };
    ID3D12PipelineState* m_tracePSO{ nullptr };
    ID3D12PipelineState* m_resolvePSO{ nullptr };   // 재투영 + 누적
    ID3D12PipelineState* m_filterPSO{ nullptr };    // bilateral
    ID3D12PipelineState* m_compositePSO{ nullptr }; // 업샘플 + 합성
    ID3D12RootSignature* m_rootSignature{ nullptr };
};

#endif
