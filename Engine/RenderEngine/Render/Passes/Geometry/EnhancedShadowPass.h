#pragma once
#include "../../../RHI/RHIFormat.h"
#include "../../../../Utility_Framework/Foundation/StandardLibrary.h"
#include "../../Graph/ShadowMath.h"

#include "../../Graph/EnhancedRenderPass.h"

// 그림자 패스 (PHASE 3-6, 세 번째 패스).
//
// 방향광 하나에 대한 3단 캐스케이드. 이 패스는 캐스케이드를 정하고 맵을
// 선언·클리어하는 것까지만 맡는다. 캐스터는 재질 그래프 경로
// (material_graph::SceneHost::DeclareShadow)가 이 맵에 그린다 — 제품 장면의
// 모든 드로우가 그래프 재질이고, 예전의 표준 재질 캐스터 경로는 제거했다.
//
// 두 가지 설계 결정:
//
//   맵을 텍스처 배열로 둔다. 텍스처 셋보다 낫다 — 리소스가 하나라 배리어도
//   하나고(전이가 전체 서브리소스 단위다), 셰이더에서 캐스케이드 번호가
//   좌표의 일부라 분기 없이 고를 수 있다. 아틀라스(가로로 이어 붙이기)보다도
//   낫다 — 아틀라스는 UV를 다시 매핑해야 하고 경계에서 필터가 옆 캐스케이드를
//   물어 온다.
//
//   분할은 PSSM(로그·균등 혼합)으로 정한다. 저작 비율을 고정으로 두면 far가
//   바뀔 때마다 손봐야 하는데, 혼합식은 near/far에서 유도되므로 따라온다.
class EnhancedShadowPass : public EnhancedRenderPass
{
public:
    static constexpr uint32_t    kCascadeCount = kShadowCascadeCount;
    static constexpr uint32_t    kShadowMapSize = 2048;
    static constexpr RHIFormat kShadowFormat = RHIFormat::D32Float;

    // 로그 분할과 균등 분할의 혼합 비율. 1이면 순수 로그(가까운 쪽에 극단적으로
    // 몰린다), 0이면 균등(먼 쪽이 낭비된다). 0.75는 관행적인 절충이다.
    static constexpr float kSplitLambda = 0.75f;

    const char* GetName() const override { return "Shadow"; }

