#include "DX12QueueService.h"
#include "DX12QueueBatchAdapter.h"
#include "DX12QueueRecorder.h"
#include "DX12DeviceResources.h"
#include "DX12Encoder.h"
#include "../RHISubmissionThread.h"
#include <atomic>
#include <algorithm>
#include <chrono>
#include <climits>
#include <exception>
#include <stdexcept>
#include <utility>
#include <mutex>
#include <thread>
#include <vector>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;
namespace
{
    std::atomic<uint64_t> nextDeviceIncarnation{1};
    class DX12ServiceQueue;
    class DX12ServiceBatch;
}

struct DX12QueueServiceState
{
    ComPtr<ID3D12Device> device;
    std::mutex admission;
    std::atomic<bool> active{true};
    uint64_t incarnation{nextDeviceIncarnation.fetch_add(1)};
    uint32_t nextQueue{1};
    uint64_t nextRecording{1};
    bool submissionClient{false};
    int testRejection{0};
    std::vector<std::shared_ptr<DX12ServiceQueue>> queues;
    std::shared_ptr<IRHITimelineFence> primaryTimeline;
    ComPtr<ID3D12CommandQueue> primaryQueue;
    std::vector<ComPtr<ID3D12Fence>> primaryWaits;
};

namespace
{
    struct DX12QueueRecordingStorage
    {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
    };

    struct DX12QueueRecordingPool
    {
        DX12QueueRecordingPool()
        {
            free.reserve(32);
        }
        std::mutex mutex;
        bool active{true};
        uint64_t created{0}, reused{0}, leased{0};
        std::vector<std::unique_ptr<DX12QueueRecordingStorage>> free;
    };

    // Only batch retirement (or a never-submitted recording) releases this lease.
    // Cached storage must not retain graph/resource ownership between recordings.
    struct DX12QueueRecordingLease
    {
        std::shared_ptr<DX12QueueRecordingPool> pool;
        std::unique_ptr<DX12QueueRecordingStorage> storage;
        std::shared_ptr<const void> owner;
        bool closed{false};
        ~DX12QueueRecordingLease()
        {
            if (!pool || !storage)
            {
                return;
            }
            std::lock_guard lock(pool->mutex);
            --pool->leased;
            if (closed && pool->active && pool->free.size() < 32)
            {
                pool->free.push_back(std::move(storage));
            }
        }
    };

    class DX12QueueRecording final : public IRHIQueueRecording
    {
    public:
        std::shared_ptr<IRHICommandQueue> queue;
        std::shared_ptr<std::vector<std::shared_ptr<DX12QueueRecordingLease>>> leases{
            std::make_shared<std::vector<std::shared_ptr<DX12QueueRecordingLease>>>()};
        std::vector<std::unique_ptr<DX12Encoder>> encoders;
        bool finished{false};

        RHIEncoder& AcquireEncoder(uint32_t target) override
        {
            if (finished || target >= encoders.size())
            {
                throw std::logic_error("Queue recording target is unavailable.");
            }
            auto& encoder = *encoders[target];
            if (encoder.GetDroppedCount() != 0)
            {
                throw std::runtime_error("Queue encoder dropped a command.");
            }
            encoder.ResetState((*leases)[target]->storage->list.Get());
            return encoder;
        }

        bool Finish(std::shared_ptr<IRHIQueueCommandBatch>& batch, std::string& error) override
        {
            batch.reset();
            if (finished)
            {
                error = "Queue recording was already sealed.";
                return false;
            }
            finished = true;
            std::vector<ID3D12CommandList*> lists;
            lists.reserve(leases->size());
            for (size_t target = 0; target < leases->size(); ++target)
            {
                auto& lease = (*leases)[target];
                if (encoders[target]->GetDroppedCount() != 0)
                {
                    error = "Queue encoder dropped a command.";
                    return false;
                }
                if (FAILED(lease->storage->list->Close()))
                {
                    error = "Queue recording Close failed.";
                    return false;
                }
                lease->closed = true;
                lists.push_back(lease->storage->list.Get());
            }
            // One retirement owns all targets, in their original contiguous order.
            return DX12SealQueueRecording(queue, lists, leases, batch, error);
        }
    };

