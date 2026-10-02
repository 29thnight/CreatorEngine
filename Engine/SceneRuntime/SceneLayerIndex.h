#pragma once

#include "EntityHandle.h"
#include "../Utility_Framework/LayerCatalog.h"
#include <array>
#include <vector>

// Scene owns this index and resolves returned handles; no Entity pointers are retained.
class SceneLayerIndex final
{
  public:
    explicit SceneLayerIndex(std::uint32_t scene) : m_scene(scene) {}
    SceneLayerIndex(const SceneLayerIndex&) = delete;
    SceneLayerIndex& operator=(const SceneLayerIndex&) = delete;

    ce::layers::result<void> Assign(const ce::layers::catalog_snapshot& catalog, EntityHandle entity,
                                    ce::layers::layer_id layer)
    {
        if (auto valid = RequireOwner(); !valid)
            return valid;

        if (!entity.IsValid() || !entity.sceneId || entity.sceneId != m_scene)
            return std::unexpected(ce::layers::error::wrong_scene);

        const auto* definition = catalog.Find(layer);
        if (!definition)
            return std::unexpected(ce::layers::error::unknown_layer);

        auto& target = m_buckets[definition->slot.Value()];
        if (std::ranges::find(target, entity) != target.end())
            return {};

        try
        {
            // Allocate before removing the original membership: failure preserves it.
            target.push_back(entity);
        }
        catch (const std::bad_alloc&)
        {
            return std::unexpected(ce::layers::error::out_of_memory);
        }

        for (auto& bucket : m_buckets)
            if (&bucket != &target)
                std::erase(bucket, entity);

        return {};
    }

    ce::layers::result<void> Remove(EntityHandle entity)
    {
        if (auto valid = RequireOwner(); !valid)
            return valid;

        if (entity.sceneId != m_scene)
            return std::unexpected(ce::layers::error::wrong_scene);

        for (auto& bucket : m_buckets)
            std::erase(bucket, entity);

        return {};
    }

    // Owner-only borrow, invalidated by the next Assign/Remove; consumers resolve each handle.
    ce::layers::result<std::span<const EntityHandle>> Members(const ce::layers::catalog_snapshot& catalog,
                                                              ce::layers::layer_id layer) const
    {
        if (auto valid = RequireOwner(); !valid)
            return std::unexpected(valid.error());

        const auto* definition = catalog.Find(layer);
        if (!definition)
            return std::unexpected(ce::layers::error::unknown_layer);

        return std::span<const EntityHandle>(m_buckets[definition->slot.Value()]);
    }

  private:
    ce::layers::result<void> RequireOwner() const noexcept
    {
        if (m_owner != std::this_thread::get_id())
            return std::unexpected(ce::layers::error::wrong_owner);

        return {};
    }

    std::uint32_t m_scene;
    std::thread::id m_owner = std::this_thread::get_id();
    std::array<std::vector<EntityHandle>, 32> m_buckets;
};
