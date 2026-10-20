// 제품 선언/캐시 본문과 arena를 CPU 경계에서 검증한다. native/GPU 성능 검사가 아니다.
#include "Engine/RenderEngine/RenderArena.h"
#include "Engine/RenderEngine/RHI/RHIFormat.h"
#include "Engine/RenderEngine/RHI/RHIResourceState.h"
#include "Engine/Utility_Framework/DynamicBitset.h"
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <new>

namespace
{
    bool count_allocations{};
    size_t allocations{}, checks{};
    int fail_arena_after{-1}, fail_new_after{-1};

    void require(bool value, const char* message)
    {
        ++checks;
        if (!value)
        {
            throw std::runtime_error(message);
        }
    }

    bool allocation_fails(int& remaining)
    {
        if (remaining < 0)
        {
            return false;
        }
        if (remaining-- == 0)
        {
            remaining = -1;
            return true;
        }
        return false;
    }

    void* allocate_bytes(size_t size, size_t alignment)
    {
        if (count_allocations)
        {
            ++allocations;
        }
        void* result{};
        const auto effective = (std::max)(alignment, sizeof(void*));
        if (posix_memalign(&result, effective, size ? size : 1) != 0)
        {
            return nullptr;
        }
        return result;
    }
} // namespace

void* graph_declaration_allocate(size_t size, size_t alignment)
{
    return allocation_fails(fail_arena_after) ? nullptr : allocate_bytes(size, alignment);
}
[[gnu::noinline]] void graph_declaration_free(void* pointer) noexcept
{
    std::free(pointer);
}
void* operator new(size_t size)
{
    if (!allocation_fails(fail_new_after))
    {
        if (auto* result = allocate_bytes(size, alignof(std::max_align_t)))
        {
            return result;
        }
    }
    throw std::bad_alloc();
}
void* operator new[](size_t size)
{
    return ::operator new(size);
}
void* operator new(size_t size, std::align_val_t alignment)
{
    if (!allocation_fails(fail_new_after))
    {
        if (auto* result = allocate_bytes(size, static_cast<size_t>(alignment)))
        {
            return result;
        }
    }
    throw std::bad_alloc();
}
void* operator new[](size_t size, std::align_val_t alignment)
{
    return ::operator new(size, alignment);
}
void operator delete(void* value) noexcept
{
    graph_declaration_free(value);
}
void operator delete[](void* value) noexcept
{
    graph_declaration_free(value);
}
void operator delete(void* value, size_t) noexcept
{
    graph_declaration_free(value);
}
void operator delete[](void* value, size_t) noexcept
{
    graph_declaration_free(value);
}
void operator delete(void* value, std::align_val_t) noexcept
{
    graph_declaration_free(value);
}
void operator delete[](void* value, std::align_val_t) noexcept
{
    graph_declaration_free(value);
}
void operator delete(void* value, size_t, std::align_val_t) noexcept
{
    graph_declaration_free(value);
}
void operator delete[](void* value, size_t, std::align_val_t) noexcept
{
    graph_declaration_free(value);
}

namespace ce
{
    template<size_t Count>
    struct marker_text
    {
        char value[Count];
        constexpr marker_text(const char (&text)[Count]) { std::copy_n(text, Count, value); }
    };
    template<marker_text Name>
    int marker()
    {
        return 0;
    }
    struct profile_scope
    {
        explicit profile_scope(int) {}
    };
} // namespace ce

// 아래 값은 선언 저장 검사의 미사용 native 경계다.
struct RHIRenderTargetBinding
{
};
#include "GraphGlobals.inl"

#if 0
// GRAPH_FIXTURE_BEGIN
class EnhancedRenderGraph
{
public:
    struct ExecuteContext { unsigned value{}; };
    using ExecuteCallback = std::function<void(const ExecuteContext&)>;
    using SplitExecuteCallback = std::function<void(const ExecuteContext&, uint32_t, uint32_t)>;
    using RepeatedExecuteCallback = SplitExecuteCallback;
    struct DiagnosticSnapshot { struct VersionEdge { uint16_t producer{}, consumer{}; }; };
    struct Stats { uint32_t dependencyWaveCount{}; };
    /* GRAPH_TYPES */
    /* GRAPH_TEMPLATES */
    /* GRAPH_DECLARATIONS */

