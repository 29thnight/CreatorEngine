#pragma once

#include "../Utility_Framework/LayerCatalog.h"
#include "PhysicsGeometry.h"
#include <array>
#include <mdspan>

namespace ce::physics
{
struct collision_policy_snapshot
{
    std::uint64_t revision = 1;
    std::array<std::uint8_t, 32 * 32> matrix{};

    bool Allows(ce::layers::layer_slot left, ce::layers::layer_slot right) const noexcept
    {
        const std::mdspan<const std::uint8_t, std::extents<std::size_t, 32, 32>> view(matrix.data());
        return view[left.Value(), right.Value()] != 0;
    }

    ce::layers::result<collision_filter> Filter(const ce::layers::catalog_snapshot& catalog,
                                                ce::layers::layer_id layer) const noexcept
    {
        const auto* definition = catalog.Find(layer);
        if (!definition)
            return std::unexpected(ce::layers::error::unknown_layer);

        std::uint32_t allowed = 0;
        for (const auto& item : catalog.definitions)
            if (item && !item->retired && Allows(definition->slot, item->slot))
                allowed |= item->slot.Mask();

        return collision_filter{definition->slot.Mask(), allowed, definition->slot.Mask()};
    }
};

// Project-owned, independent of SDK and Entity. PhysicsScene consumes immutable policy values.
class PhysicsCollisionPolicy final
{
  public:
    PhysicsCollisionPolicy()
    {
        collision_policy_snapshot initial;
        initial.matrix.fill(1);
        m_snapshot.store(std::make_shared<const collision_policy_snapshot>(initial));
    }

    std::shared_ptr<const collision_policy_snapshot> Snapshot() const noexcept { return m_snapshot.load(); }

    ce::layers::result<void> Set(const ce::layers::catalog_snapshot& catalog, ce::layers::layer_id left,
                                 ce::layers::layer_id right, bool enabled)
    {
        const auto* a = catalog.Find(left);
        const auto* b = catalog.Find(right);
        if (!a || !b)
            return std::unexpected(ce::layers::error::unknown_layer);

        auto candidate = *Snapshot();
        std::mdspan<std::uint8_t, std::extents<std::size_t, 32, 32>> view(candidate.matrix.data());
        view[a->slot.Value(), b->slot.Value()] = enabled;
        view[b->slot.Value(), a->slot.Value()] = enabled;
        return Replace(candidate);
    }

    ce::layers::result<void> Replace(collision_policy_snapshot value)
    {
        if (m_owner != std::this_thread::get_id())
            return std::unexpected(ce::layers::error::wrong_owner);

        if (auto valid = Validate(value); !valid)
            return valid;

        const auto revision = Snapshot()->revision;
        if (revision == (std::numeric_limits<std::uint64_t>::max)())
            return std::unexpected(ce::layers::error::capacity_exceeded);

        try
        {
            value.revision = revision + 1;
            m_snapshot.store(std::make_shared<const collision_policy_snapshot>(std::move(value)));
            return {};
        }
        catch (const std::bad_alloc&)
        {
            return std::unexpected(ce::layers::error::out_of_memory);
        }
    }

    static ce::layers::result<void> Validate(const collision_policy_snapshot& value) noexcept
    {
        if (!value.revision)
            return std::unexpected(ce::layers::error::invalid_definition);

        const std::mdspan<const std::uint8_t, std::extents<std::size_t, 32, 32>> view(value.matrix.data());
        for (std::size_t a = 0; a < 32; ++a)
            for (std::size_t b = 0; b < 32; ++b)
                if (view[a, b] > 1 || view[a, b] != view[b, a])
                    return std::unexpected(ce::layers::error::invalid_definition);

        return {};
    }

    static ce::layers::result<collision_policy_snapshot> ImportLegacy(std::span<const std::uint8_t> matrix) noexcept
    {
        if (matrix.size() != 32 * 32)
            return std::unexpected(ce::layers::error::invalid_definition);

        collision_policy_snapshot value;
        std::ranges::copy(matrix, value.matrix.begin());
        if (auto valid = Validate(value); !valid)
            return std::unexpected(valid.error());

        return value;
    }

  private:
    std::thread::id m_owner = std::this_thread::get_id();
    std::atomic<std::shared_ptr<const collision_policy_snapshot>> m_snapshot;
};
} // namespace ce::physics
