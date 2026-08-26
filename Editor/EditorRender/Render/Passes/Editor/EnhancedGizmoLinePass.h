#pragma once
#include "RHI/RHIFormat.h"
#include <cstdint>
#include <vector>
#include <DirectXCollision.h>

#include "Render/Graph/EnhancedRenderPass.h"
#include "EnhancedGizmoSceneTypes.h"

// 기즈모 라인 패스 (PHASE 3-6, Gizmo 계열 2차 슬라이스).
//
// ── 기존 DX11 GizmoLinePass가 하는 일과 그 값 ──
//
//   와이어 서클·구·박스·캡슐·콘·프러스텀을 CPU에서 정점으로 만들어
//   LINELIST로 그린다. 선택 오브젝트의 라이트 기즈모, 디버그 모드의
//   콜라이더 와이어가 이것으로 나온다.
//
//   ★ 도형마다 DrawLines를 부른다. 그때마다 동적 버퍼를 Map하고 드로우가
//     한 번 나간다 — 캡슐 하나가 수직선 + 구 2 + 링 2로 드로우 12회다.
//     콜라이더가 수십 개면 드로우와 Map이 수백 회가 되고, 그 비용이 전부
//     CE 단계에 쌓인다. 3-2 실측이 지목한 병목이 바로 그 단계다.
//
// ── 다시 쓰면서 바꾸는 것 ──
//
//   프레임의 모든 선을 한 목록에 모아 업로드 한 번 + 드로우 한 번으로
//   낸다. 선은 전부 같은 파이프라인·같은 상수를 쓰므로 나눌 이유가
//   없다 — 색은 정점에 있다.
//
//   도형을 정점으로 푸는 수식은 DX11 그대로다(세그먼트 수까지). 픽셀
//   대조의 기준선이 그 모양이므로 여기서 도형을 다듬으면 대조가 흔들린다.
//
//   깊이는 DX11과 같이 안 본다 — 원본이 DSV를 바인딩하지 않으므로
//   기즈모는 물체 뒤에서도 보인다. 그것이 에디터 기즈모의 의도된 동작이다.
class EnhancedGizmoLinePass : public EnhancedRenderPass
{
public:
    static constexpr RHIFormat kOutputFormat = RHIFormat::RGBA16Float;

    /// 라인 정점. 타입의 정본은 Core의 EnhancedGizmoSceneTypes.h다(E4-3a) —
    /// GT 수집(ScriptBinder)이 패스를 몰라도 같은 배치를 쓰기 위해서다.
    using Vertex = EnhancedGizmoLineVertex;

    const char* GetName() const override { return "GizmoLine"; }

    bool Initialize(const EnhancedFrameContext& context, std::string& outError) override;
    bool PrepareFrame(const EnhancedFrameContext& context, std::string& outError) override;
    void Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context) override;
    void Shutdown() override;

    struct Inputs
    {
        /// 기즈모가 그 위에 얹힐 그림. 비어 있으면 투명 위에 그린다(자가 검증).
        RGHandle color;
    };

    /// 출력 색 포맷. Initialize보다 먼저 부른다(PSO가 이 값으로 만들어진다).
    ///
    /// 기본값은 HDR이다 — 자가 검증은 자체 transient에 그리기 때문이다.
    /// 상시 러너는 포스트 체인의 LDR 결과 위에 그리므로 그 포맷을 넘긴다:
    /// 입력이 있으면 그 텍스처에 직접 그리는 구조라(Declare 참조) PSO의
    /// RTV 포맷이 입력과 어긋나면 커맨드 리스트가 통째로 무효가 되고,
    /// 그 증상은 Close 실패(E_INVALIDARG)에 이은 디바이스 제거다(실측).
    void SetOutputFormat(RHIFormat format) { m_outputFormat = format; }
    void SetInputs(const Inputs& inputs) { m_inputs = inputs; }
    void SetKeepAlive(bool keepAlive) { m_keepAlive = keepAlive; }

    RGHandle GetOutput() const { return m_output; }

    // ── 선 쌓기 ──
    //
    // 프레임마다 ResetLines 후 Add*를 부르고, 그래프 실행이 그 목록을 한 번에
    // 그린다. 도형→선 수식의 집은 Core의 EnhancedGizmoLineCollector다(E4-3a) —
    // GT 수집이 패스를 인스턴스화하지 않기 위해서다. 아래 Add*는 자가 검증
    // 편의용 위임 래퍼이고, E5가 self-test를 옮기면 함께 걷을 수 있다.
    void ResetLines() { m_lines.Reset(); }
    void SetVertices(const std::vector<Vertex>& vertices) { m_lines.Set(vertices); }
    const std::vector<Vertex>& GetVertices() const { return m_lines.GetVertices(); }

    void AddLine(const math::vector3& p0, const math::vector3& p1,
        const math::color& color)
    {
        m_lines.AddLine(p0, p1, color);
    }
    void AddWireCircle(const math::vector3& center, float radius,
        const math::vector3& up, const math::color& color)
    {
        m_lines.AddWireCircle(center, radius, up, color);
    }
    void AddWireCircleWithDirectionLines(const math::vector3& center, float radius,
        const math::vector3& up, const math::vector3& direction,
        const math::color& color)
    {
        m_lines.AddWireCircleWithDirectionLines(center, radius, up, direction, color);
    }
    void AddWireSphere(const math::vector3& center, float radius,
        const math::color& color)
    {
        m_lines.AddWireSphere(center, radius, color);
    }
    void AddWireBox(const math::matrix4x4& transform, const math::vector3& extents,
        const math::color& color)
    {
        m_lines.AddWireBox(transform, extents, color);
    }
    void AddWireCapsule(const math::matrix4x4& transform, float radius, float height,
        const math::color& color)
    {
        m_lines.AddWireCapsule(transform, radius, height, color);
    }
    void AddWireCone(const math::vector3& apex, const math::vector3& direction,
        float height, float outerConeAngleDegrees, const math::color& color)
    {
        m_lines.AddWireCone(apex, direction, height, outerConeAngleDegrees, color);
    }
    void AddBoundingFrustum(const DirectX::BoundingFrustum& frustum,
        const math::color& color)
    {
        m_lines.AddBoundingFrustum(frustum, color);
    }

    /// 한 드로우로 나가는지 보는 수. DX11은 도형 수만큼 드로우가 나갔고,
    /// 그 차이는 그림으로는 절대 드러나지 않는다.
    uint32_t GetLastVertexCount() const { return m_lastVertexCount; }
    uint32_t GetLastDrawCount() const { return m_lastDrawCount; }

private:
    bool CreatePipelines(const EnhancedFrameContext& context, std::string& outError);

    Inputs   m_inputs{};
    RHIFormat m_outputFormat{ kOutputFormat };
    RGHandle m_output;
    bool     m_keepAlive{ false };

    EnhancedGizmoLineCollector m_lines;

    // 프레임 밀봉 값(3-2). Record가 살아 있는 카메라를 읽지 않는다.
    math::matrix4x4 m_viewProjection{};
    math::vector4   m_eyePosition{};

    uint32_t m_lastVertexCount{ 0 };
    uint32_t m_lastDrawCount{ 0 };

    uint32_t m_width{ 0 };
    uint32_t m_height{ 0 };

    RHIPipelineHandle m_pso;
};