    struct DeclarationWork
    {
        ImportedIndexCache importedTextures, importedBuffers;
        size_t nextPacket{};
    };
    std::unique_ptr<DeclarationWork> m_declarationWork{std::make_unique<DeclarationWork>()};
    ce::render_arena_resource m_packetArena;
    ce::render_vector<ResourceDeclaration> m_resourceDeclarations;
    ce::render_vector<PassDeclaration> m_passDeclarations;
    ce::render_vector<DeclarationPacket> m_declarationPackets;
    size_t m_packetFirstResource{}, m_packetFirstPass{};
    uint32_t m_declarationPacketJobs{};
    bool m_collectingDeclarationPackets{}, m_cancelledDeclarationPackets{};
    ce::render_vector<Resource> m_resourceStorage;
    std::span<Resource> m_resources;
    std::unordered_map<uint32_t, uint16_t> m_importedTextures, m_importedBuffers;
    std::vector<uint16_t> m_atomicResources;
    ce::render_arena_objects m_declarationData;
    ce::render_vector<Pass> m_passStorage;
    std::span<Pass> m_passes;
    std::vector<FinalStateRequirement> m_finalStateRequirements;
    RGSchedulingMode m_scheduling{RGSchedulingMode::ExplicitVersioned};
    RGOrderPolicy m_orderPolicy{RGOrderPolicy::DependencyOrder};
    RGMeasurementDomain m_measurementDomain{RGMeasurementDomain::Normal};
    bool m_aliasing{}, m_extendLifetimes{}, m_poison{}, m_compiled{};
    uint64_t m_resourceEpoch{NextResourceEpoch()};
    uint64_t m_recordedRecording{};
    bool m_statesCommitted{}, m_queueExecutionAttempted{};
    struct QueueDiagnostics {} m_queueDiagnostics;
    uint32_t m_queueExecutionMode{};
    std::shared_ptr<const StructuralSchedule> m_activeStructuralSchedule;
    bool m_executeOrderOverridden{};
    std::vector<uint16_t> m_executeOrder, m_criticalPath;
    std::vector<int32_t> m_dependencyWaves;
    std::vector<DiagnosticSnapshot::VersionEdge> m_versionEdges;
    std::shared_ptr<const std::string> m_timingSignature;
    std::string m_declarationError;
    Stats m_stats;
    void* m_preparedPool{};
    uint64_t m_preparedRecording{}, m_preparedDescriptors{};
    bool m_preparedRecordingConsumed{};

    // scheduler/native 제출은 이 검사의 대상이 아니다. 호출 순서는 기록한다.
    bool fixtureIdle{true};
    size_t fixtureJoins{}, fixtureCancels{}, fixtureReleases{};
    void RequireQueueIdle() const
    {
        if (!fixtureIdle) throw std::logic_error("fixture graph is recording");
    }
    void JoinDeclarationPackets() { ++fixtureJoins; }
    void CancelDeclarationPackets() { ++fixtureCancels; m_collectingDeclarationPackets = false; }
    void ReleaseResources() { ++fixtureReleases; }
    void FreezeCache(StructuralSchedule& cache) const
    {
        CaptureStructuralDeclarations(cache);
        cache.key = std::make_shared<const std::string>("fixture");
        cache.timingSignature = cache.key;
    }
    bool CacheOwnsData(const StructuralSchedule& cache) const
    {
        for (const auto& pass : cache.passes)
        {
            for (const auto& use : pass.usages)
                if (use.handle.epoch != 0) return false;
            for (const auto& phase : pass.phases)
                for (const auto& use : phase.usages)
                    if (use.handle.epoch != 0) return false;
        }
        for (size_t index = 0; index < cache.resources.size() && index < m_resources.size(); ++index)
            if (!cache.resources[index].versions.empty() &&
                cache.resources[index].versions.data() == m_resources[index].versions.data()) return false;
        for (size_t index = 0; index < cache.passes.size() && index < m_passes.size(); ++index)
            if (!cache.passes[index].usages.empty() &&
                cache.passes[index].usages.data() == m_passes[index].usages.data()) return false;
        return true;
    }
};
// GRAPH_FIXTURE_END
#endif