    bool QueueOperation(const std::shared_ptr<DX12QueueServiceState>& state,
        const std::function<HRESULT()>& operation, std::string& error)
    {
        HRESULT result = E_FAIL;
        const auto work = [&](std::string&)
        {
            result = operation();
            return true; // Native rejection is handled by this queue, not a foreign owner.
        };
        if (GetRHISubmissionThread().IsCurrentThread())
        {
            work(error);
        }
        else if (!GetRHISubmissionThread().ExecuteAndWait(state.get(), "Q0 queue operation", work, error))
        {
            return false;
        }
        return SUCCEEDED(result);
    }

    bool DispatchQueueOperation(const std::shared_ptr<DX12QueueServiceState>& state,
        const std::function<bool()>& operation, std::string& error)
    {
        bool accepted = false;
        std::string dispatchError;
        const bool dispatched = GetRHISubmissionThread().ExecuteAndWait(state.get(), "Q0 queue admission",
            [&](std::string&)
            {
                accepted = operation();
                return true; // Native failure is tracked by the queue service.
            }, dispatchError);
        if (!dispatched)
        {
            error = std::move(dispatchError);
        }
        return dispatched && accepted;
    }

    class DX12ServiceBatch final : public IRHIQueueCommandBatch
    {
    public:
        std::shared_ptr<DX12QueueServiceState> state;
        RHIQueueIdentity identity;
        uint64_t recording{0};
        bool consumed{false};
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
        std::vector<ComPtr<ID3D12CommandList>> sealedLists;
        ComPtr<ID3D12Resource> source, destination;
        std::shared_ptr<const void> token;
        RHIQueueIdentity GetQueueIdentity() const override { return identity; }
        uint64_t GetRecordingId() const override { return recording; }
    };

    class DX12ServiceTimeline final : public IRHITimelineFence
    {
    public:
        std::shared_ptr<DX12QueueServiceState> state;
        ComPtr<ID3D12Fence> native;
        RHIQueueIdentity identity;
        std::atomic<uint64_t> issued{0};
        RHIQueueIdentity GetProducer() const override { return identity; }
        uint64_t GetLastIssuedValue() const override { return issued.load(); }
        RHITimelineSnapshot QueryCompletion() const override
        {
            const auto completed = native->GetCompletedValue();
            if (completed == UINT64_MAX || FAILED(state->device->GetDeviceRemovedReason()))
            {
                return {RHITimelineStatus::DeviceLost, 0};
            }
            if (!state->active.load())
            {
                return {RHITimelineStatus::Unavailable, 0};
            }
            return {RHITimelineStatus::Available, completed};
        }
    };

