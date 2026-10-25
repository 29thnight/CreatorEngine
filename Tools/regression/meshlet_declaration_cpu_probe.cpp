// GPU가 없는 환경에서 실제 선언 본문의 게시·소유 스레드·바이트 배치 경계를 검사한다.
// RHI 값 타입과 대상 본문은 제품 소스이며 native 장치·그래프 경계만 대체한다.
#include "RHI/RHIResourceTypes.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../build/ModuleAdapters/Creator.Concurrency.Jobs.h"
#else
import Creator.Concurrency.Jobs;
#endif
#include "../../Engine/EngineDiagnostics/ProfileScope.h"

namespace Probe
{
    std::atomic<std::uint32_t> checks{};
    std::atomic<std::uint32_t> submittedTasks{};

    void Require(bool condition, const char* message)
    {
        ++checks;
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }
} // namespace Probe

// 스케줄러가 켜져 있어도 이 작은 업로드는 직접 실행한다. 제출 자체를 회귀로 검사한다.
#if !defined(PROBE_REAL_SCHEDULER)
namespace job_detail
{
    struct scheduler_state
    {
        bool running_{};
    };
} // namespace job_detail

thread_pool::thread_pool() = default;
thread_pool::~thread_pool() = default;
bool thread_pool::is_worker_thread() noexcept
{
    return false;
}
job_scheduler::job_scheduler(thread_pool&) : state_(std::make_shared<job_detail::scheduler_state>()) {}
job_scheduler::~job_scheduler()
{
    shutdown();
}
void job_scheduler::start(std::size_t)
{
    state_->running_ = true;
}
bool job_scheduler::is_running() const
{
    return state_->running_;
}
void job_scheduler::shutdown() noexcept
{
    state_->running_ = false;
}
job_handle job_scheduler::submit_indexed(std::size_t, std::function<void(std::size_t)>)
{
    ++Probe::submittedTasks;
    throw std::runtime_error("Visibility declaration unexpectedly submitted a worker task.");
}
namespace ce
{
    job_scheduler& get_job_scheduler()
    {
        static thread_pool pool;
        static job_scheduler scheduler(pool);
        return scheduler;
    }
} // namespace ce
#endif

#if defined(PROBE_OPTIMIZED_DECLARATION)
#include "Render/Graph/RenderGraphDeclarationTask.h"
#endif

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
        std::unique_ptr<std::byte[]> bytes;
        std::size_t size{};
    };
    struct Binding
    {
        std::vector<RHIBindingDesc> descriptions;
    };
    std::uint64_t recording{1}, descriptors{1};
    const std::thread::id owner = std::this_thread::get_id();
    bool partiallyBound{true}, failUpload{}, failBuffer{}, invalidateBindings{};
    bool retainStorage{};
    std::size_t uploadCursor{}, bindingCursor{};
    std::uint32_t failBinding{}, bindingCalls{}, bufferCreates{}, bufferReturns{};
    std::vector<std::unique_ptr<Upload>> uploads;
    std::vector<std::unique_ptr<Binding>> bindings;
    std::mutex mutex;

    std::uint64_t GetCurrentUploadRecordingId() const { return recording; }
    std::uint64_t GetDescriptorVersionToken() const { return descriptors; }
    RHIShaderArrayCapabilities GetShaderArrayCapabilities() const
    {
        RHIShaderArrayCapabilities capabilities{};
        capabilities.partiallyBoundSrvArrays = partiallyBound;
        return capabilities;
    }
    bool ReserveUploadBatch(std::span<const RHIUploadRequest> requests, std::span<RHIBufferSlice> result,
                            std::string& error)
    {
        Probe::Require(std::this_thread::get_id() == owner, "Visibility native upload left its owner thread.");
        if (failUpload)
        {
            error = "injected upload failure";
            return false;
        }
        const std::lock_guard lock(mutex);
        for (std::size_t index = 0; index < requests.size(); ++index)
        {
            const auto slot = retainStorage ? uploadCursor++ : uploads.size();
            if (slot == uploads.size())
            {
                uploads.push_back(std::make_unique<Upload>());
            }
            const auto& upload = uploads[slot];
            const auto bytes = static_cast<std::size_t>(requests[index].bytes);
            if (upload->size != bytes)
            {
                upload->size = bytes;
                upload->bytes = std::make_unique<std::byte[]>(upload->size);
            }
            RHIBufferSlice slice{};
            slice.buffer = RHIBufferHandle{50000u + static_cast<std::uint32_t>(slot)};
            slice.cpuAddress = upload->bytes.get();
            slice.size = upload->size;
            result[index] = slice;
        }
        return true;
    }
    RHIBufferSlice UploadConstants(const void* source, std::size_t bytes)
    {
        const RHIUploadRequest request{bytes, RHIUploadUsage::ConstantBuffer, 256};
        RHIBufferSlice result{};
        std::string error;
        if (!ReserveUploadBatch(std::span{&request, 1}, std::span{&result, 1}, error))
        {
            return {};
        }
        std::memcpy(result.cpuAddress, source, bytes);
        return result;
    }
    RHIBindingTable CreateBindings(std::span<const RHIBindingDesc> descriptions)
    {
        Probe::Require(std::this_thread::get_id() == owner, "Visibility native bindings left their owner thread.");
        const std::lock_guard lock(mutex);
        ++bindingCalls;
        if (bindingCalls == failBinding)
        {
            return {};
        }
        const auto slot = retainStorage ? bindingCursor++ : bindings.size();
        if (slot == bindings.size())
        {
            bindings.push_back(std::make_unique<Binding>());
        }
        const auto& binding = bindings[slot];
        binding->descriptions.assign(descriptions.begin(), descriptions.end());
        RHIBindingTable result{};
        result.backend = slot + 1u;
        result.version = descriptors;
        result.count = static_cast<std::uint32_t>(descriptions.size());
        if (invalidateBindings)
        {
            ++descriptors;
        }
        return result;
    }
    bool CreateBuffer(const RHIBufferDesc&, RHIBufferHandle& result, std::string& error)
    {
        if (failBuffer)
        {
            error = "injected queue allocation failure";
            return false;
        }
        result = RHIBufferHandle{60000u + ++bufferCreates};
        return true;
    }
    void ReleaseBuffer(RHIBufferHandle) { ++bufferReturns; }
    bool CreateBufferReadback(std::uint64_t, RHIReadback&, std::string&) { return false; }
    void ReleaseReadback(RHIReadback) {}
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