#include "GraphProduction.inl"

namespace
{
    using graph = current::EnhancedRenderGraph;
    using old_graph = baseline::EnhancedRenderGraph;
    using usage = graph::RGPassUsage;

    void mix(uint64_t& value, uint64_t part)
    {
        value = (value ^ part) * 1099511628211ull;
    }
    void mix(uint64_t& value, std::string_view part)
    {
        mix(value, part.size());
        for (unsigned char byte : part)
        {
            mix(value, byte);
        }
    }
    void mix(uint64_t& value, RGHandle handle)
    {
        mix(value, handle.index);
        mix(value, handle.version);
        mix(value, static_cast<unsigned>(handle.kind));
    }
    template<class Graph>
    uint64_t digest(const Graph& value)
    {
        uint64_t result{1469598103934665603ull};
        mix(result, value.m_resources.size());
        for (const auto& resource : value.m_resources)
        {
            mix(result, resource.name);
            mix(result, resource.bufferKind);
            mix(result, resource.imported);
            mix(result, resource.firstImport);
            mix(result, resource.handle.id);
            mix(result, resource.buffer.id);
            mix(result, resource.versions.size());
            for (const auto& version : resource.versions)
            {
                mix(result, version.parent);
                mix(result, version.modify);
                mix(result, version.atomicMin64);
            }
        }
        mix(result, value.m_passes.size());
        const auto uses = [&](const auto& values)
        {
            mix(result, values.size());
            for (const auto& use : values)
            {
                mix(result, use.handle);
                mix(result, static_cast<unsigned>(use.state));
                mix(result, static_cast<unsigned>(use.access));
                require(use.handle.epoch == value.m_resourceEpoch, "usage epoch changed during ownership transfer");
            }
        };
        for (const auto& pass : value.m_passes)
        {
            mix(result, pass.name);
            mix(result, pass.hasSideEffect);
            mix(result, pass.maxSlices);
            mix(result, pass.recordCost);
            mix(result, pass.repeated);
            mix(result, pass.repeatCount);
            uses(pass.usages);
            mix(result, pass.phases.size());
            for (const auto& phase : pass.phases)
            {
                mix(result, phase.name);
                uses(phase.usages);
            }
        }
        return result;
    }