    class DX12ServiceQueue final : public IRHICommandQueue
    {
    public:
        ComPtr<ID3D12CommandQueue> native;
        std::shared_ptr<DX12ServiceTimeline> timeline;
        std::shared_ptr<DX12QueueRecordingPool> recordingPool{std::make_shared<DX12QueueRecordingPool>()};
        // Keep waited fences alive until the entire service has drained. Resource
        // submission/retirement is intentionally not exposed by this primitive API.
        std::vector<std::shared_ptr<DX12ServiceTimeline>> waits;
        std::vector<ComPtr<ID3D12Fence>> testGates;
        struct Retirement
        {
            std::shared_ptr<DX12ServiceBatch> batch;
            uint64_t value;
        };
        std::vector<Retirement> pending;
        bool faulted{false};
        RHIQueueIdentity GetIdentity() const override { return timeline->identity; }
        bool Signal(uint64_t value, RHITimelinePoint& point, std::string& error) override
        {
            point = {};
            error.clear();
            if (!GetRHISubmissionThread().IsCurrentThread())
            {
                return DispatchQueueOperation(timeline->state, [&] { return Signal(value, point, error); }, error);
            }
            std::lock_guard lock(timeline->state->admission);
            if (!timeline->state->active.load() || faulted || value == 0 || value == UINT64_MAX ||
                value <= timeline->issued.load() ||
                FAILED(timeline->state->device->GetDeviceRemovedReason()))
            {
                error = "Queue is unavailable or signal value is not monotonically increasing.";
                return false;
            }
            const bool accepted = QueueOperation(timeline->state,
                [&] { return native->Signal(timeline->native.Get(), value); }, error);
            if (!accepted || timeline->native->GetCompletedValue() == UINT64_MAX ||
                FAILED(timeline->state->device->GetDeviceRemovedReason()))
            {
                error = "Native queue Signal failed or device was removed.";
                return false;
            }
            timeline->issued.store(value);
            point = {timeline, value};
            return true;
        }
        bool Wait(const RHITimelinePoint& point, std::string& error) override
        {
            if (!GetRHISubmissionThread().IsCurrentThread())
            {
                return DispatchQueueOperation(timeline->state, [&] { return Wait(point, error); }, error);
            }
            error.clear();
            std::lock_guard lock(timeline->state->admission);
            if (!timeline->state->active.load() || faulted ||
                !ValidateQueueWait(GetIdentity(), point, error))
            {
                error = "Unavailable queue or invalid wait: " + error;
                return false;
            }
            const auto producer = std::dynamic_pointer_cast<DX12ServiceTimeline>(point.fence);
            if (!producer || producer->state != timeline->state ||
                FAILED(timeline->state->device->GetDeviceRemovedReason()))
            {
                error = "Wait requires a native timeline from this device service.";
                return false;
            }
            // Allocate retention before enqueue; an allocation failure cannot leave
            // an accepted GPU wait referring to an unretained native fence.
            if (std::find(waits.begin(), waits.end(), producer) == waits.end())
            {
                waits.push_back(producer);
            }
            const bool accepted = QueueOperation(timeline->state,
                [&] { return native->Wait(producer->native.Get(), point.value); }, error);
            if (!accepted || FAILED(timeline->state->device->GetDeviceRemovedReason()))
            {
                // Retain even on device loss: native admission may already have occurred.
                error = "Native queue Wait failed or device was removed.";
                return false;
            }
            return true;
        }
        bool Submit(const std::shared_ptr<IRHIQueueCommandBatch>& batch,
            uint64_t value, RHITimelinePoint& point, std::string& error) override
        {
            point = {};
            error.clear();
            if (!GetRHISubmissionThread().IsCurrentThread())
            {
                return DispatchQueueOperation(timeline->state, [&] { return Submit(batch, value, point, error); }, error);
            }
            std::lock_guard lock(timeline->state->admission);
            const auto recording = std::dynamic_pointer_cast<DX12ServiceBatch>(batch);
            if (!timeline->state->active.load() || faulted || !recording ||
                recording->state != timeline->state || recording->identity != GetIdentity() ||
                recording->consumed || value == 0 || value == UINT64_MAX || value <= timeline->issued.load() ||
                FAILED(timeline->state->device->GetDeviceRemovedReason()))
            {
                error = "Submission requires an unconsumed recording from this queue and a fresh value.";
                return false;
            }
            const int rejection = std::exchange(timeline->state->testRejection, 0);
            if (rejection == 1)
            {
                error = "Injected rejection before native execution.";
                return false;
            }
            std::vector<ID3D12CommandList*> lists;
            if (recording->sealedLists.empty())
            {
                lists.push_back(recording->list.Get());
            }
            else
            {
                for (const auto& list : recording->sealedLists)
                {
                    lists.push_back(list.Get());
                }
            }
            pending.push_back({recording, 0}); // Allocate ownership before execution.
            bool executed = false;
            const bool accepted = QueueOperation(timeline->state, [&]
            {
                native->ExecuteCommandLists(static_cast<UINT>(lists.size()), lists.data());
                executed = true;
                return rejection == 2 ? E_FAIL : native->Signal(timeline->native.Get(), value);
            }, error);
            recording->consumed = executed;
            if (!executed)
            {
                pending.pop_back();
                return false;
            }
            if (!accepted || timeline->native->GetCompletedValue() == UINT64_MAX ||
                FAILED(timeline->state->device->GetDeviceRemovedReason()))
            {
                faulted = true;
                error = "Executed submission has no proven signal; quarantined until teardown.";
                return false;
            }
            pending.back().value = value;
            timeline->issued.store(value);
            point = {timeline, value};
            return true;
        }
        size_t CollectCompleted() override
        {
            std::vector<std::shared_ptr<DX12ServiceBatch>> released;
            {
                std::lock_guard lock(timeline->state->admission);
                const auto snapshot = timeline->QueryCompletion();
                if (snapshot.status != RHITimelineStatus::Available)
                {
                    return 0;
                }
                released.reserve(pending.size());
                for (auto& entry : pending)
                {
                    if (entry.value != 0 && snapshot.completedValue >= entry.value)
                    {
                        released.push_back(std::move(entry.batch));
                    }
                }
                std::erase_if(pending, [](const Retirement& entry) { return !entry.batch; });
            }
            return released.size(); // Destroy caller-owned tokens outside admission lock.
        }
        size_t GetPendingBatchCount() const override
        {
            std::lock_guard lock(timeline->state->admission);
            return pending.size();
        }
    };

