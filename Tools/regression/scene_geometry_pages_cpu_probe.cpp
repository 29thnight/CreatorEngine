// ScenePrepare가 사용하는 제품 페이지 배치·바인딩 본문을 기준 커밋과 비교한다.
// RHI 값 타입과 record/page 선언은 그대로 쓰고 mesh/device 경계만 대체한다.
#include "RHI/RHIResourceTypes.h"
#include "MaterialGraphGeometryContract.h"

namespace Probe
{
    std::uint64_t checks{}, vertexSearches{}, topologySearches{}, vertexComparisons{}, topologyComparisons{};
    bool measureAllocations{};
    std::size_t allocations{};

    void Require(bool condition, const char* message)
    {
        ++checks;
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }
} // namespace Probe

#if defined(_MSC_VER)
#define CE_PAGE_PROBE_NOINLINE __declspec(noinline)
#else
#define CE_PAGE_PROBE_NOINLINE __attribute__((noinline))
#endif

CE_PAGE_PROBE_NOINLINE void* operator new(std::size_t bytes)
{
    if (Probe::measureAllocations)
    {
        ++Probe::allocations;
    }
    if (void* result = std::malloc(bytes ? bytes : 1))
    {
        return result;
    }
    throw std::bad_alloc();
}
CE_PAGE_PROBE_NOINLINE void* operator new[](std::size_t bytes)
{
    return ::operator new(bytes);
}
CE_PAGE_PROBE_NOINLINE void operator delete(void* memory) noexcept
{
    std::free(memory);
}
CE_PAGE_PROBE_NOINLINE void operator delete[](void* memory) noexcept
{
    std::free(memory);
}
CE_PAGE_PROBE_NOINLINE void operator delete(void* memory, std::size_t) noexcept
{
    std::free(memory);
}
CE_PAGE_PROBE_NOINLINE void operator delete[](void* memory, std::size_t) noexcept
{
    std::free(memory);
}

namespace math
{
    struct alignas(16) vector4
    {
        float x{}, y{}, z{}, w{};
    };
} // namespace math

class IRenderDeviceServices
{
  public:
    std::vector<std::vector<RHIBindingDesc>> m_tables;
    std::size_t m_failTable{};

    RHIBindingTable CreateBindings(std::span<const RHIBindingDesc> descriptions)
    {
        m_tables.emplace_back(descriptions.begin(), descriptions.end());
        if (m_tables.size() == m_failTable)
        {
            return {};
        }
        return {m_tables.size(), static_cast<std::uint32_t>(descriptions.size()), 1};
    }
};

namespace material_graph
{
    struct RenderBindings;
    struct ResidentBufferFrame;

    class MeshSurfaceBatch
    {
      public:
        RHIBufferHandle m_buffer;
        RHIBufferSlice m_meshlets;
        std::uint32_t m_elements{}, m_count{};

        RHIBufferHandle Buffer() const { return m_buffer; }
        RHIBufferSlice Meshlets() const { return m_meshlets; }
        std::uint32_t GeometryViewElements() const { return m_elements; }
        std::uint32_t Count() const { return m_count; }
    };

#include "SceneGeometryPagesProduction.inl"
} // namespace material_graph

namespace Probe
{
    using namespace material_graph;
    using Batch = std::shared_ptr<const MeshSurfaceBatch>;

    struct DrawPair
    {
        Batch m_current, m_previous;
    };

    Batch Mesh(std::uint32_t buffer, std::uint32_t elements, std::uint32_t topology, std::uint64_t offset = 0,
               std::uint64_t bytes = 64)
    {
        auto batch = std::make_shared<MeshSurfaceBatch>();
        batch->m_buffer = {buffer};
        batch->m_elements = elements;
        batch->m_count = elements / 3 + 1;
        batch->m_meshlets = {{topology}, offset, bytes, nullptr};
        return batch;
    }

    template<class Data>
    void Initialize(Data& data, std::size_t count)
    {
        data.records.resize(count + 1);
        data.recordPages.resize(count + 1, UINT32_MAX);
        // 변경 대상이 아닌 208바이트도 포함해 매 record의 원래 값을 보존하는지 검사한다.
        const auto bytes = std::as_writable_bytes(std::span(data.records));
        std::fill(bytes.begin(), bytes.end(), std::byte{0x5a});
    }