    template<class Graph>
    void populate(Graph& value, unsigned seed, bool journal)
    {
        using Use = typename Graph::RGPassUsage;
        value.m_collectingDeclarationPackets = journal;
        std::array<RGHandle, 96> handles{};
        for (unsigned index = 0; index < handles.size(); ++index)
        {
            RGHandle handle;
            if (index % 4 == 0)
            {
                handle = value.ImportBuffer({index + 1}, RHIResourceState::Common, "buffer", nullptr);
            }
            else if (index % 4 == 1)
            {
                handle = value.ImportTexture({index + 1}, RHIResourceState::Common, "texture", nullptr);
            }
            else if (index % 4 == 2)
            {
                handle = value.CreateBuffer({1024, true, true, "transient-buffer"});
            }
            else
            {
                RGTextureDesc desc;
                desc.width = desc.height = 32;
                desc.allowUnorderedAccess = true;
                desc.name = "transient-texture";
                handle = value.CreateTexture(desc);
            }
            const auto writes = (index + seed) % 5 + 1;
            for (unsigned version = 0; version < writes; ++version)
            {
                handle = version == 0 ? value.Write(handle)
                         : handle.kind == RGResourceKind::Buffer && ((seed + index + version) & 1)
                             ? value.AtomicMin64(handle)
                             : value.Modify(handle);
                require(handle.IsValid(), "valid version append failed");
            }
            handles[index] = handle;
        }
        std::vector<Use> scratch;
        scratch.reserve(24);
        for (unsigned index = 0; index < 144; ++index)
        {
            scratch.clear();
            for (unsigned element = 0; element < (index + seed) % 17; ++element)
            {
                scratch.push_back({handles[(index + element) % handles.size()], RHIResourceState::ShaderResource,
                                   RGAccessMode::Read});
            }
            const auto single =
                Use{handles[index % handles.size()], RHIResourceState::ShaderResource, RGAccessMode::Read};
            RGPassId pass;
            switch (index % 9)
            {
            case 0:
                pass = value.AddPass("const", scratch, {}, false);
                break;
            case 1:
                pass = value.AddPass("rvalue", std::move(scratch), {}, true);
                break;
            case 2:
                pass = value.AddPass("span", std::span<const Use>{scratch}, {}, false);
                break;
            case 3:
                pass = value.AddPass("list", {single}, {}, false);
                break;
            case 4:
                pass = value.AddSplitPass("split-const", scratch, {}, 4, false, 12);
                break;
            case 5:
                pass = value.AddSplitPass("split-rvalue", std::move(scratch), {}, 0, false, 3);
                break;
            case 6:
                pass = value.AddSplitPass("split-list", {single}, {}, 8, true, 21);
                break;
            case 7:
            {
                std::array phases{typename Graph::RepeatedPhaseView{"first", scratch},
                                  typename Graph::RepeatedPhaseView{"second", std::span<const Use>{&single, 1}}};
                pass = value.AddRepeatedPassView("repeated-view", scratch, phases, 3, {}, false, 8);
                break;
            }
            default:
            {
                std::vector<typename Graph::RepeatedPhase> phases;
                phases.push_back({"owned", scratch});
                pass = value.AddRepeatedPass("repeated-owned", scratch, std::move(phases), 2, {}, true, 4);
                break;
            }
            }
            require(pass.IsValid(), "pass declaration failed");
            // 호출자의 저장 값을 즉시 바꿔 빌린 span이 남으면 검산에서 실패한다.
            for (auto& use : scratch)
            {
                use.handle = {};
            }
        }
        if (journal)
        {
            Graph::MaterializeDeclarationPacket({0, value.m_resources.size(), 0, value.m_passes.size()},
                                                value.m_resources.data(), value.m_resourceDeclarations.data(),
                                                value.m_passes.data(), value.m_passDeclarations.data());
            value.m_collectingDeclarationPackets = false;
        }
    }

    void verify_parity_and_cache()
    {
        graph current;
        old_graph baseline;
        for (unsigned frame = 0; frame < 40; ++frame)
        {
            populate(current, frame, (frame & 1) != 0);
            populate(baseline, frame, (frame & 1) != 0);
            require(digest(current) == digest(baseline), "baseline/current declaration data differ");
            graph::StructuralSchedule cache;
            current.FreezeCache(cache);
            require(current.CacheOwnsData(cache), "schedule cache borrowed active graph storage or epochs");
            require(current.MatchesStructuralSchedule(cache), "fresh copied structural declarations do not match");
            const auto stale = current.m_passes.front().usages.empty() ? current.VersionHandle(0, 0)
                                                                       : current.m_passes.front().usages.front().handle;
            current.Reset();
            baseline.Reset();
            require(!current.ValidVersionHandle(stale), "Reset accepted a previous graph epoch");
            require(current.m_passes.empty() && current.m_resources.empty(), "Reset retained active declarations");
            populate(current, frame, false);
            require(current.MatchesStructuralSchedule(cache), "owning cache did not survive graph reset/reuse");
            require(current.CacheOwnsData(cache), "cache storage changed after graph reset/reuse");
            current.m_resourceEpoch += 1;
            require(!current.MatchesStructuralSchedule(cache), "cache accepted stale current usage epochs");
            current.Reset();
        }
    }

