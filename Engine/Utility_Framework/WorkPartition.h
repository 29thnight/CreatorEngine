#pragma once

#include "StandardLibrary.h"

namespace ce
{
    // Keep execution order while minimizing the largest contiguous target cost.
    // The caller owns both spans; planning never allocates or retains frame data.
    inline bool partition_contiguous_work(std::span<const std::uint64_t> costs, std::uint32_t target_count,
                                          std::span<std::uint32_t> targets) noexcept
    {
        if (!target_count || target_count > costs.size() || targets.size() != costs.size())
        {
            return false;
        }

        std::uint64_t lower{};
        std::uint64_t upper{};
        for (const auto cost : costs)
        {
            if (cost > (std::numeric_limits<std::uint64_t>::max)() - upper)
            {
                return false;
            }
            upper += cost;
            if (cost > lower)
            {
                lower = cost;
            }
        }

        while (lower < upper)
        {
            const auto limit = lower + (upper - lower) / 2;
            std::uint64_t load{};
            std::size_t groups{1};
            for (const auto cost : costs)
            {
                if (load > limit - cost)
                {
                    ++groups;
                    load = 0;
                    if (groups > target_count)
                    {
                        break;
                    }
                }
                load += cost;
            }
            if (groups > target_count)
            {
                lower = limit + 1;
            }
            else
            {
                upper = limit;
            }
        }

        // Reconstruct from the end and leave at least one item for each target.
        // This also produces exactly target_count nonempty groups for zero costs.
        std::uint32_t target = target_count - 1;
        std::uint64_t load{};
        for (std::size_t remaining = costs.size(); remaining; --remaining)
        {
            const auto index = remaining - 1;
            if (load > lower - costs[index] || index < target)
            {
                --target;
                load = 0;
            }
            targets[index] = target;
            load += costs[index];
        }
        return true;
    }
} // namespace ce