    bool SameSlice(const RHIBufferSlice& left, const RHIBufferSlice& right)
    {
        return left.buffer == right.buffer && left.offset == right.offset && left.size == right.size &&
               left.cpuAddress == right.cpuAddress;
    }

    template<class Left, class Right>
    void Compare(const Left& left, const Right& right)
    {
        Require(left.records.size() == right.records.size(), "Record count changed.");
        Require(std::memcmp(left.records.data(), right.records.data(),
                            left.records.size() * sizeof(typename Left::Record)) == 0,
                "A 224-byte geometry record changed.");
        Require(left.recordPages == right.recordPages, "Record-to-page assignment changed.");
        Require(left.pages.size() == right.pages.size(), "Page splitting changed.");
        Require(left.materialBytes == right.materialBytes, "Geometry assignment changed material bytes.");
        for (std::size_t index = 0; index < left.pages.size(); ++index)
        {
            const auto& before = left.pages[index];
            const auto& after = right.pages[index];
            Require(before.vertices.size() == after.vertices.size(), "Vertex slot count changed.");
            Require(before.topology.size() == after.topology.size(), "Topology slot count changed.");
            Require(before.owners.size() == after.owners.size(), "Retained owner count changed.");
            Require(after.vertices.size() <= 64 && after.topology.size() <= 64, "Page exceeded 64 slots.");
            for (std::size_t slot = 0; slot < before.vertices.size(); ++slot)
            {
                Require(before.vertices[slot].buffer == after.vertices[slot].buffer &&
                            before.vertices[slot].elements == after.vertices[slot].elements,
                        "Vertex slot identity or insertion order changed.");
            }
            for (std::size_t slot = 0; slot < before.topology.size(); ++slot)
            {
                Require(SameSlice(before.topology[slot], after.topology[slot]),
                        "Topology slice identity or insertion order changed.");
            }
            for (std::size_t owner = 0; owner < before.owners.size(); ++owner)
            {
                Require(before.owners[owner].get() == after.owners[owner].get(),
                        "Exact retained owner identity or insertion order changed.");
            }
        }
    }

    auto DescriptionKey(const RHIBindingDesc& description)
    {
        return std::tuple{description.kind,        description.dim,
                          description.resource.id, description.bufferResource.id,
                          description.format,      description.depthAsColor,
                          description.allowNull,   description.mostDetailedMip,
                          description.mipLevels,   description.firstSlice,
                          description.sliceCount,  description.firstElement,
                          description.numElements, description.structureByteStride};
    }

    template<class Left, class Right>
    void CompareBindings(Left& left, Right& right, std::size_t failTable = 0)
    {
        IRenderDeviceServices before, after;
        before.m_failTable = after.m_failTable = failTable;
        std::string beforeError = "original", afterError = beforeError;
        Require(left.PrepareBindings(before, beforeError) == right.PrepareBindings(after, afterError),
                "Descriptor publication result changed.");
        Require(beforeError == afterError, "Descriptor failure diagnostic changed.");
        Require(before.m_tables.size() == after.m_tables.size(), "Descriptor publication count changed.");
        for (std::size_t table = 0; table < before.m_tables.size(); ++table)
        {
            Require(before.m_tables[table].size() == 64 && after.m_tables[table].size() == 64,
                    "Descriptor page lost its fully bound 64-slot contract.");
            for (std::size_t slot = 0; slot < 64; ++slot)
            {
                Require(DescriptionKey(before.m_tables[table][slot]) == DescriptionKey(after.m_tables[table][slot]),
                        "Descriptor contents or slot-zero padding changed.");
            }
        }
    }

    template<class Data>
    CE_PAGE_PROBE_NOINLINE void Populate(Data& data, std::span<const DrawPair> draws, std::string& error)
    {
        for (std::size_t index = 0; index < draws.size(); ++index)
        {
            if (!data.AssignGeometry(static_cast<std::uint32_t>(index + 1), draws[index].m_current,
                                     draws[index].m_previous, error))
            {
                throw std::runtime_error(error);
            }
        }
    }