    void verify_growth_and_rvalue()
    {
        graph value;
        const auto input = value.ImportBuffer({1}, RHIResourceState::Common, "initial", nullptr);
        const auto atomic = value.AtomicMin64(input);
        const auto* versions = value.m_resources.front().versions.data();
        std::vector<usage> source(3, {atomic, RHIResourceState::ShaderResource, RGAccessMode::Read});
        const auto capacity = source.capacity();
        const auto* caller = source.data();
        const auto pass = value.AddPass("owned", std::move(source), {}, false);
        require(source.empty() && source.capacity() == capacity && source.data() == caller,
                "rvalue caller storage was stolen or its capacity changed");
        const auto retained = value.m_passes[pass.index].usages;
        require(retained.data() != caller, "pass borrowed rvalue caller storage");
        source.assign(3, {});
        require(retained.front().handle.version == atomic.version, "caller mutation changed graph-owned usages");
        for (unsigned index = 0; index < 1024; ++index)
        {
            value.ImportTexture({index + 2}, RHIResourceState::Common, "growth", nullptr);
            value.AddPass("growth", retained, {}, false);
        }
        require(value.m_resources.front().versions.data() == versions, "outer Resource growth moved version payload");
        require(versions[atomic.version].atomicMin64, "outer Resource growth corrupted version flags");
        require(value.m_passes.front().usages.data() == retained.data(), "pass/arena growth invalidated usage storage");
        const auto resource_capacity = value.m_resources.front().versionStorage.capacity();
        value.Reset();
        require(value.m_resourceStorage.front().versionStorage.size() == 2, "Reset destroyed retained version slots");
        require(value.m_resourceStorage.front().versionStorage.capacity() == resource_capacity,
                "Reset discarded version capacity");
        const auto next = value.ImportBuffer({1}, RHIResourceState::Common, "initial", nullptr);
        const auto ordinary = value.Modify(next);
        require(value.m_resources.front().versions.data() == versions, "warm version append changed storage");
        require(!value.m_resources.front().versions[ordinary.version].atomicMin64,
                "warm append leaked previous AtomicMin64 flag");
        source.assign(3, {ordinary, RHIResourceState::ShaderResource, RGAccessMode::Read});
        value.AddSplitPass("split", std::move(source), {}, 2, false, 3);
        require(source.empty() && source.capacity() == capacity, "split rvalue caller capacity was not retained");
        const auto* moved_versions = value.m_resources.front().versions.data();
        graph::Resource destination;
        destination = std::move(value.m_resourceStorage.front());
        require(destination.versions.data() == moved_versions &&
                    destination.versions.data() == destination.versionStorage.data(),
                "Resource move assignment separated span from owning version storage");
    }

    void verify_failures_and_limits()
    {
        graph value;
        value.m_passStorage.reserve(1);
        std::vector<usage> source(4);
        const auto capacity = source.capacity();
        bool failed{};
        fail_arena_after = 0;
        try
        {
            value.AddPass("cold", std::move(source), {}, false);
        }
        catch (const std::bad_alloc&)
        {
            failed = true;
        }
        require(failed, "cold arena allocation failure was not surfaced");
        require(source.size() == 4 && source.capacity() == capacity, "failed copy consumed caller rvalue storage");
        require(value.m_passes.front().usages.empty(), "failed copy published a partial usage span");
        value.Reset();
        auto handle = value.ImportBuffer({1}, RHIResourceState::Common, "version", nullptr);
        auto& resource = value.m_resources.front();
        while (resource.versionStorage.size() < resource.versionStorage.capacity())
        {
            handle = value.Modify(handle);
        }
        const auto previous = resource.versions;
        failed = false;
        fail_new_after = 0;
        try
        {
            value.Modify(handle);
        }
        catch (const std::bad_alloc&)
        {
            failed = true;
        }
        require(failed, "version storage growth failure was not surfaced");
        require(resource.versions.data() == previous.data() && resource.versions.size() == previous.size(),
                "failed version append changed the active span");
        require(!value.Write({}).IsValid(), "invalid handle was accepted");
        auto invalid = handle;
        invalid.epoch += 1;
        require(!value.Modify(invalid).IsValid(), "wrong epoch was accepted");
        invalid = handle;
        invalid.kind = RGResourceKind::Texture;
        require(!value.AtomicMin64(invalid).IsValid(), "wrong resource kind was accepted");
        const auto unwritten = value.CreateBuffer({64, true, false, "unwritten"});
        require(!value.Modify(unwritten).IsValid(), "unwritten transient accepted Modify");
        require(value.Write(unwritten).IsValid(), "unwritten transient rejected Write");
        value.fixtureIdle = false;
        failed = false;
        try
        {
            value.AddPass("busy", std::move(source), {}, false);
        }
        catch (const std::logic_error&)
        {
            failed = true;
        }
        require(failed && source.size() == 4, "busy graph changed borrowed input before rejection");
        value.fixtureIdle = true;
        require(!value.CheckDeclarationCapacity(RGPassId::kInvalid, RGPassId::kInvalid, "pass", "limit"),
                "pass limit was not rejected");
        require(!value.AddPass("blocked", std::move(source), {}, false).IsValid() && source.size() == 4,
                "declaration error consumed caller data");
        value.Reset();
        handle = value.ImportBuffer({1}, RHIResourceState::Common, "limit", nullptr);
        for (uint32_t index = 1; index < RGHandle::kInvalid; ++index)
        {
            handle = value.Modify(handle);
        }
        require(handle.IsValid() && !value.Modify(handle).IsValid(), "version limit wrapped the active index");
    }

