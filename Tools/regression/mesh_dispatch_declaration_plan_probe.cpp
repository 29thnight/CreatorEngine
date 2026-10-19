#include "../../Engine/RenderEngine/MeshTransformDeclarationPlan.h"

namespace
{
    std::atomic<bool> measureAllocations{};
    std::atomic<std::size_t> allocationCount{};
} // namespace

// Keep allocation instrumentation observable and out of callers' optimizer paths.
#if defined(_MSC_VER)
#define CE_ALLOCATION_PROBE_NOINLINE __declspec(noinline)
#else
#define CE_ALLOCATION_PROBE_NOINLINE __attribute__((noinline))
#endif

CE_ALLOCATION_PROBE_NOINLINE void* operator new(std::size_t bytes)
{
    if (measureAllocations.load(std::memory_order_relaxed))
    {
        allocationCount.fetch_add(1, std::memory_order_relaxed);
    }
    if (void* memory = std::malloc(bytes ? bytes : 1))
    {
        return memory;
    }
    throw std::bad_alloc();
}

CE_ALLOCATION_PROBE_NOINLINE void* operator new[](std::size_t bytes)
{
    return ::operator new(bytes);
}
CE_ALLOCATION_PROBE_NOINLINE void operator delete(void* memory) noexcept
{
    std::free(memory);
}
CE_ALLOCATION_PROBE_NOINLINE void operator delete[](void* memory) noexcept
{
    std::free(memory);
}
CE_ALLOCATION_PROBE_NOINLINE void operator delete(void* memory, std::size_t) noexcept
{
    std::free(memory);
}
CE_ALLOCATION_PROBE_NOINLINE void operator delete[](void* memory, std::size_t) noexcept
{
    std::free(memory);
}

#undef CE_ALLOCATION_PROBE_NOINLINE

namespace
{
    using Plan = material_graph::MeshTransformDeclarationPlan;