enum class RGResourceKind
{
    Texture,
    Buffer
};
enum class RGSchedulingMode
{
    DeclarationOrder,
    ExplicitSingleWriter,
    ExplicitVersioned
};
enum class RGAccessMode
{
    LegacyState,
    Read,
    Write,
    ReadWrite,
    AtomicMin64
};
struct RGHandle
{
    std::uint16_t index{0xffff}, version{};
    RGResourceKind kind{RGResourceKind::Texture};
    std::uint64_t epoch{};
    bool IsValid() const { return index != 0xffff; }
};
struct RGPassId
{
    std::uint16_t index{0xffff};
    bool IsValid() const { return index != 0xffff; }
};

enum class RHIBindPoint : std::uint8_t
{
    Graphics,
    Compute
};

class RHIEncoder
{
  public:
    void SetPipeline(RHIBindPoint, std::uint64_t) {}
    void SetConstantBuffer(RHIBindPoint, std::uint32_t, RHIBufferSlice) {}
    void SetRootBuffer(RHIBindPoint, std::uint32_t, RHIBufferSlice) {}
    void SetBindings(RHIBindPoint, std::uint32_t, RHIBindingTable) {}
    void Dispatch(std::uint32_t, std::uint32_t, std::uint32_t) {}
    void CopyBufferToReadback(RHIReadback, RHIBufferHandle, std::uint64_t) {}
};

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
        const EnhancedRenderGraph* graph;
        RHIEncoder* encoder;
    };
    struct Pass
    {
        std::string name;
        std::vector<RGPassUsage> usages;
        std::function<void(const ExecuteContext&)> execute;
        bool sideEffect{}, compute{}, independent{};
    };
    struct Resource
    {
        RHIBufferHandle buffer;
        std::uint16_t version{};
        bool finalState{};
    };
    explicit EnhancedRenderGraph(IRenderDeviceServices& value) : device(value), owner(std::this_thread::get_id()) {}
    IRenderDeviceServices& device;
    const std::thread::id owner;
    std::vector<Resource> resources;
    std::vector<Pass> passes;
    bool failImport{};
    RGSchedulingMode GetSchedulingMode() const { return RGSchedulingMode::ExplicitVersioned; }
    std::uint64_t ResourceEpoch() const { return 1; }
    IRenderDeviceServices& DeviceServices() const { return device; }
    void CheckOwner() const
    {
        Probe::Require(std::this_thread::get_id() == owner, "Graph was mutated by a declaration worker.");
    }
    RGHandle ImportBuffer(RHIBufferHandle buffer, RHIResourceState, const std::string&)
    {
        CheckOwner();
        if (failImport)
        {
            throw std::runtime_error("injected graph import failure");
        }
        const auto index = static_cast<std::uint16_t>(resources.size());
        resources.push_back({buffer});
        return {index, 0, RGResourceKind::Buffer, ResourceEpoch()};
    }
    void RequireImportedFinalState(RGHandle handle, RHIResourceState)
    {
        CheckOwner();
        resources.at(handle.index).finalState = true;
    }
    RGHandle Write(RGHandle handle) { return Modify(handle); }
    RGHandle Modify(RGHandle handle)
    {
        CheckOwner();
        handle.version = ++resources.at(handle.index).version;
        return handle;
    }
    template<class Callback>
    RGPassId AddPass(const std::string& name, const std::vector<RGPassUsage>& usages, Callback callback,
                     bool sideEffect = false)
    {
        CheckOwner();
        Probe::Require(Probe::submittedTasks == 0, "Visibility declaration submitted a worker task.");
        const auto index = static_cast<std::uint16_t>(passes.size());
        Pass pass{};
        pass.name = name;
        pass.usages = usages;
        pass.execute = std::move(callback);
        pass.sideEffect = sideEffect;
        passes.push_back(std::move(pass));
        return {index};
    }
    void DeclareComputeCompatible(RGPassId pass) { passes.at(pass.index).compute = true; }
    void DeclareRecordingIndependent(RGPassId pass) { passes.at(pass.index).independent = true; }
};

