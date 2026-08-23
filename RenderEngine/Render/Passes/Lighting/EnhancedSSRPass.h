#pragma once
#include "../../../RHI/RHIFormat.h"
#include <cstdint>

#include "../../Graph/EnhancedRenderPass.h"

// 스크린 스페이스 반사 패스 (PHASE 3-6, 미구현 패스 이식 3차).
//
// ── 기존 DX11 ScreenSpaceReflectionPass가 하는 일 ──
//
//   픽셀마다 시선을 법선으로 반사시켜 그 방향으로 월드 공간을 행진한다.
//   행진 지점을 화면에 투영해 깊이를 견주고, 광선이 표면 바로 뒤로
//   들어가면(0 < 차이 < 두께) 그 자리의 씬 색을 반사색으로 삼는다.
//
//   ★ 읽으면서 쓸 수 없으므로 씬 컬러를 통째로 복사한다. 그리고 히스토리
//     텍스처도 한 번 더 복사한다(화면 크기 CopyResource 2회).
//
//   ★ MRT 둘로 그린다 — 하나는 씬 컬러(제자리), 하나는 히스토리.
//
// ── 다시 쓰면서 바꾸는 것 ──
//
//   ★ 복사 둘이 다 사라진다.
//
//   씬 컬러 복사: 결과를 제자리에 쓰지 않고 새 transient에 쓴다. 출력이
//   입력 이미지만으로 정해지므로 다른 타깃에 써도 같은 픽셀이 나오고,
//   그러면 읽는 것과 쓰는 것이 갈려 복사가 필요 없다(SSS와 같은 처리).
//
//   히스토리 복사: 아래 ①대로 통째로 없앤다.
//
//   블렌드·깊이를 명시한다. DX11은 이 패스에서도 상태를 세우지 않고 앞
//   패스가 남긴 것에 얹혀 갔다(SSS·Decal과 같다).
//
// ── 원본에서 발견한 것 넷 ──
//
//   ①만 제거하고 ②③④는 그대로 보존한다. ①은 결과에 닿지 않는 것을
//   덜어내는 일이라 그림이 같지만, 나머지 셋은 고치면 그림이 바뀐다 —
//   기준선이 DX11이므로 여기서 고치면 픽셀 대조가 성립하지 않는다.
//
//   ① temporal 히스토리가 통째로 죽어 있다. ★ 제거한다.
//      prevSSR(t4)을 바인딩하지만 셰이더가 그것을 읽는 유일한 줄이 주석
//      처리돼 있다. 즉 히스토리 복사 → t4 바인딩 → RT1에 쓰기가 닫힌
//      고리를 이루고 아무것도 내놓지 않는다. 없애면 화면 크기 복사 하나와
//      렌더 타깃 하나가 함께 사라진다.
//      (RenderPassData가 들고 있던 DX11 쪽 원본 m_SSRPrevTexture는 T3에서
//       걷어냈다 — 만들고 놓기만 할 뿐 읽는 코드가 없었다.)
//
//   ② 하늘 조기 탈출에 return이 없다.
//      `if (depth >= 1.0f) Out.output = color;` 뒤에 return이 없어 아래로
//      계속 흐르고 마지막 줄이 덮어쓴다. 하늘 픽셀도 광선 행진을 20번
//      끝까지 돈다 — 죽은 분기이면서 낭비다.
//
//   ③ reflectFactor와 edgeFade가 계산만 되고 쓰이지 않는다.
//      거칠기 항 (1-rough)와 화면 가장자리 페이드를 구해 놓고 최종 줄은
//      `lerp(color, color + reflected, metallic)`이라 둘 다 안 쓴다.
//      결과적으로 거칠기가 무시되고, 가장자리에서 반사가 뚝 끊긴다.
//
//   ④ screenSize를 C++이 채우지 않는다.
//      CBData를 초기화 없이 선언하는데 Mathf::Vector2가 SimpleMath라
//      기본 생성자가 (0,0)을 넣는다. 셰이더는 그것으로 비트플래그를
//      짚으므로(int2(texCoord * screenSize)) 모든 픽셀이 텍셀 (0,0)의
//      플래그를 읽는다. 지형에 SSR을 걸지 않으려던 게이트가 화면
//      왼쪽 위 한 점에 전 화면이 걸리는 것으로 바뀌어 있다.
class EnhancedSSRPass : public EnhancedRenderPass
{
public:
    static constexpr RHIFormat kOutputFormat = RHIFormat::RGBA16Float;

    /// DX11 기본값 그대로. 그림의 기준선이 이 값이다.
    struct Tuning
    {
        float    stepSize{ 0.114f };
        float    maxThickness{ 0.00416f };
        int32_t  maxRayCount{ 20 };
    };

    const char* GetName() const override { return "SSR"; }

    bool Initialize(const EnhancedFrameContext& context, std::string& outError) override;
    bool PrepareFrame(const EnhancedFrameContext& context, std::string& outError) override;
    void Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context) override;
    void Shutdown() override;

    struct Inputs
    {
        /// 반사를 얹을 그림(라이팅 결과). 반사색을 뜨는 원본이기도 하다.
        RGHandle color;

        RGHandle depth;
        RGHandle metalRough;
        RGHandle normal;

        /// 오브젝트 플래그. 지형을 걸러 내려던 것인데 ④ 때문에 그렇게 돌지 않는다.
        RGHandle bitmask;
    };

    void SetInputs(const Inputs& inputs) { m_inputs = inputs; }
    void SetTuning(const Tuning& tuning) { m_tuning = tuning; }
    const Tuning& GetTuning() const { return m_tuning; }

    /// DX11 기본값은 꺼짐이다. 꺼져 있으면 아무것도 선언하지 않고
    /// GetOutput()이 입력을 그대로 돌려준다 — 뒤 패스가 분기 없이 이어진다.
    void SetEnabled(bool enabled) { m_enabled = enabled; }
    bool IsEnabled() const { return m_enabled; }

    /// 광선 잡음의 씨앗. DX11이 총 경과 초를 넘긴다.
    ///
    /// 프레임마다 달라지므로 같은 화면도 매 프레임 다른 반사가 나온다.
    /// 원래는 히스토리로 누적해 그 흔들림을 재우려던 것으로 보이는데,
    /// ①대로 히스토리가 죽어 있어 흔들림만 남았다.
    void SetTime(float seconds) { m_time = seconds; }

    /// 켜져 있으면 반사가 얹힌 새 그림, 꺼져 있으면 입력 그대로.
    RGHandle GetOutput() const { return m_output; }

    void SetKeepAlive(bool keepAlive) { m_keepAlive = keepAlive; }

private:
    bool CreatePipelines(const EnhancedFrameContext& context, std::string& outError);

    Inputs   m_inputs{};
    Tuning   m_tuning{};
    RGHandle m_output;
    bool     m_enabled{ false };
    bool     m_keepAlive{ false };
    float    m_time{ 0.f };

    // 프레임 밀봉 값(3-2). Record가 스냅샷을 다시 읽지 않는다.
    Mathf::Matrix  m_inverseProjection{};
    Mathf::Matrix  m_inverseView{};
    Mathf::Matrix  m_viewProjection{};
    Mathf::Vector4 m_cameraPosition{};

    uint32_t m_width{ 0 };
    uint32_t m_height{ 0 };

    RHIPipelineHandle m_pso;
};