    struct destruction_log
    {
        std::array<int, 32> values{};
        size_t count{};
        void append(int value) noexcept { values[count++] = value; }
    };
    struct tracked
    {
        destruction_log* log;
        int id;
        bool copy{};
        static inline int copies_before_throw{-1};
        tracked(destruction_log& output, int value) : log(&output), id(value) {}
        tracked(const tracked& other) : log(other.log), id(other.id), copy(true)
        {
            if (copies_before_throw == 0)
            {
                throw std::runtime_error("copy failure");
            }
            if (copies_before_throw > 0)
            {
                --copies_before_throw;
            }
        }
        ~tracked() noexcept
        {
            if (copy)
            {
                log->append(id);
            }
        }
    };
    struct retained_owner
    {
        destruction_log* log;
        int id;
        retained_owner(destruction_log& output, int value) : log(&output), id(value) {}
        ~retained_owner() noexcept { log->append(id); }
    };
    struct throws_before_publication
    {
        destruction_log* log;
        throws_before_publication(destruction_log& output) : log(&output)
        {
            throw std::runtime_error("constructor failure");
        }
        ~throws_before_publication() noexcept { log->append(99); }
    };
    struct trivial_throws
    {
        trivial_throws() { throw std::runtime_error("trivial constructor failure"); }
    };

    void verify_arena_lifetimes()
    {
        ce::render_arena_objects arena;
        const std::array<uint32_t, 7> source{1, 2, 3, 4, 5, 6, 7};
        const auto copied = arena.copy<uint32_t>(source);
        require(arena.stats().usedBytes == sizeof(source), "trivial array allocated destructor bookkeeping");
        require(std::equal(copied.begin(), copied.end(), source.begin()), "trivial array copy changed bytes");
        arena.emplace<uint64_t>(91);
        require(arena.stats().usedBytes == sizeof(source) + sizeof(uint64_t),
                "trivial object allocated destructor bookkeeping");
        struct alignas(128) aligned_value
        {
            std::array<uint64_t, 16> data{};
        };
        const std::array<aligned_value, 2> aligned{};
        const auto aligned_copy = arena.copy<aligned_value>(aligned);
        require(reinterpret_cast<uintptr_t>(aligned_copy.data()) % 128 == 0, "arena copy lost over-alignment");
        require(arena.copy<uint32_t>({}).empty(), "empty arena copy allocated a payload");
        destruction_log log;
        arena.emplace<retained_owner>(log, 1);
        std::array owners{tracked(log, 2), tracked(log, 3), tracked(log, 4)};
        arena.copy<tracked>(owners);
        arena.emplace<retained_owner>(log, 5);
        arena.reset();
        require(log.count == 5 && log.values[0] == 5 && log.values[1] == 4 && log.values[2] == 3 &&
                    log.values[3] == 2 && log.values[4] == 1,
                "nontrivial arena destruction order changed");
        log.count = 0;
        tracked::copies_before_throw = 1;
        bool failed{};
        try
        {
            arena.copy<tracked>(owners);
        }
        catch (const std::runtime_error&)
        {
            failed = true;
        }
        tracked::copies_before_throw = -1;
        require(failed && log.count == 1 && log.values[0] == 2, "partial copy was not destroyed immediately");
        arena.reset();
        require(log.count == 1, "failed array was destroyed twice");
        failed = false;
        try
        {
            arena.emplace<throws_before_publication>(log);
        }
        catch (const std::runtime_error&)
        {
            failed = true;
        }
        require(failed, "nontrivial constructor failure was lost");
        try
        {
            arena.emplace<trivial_throws>();
        }
        catch (const std::runtime_error&)
        {
        }
        arena.reset();
        require(log.count == 1, "failed constructor published a destructor");
        std::weak_ptr<int> observer;
        {
            std::array values{std::make_shared<int>(73)};
            observer = values.front();
            arena.copy<std::shared_ptr<int>>(values);
        }
        require(!observer.expired(), "arena did not retain nontrivial shared ownership");
        arena.reset();
        require(observer.expired(), "arena reset leaked shared ownership");

        graph value;
        log.count = 0;
        value.RetainDeclaration<retained_owner>(log, 6);
        {
            const auto token = std::make_shared<retained_owner>(log, 7);
            value.AddPass("callback", {}, [token](const auto&) {}, false);
        }
        value.Reset();
        require(log.count == 2 && log.values[0] == 7 && log.values[1] == 6,
                "Reset did not remove callbacks before destroying graph-owned declarations");
        require(value.fixtureCancels == 1 && value.fixtureReleases == 1, "Reset skipped boundary cleanup");
    }