namespace LX::Runtime
{
    struct ComputeGeneration
    {
        std::uint64_t GetHandle() const { return 1; }
    };
    struct ComputePipeline
    {
    };
} // namespace LX::Runtime

class GpuGeometryOcclusion
{
  public:
    // 선언 사용 목록의 크기는 제품의 HZB 한도와 함께 바뀌어야 한다.
#include "MeshletDeclarationConstants.inl"
    struct ViewKey
    {
    };
    struct History
    {
        struct Source
        {
            std::array<std::uint32_t, 16> projectionBits{};
        } source;
    };
    struct Pyramid
    {
        std::shared_ptr<History> history = std::make_shared<History>();
        std::vector<RHIBindingDesc> inputs;
        RGHandle graphDepth;
        const std::shared_ptr<History>& HistoryOwner() const { return history; }
        const std::vector<RHIBindingDesc>& BindingInputs(const EnhancedRenderGraph::ExecuteContext&) const
        {
            return inputs;
        }
        RHIBindingTable Bindings(const EnhancedRenderGraph::ExecuteContext&) const { return {}; }
        void AddReadUsages(EnhancedRenderGraph&, std::vector<EnhancedRenderGraph::RGPassUsage>& usages) const
        {
            usages.push_back({graphDepth, RHIResourceState::ShaderResource, RGAccessMode::Read});
        }
    };
};

namespace material_graph
{
    class MeshSurfaceBatch
    {
      public:
        static constexpr std::uint32_t GeometryViewStride = 16;
        math::matrix4x4 world;
        RHIBufferHandle buffer;
        RHIBufferSlice topology;
        RGHandle graphVertices, graphTopology;
        std::uint32_t meshletCount{};
        const math::matrix4x4& ObjectToWorld() const { return world; }
        RHIBufferHandle Buffer() const { return buffer; }
        std::uint32_t GeometryViewElements() const { return meshletCount * 4; }
        RHIBufferSlice Meshlets() const { return topology; }
        std::uint32_t MeshletCount() const { return meshletCount; }
        RGHandle GraphMeshlets(EnhancedRenderGraph&) const { return graphTopology; }
        RGHandle GraphOutput(EnhancedRenderGraph&) const { return graphVertices; }
    };
} // namespace material_graph

struct EnhancedFrameContext;
struct VisibilityDeclarationProbe;
#include "MeshletDeclarationTypes.inl"
#include "MeshletDeclarationProduction.inl"

struct VisibilityDeclarationProbe
{
    using Frame = GpuGeometryVisibility::Frame;
    IRenderDeviceServices device;
    EnhancedRenderGraph graph{device};
    std::shared_ptr<GpuGeometryVisibility::BufferPool> pool =
        std::make_shared<GpuGeometryVisibility::BufferPool>(device);
    std::vector<std::shared_ptr<const Frame>> frames;

