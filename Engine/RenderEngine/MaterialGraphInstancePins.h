#pragma once

#include "MaterialGraphRuntime.h"

#include <cstddef>
#include <limits>
#include <map>
#include <mutex>
#include <tuple>
#include <vector>

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
        using Key = std::tuple<experiment::AssetId, std::uint64_t, std::uint64_t>;

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

        own::shared_owner<const Instance> Owner(std::size_t index) const
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
        std::vector<own::shared_owner<const Instance>> instances_;
        std::map<Key, std::size_t> indices_;
    };
}
