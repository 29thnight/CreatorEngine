#pragma once
#include "ShadowMath.h"
#include "EnhancedDrawIdentity.h"
namespace shadow_math
{
inline Sphere WorldBounds(const EnhancedDrawItem& draw)
{
    auto localCenter = draw.boundRadius > 0.f || !draw.mesh
        ? draw.boundCenter : draw.mesh->GetBoundingSphere().center;
    float localRadius = enhanced_draw::BoundRadius(draw);
    if (!(localRadius > 0.f) && draw.modelMeshView.IsComplete() && draw.modelMeshView.vertexStride >= 12)
    {
        // Isolated typed fixtures may omit the producer's cached bounds.
        // Every supported model layout places float3 POSITION at byte zero.
        const auto& mesh = draw.modelMeshView;
        math::vector3 minimum{}, maximum{};
        for (uint64_t offset = 0; offset < mesh.vertexBytes; offset += mesh.vertexStride)
        {
            float p[3];
            std::memcpy(p, static_cast<const std::byte*>(mesh.vertexData) + offset, sizeof(p));
            if (offset == 0) minimum = maximum = {p[0], p[1], p[2]};
            minimum = {(std::min)(minimum.x, p[0]), (std::min)(minimum.y, p[1]), (std::min)(minimum.z, p[2])};
            maximum = {(std::max)(maximum.x, p[0]), (std::max)(maximum.y, p[1]), (std::max)(maximum.z, p[2])};
        }
        localCenter = (minimum + maximum) * .5f;
        localRadius = math::length(maximum - minimum) * .5f;
    }
    Sphere result{math::transform_point(localCenter, draw.worldMatrix),
        localRadius * ScaleBound(draw.worldMatrix)};
    if (!(localRadius > 0.f)) return result;
    // A normalized weighted skin position lies in the convex hull of its bone
    // positions. Enclose every bone-transformed source sphere, not the bind pose.
    if (draw.bonePalette && draw.boneCount && draw.boneCount <= 256)
        for (uint32_t i = 0; i < draw.boneCount; ++i)
        {
            const auto transform = draw.bonePalette[i] * draw.worldMatrix;
            result.radius = (std::max)(result.radius,
                math::distance(result.center, math::transform_point(localCenter, transform))
                    + localRadius * ScaleBound(transform));
        }
    return result;
}

}
