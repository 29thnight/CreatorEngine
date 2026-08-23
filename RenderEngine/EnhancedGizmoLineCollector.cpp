#include "EnhancedGizmoSceneTypes.h"

#include <cmath>

// ── 도형 → 선 (DX11 GizmoLinePass의 이식 — 세그먼트 수까지 그대로) ──
//
// EnhancedGizmoLinePass에 있던 수식을 그대로 옮겼다(E4-3a). 픽셀 대조의
// 기준선이 이 모양이므로 여기서 도형을 다듬으면 대조가 흔들린다.

void EnhancedGizmoLineCollector::AddLine(const Mathf::Vector3& p0, const Mathf::Vector3& p1,
    const Mathf::Color4& color)
{
    m_vertices.push_back({ p0, color });
    m_vertices.push_back({ p1, color });
}

void EnhancedGizmoLineCollector::AddWireCircle(const Mathf::Vector3& center, float radius,
    const Mathf::Vector3& up, const Mathf::Color4& color)
{
    using namespace Mathf;
    const int segmentCount = 64;

    Vector3 right = XMVector3Normalize(XMVector3Cross(up, Vector3(0, 1, 0)));
    if (XMVectorGetX(XMVector3LengthSq(right)) < 1e-5f)
        right = XMVector3Normalize(XMVector3Cross(up, Vector3(1, 0, 0)));
    const Vector3 forward = XMVector3Normalize(XMVector3Cross(right, up));

    for (int i = 0; i < segmentCount; ++i)
    {
        const float angle0 = XM_2PI * (i / (float)segmentCount);
        const float angle1 = XM_2PI * ((i + 1) / (float)segmentCount);

        const Vector3 p0 = center + radius * (cosf(angle0) * right + sinf(angle0) * forward);
        const Vector3 p1 = center + radius * (cosf(angle1) * right + sinf(angle1) * forward);

        AddLine(p0, p1, color);
    }
}

void EnhancedGizmoLineCollector::AddWireCircleWithDirectionLines(const Mathf::Vector3& center,
    float radius, const Mathf::Vector3& up, const Mathf::Vector3& direction,
    const Mathf::Color4& color)
{
    using namespace Mathf;
    const int segmentCount = 9;
    const float lineLength = radius * 3.f;

    Vector3 right = XMVector3Normalize(XMVector3Cross(up, Vector3(0, 1, 0)));
    if (XMVectorGetX(XMVector3LengthSq(right)) < 1e-5f)
        right = XMVector3Normalize(XMVector3Cross(up, Vector3(1, 0, 0)));
    const Vector3 forward = XMVector3Normalize(XMVector3Cross(right, up));

    Vector3 dirNormalized = direction;
    dirNormalized.Normalize();

    for (int i = 0; i < segmentCount; ++i)
    {
        const float angle0 = XM_2PI * (i / (float)segmentCount);
        const float angle1 = XM_2PI * ((i + 1) / (float)segmentCount);

        const Vector3 p0 = center + radius * (cosf(angle0) * right + sinf(angle0) * forward);
        const Vector3 p1 = center + radius * (cosf(angle1) * right + sinf(angle1) * forward);

        AddLine(p0, p1, color);
        AddLine(p0, p0 + dirNormalized * lineLength, color);
    }
}

void EnhancedGizmoLineCollector::AddWireSphere(const Mathf::Vector3& center, float radius,
    const Mathf::Color4& color)
{
    AddWireCircle(center, radius, Mathf::Vector3(0, 1, 0), color); // XZ
    AddWireCircle(center, radius, Mathf::Vector3(1, 0, 0), color); // YZ
    AddWireCircle(center, radius, Mathf::Vector3(0, 0, 1), color); // XY
}

void EnhancedGizmoLineCollector::AddWireBox(const Mathf::Matrix& transform,
    const Mathf::Vector3& extents, const Mathf::Color4& color)
{
    Mathf::Vector3 corners[8] = {
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
        corner = XMVector3TransformCoord(corner, transform);
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

void EnhancedGizmoLineCollector::AddWireCapsule(const Mathf::Matrix& transform,
    float radius, float height, const Mathf::Color4& color)
{
    using namespace Mathf;
    const int segmentCount = 16;

    Vector3 up = transform.Up();       up.Normalize();
    Vector3 right = transform.Right(); right.Normalize();
    Vector3 forward = transform.Forward(); forward.Normalize();

    const float halfHeight = height * 0.5f;
    const Vector3 center = transform.Translation();
    const Vector3 topCenter = center + up * halfHeight;
    const Vector3 bottomCenter = center - up * halfHeight;

    for (int i = 0; i < segmentCount; ++i)
    {
        const float angle = XM_2PI * (static_cast<float>(i) / segmentCount);
        const Vector3 dir = cosf(angle) * right + sinf(angle) * forward;
        AddLine(bottomCenter + dir * radius, topCenter + dir * radius, color);
    }

    AddWireSphere(topCenter, radius, color);
    AddWireSphere(bottomCenter, radius, color);
    AddWireCircle(topCenter, radius, up, color);
    AddWireCircle(bottomCenter, radius, up, color);
}

void EnhancedGizmoLineCollector::AddWireCone(const Mathf::Vector3& apex,
    const Mathf::Vector3& direction, float height, float outerConeAngleDegrees,
    const Mathf::Color4& color)
{
    using namespace Mathf;
    const int segmentCount = 32;

    Vector3 dir = direction;
    dir.Normalize();

    Vector3 up = Vector3(0, 1, 0);
    if (fabs(XMVectorGetX(XMVector3Dot(up, dir))) > 0.99f)
        up = Vector3(1, 0, 0);

    const Vector3 right = XMVector3Normalize(XMVector3Cross(dir, up));
    const Vector3 forward = XMVector3Normalize(XMVector3Cross(right, dir));

    const float radius = height * tanf(XMConvertToRadians(outerConeAngleDegrees) * 0.5f);

    for (int i = 0; i < segmentCount; ++i)
    {
        const float angle0 = XM_2PI * (i / (float)segmentCount);
        const float angle1 = XM_2PI * ((i + 1) / (float)segmentCount);

        const Vector3 p0 = apex + dir * height
            + radius * (cosf(angle0) * right + sinf(angle0) * forward);
        const Vector3 p1 = apex + dir * height
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
        AddLine(corners[indices[i]], corners[indices[i + 1]], color);
    }
}
