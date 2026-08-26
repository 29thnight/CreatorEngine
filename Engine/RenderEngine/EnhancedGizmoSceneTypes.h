#pragma once
#include "Core.Mathf.h"

#include <mathematics/matrix4x4.hpp>
#include <mathematics/vector3.hpp>

#include <DirectXCollision.h>
#include <cstddef>
#include <type_traits>
#include <vector>

class Texture;

// 기즈모 씬 데이터의 값 타입과 선 수집기 (E4-3a).
//
// 예전에는 이 타입들이 에디터 패스 소속(EnhancedGizmoIconPass::Icon /
// EnhancedGizmoLinePass::Vertex)이라, GT의 수집(ScriptBinder의
// EnhancedGizmoSceneBinding)이 에디터 패스를 선 수집기로 인스턴스화해야 했다.
// 수집은 Core, 표시와 조작은 Editor가 원칙이므로(EngineLayerSeparationPlan
// §1.2) 값 타입과 도형→선 수식을 Core로 올린다. 패스는 이 타입을 소비하고
// 자기 Add*는 수집기로 위임한다.
//
// ★ 도형→선 수식의 집은 여기 하나다. DX11 원본의 이식(세그먼트 수까지
//   그대로)이라 흩어 놓으면 어느 쪽이 픽셀 대조의 기준선인지 알 수 없게
//   된다 — 패스에 있던 그 규칙이 집만 옮겨 그대로 성립한다.

/// 기즈모 아이콘 하나. texture가 없으면 1x1 흰색이 묶인다.
/// raw pointer는 패스 입력 형식이고, 수명은 packet의 shared iconTextures가
/// RenderThread 소비 완료까지 붙든다(EnhancedGizmoSceneBinding.h 참조).
struct EnhancedGizmoIcon
{
    math::vector3  position{};
    float          size{ 1.f };
    Texture*       texture{ nullptr };
};

/// 라인 정점. DX11 쪽 LineVertex와 같은 배치(POSITION float3 + COLOR float4).
struct EnhancedGizmoLineVertex
{
    math::vector3  position{};
    Mathf::Color4  color{};
};

static_assert(sizeof(EnhancedGizmoLineVertex) == 28u);
static_assert(offsetof(EnhancedGizmoLineVertex, position) == 0u);
static_assert(offsetof(EnhancedGizmoLineVertex, color) == 12u);
static_assert(std::is_standard_layout_v<EnhancedGizmoLineVertex>);
static_assert(std::is_trivially_copyable_v<EnhancedGizmoLineVertex>);

/// 프레임의 기즈모 선을 쌓는 수집기. 프레임마다 Reset 후 Add*를 부르고,
/// 소비자(패스 또는 packet)가 GetVertices로 한 번에 가져간다.
class EnhancedGizmoLineCollector
{
public:
    void Reset() { m_vertices.clear(); }
    void Set(const std::vector<EnhancedGizmoLineVertex>& vertices) { m_vertices = vertices; }
    const std::vector<EnhancedGizmoLineVertex>& GetVertices() const { return m_vertices; }

    void AddLine(const math::vector3& p0, const math::vector3& p1,
        const Mathf::Color4& color);
    void AddWireCircle(const math::vector3& center, float radius,
        const math::vector3& up, const Mathf::Color4& color);

    /// 방향광 기즈모 — 9세그먼트 원 + 세그먼트 시작점마다 방향 선(길이
    /// 반지름x3). DX11 DrawWireCircleAndLines의 이식.
    void AddWireCircleWithDirectionLines(const math::vector3& center, float radius,
        const math::vector3& up, const math::vector3& direction,
        const Mathf::Color4& color);
    void AddWireSphere(const math::vector3& center, float radius,
        const Mathf::Color4& color);
    void AddWireBox(const math::matrix4x4& transform, const math::vector3& extents,
        const Mathf::Color4& color);
    void AddWireCapsule(const math::matrix4x4& transform, float radius, float height,
        const Mathf::Color4& color);
    void AddWireCone(const math::vector3& apex, const math::vector3& direction,
        float height, float outerConeAngleDegrees, const Mathf::Color4& color);
    void AddBoundingFrustum(const DirectX::BoundingFrustum& frustum,
        const Mathf::Color4& color);

private:
    std::vector<EnhancedGizmoLineVertex> m_vertices;
};
