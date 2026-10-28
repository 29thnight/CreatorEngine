#include "../Containers/ContainerAPI.h"
#pragma once
#include "../Utility_Framework/Foundation/StandardLibrary.h"
#include "../Utility_Framework/Foundation/Mathematics.Intersect.h"
#include <mathematics/frustum.hpp>
#include <mathematics/transform.hpp>

namespace foliage
{
// Owner-thread pass over already sealed poses. The resolver borrows bounds
// from the component's existing owners; neither poses nor owners are copied.
template <typename Instance, size_t Extent, typename BoundsResolver>
size_t CullSealedInstances(std::span<Instance, Extent> instances, size_t typeCount,
    BoundsResolver&& resolveBounds,
    const std::optional<math::bounding_frustum>& cameraFrustum, bool playing)
{
    size_t transformed = 0;
    if (typeCount == 0) return 0;
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
            ++transformed;
            instance.m_isCulled = cameraFrustum.has_value() && !worldBounds.is_empty() &&
                !math::intersects(*cameraFrustum, worldBounds);
        }
        else instance.m_isCulled = false;
    }
    return transformed;
}

// Component-local, non-owning derived values only. No asset pointers/owners are
// retained here, and this storage is never copied into a render proxy or asset.
class WorldBoundsCache
{
    struct Entry
    {
        math::aabb world{};
        uint64_t identity{};
        uint32_t type{};
        bool dirty{true};
        bool hasBounds{};
    };
    ce::dynamic_array<Entry> entries_;

public:
    WorldBoundsCache() = default;
    WorldBoundsCache(const WorldBoundsCache&) = delete;
    WorldBoundsCache& operator=(const WorldBoundsCache&) = delete;
    void Clear() noexcept { entries_.clear(); } // retain capacity for reload
    size_t AllocatedBytes() const noexcept { return entries_.capacity() * sizeof(Entry); }
    size_t ActiveBytes() const noexcept { return entries_.size() * sizeof(Entry); }
    // Explicit diagnostic baseline only; ordinary invalidation retains capacity.
    void ReleaseStorage() noexcept { ce::dynamic_array<Entry>{}.swap(entries_); }
    void InvalidateAll() noexcept
    {
        for (auto& entry : entries_) entry.dirty = true;
    }
    void Invalidate(size_t index) noexcept
    {
        if (index < entries_.size()) entries_[index].dirty = true;
    }
    template <typename Instance, size_t Extent>
    void InvalidateType(uint32_t type, std::span<Instance, Extent> instances) noexcept
    {
        const size_t count = (std::min)(entries_.size(), instances.size());
        for (size_t index = 0; index < count; ++index)
            if (instances[index].m_foliageTypeID == type) entries_[index].dirty = true;
    }

    // Returns the number of world transforms actually evaluated, for CPU
    // contract tests. Warm frames perform no resize/allocation or transforms.
    template <typename Instance, size_t Extent, typename BoundsResolver>
    size_t Cull(std::span<Instance, Extent> instances, size_t typeCount,
        BoundsResolver&& resolveBounds,
        const std::optional<math::bounding_frustum>& cameraFrustum, bool playing)
    {
        if (!playing || !cameraFrustum)
        {
            // Missing bounds still cull in editor/no-camera mode. Keep pending
            // invalidations for the next frame that actually needs world bounds.
            CullSealedInstances(instances, typeCount, resolveBounds, cameraFrustum, false);
            return 0;
        }
        if (typeCount == 0) return 0;
        if (entries_.size() != instances.size()) entries_.resize(instances.size());
        size_t transformed = 0;
        for (size_t index = 0; index < instances.size(); ++index)
        {
            auto& instance = instances[index];
            if (static_cast<size_t>(instance.m_foliageTypeID) >= typeCount) continue;
            auto& entry = entries_[index];
            // Identity/type tags also protect shifted/reused slots after edits.
            if (entry.dirty || entry.identity != instance.m_temporalIdentity ||
                entry.type != instance.m_foliageTypeID)
            {
                const math::aabb* bounds = resolveBounds(instance.m_foliageTypeID);
                entry.hasBounds = bounds != nullptr;
                if (bounds)
                {
                    entry.world = math::transform(*bounds, instance.m_worldMatrix);
                    ++transformed;
                }
                entry.identity = instance.m_temporalIdentity;
                entry.type = instance.m_foliageTypeID;
                entry.dirty = false;
            }
            instance.m_isCulled = !entry.hasBounds ||
                (!entry.world.is_empty() && !math::intersects(*cameraFrustum, entry.world));
        }
        return transformed;
    }
};
}