    void Build(std::uint32_t views, std::uint32_t bins, bool ordered = false, bool history = false)
    {
        frames.reserve(views);
        for (std::uint32_t viewIndex = 0; viewIndex < views; ++viewIndex)
        {
            std::shared_ptr<Frame> frame(new Frame);
            frame->m_device = &device;
            frame->m_recording = device.recording;
            frame->m_descriptors = device.descriptors;
            frame->m_binCount = bins;
            frame->m_candidateCount = bins;
            frame->m_outputCount = bins * 64;
            frame->m_visibleBytes = (frame->m_outputCount + 512u) * 4ull;
            frame->m_argumentBytes = bins * sizeof(RHIDrawIndexedIndirectArguments);
            frame->m_visibleIds = RHIBufferHandle{1000u + viewIndex * 2u};
            frame->m_arguments = RHIBufferHandle{1001u + viewIndex * 2u};
            frame->m_bufferPool = pool;
            frame->m_ordered = ordered;
            frame->m_self = frame;
            frame->m_meshletInputs = std::make_shared<GpuGeometryVisibility::MeshletInputs>();
            frame->m_meshletInputs->constants.candidateCount = bins;
            frame->m_meshletInputs->constants.binCount = bins;
            frame->m_meshletInputs->constants.width = 640u + viewIndex;
            frame->m_meshletInputs->constants.height = 480;
            frame->m_meshletInputs->constants.projectionValid = 1;
            frame->m_meshletInputs->candidates.reserve(bins);
            frame->m_meshletInputs->bins.reserve(bins);
            frame->m_meshletGeometries.reserve(bins);
            frame->m_sharedReset = std::make_shared<LX::Runtime::ComputeGeneration>();
            frame->m_sharedCull = frame->m_sharedReset;
            frame->m_sharedOcclusion = frame->m_sharedReset;
            frame->m_sharedCount = frame->m_sharedReset;
            frame->m_sharedFinalize = frame->m_sharedReset;
            frame->m_sharedScatter = frame->m_sharedReset;
            frame->m_reset = frame->m_sharedReset;
            frame->m_cull = frame->m_sharedReset;
            frame->m_meshletCount = frame->m_sharedReset;
            frame->m_meshletFinalize = frame->m_sharedReset;
            frame->m_meshletScatter = frame->m_sharedReset;
            if (history)
            {
                auto pyramid = std::make_shared<GpuGeometryOcclusion::Pyramid>();
                for (std::size_t index = 0; index < 16; ++index)
                {
                    pyramid->history->source.projectionBits[index] =
                        std::bit_cast<std::uint32_t>(float(index + viewIndex * 16u));
                }
                frame->m_historyPyramid = pyramid;
            }
            for (std::uint32_t bin = 0; bin < bins; ++bin)
            {
                const auto count = bin % 3u * 64u + 1u;
                auto geometry = std::make_shared<material_graph::MeshSurfaceBatch>();
                geometry->meshletCount = count;
                geometry->buffer = RHIBufferHandle{2000u + viewIndex * bins * 2u + bin * 2u};
                geometry->topology.buffer = RHIBufferHandle{geometry->buffer.id + 1u};
                geometry->topology.size = 256;
                geometry->topology.offset = 16;
                geometry->graphVertices =
                    graph.ImportBuffer(geometry->buffer, RHIResourceState::ShaderResource, "vertices");
                geometry->graphTopology =
                    graph.ImportBuffer(geometry->topology.buffer, RHIResourceState::ShaderResource, "topology");
                for (std::uint32_t row = 0; row < 4; ++row)
                {
                    for (std::uint32_t column = 0; column < 4; ++column)
                    {
                        geometry->world.m[row][column] = float(viewIndex * 10000u + bin * 100u + row * 4u + column);
                    }
                }
                frame->m_meshletGeometries.push_back(geometry);
                frame->m_meshletInputs->candidates.push_back({{1.f, 2.f, float(bin), 4.f}, bin, 0, 0, 8});
                frame->m_meshletInputs->bins.push_back({count, bin * 64u, 1, 0});
            }
            frames.push_back(frame);
        }
    }

    void Declare(bool schedulerRunning)
    {
        auto& scheduler = ce::get_job_scheduler();
        scheduler.shutdown();
        if (schedulerRunning)
        {
            scheduler.start(4);
        }
        Probe::submittedTasks = 0;
        Frame::DeclareMeshletBatch(graph, frames, "Probe.Visibility");
    }