    void Check(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    struct Group
    {
        std::uint32_t job{}, firstVertex{};
        bool operator==(const Group&) const = default;
    };

    struct Source
    {
        std::uint32_t points{};
        std::array<std::span<const std::byte>, 3> inputs;
    };

    void CheckRanges(Plan& plan)
    {
        std::vector<std::byte> vertices((384u << 10) + 13u);
        std::vector<std::byte> separateVertices(vertices.size());
        std::array<std::byte, 65> bones;
        std::array<std::byte, 132> lods;
        for (std::size_t index = 0; index < vertices.size(); ++index)
        {
            vertices[index] = std::byte(index % 251);
            separateVertices[index] = vertices[index];
        }
        for (std::size_t index = 0; index < bones.size(); ++index)
        {
            bones[index] = std::byte(index + 1);
        }
        for (std::size_t index = 0; index < lods.size(); ++index)
        {
            lods[index] = std::byte(index + 2);
        }
        const std::array<Source, 5> sources{{
            {33, {vertices, bones, lods}},
            {0, {vertices, {}, lods}},
            {31, {separateVertices, bones, lods}},
            {32, {std::span<const std::byte>(vertices).first(17), {}, {}}},
            {1, {vertices, bones, lods}},
        }};
        // Independent scalar serializer of the former staging layout, including
        // pointer-and-length identity and zero-initialized 16-byte alignment gaps.
        std::map<std::pair<std::uintptr_t, std::size_t>, std::uint32_t> offsets;
        std::vector<std::byte> expected;
        std::array<std::array<std::uint32_t, 3>, sources.size()> expectedOffsets;
        std::vector<Group> expectedGroups;
        for (std::size_t index = 0; index < sources.size(); ++index)
        {
            const auto& source = sources[index];
            for (unsigned part = 0; part < source.inputs.size(); ++part)
            {
                const auto bytes = source.inputs[part];
                const auto key = std::pair{reinterpret_cast<std::uintptr_t>(bytes.data()), bytes.size()};
                const auto found = offsets.find(key);
                if (found != offsets.end())
                {
                    expectedOffsets[index][part] = found->second;
                    continue;
                }
                const auto offset = (expected.size() + 15u) & ~std::size_t{15u};
                expected.resize(offset + bytes.size());
                if (!bytes.empty())
                {
                    std::memcpy(expected.data() + offset, bytes.data(), bytes.size());
                }
                offsets.emplace(key, static_cast<std::uint32_t>(offset));
                expectedOffsets[index][part] = static_cast<std::uint32_t>(offset);
            }
            for (std::uint32_t firstVertex = 0; firstVertex < source.points; firstVertex += 32)
            {
                expectedGroups.push_back({static_cast<std::uint32_t>(index), firstVertex});
            }
        }
        std::string error;
        plan.Reset();
        for (const auto& source : sources)
        {
            Check(plan.Append(source.points, source.inputs[0], source.inputs[1], source.inputs[2], error),
                  "Accept bounded declaration inputs");
        }
        Check(plan.InputBytes() == expected.size() && plan.InputCount() == offsets.size() &&
                  plan.GroupCount() == expectedGroups.size(),
              "Prefix sizes and exact identity match the scalar layout");
        for (std::size_t index = 0; index < sources.size(); ++index)
        {
            Check(plan.GetJob(index).m_inputOffsets == expectedOffsets[index], "Preserve first occurrence offsets");
        }
        std::vector<std::byte> copied(expected.size(), std::byte{0xcd});
        std::vector<Group> groups(expectedGroups.size());
        constexpr std::size_t slices = 8;
        std::array<std::thread, slices> workers;
        for (std::size_t slice = 0; slice < workers.size(); ++slice)
        {
            workers[slice] = std::thread(
                [&, slice]
                {
                    plan.CopyInputRange(copied, copied.size() * slice / slices, copied.size() * (slice + 1) / slices);
                    plan.FillGroupRange(std::span<Group>(groups), groups.size() * slice / slices,
                                        groups.size() * (slice + 1) / slices);
                });
        }
        for (auto& worker : workers)
        {
            worker.join();
        }
        Check(copied == expected && groups == expectedGroups, "Disjoint worker ranges reproduce exact bytes/groups");
        allocationCount.store(0, std::memory_order_relaxed);
        measureAllocations.store(true, std::memory_order_relaxed);
        plan.Reset();
        bool appended = true;
        for (const auto& source : sources)
        {
            appended =
                plan.Append(source.points, source.inputs[0], source.inputs[1], source.inputs[2], error) && appended;
        }
        plan.CopyInputRange(copied, 0, copied.size());
        plan.FillGroupRange(std::span<Group>(groups), 0, groups.size());
        measureAllocations.store(false, std::memory_order_relaxed);
        Check(appended && allocationCount.load(std::memory_order_relaxed) == 0,
              "Warm declaration planning/copy/fill performs no heap allocations");
        Check(copied == expected && groups == expectedGroups, "Retained storage resets deterministically");
        bool rejected = false;
        try
        {
            plan.CopyInputRange(copied, 0, copied.size() + 1);
        }
        catch (const std::out_of_range&)
        {
            rejected = true;
        }
        Check(rejected && copied == expected, "Reject invalid ranges before writing output");
    }

    void CheckLimits(Plan& plan)
    {
        std::string error;
        std::array<std::byte, Plan::kMaxInputs> values;
        plan.Reset();
        for (std::size_t index = 0; index < Plan::kMaxJobs; ++index)
        {
            Check(plan.Append(1, std::span<const std::byte>(values).subspan(index * 3, 1),
                              std::span<const std::byte>(values).subspan(index * 3 + 1, 1),
                              std::span<const std::byte>(values).subspan(index * 3 + 2, 1), error),
                  "Admit full fixed job/input capacity");
        }
        Check(plan.InputCount() == Plan::kMaxInputs && !plan.Append(1, {}, {}, {}, error),
              "Reject excess jobs without reallocating scratch");
        plan.Reset();
        for (unsigned index = 0; index < 31; ++index)
        {
            Check(plan.Append(UINT32_MAX, {}, {}, {}, error), "Use wide arithmetic for group prefixes");
        }
        Check(!plan.Append(UINT32_MAX, {}, {}, {}, error) && plan.JobCount() == 31,
              "Reject group overflow before publication");
        // Reservation only: the planner inspects identity and sizes, never reads
        // these bytes until CopyInputRange is explicitly invoked.
        const auto large = std::make_unique_for_overwrite<std::byte[]>(Plan::kMaxInputBytes + 1);
        const std::span<const std::byte> storage(large.get(), Plan::kMaxInputBytes + 1);
        plan.Reset();
        Check(plan.Append(1, storage.first(Plan::kMaxInputBytes - 8), {}, {}, error) &&
                  plan.InputBytes() == Plan::kMaxInputBytes,
              "Account for alignment padding of empty unique inputs");
        Check(!plan.Append(1, storage.first(1), {}, {}, error), "Reject packed byte overflow");
        plan.Reset();
        Check(!plan.Append(1, storage, {}, {}, error), "Reject a source exceeding the byte budget");
        plan.Reset();
        Check(plan.Append(1, std::span<const std::byte>(values).first(1), {}, {}, error) && plan.JobCount() == 1,
              "A later declaration recovers from failed planning");
    }
} // namespace

int main()
{
    try
    {
        auto plan = std::make_unique<Plan>();
        CheckRanges(*plan);
        CheckLimits(*plan);
        std::printf("MESH_DECLARATION_PLAN_OK exact_layout=1 parallel_ranges=8 warm_allocations=0 limits=1\n");
        return 0;
    }
    catch (const std::exception& failure)
    {
        measureAllocations.store(false, std::memory_order_relaxed);
        std::fprintf(stderr, "MESH_DECLARATION_PLAN_FAILED %s\n", failure.what());
        return 1;
    }
}
