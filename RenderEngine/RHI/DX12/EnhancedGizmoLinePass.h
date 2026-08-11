#pragma once
#include "../RHIFormat.h"
#ifndef DYNAMICCPP_EXPORTS
#include <cstdint>
#include <vector>
#include <wrl/client.h>
#include <DirectXCollision.h>

#include "EnhancedRenderPass.h"

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

    /// 라인 정점. DX11 쪽 LineVertex와 같은 배치(POSITION float3 + COLOR float4).
    struct Vertex
    {
        Mathf::Vector3 position{};
        Mathf::Color4  color{};
    };

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
    // 그린다. 도형 생성이 패스 소속인 이유: 이 수식이 DX11 원본의 이식이라
    // 흩어 놓으면 어느 쪽이 기준선인지 알 수 없게 된다.
    void ResetLines() { m_vertices.clear(); }

    void AddLine(const Mathf::Vector3& p0, const Mathf::Vector3& p1,
        const Mathf::Color4& color);
    void AddWireCircle(const Mathf::Vector3& center, float radius,
        const Mathf::Vector3& up, const Mathf::Color4& color);

    /// 방향광 기즈모 — 9세그먼트 원 + 세그먼트 시작점마다 방향 선(길이
    /// 반지름x3). DX11 DrawWireCircleAndLines의 이식.
    void AddWireCircleWithDirectionLines(const Mathf::Vector3& center, float radius,
        const Mathf::Vector3& up, const Mathf::Vector3& direction,
        const Mathf::Color4& color);
    void AddWireSphere(const Mathf::Vector3& center, float radius,
        const Mathf::Color4& color);
    void AddWireBox(const Mathf::Matrix& transform, const Mathf::Vector3& extents,
        const Mathf::Color4& color);
    void AddWireCapsule(const Mathf::Matrix& transform, float radius, float height,
        const Mathf::Color4& color);
    void AddWireCone(const Mathf::Vector3& apex, const Mathf::Vector3& direction,
        float height, float outerConeAngleDegrees, const Mathf::Color4& color);
    void AddBoundingFrustum(const DirectX::BoundingFrustum& frustum,
        const Mathf::Color4& color);

    /// 한 드로우로 나가는지 보는 수. DX11은 도형 수만큼 드로우가 나갔고,
    /// 그 차이는 그림으로는 절대 드러나지 않는다.
    uint32_t GetLastVertexCount() const { return m_lastVertexCount; }
    uint32_t GetLastDrawCount() const { return m_lastDrawCount; }

private:
    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    bool CreatePipelines(const EnhancedFrameContext& context, std::string& outError);

    Inputs   m_inputs{};
    RHIFormat m_outputFormat{ kOutputFormat };
    RGHandle m_output;
    bool     m_keepAlive{ false };

    std::vector<Vertex> m_vertices;

    // 프레임 밀봉 값(3-2). Record가 살아 있는 카메라를 읽지 않는다.
    Mathf::Matrix  m_viewProjection{};
    Mathf::Vector4 m_eyePosition{};

    uint32_t m_lastVertexCount{ 0 };
    uint32_t m_lastDrawCount{ 0 };

    uint32_t m_width{ 0 };
    uint32_t m_height{ 0 };

    RHIPipelineHandle m_pso;
};

#endif