    void Verify(std::span<const DrawPair> draws, bool everyRecord = false)
    {
        Baseline::SceneGpuData before;
        Prepared::SceneGpuData after;
        Initialize(before, draws.size());
        Initialize(after, draws.size());
        std::string beforeError = "unchanged success diagnostic", afterError = beforeError;
        for (std::size_t index = 0; index < draws.size(); ++index)
        {
            Require(before.AssignGeometry(static_cast<std::uint32_t>(index + 1), draws[index].m_current,
                                          draws[index].m_previous, beforeError),
                    "Baseline fixture rejected.");
            Require(after.AssignGeometry(static_cast<std::uint32_t>(index + 1), draws[index].m_current,
                                         draws[index].m_previous, afterError),
                    "Prepared fixture rejected.");
            Require(beforeError == afterError, "Successful preparation changed its diagnostic contract.");
            if (everyRecord)
            {
                Compare(before, after);
            }
        }
        Compare(before, after);
        CompareBindings(before, after);
        if (!draws.empty())
        {
            CompareBindings(before, after, 1);
            CompareBindings(before, after, 2);
        }
    }

    enum class Shape
    {
        Repeated,
        Unique,
        DistinctPrevious,
        Topology,
        Mixed
    };

    std::vector<DrawPair> Scene(std::size_t count, Shape shape)
    {
        std::vector<DrawPair> draws;
        draws.reserve(count);
        const auto shared = Mesh(1, 48, 10001);
        for (std::size_t index = 0; index < count; ++index)
        {
            const auto ordinal = static_cast<std::uint32_t>(index);
            Batch current, previous;
            switch (shape)
            {
            case Shape::Repeated:
                current = previous = shared;
                break;
            case Shape::Unique:
                current = previous = Mesh(ordinal + 1, 48, ordinal + 10001);
                break;
            case Shape::DistinctPrevious:
                current = Mesh(2 * ordinal + 1, 48, ordinal + 10001);
                previous = Mesh(2 * ordinal + 2, 48, ordinal + 10001);
                break;
            case Shape::Topology:
                current = previous = Mesh(1, 48, 10001, 4ull * index);
                break;
            case Shape::Mixed:
                current = Mesh(ordinal % 73 + 1, 48 + (ordinal % 5) * 16, 10001 + ordinal % 3, 4ull * (ordinal % 97),
                               64 + 4ull * (ordinal % 7));
                previous = index % 3 ? current : Mesh(ordinal % 79 + 1, 48 + (ordinal % 11) * 16, 12001);
                break;
            }
            draws.push_back({std::move(current), std::move(previous)});
        }
        return draws;
    }

    void Boundaries()
    {
        for (std::uint32_t filled = 0; filled <= 64; ++filled)
        {
            for (std::uint32_t shape = 0; shape < 3; ++shape)
            {
                std::vector<DrawPair> prefix;
                prefix.reserve(filled + 2);
                for (std::uint32_t index = 0; index < filled; ++index)
                {
                    auto current = Mesh(shape == 1 ? 1 : index + 1, 48, 10001, shape == 0 ? 0 : 4ull * index);
                    prefix.push_back({current, current});
                }
                for (std::uint32_t transition = 0; transition < 6; ++transition)
                {
                    auto draws = prefix;
                    auto current = Mesh(transition == 1 || transition == 4 ? 1 : 1001, transition == 4 ? 64 : 48, 10001,
                                        transition == 5 ? 0 : 1024, transition == 5 ? 68 : 64);
                    auto previous = transition == 0   ? current
                                    : transition == 1 ? Mesh(1002, 48, 10001)
                                    : transition == 2 ? Mesh(1, 48, 10001)
                                    : transition == 3
                                        ? Mesh(1002, 48, 10001)
                                        : Mesh(current->Buffer().id, current->GeometryViewElements(), 20001);
                    draws.push_back({current, previous});
                    draws.push_back({Mesh(current->Buffer().id, current->GeometryViewElements(), 10001,
                                          current->Meshlets().offset, current->Meshlets().size),
                                     previous});
                    Verify(draws, true);
                }
            }
        }
    }