    void ReleaseServiceOwners(const std::shared_ptr<DX12QueueServiceState>& state,
        std::vector<std::shared_ptr<DX12ServiceBatch>>& released)
    {
        for (const auto& queue : state->queues)
        {
            {
                std::lock_guard lock(queue->recordingPool->mutex);
                queue->recordingPool->active = false;
                queue->recordingPool->free.clear();
            }
            for (auto& entry : queue->pending)
            {
                released.push_back(std::move(entry.batch));
            }
            queue->pending.clear();
            queue->waits.clear();
            queue->testGates.clear();
        }
        state->queues.clear();
        state->primaryTimeline.reset();
        state->primaryWaits.clear();
        state->primaryQueue.Reset();
    }
}

DX12QueueService::DX12QueueService(ID3D12Device* device)
    : state_(std::make_shared<DX12QueueServiceState>())
{
    state_->device = device;
    state_->active.store(device != nullptr);
    if (device)
    {
        std::string error;
        if (!GetRHISubmissionThread().AcquireClient(state_.get(), error))
        {
            throw std::runtime_error(error);
        }
        state_->submissionClient = true;
    }
}

DX12QueueService::~DX12QueueService()
{
    std::string error;
    if (!Shutdown(error))
    {
        // Releasing accepted queues without idle/device-loss proof is unsafe.
        std::terminate();
    }
}

RHIQueueCapabilities DX12QueueService::QueryQueueCapabilities() const
{
    const bool available = state_->active.load() && state_->device &&
        SUCCEEDED(state_->device->GetDeviceRemovedReason());
    return {available, available, available, available};
}

bool DX12QueueService::CreateQueue(RHIQueueKind kind,
    std::shared_ptr<IRHICommandQueue>& queue, std::string& error)
{
    queue.reset();
    error.clear();
    std::lock_guard lock(state_->admission);
    if (!QueryQueueCapabilities().Supports(kind) || state_->nextQueue == 0)
    {
        error = "Queue service or queue kind is unavailable.";
        return false;
    }
    auto result = std::make_shared<DX12ServiceQueue>();
    result->timeline = std::make_shared<DX12ServiceTimeline>();
    result->timeline->state = state_;
    result->timeline->identity = {state_->incarnation, state_->nextQueue++, kind};
    D3D12_COMMAND_QUEUE_DESC desc{};
    desc.Type = kind == RHIQueueKind::Graphics ? D3D12_COMMAND_LIST_TYPE_DIRECT :
        kind == RHIQueueKind::Compute ? D3D12_COMMAND_LIST_TYPE_COMPUTE : D3D12_COMMAND_LIST_TYPE_COPY;
    if (FAILED(state_->device->CreateCommandQueue(&desc, IID_PPV_ARGS(&result->native))) ||
        FAILED(state_->device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
            IID_PPV_ARGS(&result->timeline->native))))
    {
        error = "Native queue/timeline creation failed.";
        return false;
    }
    state_->queues.push_back(result);
    queue = std::move(result);
    return true;
}

bool DX12QueueService::ImportPrimaryCompletion(ID3D12Fence* fence, uint64_t value,
    RHITimelinePoint& point, std::string& error)
{
    point = {};
    std::lock_guard lock(state_->admission);
    ComPtr<ID3D12Device> device;
    if (!state_->active.load() || !fence || value == 0 || value == UINT64_MAX ||
        FAILED(fence->GetDevice(IID_PPV_ARGS(&device))) || device.Get() != state_->device.Get() ||
        FAILED(state_->device->GetDeviceRemovedReason()))
    {
        error = "Primary completion requires a live fence from this device.";
        return false;
    }
    auto timeline = std::dynamic_pointer_cast<DX12ServiceTimeline>(state_->primaryTimeline);
    if (!timeline)
    {
        if (state_->nextQueue == 0)
        {
            error = "Primary timeline identity range is exhausted.";
            return false;
        }
        timeline = std::make_shared<DX12ServiceTimeline>();
        timeline->state = state_;
        timeline->native = fence;
        timeline->identity = {state_->incarnation, state_->nextQueue++, RHIQueueKind::Graphics};
        state_->primaryTimeline = timeline;
    }
    if (timeline->native.Get() != fence || value < timeline->issued.load())
    {
        error = "Primary completion fence changed or moved backwards.";
        return false;
    }
    timeline->issued.store(value);
    point = {timeline, value};
    return true;
}

