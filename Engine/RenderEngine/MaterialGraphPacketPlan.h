#pragma once
#include "../Utility_Framework/Ownership.h"

#include "../Utility_Framework/StandardLibrary.h"
#include <cstdlib>

namespace material_graph
{
    // CPU-only immutable shape. No device, graph, upload slice or GPU owner can
    // outlive its recording through this cache.
    struct PacketDispatchPlan final
    {
        static constexpr uint32_t ScanWidth = 256;
        struct ScanLevel
        {
            uint32_t count{}, source{}, prefix{}, sums{};
        };
        struct BucketRange
        {
            uint32_t first{}, count{};
        };
        uint32_t capacity{}, dispatchWidth{}, sliceCapacity{}, scratchWords{}, argumentWords{};
        std::vector<uint32_t> capacities;
        std::vector<ScanLevel> levels;
        std::vector<BucketRange> buckets;
        std::vector<std::array<uint32_t, 4>> values, selectors;

        bool Matches(uint32_t requestedCapacity, uint32_t requestedWidth, uint32_t requestedSlice,
                     std::span<const uint32_t> requestedBuckets) const
        {
            return capacity == requestedCapacity && dispatchWidth == requestedWidth &&
                   sliceCapacity == requestedSlice && std::ranges::equal(capacities, requestedBuckets);
        }
        size_t StorageBytes() const
        {
            return sizeof(*this) + capacities.capacity() * sizeof(uint32_t) + levels.capacity() * sizeof(ScanLevel) +
                   buckets.capacity() * sizeof(BucketRange) +
                   (values.capacity() + selectors.capacity()) * sizeof(std::array<uint32_t, 4>);
        }
        static own::shared_owner<const PacketDispatchPlan> Build(uint32_t capacity, uint32_t dispatchWidth,
                                                               uint32_t sliceCapacity,
                                                               std::span<const uint32_t> capacities, std::string& error)
        {
            if (!capacity || !dispatchWidth || !sliceCapacity || capacities.empty() || capacities.size() > UINT32_MAX)
            {
                error = "METEOR packet plan dimensions are invalid.";
                return {};
            }
            auto plan = own::make_shared<PacketDispatchPlan>();
            plan->capacity = capacity;
            plan->dispatchWidth = dispatchWidth;
            plan->sliceCapacity = sliceCapacity;
            plan->capacities.assign(capacities.begin(), capacities.end());
            uint64_t words = capacities.size();
            uint32_t count = static_cast<uint32_t>(capacities.size()), source{};
            for (;;)
            {
                const auto groups = (uint64_t(count) + ScanWidth - 1) / ScanWidth;
                if (words + count + groups > UINT32_MAX)
                {
                    error = "METEOR packet scan exceeds its address range.";
                    return {};
                }
                const auto prefix = static_cast<uint32_t>(words);
                words += count;
                const auto sums = static_cast<uint32_t>(words);
                words += groups;
                plan->levels.push_back({count, source, prefix, sums});
                if (groups == 1)
                {
                    break;
                }
                source = sums;
                count = static_cast<uint32_t>(groups);
            }
            plan->scratchWords = static_cast<uint32_t>(words);
            plan->buckets.reserve(capacities.size());
            plan->values.reserve(capacities.size());
            uint64_t totalSlices{};
            for (const auto bucketCapacity : capacities)
            {
                const auto bounded = std::min(capacity, bucketCapacity);
                totalSlices += std::max<uint64_t>(1, (uint64_t(bounded) + sliceCapacity - 1) / sliceCapacity);
                if (totalSlices > UINT32_MAX / 3)
                {
                    error = "METEOR packet slice arguments exceed their address range.";
                    return {};
                }
            }
            plan->selectors.reserve(static_cast<size_t>(totalSlices));
            for (uint32_t bucket = 0; bucket < capacities.size(); ++bucket)
            {
                const auto bounded = std::min(capacity, capacities[bucket]);
                const auto slices = static_cast<uint32_t>(
                    std::max<uint64_t>(1, (uint64_t(bounded) + sliceCapacity - 1) / sliceCapacity));
                const auto first = static_cast<uint32_t>(plan->selectors.size());
                plan->buckets.push_back({first, slices});
                plan->values.push_back({first, slices, bounded, 0});
                for (uint32_t slice = 0; slice < slices; ++slice)
                {
                    plan->selectors.push_back({bucket, dispatchWidth, slice * sliceCapacity, 0});
                }
            }
            plan->argumentWords = static_cast<uint32_t>(totalSlices * 3);
            return plan;
        }
    };

    class PacketDispatchPlanCache final
    {
      public:
        static constexpr size_t EntryLimit = 32, ByteLimit = 1024 * 1024;
        own::shared_owner<const PacketDispatchPlan> Acquire(uint32_t capacity, uint32_t dispatchWidth,
                                                          uint32_t sliceCapacity, std::span<const uint32_t> capacities,
                                                          std::string& error, bool reuse = true)
        {
            if (reuse)
            {
                for (const auto& entry : entries_)
                {
                    if (entry && entry->Matches(capacity, dispatchWidth, sliceCapacity, capacities))
                    {
                        ++hits_;
                        return entry;
                    }
                }
            }
            ++builds_;
            auto plan = PacketDispatchPlan::Build(capacity, dispatchWidth, sliceCapacity, capacities, error);
            if (!plan || !reuse || plan->StorageBytes() > ByteLimit)
            {
                return plan;
            }
            const auto bytes = plan->StorageBytes();
            if (entries_[next_])
            {
                bytes_ -= entries_[next_]->StorageBytes();
                entries_[next_].reset();
            }
            while (bytes_ + bytes > ByteLimit)
            {
                next_ = (next_ + 1) % EntryLimit;
                if (entries_[next_])
                {
                    bytes_ -= entries_[next_]->StorageBytes();
                    entries_[next_].reset();
                }
            }
            entries_[next_] = plan;
            bytes_ += bytes;
            next_ = (next_ + 1) % EntryLimit;
            return plan;
        }
        size_t Hits() const { return hits_; }
        size_t Builds() const { return builds_; }
        size_t Bytes() const { return bytes_; }
        static PacketDispatchPlanCache& Current()
        {
            // Packet producers migrate between workers. Each thread retains a
            // bounded CPU cache; no shared allocator/cache mutex is introduced.
            thread_local PacketDispatchPlanCache cache;
            return cache;
        }
        static bool Enabled()
        {
            static const bool enabled = []
            {
                char value[8]{};
                size_t bytes{};
                getenv_s(&bytes, value, sizeof(value), "CREATOR_PACKET_PLAN_REUSE");
                return !(value[0] == '0' && value[1] == '\0');
            }();
            return enabled;
        }

      private:
        std::array<own::shared_owner<const PacketDispatchPlan>, EntryLimit> entries_{};
        size_t next_{}, bytes_{}, hits_{}, builds_{};
    };
} // namespace material_graph