    std::uint64_t Digest() const
    {
        std::uint64_t digest = 14695981039346656037ull;
        const auto feed = [&digest](const void* value, std::size_t bytes)
        {
            const auto* data = static_cast<const std::uint8_t*>(value);
            for (std::size_t index = 0; index < bytes; ++index)
            {
                digest = (digest ^ data[index]) * 1099511628211ull;
            }
        };
        for (const auto& upload : device.uploads)
        {
            feed(upload->bytes.get(), upload->size);
        }
        for (const auto& binding : device.bindings)
        {
            for (const auto& description : binding->descriptions)
            {
                // padding 바이트 대신 각 GPU 바인딩의 의미 필드를 대조한다.
                feed(&description.kind, sizeof(description.kind));
                feed(&description.dim, sizeof(description.dim));
                feed(&description.bufferResource.id, sizeof(description.bufferResource.id));
                feed(&description.firstElement, sizeof(description.firstElement));
                feed(&description.numElements, sizeof(description.numElements));
                feed(&description.structureByteStride, sizeof(description.structureByteStride));
            }
        }
        for (const auto& pass : graph.passes)
        {
            feed(pass.name.data(), pass.name.size());
            for (const auto& usage : pass.usages)
            {
                feed(&usage.handle.index, sizeof(usage.handle.index));
                feed(&usage.handle.version, sizeof(usage.handle.version));
                feed(&usage.handle.kind, sizeof(usage.handle.kind));
                feed(&usage.state, sizeof(usage.state));
                feed(&usage.access, sizeof(usage.access));
            }
        }
        return digest;
    }

#if !defined(PROBE_REAL_SCHEDULER)
    void VerifyLayout(bool ordered)
    {
        Probe::Require(graph.passes.size() == (ordered ? 5u : 3u), "Declared stage sequence differs.");
        for (const auto& pass : graph.passes)
        {
            Probe::Require(pass.compute && pass.independent, "Declaration lost compute/recording contracts.");
        }
        if (frames.size() == 1)
        {
            Probe::Require(frames.front()->m_constants.IsValid(), "Single-view readiness was not published.");
            Probe::Require(device.bindingCalls == 3, "Single view built redundant binding tables.");
            return;
        }
        const auto& shared = frames.front()->m_sharedBatch;
        Probe::Require(bool(shared), "Shared upload result was not retained by its frames.");
        Probe::Require(shared->views == frames.size(), "Shared view count differs.");
        Probe::Require(device.bindingCalls == 6, "Shared batch built redundant per-view tables.");
        const auto* candidates = static_cast<const GpuGeometryVisibility::Candidate*>(shared->candidates.cpuAddress);
        const auto* bins = static_cast<const GpuGeometryVisibility::Bin*>(shared->binData.cpuAddress);
        const auto* worlds = static_cast<const math::vector4*>(shared->worlds.cpuAddress);
        const auto* views =
            static_cast<const GpuGeometryVisibility::SharedBatch::ViewData*>(shared->viewData.cpuAddress);
        std::uint32_t offset{}, jobs{}, groups{};
        for (std::uint32_t viewIndex = 0; viewIndex < frames.size(); ++viewIndex)
        {
            const auto& frame = frames[viewIndex];
            const auto firstBin = offset;
            Probe::Require(views[viewIndex].width == 640u + viewIndex && views[viewIndex].bins == frame->m_binCount,
                           "Shared view constants were transposed or assigned to another view.");
            for (std::uint32_t bin = 0; bin < frame->m_binCount; ++bin)
            {
                const auto count = bin % 3u * 64u + 1u;
                Probe::Require(candidates[offset].bin == viewIndex && candidates[offset].sourceIndex == jobs &&
                                   candidates[offset].outputOffset == bin &&
                                   candidates[offset].sphere.z == float(firstBin + bin),
                               "Shared candidate prefix/view/owner remap differs from its independent oracle.");
                Probe::Require(bins[offset].indexCount == count && bins[offset].preservedInstanceCount == groups,
                               "Shared scan tile prefix is wrong.");
                Probe::Require(worlds[offset * 3u + 1u].z == float(viewIndex * 10000u + bin * 100u + 9u),
                               "Object-to-world columns have incorrect row/column order.");
                jobs += count;
                groups += (count + 63u) / 64u;
                ++offset;
            }
        }
        Probe::Require(shared->jobs == jobs && shared->groups == groups && shared->bins == offset,
                       "Shared bounded totals do not match all source bins.");
    }

    static void VerifyParity(std::uint32_t views, std::uint32_t bins, bool ordered, bool history, bool partial)
    {
        VisibilityDeclarationProbe serial;
        serial.device.partiallyBound = partial;
        serial.Build(views, bins, ordered, history);
        serial.Declare(false);
        serial.VerifyLayout(ordered);
        const auto expected = serial.Digest();
        VisibilityDeclarationProbe available;
        available.device.partiallyBound = partial;
        available.Build(views, bins, ordered, history);
        available.Declare(true);
        available.VerifyLayout(ordered);
        Probe::Require(available.Digest() == expected && Probe::submittedTasks == 0,
                       "Scheduler availability changed inline declaration/upload bytes or dispatched work.");
        available.VerifyAlreadyDeclared();
    }