bool DX12QueueService::GetPrimaryGraphicsQueue(ID3D12CommandQueue* primary,
    std::shared_ptr<IRHICommandQueue>& queue, std::string& error)
{
    queue.reset();
    error.clear();
    std::lock_guard lock(state_->admission);
    ComPtr<ID3D12Device> device;
    if (!state_->active.load() || !primary || primary->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT ||
        FAILED(primary->GetDevice(IID_PPV_ARGS(&device))) || device.Get() != state_->device.Get() ||
        FAILED(state_->device->GetDeviceRemovedReason()) ||
        (state_->primaryQueue && state_->primaryQueue.Get() != primary))
    {
        error = "Primary graphics endpoint requires this device's live presentation queue.";
        return false;
    }
    for (const auto& endpoint : state_->queues)
    {
        if (endpoint->native.Get() == primary)
        {
            if (endpoint->faulted)
            {
                error = "Primary graphics endpoint requires recovery after an unfenced submission.";
                return false;
            }
            queue = endpoint;
            return true;
        }
    }
    if (state_->nextQueue == 0)
    {
        error = "Queue identity range is exhausted.";
        return false;
    }
    auto endpoint = std::make_shared<DX12ServiceQueue>();
    endpoint->native = primary;
    endpoint->timeline = std::make_shared<DX12ServiceTimeline>();
    endpoint->timeline->state = state_;
    endpoint->timeline->identity = {state_->incarnation, state_->nextQueue++, RHIQueueKind::Graphics};
    if (FAILED(state_->device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
        IID_PPV_ARGS(&endpoint->timeline->native))))
    {
        error = "Primary graphics timeline creation failed.";
        return false;
    }
    state_->queues.push_back(endpoint);
    state_->primaryQueue = primary;
    queue = std::move(endpoint);
    return true;
}

ID3D12CommandQueue* DX12QueueService::NativeQueue(const std::shared_ptr<IRHICommandQueue>& queue)
{
    const auto native = std::dynamic_pointer_cast<DX12ServiceQueue>(queue);
    if (!native || !native->timeline->state->active.load() || native->faulted)
    {
        return nullptr;
    }
    return native->native.Get();
}

bool DX12QueueService::JoinPrimaryQueue(ID3D12CommandQueue* queue,
    const RHITimelinePoint& point, std::string& error)
{
    std::unique_lock lock(state_->admission);
    const auto producer = std::dynamic_pointer_cast<DX12ServiceTimeline>(point.fence);
    ComPtr<ID3D12Device> device;
    if (!state_->active.load() || !queue || queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT ||
        FAILED(queue->GetDevice(IID_PPV_ARGS(&device))) || device.Get() != state_->device.Get() ||
        !producer || producer->state != state_ || !point.IsValid() ||
        (state_->primaryQueue && state_->primaryQueue.Get() != queue) ||
        FAILED(state_->device->GetDeviceRemovedReason()))
    {
        error = "Primary join requires an issued completion from this device service.";
        return false;
    }
    state_->primaryQueue = queue;
    for (const auto& endpoint : state_->queues)
    {
        if (endpoint->native.Get() == queue && endpoint->timeline == producer)
        {
            // Graph epilogue already joined compute. The primary frame suffix
            // follows it in FIFO order and retains the ordinary frame-slot fence.
            return true;
        }
    }
    if (std::find_if(state_->primaryWaits.begin(), state_->primaryWaits.end(),
        [&](const auto& fence) { return fence.Get() == producer->native.Get(); }) == state_->primaryWaits.end())
    {
        state_->primaryWaits.push_back(producer->native);
    }
    lock.unlock(); // Never hold admission while waiting behind an RHI-owned plan.
    if (!QueueOperation(state_, [&] { return queue->Wait(producer->native.Get(), point.value); }, error))
    {
        error = "Primary queue GPU wait failed.";
        return false;
    }
    return true;
}