    bool Initialize(const EnhancedFrameContext& context, std::string& outError) override;
    // Caster bounds come from context.shadowDraws; every caster is a graph draw.
    bool PrepareGraphFrame(const EnhancedFrameContext& context, std::string& outError);
    // Declares and clears the cascade array. Graph casters draw into it afterward.
    void DeclareGraphTargets(EnhancedRenderGraph& graph, const EnhancedFrameContext& context);
    void Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context) override
    {
        DeclareGraphTargets(graph, context);
    }

    RGHandle GetShadowMap() const { return m_shadowMap; }

    // 라이팅 패스가 그대로 상수 버퍼에 얹는 형태로 준다.
    const EnhancedShadowData& GetShadowData() const { return m_shadowData; }

    // 그림자를 드리울 방향광을 찾았는가. 없으면 그림자 없이 도는 것이 정상이다.
    bool HasDirectionalLight() const { return m_hasDirectionalLight; }

    // 기본 편향. 캐스케이드별로는 반지름 비만큼 키워 쓴다.
    // Compatibility setter: old normalized depth at a 4-radius depth span.
    void SetBias(float bias) { m_baseBias = bias * (2.f * kShadowMapSize); }
    void SetBiasTexels(float bias) { m_baseBias = (std::max)(0.f, bias); }
    void SetShadowDistance(float distance) { m_shadowDistance = (std::max)(0.01f, distance); }
    float GetShadowDistance() const { return m_shadowDistance; }
    float GetCascadeBlendBand() const { return m_blendBand; }

    // 경사 비례 계수 — 최종 편향 = 캐스케이드 편향 x (1 + 계수 x tan(경사각)).
    // 0이면 상수 편향만 쓴다(자가 검증의 A/B 재료).
    void SetSlopeScale(float scale) { m_slopeScale = scale; }

    // 캐스케이드 경계 블렌딩 폭(분할 깊이 비율). 0이면 하드 스위치.
    void SetCascadeBlendBand(float band) { m_blendBand = band; }

    float GetBiasTexels() const { return m_baseBias; }
    float GetSlopeScale() const { return m_slopeScale; }

    // 그림자를 받는 셰이더 셋이 같은 판정으로 칠하는 디버그 보기.
    // GetShadowData().cameraForward.w 로 실려 간다.
    void SetDebugView(EnhancedShadowDebugView view) { m_debugView = view; }
    EnhancedShadowDebugView GetDebugView() const { return m_debugView; }

    // 그림자 가장자리 필터. 디버그 보기와 같은 성분에 실려 간다.
    void SetFilter(EnhancedShadowFilter filter) { m_filter = filter; }
    EnhancedShadowFilter GetFilter() const { return m_filter; }

    /// Cascade values describe the last PrepareGraphFrame.
    /// 길이는 월드 단위다 — 텍셀 폭을 장면의 물체 크기와 바로 대 보라는 것이다.
    struct CascadeStats
    {
        float splitDepth{ 0.f };   // 이 캐스케이드가 끝나는 뷰 깊이
        float radius{ 0.f };       // 덮는 구의 반지름
        float worldTexel{ 0.f };   // 그림자 맵 텍셀 하나가 덮는 폭
        float depthSpan{ 0.f };    // 광원 방향 깊이 범위
        float constantBias{ 0.f }; // 상수 편향(광원 방향). 경사면은 x(1 + 경사 계수 x tan)
    };

    struct DebugStats
    {
        bool          hasDirectionalLight{ false };
        uint32_t      lightIndex{ 0 };
        math::vector3 lightDirection{};
        float         shadowDistance{ 0.f };
        float         slopeScale{ 0.f };
        uint32_t      casterCandidates{ 0 }; // 그래프 캐스터 후보(셀 수 있는 CPU 입력)
        // Retained for the profiler archive layout. This pass owns no caster
        // visibility; graph shadow visibility counts stay on the GPU.
        bool          gpuVisibilityActive{ false };
        bool          visibilityCountsExact{ true };
        uint64_t      gpuSubmittedCandidates{ 0 };
        uint64_t      gpuSubmittedBins{ 0 };
        std::array<CascadeStats, kCascadeCount> cascades{};
    };

    DebugStats GetDebugStats() const;

private:
    // 한 캐스케이드가 덮는 범위. 컬링 판정에 중심과 반지름이 필요해서 행렬만
    // 들고 있지 않는다.
    struct Cascade
    {
        math::matrix4x4 lightViewProjection{};
        math::vector3   center{};
        float          radius{ 0.f };
        float          splitDepth{ 0.f };
        float          depthSpan{ 1.f };
    };

    void ComputeCascades(const EnhancedFrameContext& context);

    RGHandle m_shadowMap;
    std::array<Cascade, kCascadeCount>  m_cascades{};

    EnhancedShadowData m_shadowData{};
    math::vector3      m_lightDirection{ 0.f, -1.f, 0.f };
    bool               m_hasDirectionalLight{ false };
    float              m_baseBias{ 0.5f };
    float              m_shadowDistance{ shadow_math::kDefaultDistance };

    // 경사 비례 계수 2는 관행 범위(1~3)의 가운데다. tan은 셰이더에서 8로
    // 상한을 두므로 최악에도 편향이 기본의 17배를 넘지 않는다.
    float              m_slopeScale{ 2.f };
    float              m_blendBand{ 0.15f };
    EnhancedShadowDebugView m_debugView{ EnhancedShadowDebugView::Off };
    // 5x5 텐트가 기본이다. 하드웨어 2x2 한 표본은 텍셀이 화면 픽셀보다 촘촘한
    // 거리에서 가장자리가 지글거리고, 비스듬한 면에서 계단이 드러났다(2026-10-04
    // 렌더 디버그 보기로 확인).
    EnhancedShadowFilter m_filter{ EnhancedShadowFilter::Tent5x5 };
    uint32_t           m_lastCasterCandidates{ 0 };

    // 캐스터 경계 구. 광원 쪽 backOff가 캐스케이드 밖 가림막까지 덮도록 쓴다.
    std::vector<shadow_math::Sphere> m_casterBounds;
};