    void VerifyAlreadyDeclared()
    {
        const auto previousUploads = device.uploads.size();
        const auto previousBindings = device.bindingCalls;
        const auto previousBuffers = device.bufferCreates;
        const auto previousSubmissions = Probe::submittedTasks.load();
        const auto previousDigest = Digest();
        const auto* vertices = pool->m_upload.m_vertices.data();
        const auto* topologies = pool->m_upload.m_topologies.data();
        const auto vertexSize = pool->m_upload.m_vertices.size();
        const auto topologySize = pool->m_upload.m_topologies.size();
        std::array<const EnhancedRenderGraph::RGPassUsage*, 5> uses;
        std::array<std::size_t, 5> sizes;
        for (std::size_t index = 0; index < uses.size(); ++index)
        {
            uses[index] = pool->m_declaration.m_uses[index].data();
            sizes[index] = pool->m_declaration.m_uses[index].size();
        }
        // 이전 선언의 scratch를 그대로 남겨 두어 no-op이 clear/reserve까지 진입하는지 검사한다.
        pool->m_declaration.m_normalize.assign(129, 0x12345678u);
        const auto* normalize = pool->m_declaration.m_normalize.data();
        const render_graph_declaration_lock held(pool->m_declaration.m_mutex);
        Probe::Require(held.owns_lock(), "No-op fixture could not retain its declaration scratch.");
        Frame::DeclareMeshletBatch(graph, frames, "Probe.Visibility");
        Probe::Require(device.uploads.size() == previousUploads && device.bindingCalls == previousBindings &&
                           device.bufferCreates == previousBuffers && Probe::submittedTasks == previousSubmissions,
                       "Already declared frames allocated or dispatched native work.");
        Probe::Require(Digest() == previousDigest, "Already declared frames mutated graph passes or native data.");
        Probe::Require(
            pool->m_upload.m_vertices.data() == vertices && pool->m_upload.m_topologies.data() == topologies &&
                pool->m_upload.m_vertices.size() == vertexSize && pool->m_upload.m_topologies.size() == topologySize,
            "Already declared frames modified upload scratch.");
        for (std::size_t index = 0; index < uses.size(); ++index)
        {
            Probe::Require(pool->m_declaration.m_uses[index].data() == uses[index] &&
                               pool->m_declaration.m_uses[index].size() == sizes[index],
                           "Already declared frames modified declaration scratch.");
        }
        Probe::Require(pool->m_declaration.m_normalize.data() == normalize &&
                           pool->m_declaration.m_normalize.size() == 129 &&
                           std::ranges::all_of(pool->m_declaration.m_normalize,
                                               [](const auto value) { return value == 0x12345678u; }),
                       "Already declared frames modified normalization scratch.");
    }

    static void VerifyScratchReuse()
    {
        VisibilityDeclarationProbe fixture;
        fixture.Build(2, 40, true);
        fixture.Declare(true);
        std::array<const EnhancedRenderGraph::RGPassUsage*, 5> uses;
        for (std::size_t index = 0; index < uses.size(); ++index)
        {
            uses[index] = fixture.pool->m_declaration.m_uses[index].data();
        }
        const auto* normalize = fixture.pool->m_declaration.m_normalize.data();
        const auto* vertices = fixture.pool->m_upload.m_vertices.data();
        const auto* topologies = fixture.pool->m_upload.m_topologies.data();
        fixture.frames.clear();
        fixture.graph.passes.clear();
        fixture.graph.resources.clear();
        fixture.Build(2, 40, true);
        fixture.Declare(true);
        for (std::size_t index = 0; index < uses.size(); ++index)
        {
            Probe::Require(fixture.pool->m_declaration.m_uses[index].data() == uses[index],
                           "Equal-sized declaration replaced retained usage storage.");
        }
        Probe::Require(fixture.pool->m_declaration.m_normalize.data() == normalize &&
                           fixture.pool->m_upload.m_vertices.data() == vertices &&
                           fixture.pool->m_upload.m_topologies.data() == topologies,
                       "Equal-sized declaration replaced retained normalization/descriptor storage.");
    }

    static void VerifyScratchBorrow()
    {
        VisibilityDeclarationProbe fixture;
        fixture.Build(2, 40);
        const auto resources = fixture.graph.resources.size();
        bool rejected{};
        {
            const render_graph_declaration_lock held(fixture.pool->m_declaration.m_mutex);
            Probe::Require(held.owns_lock(), "Fixture could not retain its declaration scratch.");
            try
            {
                fixture.Declare(true);
            }
            catch (const std::runtime_error&)
            {
                rejected = true;
            }
            Probe::Require(rejected && fixture.device.uploads.empty() && fixture.graph.resources.size() == resources,
                           "Reentrant declaration borrowed scratch or partially published graph resources.");
        }
        fixture.Declare(true);
        Probe::Require(fixture.graph.passes.size() == 3,
                       "Rejected scratch reentry poisoned the subsequent declaration.");
    }

    static void VerifyDuplicateOwners()
    {
        VisibilityDeclarationProbe unique;
        unique.Build(2, 40);
        unique.Declare(false);
        const auto expected = unique.Digest();
        VisibilityDeclarationProbe duplicate;
        duplicate.Build(2, 40);
        duplicate.frames.push_back(duplicate.frames.front());
        duplicate.frames.push_back(duplicate.frames[1]);
        duplicate.Declare(true);
        Probe::Require(duplicate.Digest() == expected && duplicate.frames.front()->m_sharedBatch->views == 2,
                       "Duplicate owners duplicated upload bytes or pass metadata.");
        duplicate.VerifyAlreadyDeclared();
    }