bool DX12QueueService::Shutdown(std::string& error)
{
    error.clear();
    std::vector<std::shared_ptr<DX12ServiceBatch>> released;
    std::vector<std::shared_ptr<DX12ServiceQueue>> queues;
    ComPtr<ID3D12CommandQueue> primary;
    {
        std::lock_guard lock(state_->admission);
        state_->active.store(false);
        queues = state_->queues;
        primary = state_->primaryQueue;
    }
    const auto release = [&]
    {
        bool releaseClient = false;
        {
            std::lock_guard lock(state_->admission);
            ReleaseServiceOwners(state_, released);
            releaseClient = std::exchange(state_->submissionClient, false);
        }
        if (releaseClient)
        {
            GetRHISubmissionThread().ReleaseClient(state_.get());
        }
        return true;
    };
    // Final markers cover waits accepted after the last public Signal. Public
    // waits only accept already-issued signals, so this primitive API cannot
    // create a wait on a future, unissued value.
    std::vector<std::pair<ComPtr<ID3D12Fence>, uint64_t>> markers;
    markers.reserve(queues.size());
    for (const auto& queue : queues)
    {
        if (FAILED(state_->device->GetDeviceRemovedReason()))
        {
            return release();
        }
        ComPtr<ID3D12Fence> marker;
        if (FAILED(state_->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&marker))) ||
            !QueueOperation(state_, [&] { return queue->native->Signal(marker.Get(), 1); }, error))
        {
            if (FAILED(state_->device->GetDeviceRemovedReason()))
            {
                return release();
            }
            error = "Queue service teardown Signal failed without device-loss proof.";
            return false;
        }
        markers.emplace_back(marker, 1);
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    if (primary && SUCCEEDED(state_->device->GetDeviceRemovedReason()))
    {
        ComPtr<ID3D12Fence> marker;
        if (FAILED(state_->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&marker))) ||
            !QueueOperation(state_, [&] { return primary->Signal(marker.Get(), 1); }, error))
        {
            if (FAILED(state_->device->GetDeviceRemovedReason()))
            {
                return release();
            }
            error = "Primary queue join teardown Signal failed.";
            return false;
        }
        markers.emplace_back(marker, 1);
    }
    for (const auto& [fence, value] : markers)
    {
        while (fence->GetCompletedValue() < value)
        {
            if (FAILED(state_->device->GetDeviceRemovedReason()))
            {
                return release();
            }
            if (std::chrono::steady_clock::now() >= deadline)
            {
                error = "Queue service teardown completion timed out.";
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    return release();
}

bool DX12QueueService::RecordBufferCopy(const std::shared_ptr<IRHICommandQueue>& queue,
    ID3D12Resource* source, ID3D12Resource* destination, uint64_t bytes,
    std::shared_ptr<IRHIQueueCommandBatch>& batch, std::string& error,
    std::shared_ptr<const void> lifetimeToken)
{
    batch.reset();
    error.clear();
    const auto target = std::dynamic_pointer_cast<DX12ServiceQueue>(queue);
    if (!target)
    {
        error = "Buffer recording requires a native DX12 queue.";
        return false;
    }
    const auto recordingState = target->timeline->state;
    std::lock_guard lock(recordingState->admission);
    if (!recordingState->active.load() ||
        target->faulted || !source || !destination || source == destination || bytes == 0 ||
        FAILED(recordingState->device->GetDeviceRemovedReason()))
    {
        error = "Buffer recording requires this service's live queue and distinct resources.";
        return false;
    }
    ComPtr<ID3D12Device> sourceDevice, destinationDevice;
    D3D12_HEAP_PROPERTIES sourceHeap{}, destinationHeap{};
    D3D12_HEAP_FLAGS flags{};
    if (FAILED(source->GetDevice(IID_PPV_ARGS(&sourceDevice))) ||
        FAILED(destination->GetDevice(IID_PPV_ARGS(&destinationDevice))) ||
        sourceDevice.Get() != recordingState->device.Get() || destinationDevice.Get() != recordingState->device.Get() ||
        source->GetDesc().Dimension != D3D12_RESOURCE_DIMENSION_BUFFER ||
        destination->GetDesc().Dimension != D3D12_RESOURCE_DIMENSION_BUFFER ||
        source->GetDesc().Width < bytes || destination->GetDesc().Width < bytes ||
        FAILED(source->GetHeapProperties(&sourceHeap, &flags)) ||
        FAILED(destination->GetHeapProperties(&destinationHeap, &flags)) ||
        (sourceHeap.Type != D3D12_HEAP_TYPE_DEFAULT && sourceHeap.Type != D3D12_HEAP_TYPE_UPLOAD) ||
        (destinationHeap.Type != D3D12_HEAP_TYPE_DEFAULT && destinationHeap.Type != D3D12_HEAP_TYPE_READBACK))
    {
        error = "Unsupported device, buffer size or heap type for COMMON copy recording.";
        return false;
    }
    auto result = std::make_shared<DX12ServiceBatch>();
    result->state = recordingState;
    result->identity = queue->GetIdentity();
    result->recording = recordingState->nextRecording++;
    result->source = source;
    result->destination = destination;
    result->token = std::move(lifetimeToken);
    const auto type = target->native->GetDesc().Type;
    if (FAILED(recordingState->device->CreateCommandAllocator(type, IID_PPV_ARGS(&result->allocator))) ||
        FAILED(recordingState->device->CreateCommandList(0, type, result->allocator.Get(), nullptr,
            IID_PPV_ARGS(&result->list))))
    {
        error = "Native queue recording creation failed.";
        return false;
    }
    const auto transition = [&](ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
        D3D12_RESOURCE_STATES after)
    {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
        result->list->ResourceBarrier(1, &barrier);
    };
    if (sourceHeap.Type == D3D12_HEAP_TYPE_DEFAULT)
    {
        transition(source, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
    }
    if (destinationHeap.Type == D3D12_HEAP_TYPE_DEFAULT)
    {
        transition(destination, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
    }
    result->list->CopyBufferRegion(destination, 0, source, 0, bytes);
    if (sourceHeap.Type == D3D12_HEAP_TYPE_DEFAULT)
    {
        transition(source, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
    }
    if (destinationHeap.Type == D3D12_HEAP_TYPE_DEFAULT)
    {
        transition(destination, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
    }
    if (FAILED(result->list->Close()))
    {
        error = "Native queue recording close failed.";
        return false;
    }
    batch = std::move(result);
    return true;
}

bool DX12SealQueueRecording(const std::shared_ptr<IRHICommandQueue>& queue,
    std::span<ID3D12CommandList* const> lists, std::shared_ptr<const void> lifetimeToken,
    std::shared_ptr<IRHIQueueCommandBatch>& batch, std::string& error)
{
    batch.reset();
    error.clear();
    const auto target = std::dynamic_pointer_cast<DX12ServiceQueue>(queue);
    if (!target || lists.empty() || lists.size() > UINT_MAX || !lifetimeToken)
    {
        error = "Sealed recording needs a native queue, closed lists and pinned storage.";
        return false;
    }
    const auto state = target->timeline->state;
    std::lock_guard lock(state->admission);
    if (!state->active.load() || target->faulted || FAILED(state->device->GetDeviceRemovedReason()))
    {
        error = "Recording queue is unavailable.";
        return false;
    }
    auto result = std::make_shared<DX12ServiceBatch>();
    result->state = state;
    result->identity = queue->GetIdentity();
    result->recording = state->nextRecording++;
    result->token = std::move(lifetimeToken);
    for (auto* list : lists)
    {
        ComPtr<ID3D12Device> owner;
        if (!list || list->GetType() != target->native->GetDesc().Type ||
            FAILED(list->GetDevice(IID_PPV_ARGS(&owner))) || owner.Get() != state->device.Get())
        {
            error = "Native list belongs to another device or queue kind.";
            return false;
        }
        result->sealedLists.emplace_back(list);
    }
    batch = std::move(result);
    return true;
}

IRenderDeviceServices& DX12QueueRecorder::DeviceServices() const
{
    return resources_;
}

bool DX12QueueRecorder::Record(const std::shared_ptr<IRHICommandQueue>& queue,
    const std::function<void(RHIEncoder&)>& commands, std::shared_ptr<const void> token,
    std::shared_ptr<IRHIQueueCommandBatch>& batch, std::string& error)
{
    batch.reset();
    error.clear();
    if (!commands)
    {
        error = "Queue recording requires a command callback.";
        return false;
    }
    std::shared_ptr<IRHIQueueRecording> recording;
    if (!BeginRecording(queue, 1, std::move(token), recording, error))
    {
        return false;
    }
    try
    {
        commands(recording->AcquireEncoder(0));
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
        return false;
    }
    catch (...)
    {
        error = "Queue recording callback threw an unknown exception.";
        return false;
    }
    return recording->Finish(batch, error);
}

bool DX12QueueRecorder::BeginRecording(const std::shared_ptr<IRHICommandQueue>& queue,
    uint32_t targetCount, std::shared_ptr<const void> token,
    std::shared_ptr<IRHIQueueRecording>& recording, std::string& error)
{
    recording.reset();
    error.clear();
    const auto target = std::dynamic_pointer_cast<DX12ServiceQueue>(queue);
    if (!target || !token || targetCount == 0 || targetCount > workers_ ||
        target->timeline->state->device.Get() != resources_.GetDevice() ||
        (queue->GetIdentity().kind != RHIQueueKind::Graphics && queue->GetIdentity().kind != RHIQueueKind::Compute))
    {
        error = "Queue encoder requires a matching graphics/compute device and pinned callback storage.";
        return false;
    }
    auto result = std::make_shared<DX12QueueRecording>();
    result->queue = queue;
    result->leases->reserve(targetCount);
    result->encoders.reserve(targetCount);
    for (uint32_t index = 0; index < targetCount; ++index)
    {
        auto lease = std::make_shared<DX12QueueRecordingLease>();
        lease->owner = token;
        lease->pool = target->recordingPool;
        bool reused = false;
        {
            std::lock_guard lock(lease->pool->mutex);
            if (!lease->pool->active)
            {
                error = "Queue recording pool is unavailable.";
                return false;
            }
            if (!lease->pool->free.empty())
            {
                lease->storage = std::move(lease->pool->free.back());
                lease->pool->free.pop_back();
                reused = true;
                ++lease->pool->reused;
            }
            else
            {
                lease->storage = std::make_unique<DX12QueueRecordingStorage>();
                ++lease->pool->created;
            }
            ++lease->pool->leased;
        }
        auto& storage = lease->storage;
        const auto type = target->native->GetDesc().Type;
        auto* device = resources_.GetDevice();
        const bool ready = reused
            ? SUCCEEDED(storage->allocator->Reset()) && SUCCEEDED(storage->list->Reset(storage->allocator.Get(), nullptr))
            : SUCCEEDED(device->CreateCommandAllocator(type, IID_PPV_ARGS(&storage->allocator))) &&
                SUCCEEDED(device->CreateCommandList(0, type, storage->allocator.Get(), nullptr, IID_PPV_ARGS(&storage->list)));
        if (!ready)
        {
            error = "Queue allocator/list creation failed.";
            return false;
        }
        result->encoders.push_back(std::make_unique<DX12Encoder>(storage->list.Get(), &resources_));
        result->leases->push_back(std::move(lease));
    }
    recording = std::move(result);
    return true;
}

bool DX12QueueRecorder::ExecuteSubmission(const std::function<bool(std::string&)>& submit,
    std::string& error)
{
    if (GetRHISubmissionThread().IsCurrentThread())
    {
        return submit(error);
    }
    // One CPU ticket covers the whole already-recorded queue plan. Native waits
    // and signals then run inline on the RHI owner, preserving issued-point and
    // unfenced-failure rules without a render-thread rendezvous per operation.
    bool accepted = false;
    std::string submissionError;
    const bool dispatched = GetRHISubmissionThread().ExecuteAndWait(&resources_, "RG8 queue plan", [&](std::string&)
    {
        try
        {
            accepted = submit(submissionError);
        }
        catch (const std::exception& exception)
        {
            submissionError = exception.what();
        }
        catch (...)
        {
            submissionError = "Queue submission threw an unknown exception.";
        }
        return true; // Queue failure/quarantine belongs to the service, not the frame owner.
    }, error);
    if (dispatched && !accepted)
    {
        error = std::move(submissionError);
    }
    return dispatched && accepted;
}

DX12QueueService::RecordingPoolStats DX12QueueService::QueryRecordingPool(
    const std::shared_ptr<IRHICommandQueue>& queue)
{
    const auto target = std::dynamic_pointer_cast<DX12ServiceQueue>(queue);
    if (!target)
    {
        return {};
    }
    const auto& pool = target->recordingPool;
    std::lock_guard lock(pool->mutex);
    return {pool->created, pool->reused, pool->leased, pool->free.size()};
}

#if !CE_SHIPPING
bool DX12QueueService::EnqueueTestGate(const std::shared_ptr<IRHICommandQueue>& queue,
    ID3D12Fence* fence, uint64_t value, std::string& error)
{
    if (!GetRHISubmissionThread().IsCurrentThread())
    {
        return DispatchQueueOperation(state_, [&] { return EnqueueTestGate(queue, fence, value, error); }, error);
    }
    std::lock_guard lock(state_->admission);
    const auto target = std::dynamic_pointer_cast<DX12ServiceQueue>(queue);
    if (!state_->active.load() || !target || target->timeline->state != state_ || !fence)
    {
        error = "Invalid test gate target.";
        return false;
    }
    target->testGates.emplace_back(fence);
    return QueueOperation(state_, [&] { return target->native->Wait(fence, value); }, error);
}

void DX12QueueService::RejectNextTestSubmission(bool afterExecute)
{
    std::lock_guard lock(state_->admission);
    state_->testRejection = afterExecute ? 2 : 1;
}
#endif
