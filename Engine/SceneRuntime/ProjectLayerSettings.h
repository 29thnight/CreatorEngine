#pragma once

#include "../Physics/PhysicsCollisionPolicy.h"
#include <functional>

struct project_layer_snapshot
{
    std::uint64_t revision = 1;
    ce::layers::catalog_snapshot catalog;
    ce::physics::collision_policy_snapshot policy;
};

// The host owns this project state. A single publication pairs catalog and collision policy.
class ProjectLayerSettings final
{
  public:
    ProjectLayerSettings()
    {
        ce::layers::LayerCatalog catalog;
        ce::physics::PhysicsCollisionPolicy policy;
        m_snapshot.store(std::make_shared<const project_layer_snapshot>(
            project_layer_snapshot{1, *catalog.Snapshot(), *policy.Snapshot()}));
    }

    std::shared_ptr<const project_layer_snapshot> Snapshot() const noexcept { return m_snapshot.load(); }

    // Complete load/undo/Play restoration. Source revisions never replace the runtime epoch.
    ce::layers::result<void> Restore(const project_layer_snapshot& source)
    {
        return Restore(source, [](const project_layer_snapshot&) -> ce::layers::result<void> { return {}; });
    }

    template<class Publication>
    ce::layers::result<void> Restore(const project_layer_snapshot& source, Publication&& publication)
    {
        return Change(
            [&](ce::layers::LayerCatalog& catalog,
                ce::physics::PhysicsCollisionPolicy& policy) -> ce::layers::result<void> {
                if (auto changed = catalog.Replace(source.catalog); !changed)
                    return changed;

                return policy.Replace(source.policy);
            },
            std::forward<Publication>(publication));
    }

    template<class Operation>
    ce::layers::result<void> Change(Operation&& operation)
    {
        return Change(std::forward<Operation>(operation),
                      [](const project_layer_snapshot&) -> ce::layers::result<void> { return {}; });
    }

    template<class Operation, class Publication>
    ce::layers::result<void> Change(Operation&& operation, Publication&& publication)
    {
        if (m_owner != std::this_thread::get_id())
            return std::unexpected(ce::layers::error::wrong_owner);

        if (m_changing)
            return std::unexpected(ce::layers::error::invalid_definition);

        struct mutation_scope
        {
            bool& flag;
            explicit mutation_scope(bool& value) : flag(value) { flag = true; }
            ~mutation_scope() { flag = false; }
        } guard(m_changing);

        const auto previous = Snapshot();
        if (previous->revision == (std::numeric_limits<std::uint64_t>::max)())
            return std::unexpected(ce::layers::error::capacity_exceeded);

        try
        {
            ce::layers::LayerCatalog catalog;
            ce::physics::PhysicsCollisionPolicy policy;
            if (auto loaded = catalog.Replace(previous->catalog); !loaded)
                return loaded;

            if (auto loaded = policy.Replace(previous->policy); !loaded)
                return loaded;

            if (auto changed = std::invoke(std::forward<Operation>(operation), catalog, policy); !changed)
                return changed;

            auto candidate = project_layer_snapshot{previous->revision + 1, *catalog.Snapshot(), *policy.Snapshot()};
            candidate.catalog.revision = candidate.revision;
            candidate.policy.revision = candidate.revision;
            auto prepared = std::make_shared<const project_layer_snapshot>(std::move(candidate));
            if (auto published = std::invoke(std::forward<Publication>(publication), *prepared); !published)
                return published;

            m_snapshot.store(std::move(prepared));
            return {};
        }
        catch (const std::bad_alloc&)
        {
            return std::unexpected(ce::layers::error::out_of_memory);
        }
    }

  private:
    bool m_changing = false;
    std::thread::id m_owner = std::this_thread::get_id();
    std::atomic<std::shared_ptr<const project_layer_snapshot>> m_snapshot;
};
