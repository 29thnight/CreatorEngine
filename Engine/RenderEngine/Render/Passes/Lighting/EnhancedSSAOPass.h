#pragma once
#include "../../../RHI/RHIFormat.h"
#include <cstdint>
#include <wrl/client.h>

#include "../../Graph/EnhancedRenderPass.h"

// Half-resolution screen-space AO using signed visibility sectors.
// The angular average is an approximation, not cosine-weighted GTAO.
// RG16F output stores visibility and view-space depth for the spatial filter.
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

        /// Visibility reduction multiplier; zero disables occlusion.
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
        RGHandle normal;   // Encoded world-space GBuffer normal (normal * 0.5 + 0.5)
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

    /// Sample rotation seed. The current filter is spatial; it has no history.
    void SetFrameIndex(uint32_t index) { m_frameIndex = index; }


private:
    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    bool CreatePipelines(const EnhancedFrameContext& context, std::string& outError);

    Inputs   m_inputs{};
    RGHandle m_output;
    RGHandle m_rawOutput;

    Tuning   m_tuning{};
    uint32_t m_frameIndex{ 0 };
    bool     m_keepAlive{ false };

    uint32_t m_width{ 0 };    // 반해상도 폭
    uint32_t m_height{ 0 };

    RHIPipelineHandle m_aoPSO;

    RHIPipelineHandle m_filterPSO;
};

