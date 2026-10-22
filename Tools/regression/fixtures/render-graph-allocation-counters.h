#pragma once

#include <cstddef>
#include <cstdint>

// Opt-in accounting for the portable CPU fixtures. These counters measure
// successful C++ new and mimalloc allocation requests, not native GPU allocation,
// every libc allocation, or retained allocator capacity. The fixture executes on
// one producer thread; worker-thread allocations are outside this scope.
namespace ce::regression
{
    struct AllocationCounts
    {
        uint64_t standardCalls{};
        uint64_t standardBytes{};
        uint64_t mimallocCalls{};
        uint64_t mimallocBytes{};
    };

    extern thread_local AllocationCounts* activeAllocationCounts;

    class ScopedAllocationMeasurement
    {
      public:
        explicit ScopedAllocationMeasurement(AllocationCounts& counts) noexcept : m_previous(activeAllocationCounts)
        {
            activeAllocationCounts = &counts;
        }
        ~ScopedAllocationMeasurement() { activeAllocationCounts = m_previous; }
        ScopedAllocationMeasurement(const ScopedAllocationMeasurement&) = delete;
        ScopedAllocationMeasurement& operator=(const ScopedAllocationMeasurement&) = delete;

      private:
        AllocationCounts* m_previous;
    };

    inline void CountStandardAllocation(std::size_t bytes) noexcept
    {
        if (activeAllocationCounts)
        {
            ++activeAllocationCounts->standardCalls;
            activeAllocationCounts->standardBytes += bytes;
        }
    }

    inline void CountMimallocAllocation(std::size_t bytes) noexcept
    {
        if (activeAllocationCounts)
        {
            ++activeAllocationCounts->mimallocCalls;
            activeAllocationCounts->mimallocBytes += bytes;
        }
    }
} // namespace ce::regression
