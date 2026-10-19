// 실제 기준/변경 준비 본문과 수명 처리를 함께 실행한다. GPU 자원·파이프라인은 CPU 모형이다.
#include "RHI/RHIResourceTypes.h"
#include "../../Engine/EngineDiagnostics/ProfileScope.h"

#include <cstdlib>
#include <new>

namespace Probe
{
    std::atomic<std::uint64_t> checks{};
    thread_local bool countAllocations{};
    thread_local std::uint64_t allocations{};
    thread_local std::function<void()> reenter;

    void Require(bool condition, const char* message)
    {
        ++checks;
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    // Keep the matched malloc/free replacement opaque to GCC's interprocedural
    // mismatched-new-delete diagnostic; sanitizers still see both operations.
    [[gnu::noinline]] void Free(void* value) noexcept
    {
        std::free(value);
    }
} // namespace Probe

void* operator new(std::size_t bytes)
{
    if (Probe::countAllocations)
    {
        ++Probe::allocations;
    }
    if (auto* result = std::malloc(bytes ? bytes : 1))
    {
        return result;
    }
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes)
{
    return ::operator new(bytes);
}
void operator delete(void* value) noexcept
{
    Probe::Free(value);
}
void operator delete[](void* value) noexcept
{
    Probe::Free(value);
}
void operator delete(void* value, std::size_t) noexcept
{
    Probe::Free(value);
}
void operator delete[](void* value, std::size_t) noexcept
{
    Probe::Free(value);
}
void* operator new(std::size_t bytes, std::align_val_t alignment)
{
    if (Probe::countAllocations)
    {
        ++Probe::allocations;
    }
    void* result{};
    if (posix_memalign(&result, static_cast<std::size_t>(alignment), bytes ? bytes : 1) == 0)
    {
        return result;
    }
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes, std::align_val_t alignment)
{
    return ::operator new(bytes, alignment);
}
void operator delete(void* value, std::align_val_t) noexcept
{
    Probe::Free(value);
}
void operator delete[](void* value, std::align_val_t) noexcept
{
    Probe::Free(value);
}
void operator delete(void* value, std::size_t, std::align_val_t) noexcept
{
    Probe::Free(value);
}
void operator delete[](void* value, std::size_t, std::align_val_t) noexcept
{
    Probe::Free(value);
}

namespace math
{
    struct vector4
    {
        float x{}, y{}, z{}, w{};
    };
    struct matrix4x4
    {
        float m[4][4]{};
    };
} // namespace math

class IRenderDeviceServices
{
  public:
    struct Upload
    {
        std::vector<std::byte> m_bytes;
    };
    std::uint64_t m_recording{1}, m_descriptors{1};
    std::atomic<std::uint32_t> m_nextBuffer{1}, m_created{}, m_released{}, m_readbacks{}, m_readbacksReleased{};
    std::uint32_t m_failBuffer{}, m_failReadback{}, m_bufferCalls{}, m_readbackCalls{};
    bool m_supported{true}, m_meshSupported{true}, m_failUpload{}, m_failBindings{}, m_changeRecording{};
    std::string m_failPipeline;
    std::deque<Upload> m_uploads;
    std::vector<std::vector<RHIBindingDesc>> m_bindings;
    std::uint64_t GetCurrentUploadRecordingId() const { return m_recording; }
    std::uint64_t GetDescriptorVersionToken() const { return m_descriptors; }
    RHIIndirectDrawCapabilities GetIndirectDrawCapabilities() const
    {
        RHIIndirectDrawCapabilities result{};
        result.indexedDraw = m_supported;
        return result;
    }
    RHIMeshShaderCapabilities GetMeshShaderCapabilities() const
    {
        RHIMeshShaderCapabilities result{};
        result.meshShader = m_meshSupported;
        result.maxGroupCountX = result.maxGroupCountY = result.maxGroupCountZ = UINT32_MAX;
        result.maxTotalGroupCount = UINT64_MAX;
        return result;
    }
    bool InitializePipeline(std::string_view name, std::string& error) const
    {
        if (m_failPipeline == name)
        {
            error = "injected native pipeline failure: " + std::string(name);
            return false;
        }
        return true;
    }
    bool CreateBuffer(const RHIBufferDesc&, RHIBufferHandle& result, std::string& error)
    {
        if (++m_bufferCalls == m_failBuffer)
        {
            error = "injected native buffer failure";
            return false;
        }
        if (m_changeRecording)
        {
            ++m_recording;
        }
        result = RHIBufferHandle{m_nextBuffer.fetch_add(1)};
        ++m_created;
        return true;
    }
    void ReleaseBuffer(RHIBufferHandle) { ++m_released; }
    bool CreateBufferReadback(std::uint64_t bytes, RHIReadback& result, std::string& error)
    {
        if (++m_readbackCalls == m_failReadback)
        {
            error = "injected native readback failure";
            return false;
        }
        result.buffer = RHIBufferHandle{m_nextBuffer.fetch_add(1)};
        result.sliceBytes = bytes;
        ++m_readbacks;
        return true;
    }
    void ReleaseReadback(RHIReadback) { ++m_readbacksReleased; }
    bool MapReadback(RHIReadback readback, RHIReadbackImage& result, std::string&)
    {
        result.data.resize(readback.sliceBytes);
        return true;
    }
    bool ReserveUploadBatch(std::span<const RHIUploadRequest> requests, std::span<RHIBufferSlice> slices,
                            std::string& error)
    {
        if (m_failUpload)
        {
            error = "injected native upload failure";
            return false;
        }
        for (std::size_t i = 0; i < requests.size(); ++i)
        {
            auto& upload = m_uploads.emplace_back();
            upload.m_bytes.resize(requests[i].bytes);
            slices[i].buffer = RHIBufferHandle{m_nextBuffer.fetch_add(1)};
            slices[i].cpuAddress = upload.m_bytes.data();
            slices[i].size = upload.m_bytes.size();
        }
        return true;
    }
    RHIBindingTable CreateBindings(std::span<const RHIBindingDesc> descriptions)
    {
        if (m_failBindings)
        {
            return {};
        }
        m_bindings.emplace_back(descriptions.begin(), descriptions.end());
        RHIBindingTable result{};
        result.backend = m_bindings.size();
        result.version = m_descriptors;
        result.count = static_cast<std::uint32_t>(descriptions.size());
        return result;
    }
    void UnregisterUploadTransactionListener(void*) {}
};

class IRHIUploadTransactionListener
{
  public:
    virtual ~IRHIUploadTransactionListener() = default;
    virtual void OnUploadSubmitted(std::uint64_t, RHICompletionPoint) = 0;
    virtual void OnUploadCompleted(std::uint64_t) = 0;
    virtual void OnUploadAborted(std::uint64_t) = 0;
    virtual void OnUploadSubmissionRejected(std::uint64_t, RHICompletionPoint) = 0;
};
struct RGHandle
{
    std::uint32_t index{};
};
enum class RGAccessMode
{
    Read,
    Write
};
class RHIEncoder;
class EnhancedRenderGraph
{
  public:
    struct RGPassUsage
    {
        RGHandle handle;
        RHIResourceState state{};
        RGAccessMode access{};
    };
    struct ExecuteContext
    {
        const EnhancedRenderGraph* graph{};
        RHIEncoder* encoder{};
    };
};
struct EnhancedFrameContext
{
    IRenderDeviceServices* resources{};
    void* rootSignatures{reinterpret_cast<void*>(1)};
    void* psoManager{reinterpret_cast<void*>(2)};
    std::uint32_t width{1186}, height{514};
    std::uint64_t frameId{11}, sceneEpoch{7};
};
namespace LX::Runtime
{
    struct ComputeGeneration
    {
        std::uint32_t m_id{};
    };
    struct ComputePipeline
    {
        std::shared_ptr<const ComputeGeneration> m_generation;
        ComputePipeline() = default;
        explicit ComputePipeline(std::uint32_t id) : m_generation(std::make_shared<ComputeGeneration>(id)) {}
        const std::shared_ptr<const ComputeGeneration>& GetGeneration() const { return m_generation; }
    };
} // namespace LX::Runtime
class GpuGeometryOcclusion
{
  public:
    static constexpr std::uint32_t kMaximumLevels = 16;
    struct ViewKey
    {
        IRenderDeviceServices* device{};
        std::uint64_t recording{}, descriptors{}, frameId{}, sceneEpoch{};
        std::uint32_t width{}, height{};
        std::array<std::uint32_t, 16> projectionBits{};
        bool operator==(const ViewKey&) const = default;
    };
    struct History
    {
        ViewKey source;
    };
    struct Pyramid
    {
        ViewKey m_view;
        std::shared_ptr<History> m_history;
        void RequireView(const ViewKey& view) const
        {
            if (view != m_view)
            {
                throw std::runtime_error("injected HZB view mismatch");
            }
        }
        const std::shared_ptr<History>& HistoryOwner() const { return m_history; }
    };
    static std::uint32_t LevelCount(std::uint32_t width, std::uint32_t height)
    {
        if (!width || !height || width > (1u << kMaximumLevels) || height > (1u << kMaximumLevels))
        {
            return 0;
        }
        std::uint32_t levels{};
        do
        {
            width = (width + 1u) / 2u;
            height = (height + 1u) / 2u;
            ++levels;
        } while (width > 1u || height > 1u);
        return levels;
    }
    static ViewKey CaptureView(const EnhancedFrameContext& context, const math::matrix4x4& projection)
    {
        ViewKey result{context.resources,
                       context.resources->GetCurrentUploadRecordingId(),
                       context.resources->GetDescriptorVersionToken(),
                       context.frameId,
                       context.sceneEpoch,
                       context.width,
                       context.height};
        result.projectionBits = std::bit_cast<std::array<std::uint32_t, 16>>(projection);
        return result;
    }
};
namespace assets::cluster
{
    struct Stream
    {
        void SubmitGpuRequests(std::span<const std::uint32_t>) {}
    };
    struct ResidentSet
    {
    };
} // namespace assets::cluster
namespace material_graph
{
    struct MeshSurfaceInput
    {
        struct HierarchyData
        {
            std::uint32_t groupCount{1};
        } m_hierarchy;
        std::shared_ptr<assets::cluster::Stream> m_stream;
        std::shared_ptr<assets::cluster::Stream> ClusterStream() const
        {
            if (Probe::reenter)
            {
                auto callback = std::move(Probe::reenter);
                callback();
            }
            return m_stream;
        }
        const HierarchyData& Hierarchy() const { return m_hierarchy; }
    };
    struct MeshSurfaceBatch
    {
        IRenderDeviceServices* m_device{};
        std::shared_ptr<MeshSurfaceInput> m_input;
        std::shared_ptr<const assets::cluster::ResidentSet> m_residency;
        math::matrix4x4 m_world;
        std::uint32_t m_meshlets{1};
        bool m_direct{};
        RHIBufferSlice m_topology;
        IRenderDeviceServices* Device() const { return m_device; }
        const std::shared_ptr<MeshSurfaceInput>& Input() const { return m_input; }
        const std::shared_ptr<const assets::cluster::ResidentSet>& ClusterResidency() const { return m_residency; }
        const math::matrix4x4& ObjectToWorld() const { return m_world; }
        std::uint32_t MeshletCount() const { return m_meshlets; }
        bool UsesDirectClusters() const { return m_direct; }
        RHIBufferSlice Meshlets() const { return m_topology; }
    };
} // namespace material_graph

template<class>
struct PreparationAccess;
namespace Baseline
{
#include "Baseline.inl"
}
namespace Optimized
{
#include "Optimized.inl"
}

struct Snapshot
{
    std::vector<std::byte> m_bytes;
    std::vector<const material_graph::MeshSurfaceBatch*> m_owners;
    std::vector<std::uint32_t> m_offsets;
    std::vector<std::uint64_t> m_readbackBytes;
    std::array<std::uint64_t, 18> m_values{};
    bool operator==(const Snapshot&) const = default;
    template<class T>
    void Append(const T& value)
    {
        const auto bytes = std::as_bytes(std::span{&value, 1});
        m_bytes.insert(m_bytes.end(), bytes.begin(), bytes.end());
    }
    template<class T>
    void Append(std::span<const T> values)
    {
        const auto bytes = std::as_bytes(values);
        m_bytes.insert(m_bytes.end(), bytes.begin(), bytes.end());
    }
};

template<class Host>
struct PreparationAccess
{
    using Frame = typename Host::Frame;
    using Candidate = typename Host::Candidate;
    using Bin = typename Host::Bin;
    Host m_host;
    explicit PreparationAccess(IRenderDeviceServices& device)
    {
        m_host.m_device = &device;
        m_host.m_bufferPool = std::make_shared<typename Host::BufferPool>(device);
        std::uint32_t id{};
        const auto init = [&](auto& pipeline) { pipeline = LX::Runtime::ComputePipeline(++id); };
        init(m_host.m_reset);
        init(m_host.m_cull);
        init(m_host.m_meshletReset);
        init(m_host.m_meshletCull);
        init(m_host.m_meshletCount);
        init(m_host.m_meshletFinalize);
        init(m_host.m_meshletScatter);
        init(m_host.m_meshletOcclusionCull);
        init(m_host.m_meshletHistoryCull);
        init(m_host.m_meshletRecoveryCull);
        init(m_host.m_occlusionCull);
        init(m_host.m_sharedReset);
        init(m_host.m_sharedCull);
        init(m_host.m_sharedOcclusion);
        init(m_host.m_sharedCount);
        init(m_host.m_sharedFinalize);
        init(m_host.m_sharedScatter);
    }
    void Clear()
    {
        std::lock_guard lock(m_host.m_recordingMutex);
        for (auto& entry : m_host.m_recordings)
        {
            entry.second.owners.clear();
        }
    }
    std::size_t Owners() const
    {
        std::size_t count{};
        for (const auto& entry : m_host.m_recordings)
        {
            count += entry.second.owners.size();
        }
        return count;
    }
    static Snapshot Capture(const std::shared_ptr<const Frame>& frame)
    {
        Snapshot result;
        if (!frame)
        {
            return result;
        }
        result.m_values = {1,
                           frame->m_candidateCount,
                           frame->m_binCount,
                           frame->m_outputCount,
                           frame->m_meshletGroups,
                           frame->m_meshletJobs,
                           frame->m_visibleBytes,
                           frame->m_argumentBytes,
                           frame->m_width,
                           frame->m_height,
                           frame->m_occlusionLevelCount,
                           frame->m_ordered,
                           bool(frame->m_firstStage),
                           bool(frame->m_historyPyramid),
                           frame->m_recording,
                           frame->m_descriptors,
                           frame->m_cull ? frame->m_cull->m_id : 0,
                           frame->m_occlusionCull ? frame->m_occlusionCull->m_id : 0};
        result.Append(frame->m_preparedStats);
        if (frame->m_meshletInputs)
        {
            result.Append(frame->m_meshletInputs->constants);
            result.Append(std::span<const Candidate>(frame->m_meshletInputs->candidates));
            result.Append(std::span<const Bin>(frame->m_meshletInputs->bins));
        }
        else
        {
            for (const auto slice : {frame->m_candidates, frame->m_bins, frame->m_constants})
            {
                const auto* bytes = static_cast<const std::byte*>(slice.cpuAddress);
                result.m_bytes.insert(result.m_bytes.end(), bytes, bytes + slice.size);
            }
        }
        for (const auto& geometry : frame->m_meshletGeometries)
        {
            result.m_owners.push_back(geometry.get());
        }
        result.m_offsets = frame->m_meshletOffsets;
        for (const auto readback : frame->m_streamingFeedback)
        {
            result.m_readbackBytes.push_back(readback.sliceBytes);
        }
        return result;
    }
    static std::uint32_t GroupOwner(const Frame& frame, std::size_t bin)
    {
        return static_cast<std::uint32_t>(frame.m_meshletInputs->candidates[bin].sphere.z);
    }
    static std::uint32_t Flags(const Frame& frame, std::size_t bin)
    {
        return frame.m_meshletInputs->candidates[bin].flags;
    }
    static std::uint32_t Hash(const material_graph::MeshSurfaceBatch& geometry)
    {
        return Optimized::MeshletGroupLookup::Hash(
            reinterpret_cast<std::uintptr_t>(geometry.Input()->ClusterStream().get()),
            reinterpret_cast<std::uintptr_t>(geometry.ClusterResidency().get()),
            std::bit_cast<std::array<std::uint32_t, 16>>(geometry.ObjectToWorld()));
    }
    static void WrapGeneration(std::span<const std::shared_ptr<const material_graph::MeshSurfaceBatch>> geometries)
    {
        Optimized::MeshletGroupLookup lookup(geometries);
        for (auto& slot : lookup.m_scratch->m_slots)
        {
            slot = {Hash(*geometries.front()), UINT32_MAX, 1};
        }
        lookup.m_scratch->m_generation = UINT32_MAX;
    }
    static void CheckNestedScope(std::span<const std::shared_ptr<const material_graph::MeshSurfaceBatch>> geometries)
    {
        Optimized::MeshletGroupLookup outer(geometries);
        Probe::Require(outer.m_owned, "Outer lookup owns its per-thread scratch.");
        {
            Optimized::MeshletGroupLookup nested(geometries);
            Probe::Require(!nested.m_owned, "Nested lookup does not reset its caller's scratch.");
        }
        Probe::Require(outer.m_scratch->m_active, "Nested scope preserves active outer scratch.");
    }
    static constexpr std::size_t LookupScratchBytes() { return sizeof(Optimized::MeshletGroupLookup::Scratch); }
};

using BaselineAccess = PreparationAccess<Baseline::GpuGeometryVisibility>;
using OptimizedAccess = PreparationAccess<Optimized::GpuGeometryVisibility>;

math::matrix4x4 Identity()
{
    math::matrix4x4 result{};
    for (unsigned axis = 0; axis < 4; ++axis)
    {
        result.m[axis][axis] = 1.f;
    }
    return result;
}

struct InputSet
{
    std::shared_ptr<assets::cluster::Stream> m_stream = std::make_shared<assets::cluster::Stream>();
    std::shared_ptr<const assets::cluster::ResidentSet> m_residency = std::make_shared<assets::cluster::ResidentSet>();
    std::vector<std::shared_ptr<const material_graph::MeshSurfaceBatch>> m_geometry;
    void Build(IRenderDeviceServices& device, std::size_t count, bool direct, std::size_t uniqueWorlds = 0)
    {
        m_geometry.clear();
        m_geometry.reserve(count);
        for (std::size_t i = 0; i < count; ++i)
        {
            auto input = std::make_shared<material_graph::MeshSurfaceInput>();
            input->m_stream = m_stream;
            input->m_hierarchy.groupCount = 17;
            auto geometry = std::make_shared<material_graph::MeshSurfaceBatch>();
            geometry->m_device = &device;
            geometry->m_input = std::move(input);
            geometry->m_residency = m_residency;
            geometry->m_world = Identity();
            geometry->m_world.m[3][0] = static_cast<float>(uniqueWorlds ? i % uniqueWorlds : i);
            geometry->m_direct = direct;
            geometry->m_meshlets = static_cast<std::uint32_t>(1 + i % 7);
            geometry->m_topology.buffer = RHIBufferHandle{100000u + static_cast<std::uint32_t>(i)};
            geometry->m_topology.offset = i * 16;
            geometry->m_topology.size = 256;
            m_geometry.push_back(std::move(geometry));
        }
    }
    material_graph::MeshSurfaceBatch& Mutable(std::size_t i)
    {
        return *std::const_pointer_cast<material_graph::MeshSurfaceBatch>(m_geometry.at(i));
    }
};

struct Fixture
{
    IRenderDeviceServices m_device;
    EnhancedFrameContext m_context{&m_device};
    math::matrix4x4 m_projection = Identity();
    InputSet m_inputs;
    BaselineAccess m_baseline{m_device};
    OptimizedAccess m_optimized{m_device};
    std::shared_ptr<const BaselineAccess::Frame> m_oldFrame, m_oldFirst;
    std::shared_ptr<const OptimizedAccess::Frame> m_newFrame, m_newFirst;
    std::shared_ptr<const GpuGeometryOcclusion::Pyramid> m_history;
    bool m_current{}, m_ordered{}, m_first{};
    float m_error{1.25f};
    std::uint32_t m_lodWidth{}, m_lodHeight{};
    std::string m_oldError, m_newError;
    bool m_oldResult{}, m_newResult{};
    std::function<void(bool)> m_beforeVariant;

