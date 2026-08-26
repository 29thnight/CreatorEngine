#include "EnhancedGizmoSceneTypes.h"

#include <mathematics/scalar.hpp>

#include <cmath>

// ── 도형 → 선 (DX11 GizmoLinePass의 이식 — 세그먼트 수까지 그대로) ──
//
// EnhancedGizmoLinePass에 있던 수식을 그대로 옮겼다(E4-3a). 픽셀 대조의
// 기준선이 이 모양이므로 여기서 도형을 다듬으면 대조가 흔들린다.

void EnhancedGizmoLineCollector::AddLine(const math::vector3& p0, const math::vector3& p1,
    const Mathf::Color4& color)
{
    m_vertices.push_back({ p0, color });
    m_vertices.push_back({ p1, color });
}

void EnhancedGizmoLineCollector::AddWireCircle(const math::vector3& center, float radius,
    const math::vector3& up, const Mathf::Color4& color)
{
    const int segmentCount = 64;

    math::vector3 right = math::normalize(math::cross(up, math::vector3::unit_y()));
    if (math::length_sq(right) < 1e-5f)
        right = math::normalize(math::cross(up, math::vector3::unit_x()));
    const math::vector3 forward = math::normalize(math::cross(right, up));

    for (int i = 0; i < segmentCount; ++i)
    {
        const float angle0 = math::two_pi * (i / (float)segmentCount);
        const float angle1 = math::two_pi * ((i + 1) / (float)segmentCount);

        const math::vector3 p0 = center + radius * (cosf(angle0) * right + sinf(angle0) * forward);
        const math::vector3 p1 = center + radius * (cosf(angle1) * right + sinf(angle1) * forward);

        AddLine(p0, p1, color);
    }
}

void EnhancedGizmoLineCollector::AddWireCircleWithDirectionLines(const math::vector3& center,
    float radius, const math::vector3& up, const math::vector3& direction,
    const Mathf::Color4& color)
{
    const int segmentCount = 9;
    const float lineLength = radius * 3.f;

    math::vector3 right = math::normalize(math::cross(up, math::vector3::unit_y()));
    if (math::length_sq(right) < 1e-5f)
        right = math::normalize(math::cross(up, math::vector3::unit_x()));
    const math::vector3 forward = math::normalize(math::cross(right, up));

    const math::vector3 dirNormalized = math::normalize(direction);

    for (int i = 0; i < segmentCount; ++i)
    {
        const float angle0 = math::two_pi * (i / (float)segmentCount);
        const float angle1 = math::two_pi * ((i + 1) / (float)segmentCount);

        const math::vector3 p0 = center + radius * (cosf(angle0) * right + sinf(angle0) * forward);
        const math::vector3 p1 = center + radius * (cosf(angle1) * right + sinf(angle1) * forward);

        AddLine(p0, p1, color);
        AddLine(p0, p0 + dirNormalized * lineLength, color);
    }
}

void EnhancedGizmoLineCollector::AddWireSphere(const math::vector3& center, float radius,
    const Mathf::Color4& color)
{
    AddWireCircle(center, radius, math::vector3::unit_y(), color); // XZ
    AddWireCircle(center, radius, math::vector3::unit_x(), color); // YZ
    AddWireCircle(center, radius, math::vector3::unit_z(), color); // XY
}

void EnhancedGizmoLineCollector::AddWireBox(const math::matrix4x4& transform,
    const math::vector3& extents, const Mathf::Color4& color)
{
    math::vector3 corners[8] = {
        { -extents.x, -extents.y, -extents.z },
        {  extents.x, -extents.y, -extents.z },
        {  extents.x,  extents.y, -extents.z },
        { -extents.x,  extents.y, -extents.z },
        { -extents.x, -extents.y,  extents.z },
        {  extents.x, -extents.y,  extents.z },
        {  extents.x,  extents.y,  extents.z },
        { -extents.x,  extents.y,  extents.z },
    };

    for (auto& corner : corners)
    {
        corner = math::transform_point(corner, transform);
    }

    constexpr uint32_t indices[24] = {
        0,1, 1,2, 2,3, 3,0,
        4,5, 5,6, 6,7, 7,4,
        0,4, 1,5, 2,6, 3,7
    };

    for (size_t i = 0; i < 24; i += 2)
    {
        AddLine(corners[indices[i]], corners[indices[i + 1]], color);
    }
}

void EnhancedGizmoLineCollector::AddWireCapsule(const math::matrix4x4& transform,
    float radius, float height, const Mathf::Color4& color)
{
    const int segmentCount = 16;

    const math::vector3 up = math::normalize(transform.up());
    const math::vector3 right = math::normalize(transform.right());
    const math::vector3 forward = math::normalize(transform.forward());

    const float halfHeight = height * 0.5f;
    const math::vector3 center = transform.translation();
    const math::vector3 topCenter = center + up * halfHeight;
    const math::vector3 bottomCenter = center - up * halfHeight;

    for (int i = 0; i < segmentCount; ++i)
    {
        const float angle = math::two_pi * (static_cast<float>(i) / segmentCount);
        const math::vector3 dir = cosf(angle) * right + sinf(angle) * forward;
        AddLine(bottomCenter + dir * radius, topCenter + dir * radius, color);
    }

    AddWireSphere(topCenter, radius, color);
    AddWireSphere(bottomCenter, radius, color);
    AddWireCircle(topCenter, radius, up, color);
    AddWireCircle(bottomCenter, radius, up, color);
}

void EnhancedGizmoLineCollector::AddWireCone(const math::vector3& apex,
    const math::vector3& direction, float height, float outerConeAngleDegrees,
    const Mathf::Color4& color)
{
    const int segmentCount = 32;

    const math::vector3 dir = math::normalize(direction);

    math::vector3 up = math::vector3::unit_y();
    if (fabs(math::dot(up, dir)) > 0.99f)
        up = math::vector3::unit_x();

    const math::vector3 right = math::normalize(math::cross(dir, up));
    const math::vector3 forward = math::normalize(math::cross(right, dir));

    const float radius = height * tanf(math::radians(outerConeAngleDegrees) * 0.5f);

    for (int i = 0; i < segmentCount; ++i)
    {
        const float angle0 = math::two_pi * (i / (float)segmentCount);
        const float angle1 = math::two_pi * ((i + 1) / (float)segmentCount);

        const math::vector3 p0 = apex + dir * height
            + radius * (cosf(angle0) * right + sinf(angle0) * forward);
        const math::vector3 p1 = apex + dir * height
            + radius * (cosf(angle1) * right + sinf(angle1) * forward);

        AddLine(p0, p1, color);
        AddLine(apex, p0, color);
    }
}

void EnhancedGizmoLineCollector::AddBoundingFrustum(const DirectX::BoundingFrustum& frustum,
    const Mathf::Color4& color)
{
    DirectX::XMFLOAT3 corners[DirectX::BoundingFrustum::CORNER_COUNT];
    frustum.GetCorners(corners);

    constexpr uint32_t indices[24] = {
        0,1, 1,2, 2,3, 3,0,     // 근평면
        0,4, 1,5, 2,6, 3,7,     // 모서리
        4,5, 5,6, 6,7, 7,4      // 원평면
    };

    for (size_t i = 0; i < 24; i += 2)
    {
        const DirectX::XMFLOAT3& a = corners[indices[i]];
        const DirectX::XMFLOAT3& b = corners[indices[i + 1]];
        AddLine({ a.x, a.y, a.z }, { b.x, b.y, b.z }, color);
    }
}