    static void VerifyFailure(std::uint32_t mode)
    {
        VisibilityDeclarationProbe fixture;
        fixture.Build(2, 40);
        fixture.device.failUpload = mode == 0;
        fixture.device.failBinding = mode == 1 ? 3u : 0u;
        fixture.device.failBuffer = mode == 2;
        fixture.graph.failImport = mode == 3;
        fixture.device.invalidateBindings = mode == 4;
        bool rejected{};
        try
        {
            fixture.Declare(true);
        }
        catch (const std::runtime_error&)
        {
            rejected = true;
        }
        Probe::Require(rejected, "Injected declaration failure was ignored.");
        Probe::Require(Probe::submittedTasks == 0, "Failed visibility declaration submitted an upload task.");
        Probe::Require(fixture.graph.passes.empty(), "Failure published an executable pass callback.");
        for (const auto& frame : fixture.frames)
        {
            Probe::Require(!frame->m_sharedBatch && !frame->m_constants.IsValid(),
                           "Failed upload published incomplete readiness.");
        }
    }

    static void VerifyLifetime()
    {
        VisibilityDeclarationProbe fixture;
        fixture.Build(2, 40);
        fixture.Declare(true);
        const std::weak_ptr<const Frame> frame = fixture.frames.front();
        fixture.frames.clear();
        Probe::Require(!frame.expired(), "Graph callback lost its captured frame owner.");
        fixture.graph.passes.clear();
        Probe::Require(frame.expired(), "Completed declaration kept an unbounded frame owner.");
        Probe::Require(fixture.pool->free.size() == 5,
                       "Shared queue and each frame's output/argument buffers were not returned exactly once.");
    }

    static void VerifyInputBoundaries()
    {
        VisibilityDeclarationProbe empty;
        empty.Declare(true);
        Probe::Require(empty.graph.passes.empty() && Probe::submittedTasks == 0, "Empty declaration dispatched work.");
        VisibilityDeclarationProbe invalid;
        invalid.frames.push_back({});
        bool rejected{};
        try
        {
            invalid.Declare(true);
        }
        catch (const std::runtime_error&)
        {
            rejected = true;
        }
        Probe::Require(rejected && Probe::submittedTasks == 0, "Null input entered a declaration task.");
        VisibilityDeclarationProbe partition;
        partition.Build(9, 2);
        partition.Declare(true);
        Probe::Require(partition.graph.passes.size() == 6, "Bounded view partition lost a batch.");
        VisibilityDeclarationProbe binPartition;
        binPartition.Build(2, 2050);
        binPartition.Declare(true);
        Probe::Require(binPartition.graph.passes.size() == 6 && Probe::submittedTasks == 0,
                       "4096-bin limit did not split oversized combined views.");
        for (const auto& frame : binPartition.frames)
        {
            Probe::Require(frame->m_constants.IsValid() && !frame->m_sharedBatch,
                           "A split one-view batch did not publish its single-view upload.");
        }
        VisibilityDeclarationProbe oversizedView;
        oversizedView.Build(1, 1);
        const_cast<Frame*>(oversizedView.frames.front().get())->m_binCount = 4097;
        rejected = false;
        try
        {
            oversizedView.Declare(true);
        }
        catch (const std::runtime_error&)
        {
            rejected = true;
        }
        Probe::Require(rejected && Probe::submittedTasks == 0 && oversizedView.device.uploads.empty(),
                       "An oversized single view entered upload work or recursive splitting.");
    }

    static int Run()
    {
        VerifyParity(1, 4, false, false, true);
        VerifyParity(1, 80, true, true, true);
        VerifyParity(2, 40, false, true, true);
        VerifyParity(3, 24, true, true, true);
        VerifyParity(2, 2, false, false, false);
        VerifyDuplicateOwners();
        VerifyScratchReuse();
        VerifyScratchBorrow();
        for (std::uint32_t mode = 0; mode < 5; ++mode)
        {
            VerifyFailure(mode);
        }
        VerifyLifetime();
        VerifyInputBoundaries();
        ce::get_job_scheduler().shutdown();
        std::cout << "MESHLET_DECLARATION_CPU_OK checks=" << Probe::checks
                  << " parity=single,shared,ordered,history,fixed-array"
                  << " failure=upload,bindings,queue,graph,epoch"
                  << " boundary=empty,null,duplicate,8-views,4096-bins"
                  << " scratch=retained,no-op,reentry ownership=inline,retained GPU=false\n";
        return 0;
    }
#endif

#if defined(PROBE_REAL_SCHEDULER)
    void ResetBenchmarkDeclaration(std::size_t geometryResources)
    {
        // 입력 생성/해제는 계측 구간 밖에 둔다. 동일 입력을 다시 선언하며 풀 용량만 재사용한다.
        graph.passes.clear();
        for (const auto& owner : frames)
        {
            auto& frame = *const_cast<Frame*>(owner.get());
            frame.m_graph = nullptr;
            frame.m_graphEpoch = 0;
            frame.m_sharedBatch.reset();
            frame.m_constants = {};
        }
        graph.resources.resize(geometryResources);
        if (!device.retainStorage)
        {
            device.uploads.clear();
            device.bindings.clear();
        }
        device.uploadCursor = 0;
        device.bindingCursor = 0;
        device.bindingCalls = 0;
    }