    void Failures()
    {
        const auto valid = Mesh(1, 48, 10001);
        for (unsigned failure = 0; failure < 11; ++failure)
        {
            Baseline::SceneGpuData before;
            Prepared::SceneGpuData after;
            Initialize(before, 3);
            Initialize(after, 3);
            const std::array prefix{DrawPair{valid, valid}};
            std::string beforeError, afterError;
            Populate(before, prefix, beforeError);
            Populate(after, prefix, afterError);
            Batch current = valid, previous = valid;
            std::uint32_t record = 2;
            switch (failure)
            {
            case 0:
                record = 0;
                break;
            case 1:
                record = 4;
                break;
            case 2:
                before.recordPages.pop_back();
                after.recordPages.pop_back();
                break;
            case 3:
                current.reset();
                break;
            case 4:
                previous.reset();
                break;
            case 5:
                current = Mesh(0, 48, 10001);
                break;
            case 6:
                previous = Mesh(0, 48, 10001);
                break;
            case 7:
                current = Mesh(1, 48, 0);
                break;
            case 8:
                current = Mesh(1, 48, 10001, 1);
                break;
            case 9:
                current = Mesh(1, 48, 10001, 0, 65);
                break;
            case 10:
                current = Mesh(1, 48, 10001, 4ull * (std::uint64_t(UINT32_MAX) + 1));
                break;
            }
            Require(!before.AssignGeometry(record, current, previous, beforeError), "Baseline accepted bad input.");
            Require(!after.AssignGeometry(record, current, previous, afterError), "Prepared accepted bad input.");
            Require(!beforeError.empty() && beforeError == afterError, "Failure diagnostic changed.");
            Compare(before, after);
        }
        for (const auto bytes :
             std::array<std::uint64_t, 3>{0, 4ull * std::uint64_t(UINT32_MAX), 4ull * (std::uint64_t(UINT32_MAX) + 1)})
        {
            Baseline::SceneGpuData before;
            Prepared::SceneGpuData after;
            Initialize(before, 1);
            Initialize(after, 1);
            const auto current = Mesh(1, 48, 10001, 4ull * std::uint64_t(UINT32_MAX), bytes);
            std::string beforeError, afterError;
            const bool expected = bytes / 4 <= UINT32_MAX;
            Require(before.AssignGeometry(1, current, current, beforeError) == expected, "Baseline raw boundary.");
            Require(after.AssignGeometry(1, current, current, afterError) == expected, "Prepared raw boundary.");
            Require(beforeError == afterError, "Raw addressing boundary diagnostic changed.");
            Compare(before, after);
        }
    }

    struct Operations
    {
        std::uint64_t m_vertexSearches{}, m_topologySearches{}, m_vertexComparisons{}, m_topologyComparisons{};
        std::size_t m_allocations{};
    };

    template<class Data>
    Operations Count(std::span<const DrawPair> draws)
    {
        Data data;
        Initialize(data, draws.size());
        std::string error;
        vertexSearches = topologySearches = vertexComparisons = topologyComparisons = 0;
        allocations = 0;
        measureAllocations = true;
        Populate(data, draws, error);
        measureAllocations = false;
        return {vertexSearches, topologySearches, vertexComparisons, topologyComparisons, allocations};
    }

    void OperationCounts(const char* label, std::span<const DrawPair> draws)
    {
        const auto before = Count<BaselineCounted::SceneGpuData>(draws);
        const auto after = Count<PreparedCounted::SceneGpuData>(draws);
        Require(after.m_vertexSearches <= 2 * draws.size() && after.m_topologySearches <= draws.size(),
                "Prepared assignment exceeded three searches per record.");
        Require(after.m_vertexComparisons <= before.m_vertexComparisons &&
                    after.m_topologyComparisons <= before.m_topologyComparisons,
                "Prepared assignment repeated more slot comparisons.");
        Require(before.m_allocations == after.m_allocations, "Page output allocation sequence changed.");
        std::printf("OPERATIONS %s records=%zu vertex_searches=%llu/%llu topology_searches=%llu/%llu "
                    "vertex_comparisons=%llu/%llu topology_comparisons=%llu/%llu allocations=%zu/%zu\n",
                    label, draws.size(), static_cast<unsigned long long>(before.m_vertexSearches),
                    static_cast<unsigned long long>(after.m_vertexSearches),
                    static_cast<unsigned long long>(before.m_topologySearches),
                    static_cast<unsigned long long>(after.m_topologySearches),
                    static_cast<unsigned long long>(before.m_vertexComparisons),
                    static_cast<unsigned long long>(after.m_vertexComparisons),
                    static_cast<unsigned long long>(before.m_topologyComparisons),
                    static_cast<unsigned long long>(after.m_topologyComparisons), before.m_allocations,
                    after.m_allocations);
    }