    void Clear()
    {
        m_oldFrame.reset();
        m_newFrame.reset();
        m_oldFirst.reset();
        m_newFirst.reset();
        m_baseline.Clear();
        m_optimized.Clear();
    }
    void Build(std::size_t count, bool direct = true, std::size_t uniqueWorlds = 0)
    {
        Clear();
        m_inputs.Build(m_device, count, direct, uniqueWorlds);
    }
    void Run(bool expected)
    {
        m_oldError = m_newError = "prior error";
        std::string oldException, newException;
        const auto recording = m_device.m_recording;
        m_device.m_bufferCalls = m_device.m_readbackCalls = 0;
        try
        {
            if (m_beforeVariant)
            {
                m_beforeVariant(false);
            }
            m_oldResult = m_baseline.m_host.PrepareMeshlets(
                m_context, m_projection, m_inputs.m_geometry, m_oldFrame, m_oldError, m_current, m_history,
                m_first ? m_oldFirst : nullptr, m_ordered, m_error, m_lodWidth, m_lodHeight);
        }
        catch (const std::exception& failure)
        {
            oldException = failure.what();
            m_oldResult = false;
        }
        m_device.m_recording = recording;
        m_device.m_bufferCalls = m_device.m_readbackCalls = 0;
        try
        {
            if (m_beforeVariant)
            {
                m_beforeVariant(true);
            }
            m_newResult = m_optimized.m_host.PrepareMeshlets(
                m_context, m_projection, m_inputs.m_geometry, m_newFrame, m_newError, m_current, m_history,
                m_first ? m_newFirst : nullptr, m_ordered, m_error, m_lodWidth, m_lodHeight);
        }
        catch (const std::exception& failure)
        {
            newException = failure.what();
            m_newResult = false;
        }
        m_device.m_recording = recording;
        Probe::Require(m_oldResult == m_newResult && m_newResult == expected,
                       "Prepare result parity and expected admission.");
        Probe::Require(oldException == newException && m_oldError == m_newError, "Exact exception and error parity.");
        Probe::Require(BaselineAccess::Capture(m_oldFrame) == OptimizedAccess::Capture(m_newFrame),
                       "Complete prepared bytes, offsets, owners, readbacks and metadata match baseline.");
        Probe::Require(m_baseline.Owners() == m_optimized.Owners(),
                       "Recording publication count matches on success and failure.");
    }
    void SaveFirst()
    {
        m_oldFirst = m_oldFrame;
        m_newFirst = m_newFrame;
        m_first = true;
        m_current = true;
    }
};

void CheckGeometryAndHistory()
{
    for (const auto count : {0u, 1u, 2u, 31u, 64u, 129u, 512u, 4096u})
    {
        for (const bool direct : {false, true})
        {
            Fixture fixture;
            fixture.Build(count, direct, direct ? 11 : 0);
            fixture.Run(true);
            if (count)
            {
                for (std::size_t bin = 0; bin < count; ++bin)
                {
                    Probe::Require(OptimizedAccess::GroupOwner(*fixture.m_newFrame, bin) == (direct ? bin % 11 : bin),
                                   "Independent first-bin group owner oracle.");
                }
                fixture.SaveFirst();
                fixture.Run(true);
                for (std::size_t bin = 0; bin < count; ++bin)
                {
                    Probe::Require((OptimizedAccess::Flags(*fixture.m_newFrame, bin) & 128u) != 0,
                                   "Same first-stage group data is reused.");
                }
                fixture.m_error = 2.f;
                fixture.Run(true);
                for (std::size_t bin = 0; bin < count; ++bin)
                {
                    Probe::Require((OptimizedAccess::Flags(*fixture.m_newFrame, bin) & 128u) == 0,
                                   "Changed LOD metric does not reuse first-stage group selection.");
                }
                fixture.m_first = fixture.m_current = false;
                fixture.m_ordered = true;
                fixture.Run(true);
                auto history = std::make_shared<GpuGeometryOcclusion::Pyramid>();
                history->m_view = GpuGeometryOcclusion::CaptureView(fixture.m_context, fixture.m_projection);
                history->m_history = std::make_shared<GpuGeometryOcclusion::History>();
                fixture.m_history = history;
                fixture.m_ordered = false;
                fixture.Run(true);
                history->m_history.reset();
                fixture.Run(false);
                Probe::Require(bool(fixture.m_newFrame),
                               "Post-prepare history failure retains the published frame like baseline.");
            }
        }
    }
    Fixture fixture;
    fixture.Build(16, true, 1);
    fixture.m_inputs.Mutable(1).m_world.m[0][1] = -0.f;
    fixture.m_inputs.Mutable(2).m_world.m[0][1] = std::bit_cast<float>(0x7fc00001u);
    fixture.m_inputs.Mutable(3).m_world.m[0][1] = std::bit_cast<float>(0x7fc00002u);
    fixture.m_inputs.Mutable(4).m_world = fixture.m_inputs.Mutable(2).m_world;
    fixture.m_inputs.Mutable(5).m_input->m_stream = std::make_shared<assets::cluster::Stream>();
    fixture.m_inputs.Mutable(6).m_residency = std::make_shared<assets::cluster::ResidentSet>();
    fixture.m_inputs.Mutable(7).m_direct = false;
    fixture.Run(true);
    for (const auto bin : {0u, 1u, 2u, 3u, 5u, 6u, 7u})
    {
        Probe::Require(OptimizedAccess::GroupOwner(*fixture.m_newFrame, bin) == bin,
                       "Full pointer and signed-zero/NaN bit identity.");
    }
    Probe::Require(OptimizedAccess::GroupOwner(*fixture.m_newFrame, 4) == 2,
                   "Equal NaN payload bits share the first bin.");
    fixture.m_projection.m[1][1] = std::numeric_limits<float>::infinity();
    fixture.Run(true);
}

void CheckFailures()
{
    for (unsigned failure = 0; failure < 20; ++failure)
    {
        Fixture fixture;
        fixture.Build(3, true, 1);
        switch (failure)
        {
        case 0:
            fixture.m_context.resources = nullptr;
            break;
        case 1:
            fixture.m_context.rootSignatures = nullptr;
            break;
        case 2:
            fixture.m_context.psoManager = nullptr;
            break;
        case 3:
            fixture.m_device.m_recording = 0;
            break;
        case 4:
            fixture.m_inputs.m_geometry[1].reset();
            break;
        case 5:
            fixture.m_inputs.Mutable(1).m_meshlets = 0;
            break;
        case 6:
            fixture.m_inputs.Mutable(1).m_device = nullptr;
            break;
        case 7:
            fixture.m_inputs.Mutable(1).m_input->m_hierarchy.groupCount = UINT32_MAX;
            break;
        case 8:
            fixture.m_inputs.Mutable(1).m_input->m_stream.reset();
            break;
        case 9:
            fixture.m_inputs.Mutable(1).m_residency.reset();
            break;
        case 10:
            fixture.m_inputs.Mutable(1).m_input->m_hierarchy.groupCount = 19;
            break;
        case 11:
            fixture.m_device.m_meshSupported = false;
            break;
        case 12:
            fixture.m_inputs.Mutable(1).m_meshlets = UINT32_MAX;
            break;
        case 13:
            fixture.m_current = true;
            fixture.m_context.width = 0;
            break;
        case 14:
            fixture.m_current = true;
            fixture.m_context.height = 65537;
            break;
        case 15:
            fixture.m_device.m_failBuffer = 1;
            break;
        case 16:
            fixture.m_device.m_failBuffer = 2;
            break;
        case 17:
            fixture.m_device.m_failReadback = 1;
            break;
        case 18:
            fixture.m_device.m_changeRecording = true;
            break;
        case 19:
            fixture.m_inputs.Mutable(1).m_topology.offset = 3;
            break;
        }
        fixture.Run(false);
    }
    for (const auto pipeline :
         {"Initialize", "InitializeMeshlets", "InitializeSharedMeshlets", "InitializeMeshletOcclusion"})
    {
        Fixture fixture;
        fixture.Build(3);
        fixture.m_current = true;
        fixture.m_device.m_failPipeline = pipeline;
        fixture.Run(false);
    }
    {
        Fixture fixture;
        fixture.Build(4097);
        fixture.Run(false);
    }
    {
        Fixture fixture;
        fixture.Build(32);
        fixture.m_device.m_supported = false;
        fixture.Run(true);
        Probe::Require(!fixture.m_oldFrame && !fixture.m_newFrame,
                       "Unsupported indirect device returns no prepared frame.");
    }
    for (const auto count : {0u, 2u})
    {
        Fixture fixture;
        fixture.Build(2);
        fixture.Run(true);
        fixture.SaveFirst();
        fixture.m_inputs.Build(fixture.m_device, count, true);
        fixture.m_history = std::make_shared<GpuGeometryOcclusion::Pyramid>();
        fixture.Run(false);
        fixture.m_history.reset();
        fixture.m_ordered = true;
        fixture.Run(false);
        fixture.m_ordered = false;
        fixture.Run(count == 0);
    }
}

void CheckCollisionsAndReentry()
{
    {
        Fixture full;
        full.Build(4096, true);
        full.Run(true);
        for (std::uint32_t bin = 0; bin < 4096; ++bin)
        {
            Probe::Require(OptimizedAccess::GroupOwner(*full.m_newFrame, bin) == bin,
                           "Maximum legal occupancy preserves every distinct first-bin key.");
        }
    }
    Fixture fixture;
    fixture.Build(3, true, 1);
    auto words = std::bit_cast<std::array<std::uint32_t, 16>>(fixture.m_inputs.Mutable(0).m_world);
    const auto stream = reinterpret_cast<std::uintptr_t>(fixture.m_inputs.m_stream.get());
    const auto residency = reinterpret_cast<std::uintptr_t>(fixture.m_inputs.m_residency.get());
    std::uint32_t prefix = 2166136261u;
    for (const auto word : {std::uint32_t(stream), std::uint32_t(std::uint64_t(stream) >> 32u),
                            std::uint32_t(residency), std::uint32_t(std::uint64_t(residency) >> 32u)})
    {
        prefix = (prefix ^ word) * 16777619u;
    }
    // Two different exact keys collide in the complete current hash, not only
    // its table index. The old ordered map supplies the independent owner oracle.
    const auto changedFirst = words[0] ^ 1u;
    words[1] ^= ((prefix ^ words[0]) * 16777619u) ^ ((prefix ^ changedFirst) * 16777619u);
    words[0] = changedFirst;
    fixture.m_inputs.Mutable(1).m_world = std::bit_cast<math::matrix4x4>(words);
    fixture.m_inputs.Mutable(2).m_world = fixture.m_inputs.Mutable(1).m_world;
    Probe::Require(OptimizedAccess::Hash(fixture.m_inputs.Mutable(0)) ==
                       OptimizedAccess::Hash(fixture.m_inputs.Mutable(1)),
                   "Fixture constructs a complete hash collision.");
    fixture.Run(true);
    Probe::Require(OptimizedAccess::GroupOwner(*fixture.m_newFrame, 1) == 1 &&
                       OptimizedAccess::GroupOwner(*fixture.m_newFrame, 2) == 1,
                   "Hash collisions require full-key equality and preserve first owner.");
    OptimizedAccess::WrapGeneration(fixture.m_inputs.m_geometry);
    fixture.Run(true);
    OptimizedAccess::CheckNestedScope(fixture.m_inputs.m_geometry);

    Fixture nested;
    nested.Build(71, true, 7);
    fixture.m_beforeVariant = [&](bool optimized)
    {
        Probe::reenter = [&, optimized]
        {
            if (optimized)
            {
                nested.m_newResult = nested.m_optimized.m_host.PrepareMeshlets(nested.m_context, nested.m_projection,
                                                                               nested.m_inputs.m_geometry,
                                                                               nested.m_newFrame, nested.m_newError);
            }
            else
            {
                nested.m_oldResult = nested.m_baseline.m_host.PrepareMeshlets(nested.m_context, nested.m_projection,
                                                                              nested.m_inputs.m_geometry,
                                                                              nested.m_oldFrame, nested.m_oldError);
            }
        };
    };
    fixture.Run(true);
    Probe::Require(nested.m_oldResult && nested.m_newResult && nested.m_oldError == nested.m_newError,
                   "Preparation reentry admits both variants.");
    Probe::Require(BaselineAccess::Capture(nested.m_oldFrame) == OptimizedAccess::Capture(nested.m_newFrame),
                   "Nested preparation uses exact independent group identity without corrupting outer slots.");
    fixture.m_beforeVariant = [](bool)
    { Probe::reenter = [] { throw std::runtime_error("injected preparation reentry exception"); }; };
    fixture.Run(false);
    fixture.m_beforeVariant = {};
    OptimizedAccess::CheckNestedScope(fixture.m_inputs.m_geometry);
    fixture.Run(true);
}

void CheckGenericValidation()
{
    using Candidate = BaselineAccess::Candidate;
    using Bin = BaselineAccess::Bin;
    for (unsigned scenario = 0; scenario < 17; ++scenario)
    {
        Fixture fixture;
        std::array<Candidate, 65> candidates{};
        std::array<Bin, 3> bins{{{9, 0, -3, 0}, {12, 32, 5, 0}, {7, 3, 2, 0}}};
        std::size_t count = 3, binCount = 2;
        bool expected = true, current = false;
        candidates[0] = {{1.f, 2.f, 3.f, 4.f}, 1, 0, 128, 0};
        candidates[1] = {{5.f, 6.f, 7.f, -1.f}, 0, 0, 0, 1};
        candidates[2] = {{8.f, 9.f, 10.f, 2.f}, 0, 1, 0, 0};
        switch (scenario)
        {
        case 0:
            break; // Unsorted, disjoint bins.
        case 1:
            binCount = 3;
            break; // Empty third bin.
        case 2:
            bins[0].preservedInstanceCount = 2;
            break;
        case 3:
            bins[0].preservedInstanceCount = 3;
            expected = false;
            break;
        case 4:
            bins[0].preservedInstanceCount = 2;
            candidates[2].sourceIndex = 0;
            expected = false;
            break;
        case 5:
            bins[0].preservedInstanceCount = 2;
            candidates[2].sourceIndex = 2;
            expected = false;
            break;
        case 6:
            candidates[1].bin = 2;
            expected = false;
            break;
        case 7:
            candidates[1].outputOffset = 3;
            expected = false;
            break;
        case 8:
            candidates[2].outputOffset = 64;
            expected = false;
            break;
        case 9:
            candidates[0].outputOffset = 0;
            expected = false;
            break;
        case 10:
            count = candidates.size();
            binCount = 1;
            for (auto& candidate : candidates)
            {
                candidate.outputOffset = UINT32_MAX - 63u;
                candidate.bin = 0;
            }
            expected = false;
            break;
        case 11:
            count = 0;
            break;
        case 12:
            count = binCount = 0;
            break;
        case 13:
            current = true;
            break;
        case 14:
            fixture.m_device.m_failUpload = true;
            expected = false;
            break;
        case 15:
            fixture.m_device.m_failBindings = true;
            expected = false;
            break;
        case 16:
            current = true;
            fixture.m_device.m_failPipeline = "InitializeOcclusion";
            expected = false;
            break;
        }
        const auto newCandidates = std::bit_cast<std::array<OptimizedAccess::Candidate, 65>>(candidates);
        const auto newBins = std::bit_cast<std::array<OptimizedAccess::Bin, 3>>(bins);
        const bool oldResult = fixture.m_baseline.m_host.Prepare(
            fixture.m_context, fixture.m_projection, std::span(candidates).first(count),
            std::span(bins).first(binCount), fixture.m_oldFrame, fixture.m_oldError, current);
        const bool newResult = fixture.m_optimized.m_host.Prepare(
            fixture.m_context, fixture.m_projection, std::span(newCandidates).first(count),
            std::span(newBins).first(binCount), fixture.m_newFrame, fixture.m_newError, current);
        Probe::Require(oldResult == newResult && newResult == expected,
                       "Generic validator admission matches baseline.");
        Probe::Require(fixture.m_oldError == fixture.m_newError, "Generic validator preserves exact failure messages.");
        Probe::Require(BaselineAccess::Capture(fixture.m_oldFrame) == OptimizedAccess::Capture(fixture.m_newFrame),
                       "Generic uploaded candidate/bin/constant bytes and offsets match baseline.");
    }
}

void CheckOwnershipAndParallelCalls()
{
    {
        Fixture fixture;
        fixture.Build(17, true, 3);
        fixture.Run(true);
        std::weak_ptr<const material_graph::MeshSurfaceBatch> owner = fixture.m_inputs.m_geometry.front();
        fixture.m_inputs.m_geometry.clear();
        fixture.m_oldFrame.reset();
        fixture.m_newFrame.reset();
        Probe::Require(!owner.expired(), "Recording retains immutable geometry after caller drops its frame.");
        fixture.m_baseline.Clear();
        Probe::Require(!owner.expired(), "Optimized recording independently retains immutable geometry.");
        fixture.m_optimized.Clear();
        Probe::Require(owner.expired(), "Lookup scratch retains no geometry or residency owners.");
    }
    IRenderDeviceServices device;
    InputSet inputs;
    inputs.Build(device, 257, true, 13);
    BaselineAccess baseline(device);
    OptimizedAccess optimized(device);
    constexpr std::size_t workerCount = 8, iterations = 12;
    std::array<Snapshot, workerCount> expected;
    for (std::size_t worker = 0; worker < workerCount; ++worker)
    {
        EnhancedFrameContext context{&device};
        context.width += static_cast<std::uint32_t>(worker);
        auto projection = Identity();
        projection.m[3][0] = static_cast<float>(worker);
        std::shared_ptr<const BaselineAccess::Frame> frame;
        std::string error;
        Probe::Require(baseline.m_host.PrepareMeshlets(context, projection, inputs.m_geometry, frame, error, true),
                       "Serial baseline creates each concurrent worker oracle.");
        expected[worker] = BaselineAccess::Capture(frame);
    }
    std::array<std::thread, workerCount> workers;
    std::array<std::exception_ptr, workerCount> failures;
    for (std::size_t worker = 0; worker < workerCount; ++worker)
    {
        workers[worker] = std::thread(
            [&, worker]
            {
                try
                {
                    EnhancedFrameContext context{&device};
                    context.width += static_cast<std::uint32_t>(worker);
                    auto projection = Identity();
                    projection.m[3][0] = static_cast<float>(worker);
                    for (std::size_t iteration = 0; iteration < iterations; ++iteration)
                    {
                        std::shared_ptr<const OptimizedAccess::Frame> frame;
                        std::string error;
                        Probe::Require(optimized.m_host.PrepareMeshlets(context, projection, inputs.m_geometry, frame,
                                                                        error, true),
                                       "Shared prewarmed host accepts simultaneous independent preparations.");
                        Probe::Require(OptimizedAccess::Capture(frame) == expected[worker],
                                       "Concurrent preparations match baseline bytes, group identity and owners.");
                    }
                }
                catch (...)
                {
                    failures[worker] = std::current_exception();
                }
            });
    }
    for (auto& worker : workers)
    {
        worker.join();
    }
    for (const auto& failure : failures)
    {
        if (failure)
        {
            std::rethrow_exception(failure);
        }
    }
    Probe::Require(optimized.Owners() == workerCount * iterations, "Concurrent publication retains every frame once.");
}

template<class Access>
struct BenchmarkCohort
{
    Access m_access;
    std::array<std::shared_ptr<const typename Access::Frame>, 8> m_frames;
    explicit BenchmarkCohort(IRenderDeviceServices& device) : m_access(device) {}
    void Clear()
    {
        for (auto& frame : m_frames)
        {
            frame.reset();
        }
        m_access.Clear();
    }
    std::uint64_t Run(IRenderDeviceServices& device, const InputSet& inputs)
    {
        std::string error;
        std::uint64_t checksum{};
        for (std::size_t view = 0; view < 4; ++view)
        {
            EnhancedFrameContext context{&device};
            if (view)
            {
                context.width = context.height = 1024;
            }
            auto projection = Identity();
            projection.m[3][0] = static_cast<float>(view);
            for (std::size_t stage = 0; stage < 2; ++stage)
            {
                const auto index = view * 2 + stage;
                if (!m_access.m_host.PrepareMeshlets(context, projection, inputs.m_geometry, m_frames[index], error,
                                                     stage != 0, {}, stage ? m_frames[index - 1] : nullptr))
                {
                    throw std::runtime_error(error);
                }
                checksum += m_frames[index]->GetPreparedStats().candidateCount;
            }
        }
        return checksum;
    }
};

void RunBenchmark()
{
    constexpr std::size_t warmups = 12, samples = 96;
    std::uint64_t checksum{};
    std::cout << "BENCHMARK_METHOD views=4 stages_per_view=2 warmup_cohorts=" << warmups << " samples=" << samples
              << " order=alternating cleanup=outside-timing"
              << " pool=production-reuse-with-original-caps RHI=logical-buffer-readback pipeline=fake-prewarmed"
              << " TLS=warm TLS_bytes_per_thread=" << OptimizedAccess::LookupScratchBytes()
              << " metadata_allocations=included\n";
    for (const auto count : {4u, 32u, 128u, 512u, 2048u, 4096u})
    {
        for (unsigned kind = 0; kind < 3; ++kind)
        {
            IRenderDeviceServices device;
            InputSet inputs;
            inputs.Build(device, count, kind != 0, kind == 1 ? 11 : 0);
            BenchmarkCohort<BaselineAccess> baseline(device);
            BenchmarkCohort<OptimizedAccess> optimized(device);
            std::array<double, samples> oldTimes{}, newTimes{};
            std::array<std::uint64_t, samples> oldAllocations{}, newAllocations{};
            std::array<std::uint32_t, samples> oldReadbackCreates{}, newReadbackCreates{};
            const auto measure = [&](auto& cohort, bool record, std::size_t index, auto& times, auto& allocations)
            {
                cohort.Clear();
                const auto readbacksBefore = device.m_readbacks.load();
                Probe::allocations = 0;
                Probe::countAllocations = true;
                const auto begin = std::chrono::steady_clock::now();
                checksum += cohort.Run(device, inputs);
                const auto end = std::chrono::steady_clock::now();
                Probe::countAllocations = false;
                if (record)
                {
                    times[index] = std::chrono::duration<double, std::micro>(end - begin).count();
                    allocations[index] = Probe::allocations;
                    auto& readbacks = std::is_same_v<std::decay_t<decltype(cohort)>, BenchmarkCohort<BaselineAccess>>
                                          ? oldReadbackCreates
                                          : newReadbackCreates;
                    readbacks[index] = device.m_readbacks.load() - readbacksBefore;
                }
            };
            for (std::size_t iteration = 0; iteration < warmups + samples; ++iteration)
            {
                const bool record = iteration >= warmups;
                const auto index = record ? iteration - warmups : 0;
                if ((iteration & 1u) == 0)
                {
                    measure(baseline, record, index, oldTimes, oldAllocations);
                    measure(optimized, record, index, newTimes, newAllocations);
                }
                else
                {
                    measure(optimized, record, index, newTimes, newAllocations);
                    measure(baseline, record, index, oldTimes, oldAllocations);
                }
            }
            std::ranges::sort(oldTimes);
            std::ranges::sort(newTimes);
            std::ranges::sort(oldAllocations);
            std::ranges::sort(newAllocations);
            std::ranges::sort(oldReadbackCreates);
            std::ranges::sort(newReadbackCreates);
            std::cout << std::fixed << std::setprecision(3) << "MESHLET_CPU_BENCH bins=" << count << " kind="
                      << (kind == 0   ? "transformed"
                          : kind == 1 ? "direct-shared11"
                                      : "direct-unique")
                      << " old_p50_us=" << oldTimes[samples / 2] << " new_p50_us=" << newTimes[samples / 2]
                      << " old_p95_us=" << oldTimes[samples * 95 / 100]
                      << " new_p95_us=" << newTimes[samples * 95 / 100]
                      << " old_allocations=" << oldAllocations[samples / 2]
                      << " new_allocations=" << newAllocations[samples / 2]
                      << " old_native_readback_calls=" << oldReadbackCreates[samples / 2]
                      << " new_native_readback_calls=" << newReadbackCreates[samples / 2] << '\n';
        }
    }
    std::cout << "BENCHMARK_CHECKSUM " << checksum << '\n';
}

int main(int argc, char** argv)
{
    try
    {
        CheckGeometryAndHistory();
        CheckFailures();
        CheckCollisionsAndReentry();
        CheckGenericValidation();
        CheckOwnershipAndParallelCalls();
        std::cout << "MESHLET_PREPARATION_CPU_PASS checks=" << Probe::checks.load()
                  << " bins=0..4097 full-key-collision signed-zero NaN generation-wrap nested-reentry"
                  << " generic-parity error-parity retained-owners concurrent-workers=8 no-GPU\n";
        if (argc == 2 && std::string_view(argv[1]) == "--benchmark")
        {
            RunBenchmark();
        }
        return 0;
    }
    catch (const std::exception& failure)
    {
        std::cerr << "MESHLET_PREPARATION_CPU_FAIL " << failure.what() << '\n';
        return 1;
    }
}