    static void BenchmarkCase(std::uint32_t bins, bool partial, std::size_t workers, bool retainStorage)
    {
        constexpr std::size_t samples = 120;
        constexpr std::size_t warmup = 12;
        VisibilityDeclarationProbe fixture;
        fixture.device.partiallyBound = partial;
        fixture.device.retainStorage = retainStorage;
        fixture.Build(2, bins);
        const auto geometryResources = fixture.graph.resources.size();
        std::array<double, samples> stopped{}, running{};
        std::uint64_t checksum{};
        auto& scheduler = ce::get_job_scheduler();
        const auto measure = [&](bool schedulerRunning, std::span<double> timings)
        {
            scheduler.shutdown();
            if (schedulerRunning)
            {
                scheduler.start(workers);
            }
            for (std::size_t index = 0; index < warmup + timings.size(); ++index)
            {
                fixture.ResetBenchmarkDeclaration(geometryResources);
                const auto begin = std::chrono::steady_clock::now();
                Frame::DeclareMeshletBatch(fixture.graph, fixture.frames, "Probe.Visibility");
                const auto end = std::chrono::steady_clock::now();
                if (index >= warmup)
                {
                    timings[index - warmup] = std::chrono::duration<double, std::micro>(end - begin).count();
                }
                checksum += fixture.frames.front()->m_sharedBatch->jobs;
            }
        };
        // 여러 블록의 순서를 교차해 한쪽만 앞서 실행한 캐시/온도 효과를 줄인다.
        for (std::size_t round = 0; round < 4; ++round)
        {
            auto serialBlock = std::span{stopped}.subspan(round * samples / 4, samples / 4);
            auto runningBlock = std::span{running}.subspan(round * samples / 4, samples / 4);
            if (round % 2 == 0)
            {
                measure(false, serialBlock);
                measure(true, runningBlock);
            }
            else
            {
                measure(true, runningBlock);
                measure(false, serialBlock);
            }
        }
        scheduler.shutdown();
        std::ranges::sort(stopped);
        std::ranges::sort(running);
        std::cout << "MESHLET_CPU_BENCH total_bins=" << bins * 2u << " partial=" << partial << " workers=" << workers
                  << " retained_native=" << retainStorage << " samples=" << samples
                  << " stopped_p50_us=" << stopped[samples / 2] << " stopped_p95_us=" << stopped[samples * 95 / 100]
                  << " running_p50_us=" << running[samples / 2] << " running_p95_us=" << running[samples * 95 / 100]
                  << " checksum=" << checksum << '\n';
    }

    static int RunBenchmark(std::size_t workers, bool retainStorage)
    {
        Probe::submittedTasks = 0;
        for (const std::uint32_t bins : {16u, 32u, 64u, 256u, 1024u, 2048u})
        {
            BenchmarkCase(bins, true, workers, retainStorage);
        }
        BenchmarkCase(2, false, workers, retainStorage);
        std::cout << "BENCHMARK_SCOPE exact-production-declaration-and-upload real-JobScheduler real-enkiTS"
                  << " inline-in-both-modes fake-native-device fake-graph shipping-profile-off"
                  << " warmed-input no-GPU no-frame-speedup\n";
        return 0;
    }
#endif
};

int main(int argc, char** argv)
{
    try
    {
#if defined(PROBE_REAL_SCHEDULER)
        return VisibilityDeclarationProbe::RunBenchmark(argc > 1 ? std::stoul(argv[1]) : 4u,
                                                        argc > 2 && std::string_view(argv[2]) == "1");
#else
        return VisibilityDeclarationProbe::Run();
#endif
    }
    catch (const std::exception& error)
    {
        ce::get_job_scheduler().shutdown();
        std::cerr << "MESHLET_DECLARATION_CPU_FAILED " << error.what() << '\n';
        return 1;
    }
}
