#if defined(CE_CONTRACT_STANDALONE)
#include "../build/ModuleAdapters/Creator.Core.WorkPartition.h"
#else
import Creator.Core.WorkPartition;
#endif

#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <new>

namespace
{
    std::atomic<std::uint64_t> allocation_count{};

    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            std::fprintf(stderr, "WORK_PARTITION_FAIL %s\n", message);
            std::abort();
        }
    }

    // Enumerate every cut, independently of the production binary-search planner.
    std::uint64_t oracle(std::span<const std::uint64_t> costs, std::uint32_t groups)
    {
        if (groups == 1)
        {
            std::uint64_t total{};
            for (const auto cost : costs)
            {
                total += cost;
            }
            return total;
        }
        auto best = (std::numeric_limits<std::uint64_t>::max)();
        std::uint64_t prefix{};
        for (std::size_t count = 1; count <= costs.size() - groups + 1; ++count)
        {
            prefix += costs[count - 1];
            const auto tail = oracle(costs.subspan(count), groups - 1);
            const auto longest = prefix > tail ? prefix : tail;
            if (longest < best)
            {
                best = longest;
            }
        }
        return best;
    }

    std::uint64_t check(std::span<const std::uint64_t> costs, std::uint32_t groups, bool compare_oracle)
    {
        std::array<std::uint32_t, 4096> storage;
        require(costs.size() <= storage.size(), "probe capacity");
        const auto targets = std::span{storage}.first(costs.size());
        const auto before = allocation_count.load();
        require(ce::partition_contiguous_work(costs, groups, targets), "valid work rejected");
        require(allocation_count.load() == before, "planner allocated");
        std::array<std::uint64_t, 8> loads{};
        std::array<std::uint32_t, 8> counts{};
        std::uint64_t maximum{};
        for (std::size_t index = 0; index < costs.size(); ++index)
        {
            const auto target = targets[index];
            require(target < groups, "out-of-range target");
            require(index == 0 || (target >= targets[index - 1] && target <= targets[index - 1] + 1),
                    "noncontiguous partition");
            loads[target] += costs[index];
            ++counts[target];
        }
        for (std::uint32_t group = 0; group < groups; ++group)
        {
            require(counts[group] != 0, "empty target");
            if (loads[group] > maximum)
            {
                maximum = loads[group];
            }
        }
        if (compare_oracle)
        {
            require(maximum == oracle(costs, groups), "not an optimal contiguous partition");
        }
        return maximum;
    }
} // namespace

void* operator new(std::size_t bytes)
{
    allocation_count.fetch_add(1, std::memory_order_relaxed);
    if (auto* memory = std::malloc(bytes ? bytes : 1))
    {
        return memory;
    }
    throw std::bad_alloc();
}

void* operator new[](std::size_t bytes)
{
    return ::operator new(bytes);
}
void operator delete(void* memory) noexcept
{
    std::free(memory);
}
void operator delete[](void* memory) noexcept
{
    std::free(memory);
}
void operator delete(void* memory, std::size_t) noexcept
{
    std::free(memory);
}
void operator delete[](void* memory, std::size_t) noexcept
{
    std::free(memory);
}

int main()
{
    std::uint64_t cases{};
    std::uint64_t oracleCases{};
    std::array<std::uint64_t, 8> input{};
    for (std::size_t size = 1; size <= input.size(); ++size)
    {
        const auto combinations = std::uint32_t{1} << (size * 2);
        for (std::uint32_t code = 0; code < combinations; ++code)
        {
            auto digits = code;
            for (std::size_t index = 0; index < size; ++index)
            {
                input[index] = digits & 3u;
                digits >>= 2;
            }
            for (std::uint32_t groups = 1; groups <= size; ++groups)
            {
                check(std::span{input}.first(size), groups, true);
                ++cases;
                ++oracleCases;
            }
        }
    }

    const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    const std::array<std::uint64_t, 4> large{maximum - 3, 1, 1, 1};
    for (std::uint32_t groups = 1; groups <= large.size(); ++groups)
    {
        check(large, groups, true);
        ++cases;
        ++oracleCases;
    }

    std::array<std::uint32_t, 4> invalid{17, 17, 17, 17};
    const auto unchanged = invalid;
    const std::array<std::uint64_t, 4> overflow{maximum, 1, 0, 0};
    require(!ce::partition_contiguous_work(large, 0, invalid), "zero targets accepted");
    require(!ce::partition_contiguous_work(large, 5, invalid), "too many targets accepted");
    require(!ce::partition_contiguous_work(overflow, 2, invalid), "overflow accepted");
    require(!ce::partition_contiguous_work(large, 2, std::span{invalid}.first(3)), "size mismatch accepted");
    require(invalid == unchanged, "invalid input changed output");

    std::array<std::uint64_t, 4096> long_work;
    for (std::size_t index = 0; index < long_work.size(); ++index)
    {
        long_work[index] = index % 19 == 0 ? 1009 : 1 + index % 13;
    }
    std::uint64_t previous = (std::numeric_limits<std::uint64_t>::max)();
    for (std::uint32_t groups = 1; groups <= 8; ++groups)
    {
        const auto longest = check(long_work, groups, false);
        require(longest <= previous, "more targets increased maximum load");
        previous = longest;
        ++cases;
    }
    std::printf("WORK_PARTITION_OK cases=%llu oracle_cases=%llu stress_cases=%llu invalid_cases=4 "
                "allocations=%llu oracle=all-cuts scope=CPU-partition-only\n",
                static_cast<unsigned long long>(cases), static_cast<unsigned long long>(oracleCases),
                static_cast<unsigned long long>(cases - oracleCases),
                static_cast<unsigned long long>(allocation_count.load()));
}
