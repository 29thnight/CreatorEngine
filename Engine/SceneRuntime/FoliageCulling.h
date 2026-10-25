#pragma once
#include "../Utility_Framework/StandardLibrary.h"
#include "../Utility_Framework/Mathematics.Intersect.h"
#include <mathematics/frustum.hpp>
#include <mathematics/transform.hpp>

namespace foliage
{
// Owner-thread pass over already sealed poses. The resolver borrows bounds
// from the component's existing owners; neither poses nor owners are copied.
template <typename Instance, size_t Extent, typename BoundsResolver>
void CullSealedInstances(std::span<Instance, Extent> instances, size_t typeCount,
    BoundsResolver&& resolveBounds,
    const std::optional<math::bounding_frustum>& cameraFrustum, bool playing)
{
    if (typeCount == 0) return;
    for (auto& instance : instances)
    {
        if (static_cast<size_t>(instance.m_foliageTypeID) >= typeCount) continue;
        const math::aabb* bounds = resolveBounds(instance.m_foliageTypeID);
        if (!bounds)
        {
            instance.m_isCulled = true;
            continue;
        }
        if (playing)
        {
            const math::aabb worldBounds = math::transform(*bounds, instance.m_worldMatrix);
            instance.m_isCulled = cameraFrustum.has_value() && !worldBounds.is_empty() &&
                !math::intersects(*cameraFrustum, worldBounds);
        }
        else instance.m_isCulled = false;
    }
}
}