    template<class Graph>
    struct warm_workload
    {
        Graph value;
        std::array<std::vector<typename Graph::RGPassUsage>, 128> scratch;
        void run(bool move)
        {
            value.Reset();
            auto handle = value.ImportBuffer({1}, RHIResourceState::Common, "warm", nullptr);
            for (unsigned index = 0; index < 128; ++index)
            {
                handle = value.Modify(handle);
                auto& uses = scratch[index];
                uses.assign(8, {handle, RHIResourceState::ShaderResource, RGAccessMode::Read});
                if (move)
                {
                    value.AddPass("warm", std::move(uses), {}, false);
                }
                else
                {
                    value.AddPass("warm", uses, {}, false);
                }
            }
        }
        size_t measure(bool move)
        {
            for (unsigned repeat = 0; repeat < 3; ++repeat)
            {
                run(move);
            }
            allocations = 0;
            count_allocations = true;
            run(move);
            count_allocations = false;
            return allocations;
        }
    };

    void verify_warm_allocations()
    {
        warm_workload<graph> current;
        warm_workload<old_graph> baseline;
        const auto old_lvalue = baseline.measure(false);
        const auto new_lvalue = current.measure(false);
        const auto old_rvalue = baseline.measure(true);
        const auto new_rvalue = current.measure(true);
        require(old_lvalue == 0 && new_lvalue == 0, "warm lvalue/version declaration unexpectedly allocated");
        require(old_rvalue == 128 && new_rvalue == 0,
                "rvalue caller capacity reuse did not eliminate warm allocations");
        std::cout << "ALLOCATIONS passes=128 usagesPerPass=8 baselineLvalue=" << old_lvalue
                  << " currentLvalue=" << new_lvalue << " baselineRvalue=" << old_rvalue
                  << " currentRvalue=" << new_rvalue
                  << " currentArenaUsedBytes=" << current.value.m_declarationData.stats().usedBytes << '\n';
    }
} // namespace

int main()
{
    try
    {
        verify_parity_and_cache();
        verify_growth_and_rvalue();
        verify_failures_and_limits();
        verify_arena_lifetimes();
        verify_warm_allocations();
        std::cout << "PASS checks=" << checks
                  << " exactDeclarationBodies=true exactCacheBodies=true actualArena=true"
                     " growth=true reset=true epoch=true cacheOwnership=true atomicFlagReset=true"
                     " partialFailure=true nontrivialDestruction=true nativeGPU=false frameTiming=false\n";
    }
    catch (const std::exception& failure)
    {
        count_allocations = false;
        std::cerr << "FAIL " << failure.what() << '\n';
        return 1;
    }
}
