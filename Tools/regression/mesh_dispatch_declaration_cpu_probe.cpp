// 제품 PrepareDispatch와 PrepareDeclarationDispatch 본문을 그대로 실행한다.
// RHI 값 타입·정점 기술표·계획·합류 helper는 제품 코드이고 장치/실행기만 대체한다.
#include "RHI/RHIResourceTypes.h"
#include "Assets/ModelVertexLayout.h"
#include "MeshTransformDeclarationPlan.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../build/ModuleAdapters/Creator.Concurrency.Jobs.h"
#else
import Creator.Concurrency.Jobs;
#endif

namespace Probe
{
    std::atomic<bool> measureAllocations{};
    std::atomic<std::size_t> allocationCount{};
    std::atomic<std::uint32_t> checks{}, submissions{}, activeTasks{}, waits{};
    thread_local bool workerThread{};
    bool rejectSubmission{}, failWorker{};
    std::mutex gateMutex;
    std::condition_variable gateCondition;
    std::atomic<bool> holdWorkers{};
    bool workerEntered{}, releaseWorkers{};

    void Require(bool condition, const char* message)
    {
        ++checks;
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    void ReleaseWorkers()
    {
        const std::lock_guard lock(gateMutex);
        releaseWorkers = true;
        gateCondition.notify_all();
    }

    void EnterWorker()
    {
        if (!holdWorkers.load(std::memory_order_relaxed))
        {
            return;
        }
        std::unique_lock lock(gateMutex);
        if (holdWorkers)
        {
            workerEntered = true;
            gateCondition.notify_all();
            if (!gateCondition.wait_for(lock, std::chrono::seconds(3), [] { return releaseWorkers; }))
            {
                throw std::runtime_error("Declaration owner did not drain accepted workers.");
            }
        }
    }

    void AwaitWorker()
    {
        if (!holdWorkers.load(std::memory_order_relaxed))
        {
            return;
        }
        std::unique_lock lock(gateMutex);
        if (holdWorkers && !gateCondition.wait_for(lock, std::chrono::seconds(3), [] { return workerEntered; }))
        {
            throw std::runtime_error("Descriptor publication did not overlap admitted CPU work.");
        }
    }

    void ResetGate(bool hold)
    {
        const std::lock_guard lock(gateMutex);
        holdWorkers = hold;
        workerEntered = false;
        releaseWorkers = false;
        rejectSubmission = false;
        failWorker = false;
        submissions = 0;
        waits = 0;
    }
} // namespace Probe

#if defined(_MSC_VER)
#define CE_ALLOCATION_PROBE_NOINLINE __declspec(noinline)
#else
#define CE_ALLOCATION_PROBE_NOINLINE __attribute__((noinline))
#endif

CE_ALLOCATION_PROBE_NOINLINE void* operator new(std::size_t bytes)
{
    if (Probe::measureAllocations.load(std::memory_order_relaxed))
    {
        Probe::allocationCount.fetch_add(1, std::memory_order_relaxed);
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

#if !defined(CE_MESH_DISPATCH_NATIVE_EXECUTOR)
// 실제 Creator.Concurrency.Jobs 계약에 맞춘 작은 지속 실행기. 벤치마크에는 게이트/지연을 쓰지 않는다.
// 완료 handle 파괴는 합류하지 않으며, 마지막 콜백 해제 뒤에만 완료가 보인다.
namespace job_detail
{
    struct completion_state
    {
        std::function<void(std::size_t)> task_;
        std::size_t count_{};
        std::atomic<std::size_t> remaining_{};
        std::atomic<bool> complete_{};
        std::mutex mutex_;
        std::condition_variable done_;
        std::exception_ptr failure_;
    };

    struct scheduler_state
    {
        std::mutex mutex_;
        std::condition_variable available_;
        std::array<std::thread, 4> workers_;
        std::shared_ptr<completion_state> work_;
        std::uint64_t generation_{};
        bool running_{}, stopping_{};
    };
} // namespace job_detail

thread_pool::thread_pool() = default;
thread_pool::~thread_pool() = default;
bool thread_pool::is_worker_thread() noexcept
{
    return Probe::workerThread;
}
std::size_t thread_pool::size() const
{
    return ce::get_job_scheduler().is_running() ? 4 : 0;
}
job_scheduler::job_scheduler(thread_pool&) : state_(std::make_shared<job_detail::scheduler_state>()) {}
job_scheduler::~job_scheduler()
{
    shutdown();
}
bool job_scheduler::is_running() const
{
    return state_->running_;
}
void job_scheduler::start(std::size_t)
{
    if (state_->running_)
    {
        return;
    }
    state_->running_ = true;
    state_->stopping_ = false;
    state_->generation_ = 0;
    for (std::size_t worker = 0; worker < state_->workers_.size(); ++worker)
    {
        state_->workers_[worker] = std::thread(
            [state = state_, worker]
            {
                Probe::workerThread = true;
                std::uint64_t generation = 0;
                for (;;)
                {
                    std::unique_lock lock(state->mutex_);
                    state->available_.wait(lock, [&] { return state->stopping_ || state->generation_ != generation; });
                    if (state->stopping_)
                    {
                        break;
                    }
                    generation = state->generation_;
                    auto work = state->work_;
                    lock.unlock();
                    try
                    {
                        Probe::EnterWorker();
                        for (std::size_t index = worker; index < work->count_; index += state->workers_.size())
                        {
                            if (Probe::failWorker && index == 0)
                            {
                                throw std::runtime_error("Injected accepted worker failure.");
                            }
                            work->task_(index);
                        }
                    }
                    catch (...)
                    {
                        const std::lock_guard failureLock(work->mutex_);
                        if (!work->failure_)
                        {
                            work->failure_ = std::current_exception();
                        }
                    }
                    if (work->remaining_.fetch_sub(1, std::memory_order_acq_rel) == 1)
                    {
                        work->task_ = {};
                        const std::lock_guard completionLock(work->mutex_);
                        --Probe::activeTasks;
                        work->complete_.store(true, std::memory_order_release);
                        work->done_.notify_all();
                    }
                }
                Probe::workerThread = false;
            });
    }
}

void job_scheduler::shutdown() noexcept
{
    if (!state_->running_)
    {
        return;
    }
    Probe::ReleaseWorkers();
    if (state_->work_)
    {
        std::unique_lock lock(state_->work_->mutex_);
        state_->work_->done_.wait(lock, [&] { return state_->work_->complete_.load(std::memory_order_acquire); });
    }
    {
        const std::lock_guard lock(state_->mutex_);
        state_->stopping_ = true;
        state_->available_.notify_all();
    }
    for (auto& worker : state_->workers_)
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }
    state_->work_.reset();
    state_->running_ = false;
}

job_handle job_scheduler::submit_indexed(std::size_t count, std::function<void(std::size_t)> task)
{
    if (!is_running() || Probe::rejectSubmission)
    {
        throw std::runtime_error("Injected submission rejection.");
    }
    auto work = std::make_shared<job_detail::completion_state>();
    work->task_ = std::move(task);
    work->count_ = count;
    work->remaining_ = state_->workers_.size();
    {
        const std::lock_guard lock(state_->mutex_);
        Probe::Require(!state_->work_ || state_->work_->complete_.load(std::memory_order_acquire),
                       "One declaration batch must join before the next admission.");
        state_->work_ = work;
        ++state_->generation_;
        ++Probe::submissions;
        ++Probe::activeTasks;
        state_->available_.notify_all();
    }
    return job_handle(work);
}

bool job_handle::is_complete() const
{
    return !state_ || state_->complete_.load(std::memory_order_acquire);
}
void job_handle::wait() const
{
    ++Probe::waits;
    Probe::ReleaseWorkers();
    if (!state_)
    {
        return;
    }
    std::unique_lock lock(state_->mutex_);
    state_->done_.wait(lock, [&] { return state_->complete_.load(std::memory_order_acquire); });
    if (state_->failure_)
    {
        std::rethrow_exception(state_->failure_);
    }
}
void job_handle::wait_assisting() const
{
    wait();
}

namespace ce
{
    thread_pool& get_thread_pool()
    {
        static thread_pool pool;
        return pool;
    }
    job_scheduler& get_job_scheduler()
    {
        static job_scheduler scheduler(get_thread_pool());
        return scheduler;
    }
} // namespace ce
#endif

#include "Render/Graph/RenderGraphDeclarationTask.h"

namespace math
{
    struct matrix4x4
    {
        std::array<float, 16> elements{};
        static matrix4x4 identity()
        {
            matrix4x4 result;
            result.elements[0] = result.elements[5] = result.elements[10] = result.elements[15] = 1;
            return result;
        }
    };
} // namespace math

// 다시 쓰는 CPU 업로드 arena와 descriptor 배열. 제품의 GPU 메모리 비용을 흉내 내지 않는다.
class IRenderDeviceServices
{
  public:
    enum class Failure
    {
        None,
        Reservation,
        InvalidSlice,
        StaleReservation,
        InvalidBindings,
        StaleBindings,
        ThrowBindings
    };
    explicit IRenderDeviceServices(std::size_t capacity = 64u << 20)
        : m_storage(std::make_unique_for_overwrite<std::byte[]>(capacity + 256)), m_capacity(capacity)
    {
        m_bytes = reinterpret_cast<std::byte*>((reinterpret_cast<std::uintptr_t>(m_storage.get()) + 255u) &
                                               ~std::uintptr_t{255u});
        m_bindings.reserve(4096);
    }
    void Reset()
    {
        m_cursor = 0;
        m_reservations = 0;
        m_bindingCalls = 0;
        m_failure = Failure::None;
        m_recording = 1;
        m_descriptors = 1;
        m_bindings.clear();
    }
    bool ReserveUploadBatch(std::span<const RHIUploadRequest> requests, std::span<RHIBufferSlice> result,
                            std::string& error)
    {
        ++m_reservations;
        if (m_failure == Failure::Reservation)
        {
            error = "Injected reservation failure.";
            return false;
        }
        Probe::Require(requests.size() <= 4 && result.size() == requests.size(), "Bounded upload request count.");
        std::array<RHIBufferSlice, 4> slices;
        auto cursor = m_cursor;
        for (std::size_t index = 0; index < requests.size(); ++index)
        {
            const auto& request = requests[index];
            const auto alignment = (std::max)(
                request.minimumAlignment, std::uint64_t{request.usage == RHIUploadUsage::ConstantBuffer ? 256u : 16u});
            cursor = (cursor + alignment - 1) & ~(alignment - 1);
            if (request.bytes > m_capacity - cursor)
            {
                error = "Probe upload capacity exceeded.";
                return false;
            }
            slices[index] = {RHIBufferHandle{1}, cursor, request.bytes, m_bytes + cursor};
            cursor += request.bytes;
        }
        std::copy_n(slices.begin(), requests.size(), result.begin());
        m_cursor = cursor;
        if (m_failure == Failure::InvalidSlice)
        {
            result.back().cpuAddress = nullptr;
        }
        if (m_failure == Failure::StaleReservation)
        {
            ++m_recording;
        }
        return true;
    }
    RHIBufferSlice AllocateUpload(const RHIUploadRequest& request)
    {
        RHIBufferSlice slice;
        std::string error;
        ReserveUploadBatch(std::span{&request, 1}, std::span{&slice, 1}, error);
        return slice;
    }
    RHIBufferSlice UploadConstants(const void* source, std::size_t bytes)
    {
        auto slice = AllocateUpload({bytes, RHIUploadUsage::ConstantBuffer, 1});
        if (slice.IsWritable() && bytes)
        {
            std::memcpy(slice.cpuAddress, source, bytes);
        }
        return slice;
    }
    RHIBindingTable CreateBindings(std::span<const RHIBindingDesc> descriptions)
    {
        Probe::Require(!thread_pool::is_worker_thread() || !Probe::submissions,
                       "Descriptor creation remains on the declaration owner.");
        ++m_bindingCalls;
        Probe::AwaitWorker();
        if (m_failure == Failure::ThrowBindings)
        {
            throw std::runtime_error("Injected descriptor owner failure.");
        }
        if (m_failure == Failure::InvalidBindings)
        {
            return {};
        }
        m_bindings.assign(descriptions.begin(), descriptions.end());
        RHIBindingTable result;
        result.backend = 1;
        result.count = static_cast<std::uint32_t>(descriptions.size());
        result.version = m_descriptors;
        if (m_failure == Failure::StaleBindings)
        {
            ++m_descriptors;
        }
        return result;
    }
    std::uint64_t GetCurrentUploadRecordingId() const { return m_recording; }
    std::uint64_t GetDescriptorVersionToken() const { return m_descriptors; }
    void ReleaseBuffer(RHIBufferHandle) {}

    Failure m_failure{};
    std::uint64_t m_recording{1}, m_descriptors{1};
    std::uint32_t m_reservations{}, m_bindingCalls{};
    std::vector<RHIBindingDesc> m_bindings;

  private:
    std::unique_ptr<std::byte[]> m_storage;
    std::byte* m_bytes{};
    std::size_t m_capacity{}, m_cursor{};
};

namespace material_graph
{
    struct IblVector
    {
        float x{}, y{}, z{}, w{};
    };
#include "MeshDispatchDeclarationTypes.inl"

    struct ProbePipeline
    {
        bool IsValid() const { return true; }
    };
    struct ProbeInput
    {
        struct Bone
        {
            std::array<float, 12> values;
        };
        struct GeometryData
        {
            std::uint32_t vertexStride{}, vertexAttributeMask{};
        };
        std::span<const std::byte> m_vertices;
        std::span<const Bone> m_bones;
        std::span<const float> m_lods;
        GeometryData m_geometry;
        math::matrix4x4 m_world{math::matrix4x4::identity()};
        std::span<const std::byte> PackedVertices() const { return m_vertices; }
        std::span<const Bone> Bones() const { return m_bones; }
        std::span<const float> Lods() const { return m_lods; }
        const GeometryData& Geometry() const { return m_geometry; }
        const math::matrix4x4& World() const { return m_world; }
    };
    class MeshSurfaceBatch
    {
      public:
        static bool PrepareDispatch(std::span<const std::shared_ptr<const MeshSurfaceBatch>> batches,
                                    std::shared_ptr<MeshTransformDispatch>& result, std::string& error);
        static bool PrepareDeclarationDispatch(std::span<const std::shared_ptr<const MeshSurfaceBatch>> batches,
                                               std::shared_ptr<MeshTransformDispatch>& result, std::string& error);
        static bool PrepareDeclarationDispatchInlineControl(
            std::span<const std::shared_ptr<const MeshSurfaceBatch>> batches,
            std::shared_ptr<MeshTransformDispatch>& result, std::string& error);
        bool IsPreparedForGraph() const;
        std::uint32_t Count() const { return count_; }
        IRenderDeviceServices* device_{};
        std::shared_ptr<MeshSurfaceBufferPool> bufferPool_;
        std::shared_ptr<ProbePipeline> pipeline_;
        std::shared_ptr<const ProbeInput> input_;
        std::shared_ptr<const MeshSurfaceBatch> transformedOwner_;
        RHIBufferHandle buffer_;
        std::uint32_t count_{};
        bool objectSpace_{};
        std::uint64_t recordingId_{1}, descriptorVersion_{1};
        std::atomic<std::uint32_t> recordedStages_{};
    };
    bool Fail(std::string& error, const char* message)
    {
        error = message;
        return false;
    }
#include "MeshDispatchDeclarationProduction.inl"
} // namespace material_graph

namespace Probe
{
    using material_graph::MeshSurfaceBatch;
    using material_graph::MeshTransformDispatch;

    struct Fixture
    {
        IRenderDeviceServices device;
        std::shared_ptr<material_graph::MeshSurfaceBufferPool> pool{
            std::make_shared<material_graph::MeshSurfaceBufferPool>(device)};
        std::shared_ptr<material_graph::ProbePipeline> pipeline{std::make_shared<material_graph::ProbePipeline>()};
        std::vector<std::byte> vertices;
        std::vector<float> lods;
        std::array<material_graph::ProbeInput::Bone, 3> bones{};
        std::vector<std::shared_ptr<const MeshSurfaceBatch>> batches;

        Fixture(std::size_t count, std::uint32_t points, bool shared)
        {
            constexpr std::uint32_t mask =
                assets::Bit(assets::VertexAttribute::Position) | assets::Bit(assets::VertexAttribute::Normal) |
                assets::Bit(assets::VertexAttribute::Uv0) | assets::Bit(assets::VertexAttribute::Tangent);
            constexpr auto stride = assets::StrideOf(mask);
            const auto unique = shared ? 1 : count;
            vertices.resize(std::size_t(points) * stride * unique + 13);
            lods.resize(std::size_t(points) * unique);
            for (std::size_t index = 0; index < vertices.size(); ++index)
            {
                vertices[index] = std::byte(index % 251);
            }
            for (std::size_t index = 0; index < lods.size(); ++index)
            {
                lods[index] = static_cast<float>(index % 17);
            }
            for (std::size_t index = 0; index < count; ++index)
            {
                auto input = std::make_shared<material_graph::ProbeInput>();
                const auto part = shared ? 0 : index;
                input->m_vertices =
                    std::span<const std::byte>(vertices).subspan(part * points * stride, std::size_t(points) * stride);
                input->m_lods = std::span<const float>(lods).subspan(part * points, points);
                input->m_bones = index % 2 ? std::span<const material_graph::ProbeInput::Bone>{} : bones;
                input->m_geometry = {stride, mask};
                input->m_world.elements[12] = static_cast<float>(index + 1);
                auto batch = std::make_shared<MeshSurfaceBatch>();
                batch->device_ = &device;
                batch->bufferPool_ = pool;
                batch->pipeline_ = pipeline;
                batch->input_ = std::move(input);
                batch->count_ = points;
                batch->objectSpace_ = index % 3 == 0;
                batch->buffer_ = RHIBufferHandle{100u + static_cast<std::uint32_t>(index)};
                batches.push_back(std::move(batch));
            }
        }
    };

    struct Snapshot
    {
        std::array<std::vector<std::byte>, 4> uploads;
        std::uint64_t points{}, bytes{};
        std::uint32_t groups{}, x{}, y{};
        explicit Snapshot(const MeshTransformDispatch& dispatch)
            : points(dispatch.pointCount), bytes(dispatch.inputBytes), groups(dispatch.groupCount),
              x(dispatch.dispatchX), y(dispatch.dispatchY)
        {
            const RHIBufferSlice slices[]{dispatch.uniform, dispatch.inputs, dispatch.jobs, dispatch.groups};
            for (std::size_t index = 0; index < uploads.size(); ++index)
            {
                const auto* begin = static_cast<const std::byte*>(slices[index].cpuAddress);
                uploads[index].assign(begin, begin + slices[index].size);
            }
        }
        void Check(const MeshTransformDispatch& dispatch) const
        {
            Require(points == dispatch.pointCount && bytes == dispatch.inputBytes && groups == dispatch.groupCount &&
                        x == dispatch.dispatchX && y == dispatch.dispatchY,
                    "Dispatch dimensions/metrics match production legacy.");
            const RHIBufferSlice slices[]{dispatch.uniform, dispatch.inputs, dispatch.jobs, dispatch.groups};
            for (std::size_t index = 0; index < uploads.size(); ++index)
            {
                Require(slices[index].size == uploads[index].size() &&
                            (uploads[index].empty() ||
                             std::memcmp(slices[index].cpuAddress, uploads[index].data(), uploads[index].size()) == 0),
                        "All four final upload slices match production legacy bytes.");
            }
        }
    };

    void CheckParity(Fixture& fixture, bool parallel, std::uint64_t expectedBytes = 0)
    {
        ResetGate(false);
        std::string error;
        std::shared_ptr<MeshTransformDispatch> dispatch;
        fixture.device.Reset();
        Require(MeshSurfaceBatch::PrepareDispatch(fixture.batches, dispatch, error),
                "Legacy serializer prepares fixture.");
        Require(fixture.device.m_reservations == 4, "Legacy issues four upload reservations.");
        const Snapshot expected(*dispatch);
        fixture.device.Reset();
        Require(MeshSurfaceBatch::PrepareDeclarationDispatch(fixture.batches, dispatch, error),
                "Declaration serializer prepares fixture.");
        Require(!expectedBytes || dispatch->inputBytes == expectedBytes,
                "Exact admission-boundary byte count is preserved.");
        Require(fixture.device.m_reservations == 1 && fixture.device.m_bindingCalls == 1,
                "One upload transaction and one descriptor creation per declaration.");
#if !defined(CE_MESH_DISPATCH_NATIVE_EXECUTOR)
        Require(submissions == (parallel ? 1u : 0u) && activeTasks == 0,
                "Bounded indexed admission is joined before output publication.");
#else
        (void)parallel;
#endif
        expected.Check(*dispatch);
        for (std::size_t index = 0; index < fixture.batches.size(); ++index)
        {
            const auto& descriptor = fixture.device.m_bindings[index];
            Require(descriptor.bufferResource == fixture.batches[index]->buffer_ &&
                        descriptor.numElements == fixture.batches[index]->Count() &&
                        descriptor.structureByteStride == sizeof(material_graph::SurfacePoint),
                    "Descriptor output order remains the sealed job order.");
        }
    }

    void CheckFailures(Fixture& fixture)
    {
        using Failure = IRenderDeviceServices::Failure;
        for (const auto failure : {Failure::Reservation, Failure::InvalidSlice, Failure::StaleReservation,
                                   Failure::InvalidBindings, Failure::StaleBindings, Failure::ThrowBindings})
        {
            fixture.device.Reset();
            const bool accepted = failure == Failure::InvalidBindings || failure == Failure::StaleBindings ||
                                  failure == Failure::ThrowBindings;
            ResetGate(accepted);
            fixture.device.m_failure = failure;
            auto sentinel = std::make_shared<MeshTransformDispatch>();
            auto result = sentinel;
            std::string error;
            bool succeeded = false, threw = false;
            try
            {
                succeeded = MeshSurfaceBatch::PrepareDeclarationDispatch(fixture.batches, result, error);
            }
            catch (const std::runtime_error&)
            {
                threw = true;
            }
            Require(!succeeded && result == sentinel && activeTasks == 0,
                    "Rejected declarations drain accepted work and preserve the previous result.");
            Require(threw == (failure == Failure::ThrowBindings),
                    "Descriptor exceptions preserve exception semantics.");
            Require(submissions == (accepted ? 1u : 0u) && waits == (accepted ? 1u : 0u),
                    "Only admitted work is joined, including descriptor failure/unwind.");
        }
        for (bool rejected : {false, true})
        {
            fixture.device.Reset();
            ResetGate(!rejected);
            rejectSubmission = rejected;
            failWorker = !rejected;
            auto sentinel = std::make_shared<MeshTransformDispatch>();
            auto result = sentinel;
            std::string error;
            bool threw = false;
            try
            {
                MeshSurfaceBatch::PrepareDeclarationDispatch(fixture.batches, result, error);
            }
            catch (const std::runtime_error&)
            {
                threw = true;
            }
            Require(threw && result == sentinel && activeTasks == 0,
                    "Submission/accepted worker failure cannot publish or leave running captures.");
            Require(submissions == (rejected ? 0u : 1u), "Rejected scheduler work never becomes accepted.");
        }
        fixture.device.Reset();
        ResetGate(false);
        std::shared_ptr<MeshTransformDispatch> dispatch;
        std::string error;
        Require(MeshSurfaceBatch::PrepareDeclarationDispatch(fixture.batches, dispatch, error),
                "Scratch can be reused after all failure paths.");
    }

    void CheckRaggedParity()
    {
        Fixture fixture(5, 33, true);
        const auto original = fixture.batches[0]->input_->PackedVertices();
        const std::vector<std::byte> separateVertices(original.begin(), original.end());
        for (std::size_t index = 1; index < fixture.batches.size(); ++index)
        {
            auto batch = std::const_pointer_cast<MeshSurfaceBatch>(fixture.batches[index]);
            auto input = std::const_pointer_cast<material_graph::ProbeInput>(batch->input_);
            batch->count_ = index == 1 ? 0 : index == 2 ? 31 : index == 3 ? 32 : 1;
            input->m_vertices = (index == 2 ? std::span<const std::byte>(separateVertices) : original)
                                    .first(std::size_t(batch->count_) * input->m_geometry.vertexStride);
            input->m_lods = input->m_lods.first(batch->count_);
            if (index == 1)
            {
                // 빈 범위도 포인터+크기가 identity이며 null 빈 범위와 섞이지 않는다.
                input->m_vertices = std::span<const std::byte>(fixture.vertices.data() + 1, 0);
                input->m_bones = std::span<const material_graph::ProbeInput::Bone>(fixture.bones.data() + 1, 0);
                input->m_lods = {};
            }
        }
        CheckParity(fixture, false);
    }

    void CheckAdmissionBoundary()
    {
        constexpr std::uint64_t threshold = 8u << 20;
        const bool hasWorkers = ce::get_thread_pool().size() >= 4;
        for (const std::uint64_t target : {threshold - 16, threshold})
        {
            Fixture fixture(1, 160000, false);
            auto input = std::const_pointer_cast<material_graph::ProbeInput>(fixture.batches.front()->input_);
            const auto originalBytes = input->m_vertices.size();
            const auto baseBytes = originalBytes + input->m_bones.size_bytes() + input->m_lods.size_bytes() +
                                   sizeof(MeshTransformDispatch::Job) + 5000 * sizeof(MeshTransformDispatch::Group);
            Require(target > baseBytes && (target - baseBytes) % 16 == 0,
                    "Boundary fixture keeps packed input alignment.");
            // 원본 범위 뒤에 읽을 수 있는 바이트를 보태 전송 계획의 분기 경계를 정확히 맞춘다.
            fixture.vertices.resize(originalBytes + target - baseBytes, std::byte{0x5d});
            input->m_vertices = fixture.vertices;
            const bool parallel = target == threshold && hasWorkers;
            CheckParity(fixture, parallel, target);
            fixture.device.Reset();
            std::shared_ptr<MeshTransformDispatch> dispatch;
            std::string error;
            error.reserve(256);
            allocationCount = 0;
            measureAllocations = true;
            const bool success = MeshSurfaceBatch::PrepareDeclarationDispatch(fixture.batches, dispatch, error);
            measureAllocations = false;
            Require(success && (parallel ? allocationCount.load() > 1 : allocationCount.load() == 1),
                    "Byte/worker admission boundary suppresses scheduler allocations when running inline.");
        }
    }

    void CheckNested(Fixture& fixture)
    {
        fixture.device.Reset();
        ResetGate(false);
        workerThread = true;
        std::shared_ptr<MeshTransformDispatch> dispatch;
        std::string error;
        Require(MeshSurfaceBatch::PrepareDeclarationDispatch(fixture.batches, dispatch, error) && submissions == 0,
                "Nested workers reuse declaration scratch synchronously without allocating staging containers.");
        {
            render_graph_declaration_lock lock(fixture.pool->m_declarationMutex);
            Require(lock.owns_lock(), "Nested probe holds one declaration guard.");
            Require(!MeshSurfaceBatch::PrepareDeclarationDispatch(fixture.batches, dispatch, error),
                    "An assisting worker never blocks on its declaration owner's scratch.");
        }
        workerThread = false;
    }

    std::size_t MeasureAllocations(Fixture& fixture, bool declaration)
    {
        fixture.device.Reset();
        ResetGate(false);
        std::string error;
        error.reserve(256);
        std::shared_ptr<MeshTransformDispatch> dispatch;
        allocationCount = 0;
        measureAllocations = true;
        const bool success = declaration
                                 ? MeshSurfaceBatch::PrepareDeclarationDispatch(fixture.batches, dispatch, error)
                                 : MeshSurfaceBatch::PrepareDispatch(fixture.batches, dispatch, error);
        measureAllocations = false;
        Require(success, "Allocation probe declaration completes.");
        return allocationCount.load();
    }

    void Benchmark(const char* name, std::size_t jobs, std::uint32_t points, bool shared, std::size_t iterations,
                   std::size_t workers)
    {
        Fixture fixture(jobs, points, shared);
        ce::get_job_scheduler().shutdown();
        CheckParity(fixture, false);
        const auto legacyAllocations = MeasureAllocations(fixture, false);
        const auto declarationAllocations = MeasureAllocations(fixture, true);
        Require(declarationAllocations == 1,
                "Warm synchronous declaration only allocates its durable dispatch result.");
        std::array<std::vector<double>, 3> samples;
        for (auto& sample : samples)
        {
            sample.reserve(iterations);
        }
        std::string error;
        error.reserve(256);
        std::shared_ptr<MeshTransformDispatch> dispatch;
        ce::get_job_scheduler().start(workers);
        const auto parallelAllocations = MeasureAllocations(fixture, true);
        fixture.device.Reset();
        Require(MeshSurfaceBatch::PrepareDispatch(fixture.batches, dispatch, error), "Create control reference bytes.");
        const Snapshot controlReference(*dispatch);
        fixture.device.Reset();
        Require(MeshSurfaceBatch::PrepareDeclarationDispatchInlineControl(fixture.batches, dispatch, error),
                "Explicit inline control prepares the same sealed input.");
        controlReference.Check(*dispatch);
        for (std::size_t iteration = 0; iteration < iterations + 6; ++iteration)
        {
            for (std::size_t order = 0; order < 3; ++order)
            {
                const auto version = (order + iteration) % 3;
                fixture.device.Reset();
                const auto start = std::chrono::steady_clock::now();
                const bool success =
                    version == 0 ? MeshSurfaceBatch::PrepareDispatch(fixture.batches, dispatch, error)
                    : version == 1
                        ? MeshSurfaceBatch::PrepareDeclarationDispatch(fixture.batches, dispatch, error)
                        : MeshSurfaceBatch::PrepareDeclarationDispatchInlineControl(fixture.batches, dispatch, error);
                const auto end = std::chrono::steady_clock::now();
                Require(success, "Timed production preparation succeeds.");
                if (iteration >= 6)
                {
                    samples[version].push_back(std::chrono::duration<double, std::micro>(end - start).count());
                }
            }
        }
        for (auto& sample : samples)
        {
            std::sort(sample.begin(), sample.end());
        }
        std::printf("MESH_DECLARATION_CPU_BENCH name=%s workers=%zu jobs=%zu points_each=%u shared=%d iterations=%zu "
                    "legacy_median_us=%.3f declaration_median_us=%.3f inline_control_median_us=%.3f "
                    "legacy_p95_us=%.3f declaration_p95_us=%.3f inline_control_p95_us=%.3f "
                    "legacy_allocations=%zu declaration_inline_allocations=%zu declaration_parallel_allocations=%zu\n",
                    name, workers, jobs, points, shared, iterations, samples[0][iterations / 2],
                    samples[1][iterations / 2], samples[2][iterations / 2], samples[0][iterations * 95 / 100],
                    samples[1][iterations * 95 / 100], samples[2][iterations * 95 / 100], legacyAllocations,
                    declarationAllocations, parallelAllocations);
    }
} // namespace Probe

int main(int argc, char** argv)
{
    try
    {
        Probe::Fixture small(5, 33, true);
        Probe::CheckParity(small, false);
        Probe::CheckRaggedParity();
        const auto legacy = Probe::MeasureAllocations(small, false);
        const auto declaration = Probe::MeasureAllocations(small, true);
        Probe::Require(declaration == 1 && legacy > declaration,
                       "Warm scratch removes local staging allocations; durable result allocation remains explicit.");
        Probe::Fixture large(7, 65537, false);
        ce::get_job_scheduler().start(4);
        Probe::CheckAdmissionBoundary();
#if defined(CE_MESH_DISPATCH_NATIVE_EXECUTOR)
        ce::get_job_scheduler().shutdown();
        ce::get_job_scheduler().start(1);
        Probe::CheckAdmissionBoundary();
        ce::get_job_scheduler().shutdown();
        ce::get_job_scheduler().start(4);
#endif
        Probe::CheckParity(large, true);
#if !defined(CE_MESH_DISPATCH_NATIVE_EXECUTOR)
        Probe::CheckFailures(large);
        Probe::CheckNested(large);
#endif
        ce::get_job_scheduler().shutdown();
        Probe::CheckParity(large, false);
        if (argc >= 2 && std::string_view(argv[1]) == "--benchmark")
        {
            const auto workers = argc > 2 ? static_cast<std::size_t>(std::stoul(argv[2])) : 4;
            Probe::Benchmark("tiny", 1, 32, false, 2000, workers);
            Probe::Benchmark("shared", 64, 4096, true, 160, workers);
            Probe::Benchmark("half_mib_below", 1, 10000, false, 120, workers);
            Probe::Benchmark("half_mib_above", 1, 10064, false, 120, workers);
            Probe::Benchmark("one_mib_below", 1, 20000, false, 120, workers);
            Probe::Benchmark("one_mib_above", 1, 20224, false, 120, workers);
            Probe::Benchmark("two_mib_above", 1, 40320, false, 120, workers);
            Probe::Benchmark("four_mib_above", 1, 80640, false, 120, workers);
            Probe::Benchmark("eight_mib_above", 1, 161280, false, 100, workers);
            Probe::Benchmark("large_single", 1, 262144, false, 80, workers);
            Probe::Benchmark("unique", 16, 32768, false, 60, workers);
        }
        ce::get_job_scheduler().shutdown();
        std::printf("MESH_DECLARATION_CPU_OK checks=%u byte_parity=1 "
#if !defined(CE_MESH_DISPATCH_NATIVE_EXECUTOR)
                    "failure_join=1 nested_inline=1 "
#else
                    "production_executor=1 fault_injection=separate_fake_mode "
#endif
                    "upload_reservations=4_to_1 warm_inline_allocations=%zu legacy_fixture_allocations=%zu\n",
                    Probe::checks.load(), declaration, legacy);
        return 0;
    }
    catch (const std::exception& failure)
    {
        Probe::measureAllocations = false;
        Probe::workerThread = false;
        Probe::ReleaseWorkers();
        ce::get_job_scheduler().shutdown();
        std::fprintf(stderr, "MESH_DECLARATION_CPU_FAILED %s\n", failure.what());
        return 1;
    }
}
