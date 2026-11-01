#include "../Containers/ContainerAPI.h"
#pragma once
#include "../Utility_Framework/Foundation/EngineOrderedMap.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../../Tools/build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif

#include "MaterialGraphRuntime.h"

#include "../Utility_Framework/Foundation/StandardLibrary.h"

namespace material_graph
{
    // One strong instance pin per exact graph generation and immutable value /
    // texture representation. Draws and bindings borrow by table index.
    // This is synchronized append-only recording state, not an immutable asset.
    // Later views may append; existing entries remain untouched until teardown.
    class InstanceFramePins final
    {
    public:
        static constexpr std::size_t InvalidIndex = (std::numeric_limits<std::size_t>::max)();
        using Key = ce::tuple<experiment::AssetId, std::uint64_t, std::uint64_t>;

        static Key Identity(const Instance& instance)
        {
            return {instance.generation ? instance.generation->assetId : experiment::AssetId{},
                instance.generation ? instance.generation->generation : 0, instance.representationId};
        }

        std::size_t Find(const Instance& instance) const
        {
            std::lock_guard lock(mutex_);
            const auto found = indices_.find(Identity(instance));
            return found != indices_.end() ? found->second : InvalidIndex;
        }

        std::size_t Retain(const own::shared_owner<const Instance>& instance)
        {
            if (!instance)
            {
                return InvalidIndex;
            }
            std::lock_guard lock(mutex_);
            if (const auto found = indices_.find(Identity(*instance)); found != indices_.end())
            {
                return found->second;
            }
            const auto index = instances_.size();
            instances_.push_back(instance);
            try
            {
                indices_.emplace(Identity(*instance), index);
            }
            catch (...)
            {
                instances_.pop_back();
                throw;
            }
            return index;
        }

        own::local_view<const Instance> Borrow(std::size_t index) const
        {
            std::lock_guard lock(mutex_);
            return index < instances_.size() ? instances_[index].borrow() : own::local_view<const Instance>{};
        }

        // A producer index is only a hint. Resolve and validate the exact immutable
        // instance under one lock; stale/foreign indices retain the lookup fallback.
        own::local_view<const Instance> BorrowExact(const Instance& instance, std::size_t& index) const
        {
            std::lock_guard lock(mutex_);
            const auto identity = Identity(instance);
            const auto matches = [&](std::size_t candidate)
            {
                return candidate < instances_.size() && instances_[candidate] &&
                       &*instances_[candidate] == &instance && Identity(*instances_[candidate]) == identity;
            };
            if (!matches(index))
            {
                const auto found = indices_.find(identity);
                index = found != indices_.end() ? found->second : InvalidIndex;
                if (!matches(index))
                {
                    index = InvalidIndex;
                    return {};
                }
            }
            return instances_[index].borrow();
        }

        own::shared_owner<const Instance> Owner(std::size_t index) const
        {
            std::lock_guard lock(mutex_);
            return instances_.at(index);
        }

        // The table owns the instance. Identity checks do not acquire a strong owner.
        bool Matches(std::size_t index, const own::weak_owner<const Instance>& expected) const
        {
            std::lock_guard lock(mutex_);
            return index < instances_.size() && own::identity(instances_[index]) == own::identity(expected);
        }

        own::weak_owner<const Instance> Observe(std::size_t index) const
        {
            std::lock_guard lock(mutex_);
            return instances_.at(index);
        }

        std::size_t Size() const
        {
            std::lock_guard lock(mutex_);
            return instances_.size();
        }

    private:
        mutable std::mutex mutex_;
        ce::dynamic_array<own::shared_owner<const Instance>> instances_;
        ce::engine_ordered_map<Key, std::size_t> indices_;
    };
}