    void WarmAllocationGate()
    {
        const auto draws = Scene(4096, Shape::Repeated);
        Prepared::SceneGpuData data;
        Initialize(data, draws.size());
        std::string error;
        Populate(data, draws, error);
        allocations = 0;
        measureAllocations = true;
        for (unsigned repeat = 0; repeat < 8; ++repeat)
        {
            Populate(data, draws, error);
        }
        measureAllocations = false;
        Require(allocations == 0, "Reusing existing page slots allocated memory.");
        std::printf("WARM_EXISTING_PAGE_ALLOCATIONS %zu records=4096 repeats=8\n", allocations);
    }

    template<class Data>
    double Time(std::span<const DrawPair> draws)
    {
        Data data;
        Initialize(data, draws.size());
        std::string error;
        const auto begin = std::chrono::steady_clock::now();
        Populate(data, draws, error);
        const auto end = std::chrono::steady_clock::now();
        Require(data.recordPages.back() == data.pages.size() - 1, "Timed work was incomplete.");
        return std::chrono::duration<double, std::micro>(end - begin).count();
    }

    void Benchmark(const char* label, std::span<const DrawPair> draws)
    {
        constexpr std::size_t warmup = 32, samples = 129;
        std::array<double, samples> before{}, after{};
        for (std::size_t index = 0; index < warmup + samples; ++index)
        {
            double first{}, second{};
            if (index % 2)
            {
                second = Time<Prepared::SceneGpuData>(draws);
                first = Time<Baseline::SceneGpuData>(draws);
            }
            else
            {
                first = Time<Baseline::SceneGpuData>(draws);
                second = Time<Prepared::SceneGpuData>(draws);
            }
            if (index >= warmup)
            {
                before[index - warmup] = first;
                after[index - warmup] = second;
            }
        }
        std::sort(before.begin(), before.end());
        std::sort(after.begin(), after.end());
        std::printf("TIMING %s records=%zu samples=%zu baseline_p50_us=%.3f prepared_p50_us=%.3f "
                    "baseline_p95_us=%.3f prepared_p95_us=%.3f change_percent=%.2f\n",
                    label, draws.size(), samples, before[samples / 2], after[samples / 2], before[samples * 95 / 100],
                    after[samples * 95 / 100], 100.0 * (after[samples / 2] / before[samples / 2] - 1.0));
    }
} // namespace Probe

int main(int argc, char** argv)
{
    try
    {
        Probe::Verify({});
        Probe::Boundaries();
        Probe::Failures();
        Probe::WarmAllocationGate();
        for (const auto& [name, shape] :
             std::array{std::pair{"repeated", Probe::Shape::Repeated}, std::pair{"unique", Probe::Shape::Unique},
                        std::pair{"distinct_previous", Probe::Shape::DistinctPrevious},
                        std::pair{"topology", Probe::Shape::Topology}, std::pair{"mixed", Probe::Shape::Mixed}})
        {
            for (const auto count : {std::size_t{1}, std::size_t{64}, std::size_t{4096}})
            {
                const auto draws = Probe::Scene(count, shape);
                Probe::Verify(draws);
                Probe::OperationCounts(name, draws);
                if (argc > 1 && std::string_view(argv[1]) == "--benchmark")
                {
                    Probe::Benchmark(name, draws);
                }
            }
        }
        const auto ragged = Probe::Scene(4129, Probe::Shape::Mixed);
        Probe::Verify(ragged);
        std::printf("PASS scene geometry pages: %llu checks; exact record/page/owner/descriptor/error parity.\n",
                    static_cast<unsigned long long>(Probe::checks));
        return 0;
    }
    catch (const std::exception& failure)
    {
        Probe::measureAllocations = false;
        std::fprintf(stderr, "FAIL scene geometry pages: %s\n", failure.what());
        return 1;
    }
}

#undef CE_PAGE_PROBE_NOINLINE
