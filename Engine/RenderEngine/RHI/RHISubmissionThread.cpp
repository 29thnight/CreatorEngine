#include "RHISubmissionThread.h"

#include "IRHIDeviceResources.h"
#include "RHIParallelCommandPool.h"
#include "RHIRecordedBatch.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

const char* ToString(RHILifecycleCommand command)
{
    switch (command)
    {
    case RHILifecycleCommand::None: return "none";
    case RHILifecycleCommand::SceneReplacement: return "scene replacement";
    case RHILifecycleCommand::PipelineRebuild: return "pipeline rebuild";
    case RHILifecycleCommand::DisplaySlotReplacement: return "display slot replacement";
    case RHILifecycleCommand::SwapChainResize: return "swapchain resize";
    case RHILifecycleCommand::BackendShutdown: return "backend shutdown";
    case RHILifecycleCommand::UnrecoverableDeviceError: return "unrecoverable device error";
    case RHILifecycleCommand::OfflineReadbackCapture: return "offline readback/capture";
    }
    return "unknown";
}

bool RequiresGpuDrain(RHILifecycleCommand command)
{
    switch (command)
    {
    case RHILifecycleCommand::SwapChainResize:
    case RHILifecycleCommand::BackendShutdown:
    case RHILifecycleCommand::UnrecoverableDeviceError:
    case RHILifecycleCommand::OfflineReadbackCapture:
        return true;
    default:
        return false;
    }
}

enum class RHISubmissionThread::EntryKind : uint8_t
{
    Submission,
    RecordedBatch,
    Lifecycle
};

struct RHISubmissionTicket::State
{
    mutable std::mutex mutex;
    std::condition_variable wake;
    uint64_t sequence{ 0 };
    bool complete{ false };
    bool success{ false };
    std::string error;
    std::shared_ptr<RHIRecordedBatch> recordedBatch;
};

struct RHISubmissionThread::Impl
{
    struct OwnerState
    {
        uint64_t generation{ 1 };
        uint64_t lifecycleCommands{ 0 };
        uint64_t drainCommands{ 0 };
        RHILifecycleCommand lastCommand{ RHILifecycleCommand::None };
        bool registered{ false };
        bool accepting{ false };
        bool transitioning{ false };
        bool faulted{ false };
        std::string fault;
    };

    struct Entry
    {
        uint64_t sequence{ 0 };
        uint64_t generation{ 0 };
        const void* owner{ nullptr };
        EntryKind kind{ EntryKind::Submission };
        std::string label;
        Work work;
        std::shared_ptr<RHISubmissionTicket::State> ticket;
        RHICompletionPoint retirementPoint{};
        CompletionQuery completionQuery;
        std::shared_ptr<const void> lifetimeToken;
    };

    struct Retirement
    {
        uint64_t sequence{ 0 };
        uint64_t generation{ 0 };
        const void* owner{ nullptr };
        EntryKind kind{ EntryKind::Submission };
        RHICompletionPoint point{};
        CompletionQuery completionQuery;
        std::shared_ptr<const void> lifetimeToken;
    };

    mutable std::mutex mutex;
    std::condition_variable workWake;
    std::condition_variable spaceWake;
    std::condition_variable drainWake;
    std::deque<Entry> queue;
    std::vector<Retirement> retirements;
    std::deque<std::pair<const void*, std::string>> failures;
    std::unordered_map<const void*, OwnerState> owners;
    std::thread thread;
    std::thread::id threadId{};
    const void* runningOwner{ nullptr };
    uint64_t runningGeneration{ 0 };
    EntryKind runningKind{ EntryKind::Submission };
    uint32_t clientCount{ 0 };
    uint64_t nextSequence{ 1 };
    bool stopRequested{ false };
    bool abandonRetirements{ false };
    RHISubmissionThreadStats stats{};

    RHISubmissionOwnerStats GetOwnerStatsLocked(const void* owner) const
    {
        RHISubmissionOwnerStats result{};
        const auto state = owners.find(owner);
        if (state != owners.end())
        {
            result.generation = state->second.generation;
            result.lifecycleCommands = state->second.lifecycleCommands;
            result.drainCommands = state->second.drainCommands;
            result.lastCommand = state->second.lastCommand;
            result.registered = state->second.registered;
            result.accepting = state->second.accepting;
            result.transitioning = state->second.transitioning;
            result.faulted = state->second.faulted;
        }

        const auto countEntry = [&result, owner](const Entry& entry)
        {
            if (entry.owner != owner) return;
            ++result.pendingTasks;
            if (EntryKind::RecordedBatch == entry.kind) ++result.pendingBatches;
        };
        for (const Entry& entry : queue) countEntry(entry);
        if (runningOwner == owner)
        {
            ++result.pendingTasks;
            if (EntryKind::RecordedBatch == runningKind) ++result.pendingBatches;
        }
        for (const Retirement& retirement : retirements)
        {
            if (retirement.owner != owner) continue;
            ++result.pendingRetirements;
            if (EntryKind::RecordedBatch == retirement.kind)
                ++result.pendingBatches;
        }
        return result;
    }

    bool HasSubmissionLocked(const void* owner) const
    {
        if (runningOwner == owner) return true;
        return std::any_of(queue.begin(), queue.end(),
            [owner](const Entry& entry) { return entry.owner == owner; });
    }

    bool HasRetirementLocked(const void* owner) const
    {
        return std::any_of(retirements.begin(), retirements.end(),
            [owner](const Retirement& retirement)
            {
                return retirement.owner == owner;
            });
    }

    void PollRetirementsLocked()
    {
        for (auto it = retirements.begin(); it != retirements.end();)
        {
            const uint64_t completed = it->completionQuery
                ? it->completionQuery() : 0;
            if (completed < it->point.value)
            {
                ++it;
                continue;
            }
            it = retirements.erase(it);
            ++stats.retired;
            drainWake.notify_all();
        }
    }

    void Run()
    {
        {
            std::lock_guard lock(mutex);
            threadId = std::this_thread::get_id();
            stats.threadIdHash = static_cast<uint64_t>(
                std::hash<std::thread::id>{}(threadId));
            stats.running = true;
            stats.accepting = true;
            workWake.notify_all();
        }

        for (;;)
        {
            Entry entry;
            bool hasEntry = false;
            {
                std::unique_lock lock(mutex);
                workWake.wait_for(lock, std::chrono::milliseconds(1), [this]
                {
                    return stopRequested || !queue.empty() || !retirements.empty();
                });
                PollRetirementsLocked();
                if (!queue.empty())
                {
                    entry = std::move(queue.front());
                    queue.pop_front();
                    runningOwner = entry.owner;
                    runningGeneration = entry.generation;
                    runningKind = entry.kind;
                    hasEntry = true;
                    spaceWake.notify_one();
                }
                else if (stopRequested &&
                    (retirements.empty() || abandonRetirements))
                {
                    break;
                }
            }

            if (!hasEntry) continue;

            std::string error;
            bool success = false;
            try
            {
                success = entry.work && entry.work(error);
            }
            catch (const std::exception& exception)
            {
                error = std::string("RHI submission exception: ") + exception.what();
            }
            catch (...)
            {
                error = "RHI submission unknown exception";
            }
            if (!success && error.empty()) error = entry.label + " 실패";

            {
                std::lock_guard ticketLock(entry.ticket->mutex);
                entry.ticket->success = success;
                entry.ticket->error = error;
                entry.ticket->complete = true;
            }
            entry.ticket->wake.notify_all();

            {
                std::lock_guard lock(mutex);
                ++stats.executed;
                if (!success)
                {
                    ++stats.failed;
                    failures.emplace_back(entry.owner, error);
                }
                else if (!abandonRetirements && entry.retirementPoint.IsValid() &&
                    entry.completionQuery && (entry.lifetimeToken ||
                        EntryKind::RecordedBatch == entry.kind))
                {
                    retirements.push_back(Retirement{
                        entry.sequence, entry.generation, entry.owner, entry.kind,
                        entry.retirementPoint,
                        std::move(entry.completionQuery),
                        std::move(entry.lifetimeToken) });
                }
                if (stats.lastCompletedSequence + 1 != entry.sequence)
                    ++stats.orderingErrors;
                stats.lastCompletedSequence = entry.sequence;
                runningOwner = nullptr;
                runningGeneration = 0;
                runningKind = EntryKind::Submission;
                PollRetirementsLocked();
                drainWake.notify_all();
            }
        }

        {
            std::lock_guard lock(mutex);
            stats.running = false;
            stats.accepting = false;
            threadId = {};
            drainWake.notify_all();
        }
    }
};

uint64_t RHISubmissionTicket::GetSequence() const
{
    return m_state ? m_state->sequence : 0;
}

bool RHISubmissionTicket::IsComplete() const
{
    if (!m_state) return false;
    std::lock_guard lock(m_state->mutex);
    return m_state->complete;
}

const RHIRecordedBatch* RHISubmissionTicket::GetRecordedBatch() const
{
    return m_state ? m_state->recordedBatch.get() : nullptr;
}

RHIRecordedBatch* RHISubmissionTicket::GetRecordedBatch()
{
    return m_state ? m_state->recordedBatch.get() : nullptr;
}

RHISubmissionThread::RHISubmissionThread()
    : m_impl(std::make_unique<Impl>())
{
}

RHISubmissionThread::~RHISubmissionThread()
{
    Impl& impl = *m_impl;
    {
        std::lock_guard lock(impl.mutex);
        // 함수-local singleton의 소멸 순서는 이를 늦게 획득한 render singleton보다
        // 앞설 수 있다. 정상 owner는 ReleaseClient에서 drain하지만, CRT 정리의
        // fallback에서 GPU retirement를 계속 poll하면 이미 소멸한 completionSource를
        // 호출하거나 영원히 join하지 못한다. 프로세스 종료 시점에는 새 제출을
        // 금지하고 아직 남은 값 수명만 놓은 뒤 worker를 끝낸다.
        impl.stats.accepting = false;
        impl.stopRequested = true;
        impl.abandonRetirements = true;
        for (Impl::Entry& entry : impl.queue)
        {
            {
                std::lock_guard ticketLock(entry.ticket->mutex);
                entry.ticket->success = false;
                entry.ticket->error = "RHI submission thread 정적 종료";
                entry.ticket->complete = true;
            }
            entry.ticket->wake.notify_all();
        }
        impl.queue.clear();
        impl.retirements.clear();
    }
    impl.workWake.notify_all();
    if (impl.thread.joinable()) impl.thread.join();
}

bool RHISubmissionThread::AcquireClient(const void* owner, std::string& outError)
{
    if (nullptr == owner)
    {
        outError = "RHI submission client owner가 없다";
        return false;
    }
    Impl& impl = *m_impl;
    std::unique_lock lock(impl.mutex);
    Impl::OwnerState& ownerState = impl.owners[owner];
    if (ownerState.registered)
    {
        outError = "RHI submission owner가 이미 등록돼 있다";
        return false;
    }
    if (0 == impl.clientCount)
    {
        impl.stopRequested = false;
        impl.abandonRetirements = false;
        impl.stats.accepting = false;
        try
        {
            impl.thread = std::thread([&impl] { impl.Run(); });
        }
        catch (const std::exception& exception)
        {
            outError = std::string("RHI thread 생성 실패: ") + exception.what();
            return false;
        }
        impl.workWake.wait(lock, [&impl] { return impl.stats.running; });
    }
    ownerState.registered = true;
    ownerState.accepting = true;
    ownerState.transitioning = false;
    ownerState.faulted = false;
    ownerState.fault.clear();
    ++impl.clientCount;
    return true;
}

void RHISubmissionThread::ReleaseClient(const void* owner)
{
    std::string ignored;
    const RHISubmissionOwnerStats before = GetOwnerStats(owner);
    if (before.faulted)
    {
        RHILifecycleResult abandoned{};
        AbandonForDeviceError(owner, abandoned, ignored);
    }
    else
    {
        Drain(owner, ignored);
    }

    Impl& impl = *m_impl;
    bool shouldJoin = false;
    {
        std::lock_guard lock(impl.mutex);
        const auto found = impl.owners.find(owner);
        if (found != impl.owners.end())
        {
            found->second.registered = false;
            found->second.accepting = false;
            found->second.transitioning = false;
        }
        if (0 != impl.clientCount) --impl.clientCount;
        if (0 == impl.clientCount && impl.thread.joinable())
        {
            impl.stats.accepting = false;
            impl.stopRequested = true;
            shouldJoin = true;
        }
    }
    if (shouldJoin)
    {
        impl.workWake.notify_all();
        impl.thread.join();
    }
}

bool RHISubmissionThread::Enqueue(const void* owner, const char* label, Work work,
    RHISubmissionTicket& outTicket, std::string& outError,
    RHICompletionPoint retirementPoint, CompletionQuery completionQuery,
    std::shared_ptr<const void> lifetimeToken)
{
    return EnqueueInternal(owner, label, std::move(work), outTicket, outError,
        retirementPoint, std::move(completionQuery), std::move(lifetimeToken),
        EntryKind::Submission, false, 0);
}

bool RHISubmissionThread::EnqueueInternal(const void* owner, const char* label,
    Work work, RHISubmissionTicket& outTicket, std::string& outError,
    RHICompletionPoint retirementPoint, CompletionQuery completionQuery,
    std::shared_ptr<const void> lifetimeToken, EntryKind kind,
    bool allowTransition, uint64_t expectedGeneration)
{
    if (nullptr == owner || !work)
    {
        outError = "RHI submission owner/work가 없다";
        return false;
    }

    Impl& impl = *m_impl;
    auto state = std::make_shared<RHISubmissionTicket::State>();
    std::unique_lock lock(impl.mutex);
    const auto foundOwner = impl.owners.find(owner);
    if (foundOwner == impl.owners.end() || !foundOwner->second.registered)
    {
        outError = "RHI submission owner가 등록돼 있지 않다";
        return false;
    }
    Impl::OwnerState& ownerState = foundOwner->second;
    if (0 != expectedGeneration && ownerState.generation != expectedGeneration)
    {
        outError = "RHI submission generation이 현재 owner generation과 다르다";
        return false;
    }
    if ((!ownerState.accepting || ownerState.faulted ||
        ownerState.transitioning) && !allowTransition)
    {
        outError = ownerState.faulted && !ownerState.fault.empty()
            ? ownerState.fault
            : "RHI submission owner가 lifecycle 전환 중이라 작업을 받지 않는다";
        return false;
    }
    bool countedSaturation = false;
    std::chrono::steady_clock::time_point waitStarted{};
    while (impl.queue.size() >= kQueueCapacity && impl.stats.accepting)
    {
        if (!countedSaturation)
        {
            ++impl.stats.saturationWaits;
            countedSaturation = true;
            waitStarted = std::chrono::steady_clock::now();
        }
        impl.spaceWake.wait(lock);
    }
    if (countedSaturation)
    {
        const uint64_t waited = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - waitStarted).count());
        impl.stats.producerWaitNanoseconds += waited;
        impl.stats.maxProducerWaitNanoseconds = (std::max)(
            impl.stats.maxProducerWaitNanoseconds, waited);
    }
    if (!impl.stats.accepting)
    {
        outError = "RHI submission thread가 작업을 받지 않는다";
        return false;
    }

    const uint64_t sequence = impl.nextSequence++;
    state->sequence = sequence;
    impl.queue.push_back(Impl::Entry{
        sequence, ownerState.generation, owner, kind,
        label ? label : "RHI submission", std::move(work), state,
        retirementPoint, std::move(completionQuery), std::move(lifetimeToken) });
    ++impl.stats.enqueued;
    impl.stats.maxQueueDepth = (std::max)(impl.stats.maxQueueDepth,
        static_cast<uint32_t>(impl.queue.size()));
    outTicket.m_state = std::move(state);
    lock.unlock();
    impl.workWake.notify_one();
    return true;
}

bool RHISubmissionThread::ExecuteAndWait(const void* owner, const char* label,
    Work work, std::string& outError)
{
    RHISubmissionTicket ticket;
    return Enqueue(owner, label, std::move(work), ticket, outError) &&
        Wait(ticket, outError);
}

bool RHISubmissionThread::Wait(const RHISubmissionTicket& ticket,
    std::string& outError) const
{
    if (!ticket.m_state)
    {
        outError = "유효하지 않은 RHI submission ticket";
        return false;
    }
    std::unique_lock lock(ticket.m_state->mutex);
    ticket.m_state->wake.wait(lock, [&ticket]
    {
        return ticket.m_state->complete;
    });
    if (!ticket.m_state->success) outError = ticket.m_state->error;
    return ticket.m_state->success;
}

bool RHISubmissionThread::DrainSubmissions(const void* owner, std::string& outError)
{
    Impl& impl = *m_impl;
    std::unique_lock lock(impl.mutex);
    ++impl.stats.drainCount;
    impl.drainWake.wait(lock, [&impl, owner]
    {
        return !impl.HasSubmissionLocked(owner);
    });
    lock.unlock();
    return !ConsumeFailure(owner, outError);
}

bool RHISubmissionThread::Drain(const void* owner, std::string& outError)
{
    Impl& impl = *m_impl;
    std::unique_lock lock(impl.mutex);
    ++impl.stats.drainCount;
    impl.workWake.notify_all();
    impl.drainWake.wait(lock, [&impl, owner]
    {
        return !impl.HasSubmissionLocked(owner) &&
            !impl.HasRetirementLocked(owner);
    });
    lock.unlock();
    return !ConsumeFailure(owner, outError);
}

bool RHISubmissionThread::ConsumeFailure(const void* owner, std::string& outError)
{
    Impl& impl = *m_impl;
    std::lock_guard lock(impl.mutex);
    for (auto it = impl.failures.begin(); it != impl.failures.end(); ++it)
    {
        if (it->first != owner) continue;
        outError = std::move(it->second);
        impl.failures.erase(it);
        return true;
    }
    return false;
}

bool RHISubmissionThread::ExecuteLifecycleDrain(const void* owner,
    RHILifecycleCommand command, Work gpuDrain,
    RHILifecycleResult& outResult, std::string& outError)
{
    outResult = {};
    outResult.command = command;
    if (!RequiresGpuDrain(command) ||
        RHILifecycleCommand::UnrecoverableDeviceError == command)
    {
        outError = "GPU drain lifecycle command가 아니다: " +
            std::string(ToString(command));
        return false;
    }
    if (nullptr == owner || !gpuDrain)
    {
        outError = "lifecycle owner/GPU drain 작업이 없다";
        return false;
    }
    if (IsCurrentThread())
    {
        outError = "RHI thread 안에서 lifecycle drain을 기다릴 수 없다";
        return false;
    }

    Impl& impl = *m_impl;
    {
        std::lock_guard lock(impl.mutex);
        const auto found = impl.owners.find(owner);
        if (found == impl.owners.end() || !found->second.registered)
        {
            outError = "lifecycle owner가 등록돼 있지 않다";
            return false;
        }
        Impl::OwnerState& state = found->second;
        if (state.transitioning)
        {
            outError = "owner lifecycle 전환이 이미 진행 중이다";
            return false;
        }
        if (state.faulted)
        {
            outError = state.fault.empty()
                ? "owner가 device error 상태다" : state.fault;
            return false;
        }
        state.transitioning = true;
        state.accepting = false;
        outResult.previousGeneration = state.generation;
    }

    RHISubmissionTicket ticket;
    const std::string label = std::string("RHI lifecycle: ") + ToString(command);
    if (!EnqueueInternal(owner, label.c_str(), std::move(gpuDrain), ticket,
        outError, {}, {}, {}, EntryKind::Lifecycle, true,
        outResult.previousGeneration) ||
        !Wait(ticket, outError))
    {
        std::lock_guard lock(impl.mutex);
        Impl::OwnerState& state = impl.owners[owner];
        state.transitioning = false;
        state.faulted = true;
        state.fault = outError;
        ++impl.stats.lifecycleFailures;
        return false;
    }

    std::unique_lock lock(impl.mutex);
    ++impl.stats.drainCount;
    impl.workWake.notify_all();
    impl.drainWake.wait(lock, [&impl, owner]
    {
        return !impl.HasSubmissionLocked(owner) &&
            !impl.HasRetirementLocked(owner);
    });

    for (auto it = impl.failures.begin(); it != impl.failures.end(); ++it)
    {
        if (it->first != owner) continue;
        outError = std::move(it->second);
        impl.failures.erase(it);
        Impl::OwnerState& state = impl.owners[owner];
        state.transitioning = false;
        state.faulted = true;
        state.fault = outError;
        ++impl.stats.lifecycleFailures;
        return false;
    }

    Impl::OwnerState& state = impl.owners[owner];
    ++state.generation;
    ++state.lifecycleCommands;
    ++state.drainCommands;
    state.lastCommand = command;
    state.transitioning = false;
    state.accepting = RHILifecycleCommand::BackendShutdown != command;
    ++impl.stats.lifecycleCommands;

    const RHISubmissionOwnerStats ownerStats = impl.GetOwnerStatsLocked(owner);
    outResult.generation = ownerStats.generation;
    outResult.pendingTasks = ownerStats.pendingTasks;
    outResult.pendingBatches = ownerStats.pendingBatches;
    outResult.pendingRetirements = ownerStats.pendingRetirements;
    outResult.drained = true;
    if (!outResult.IsClean())
    {
        outError = "lifecycle drain 뒤 owner pending이 0이 아니다";
        state.faulted = true;
        state.fault = outError;
        state.accepting = false;
        ++impl.stats.lifecycleFailures;
        return false;
    }
    return true;
}

bool RHISubmissionThread::AbandonForDeviceError(const void* owner,
    RHILifecycleResult& outResult, std::string& outError)
{
    outResult = {};
    outResult.command = RHILifecycleCommand::UnrecoverableDeviceError;
    if (nullptr == owner || IsCurrentThread())
    {
        outError = "device-error abandon은 owner의 producer thread에서 호출해야 한다";
        return false;
    }

    Impl& impl = *m_impl;
    std::vector<std::shared_ptr<RHISubmissionTicket::State>> cancelled;
    std::unique_lock lock(impl.mutex);
    const auto found = impl.owners.find(owner);
    if (found == impl.owners.end() || !found->second.registered)
    {
        outError = "device-error owner가 등록돼 있지 않다";
        return false;
    }
    Impl::OwnerState& state = found->second;
    outResult.previousGeneration = state.generation;
    state.accepting = false;
    state.transitioning = true;
    state.faulted = true;

    for (auto it = impl.queue.begin(); it != impl.queue.end();)
    {
        if (it->owner != owner) { ++it; continue; }
        cancelled.push_back(it->ticket);
        it = impl.queue.erase(it);
    }
    impl.spaceWake.notify_all();
    impl.drainWake.wait(lock, [&impl, owner]
    {
        return impl.runningOwner != owner;
    });
    impl.retirements.erase(std::remove_if(impl.retirements.begin(),
        impl.retirements.end(), [owner](const Impl::Retirement& retirement)
        {
            return retirement.owner == owner;
        }), impl.retirements.end());
    impl.failures.erase(std::remove_if(impl.failures.begin(), impl.failures.end(),
        [owner](const auto& failure) { return failure.first == owner; }),
        impl.failures.end());

    ++state.generation;
    ++state.lifecycleCommands;
    ++state.drainCommands;
    state.lastCommand = RHILifecycleCommand::UnrecoverableDeviceError;
    state.transitioning = false;
    ++impl.stats.lifecycleCommands;

    const RHISubmissionOwnerStats ownerStats = impl.GetOwnerStatsLocked(owner);
    outResult.generation = ownerStats.generation;
    outResult.pendingTasks = ownerStats.pendingTasks;
    outResult.pendingBatches = ownerStats.pendingBatches;
    outResult.pendingRetirements = ownerStats.pendingRetirements;
    outResult.drained = true;
    lock.unlock();

    for (const auto& ticketState : cancelled)
    {
        {
            std::lock_guard ticketLock(ticketState->mutex);
            ticketState->success = false;
            ticketState->error = "device error로 제출이 폐기됐다";
            ticketState->complete = true;
        }
        ticketState->wake.notify_all();
    }
    if (!outResult.IsClean())
    {
        outError = "device-error abandon 뒤 owner pending이 0이 아니다";
        return false;
    }
    return true;
}

void RHISubmissionThread::MarkUnrecoverableDeviceError(const void* owner,
    const std::string& error)
{
    if (nullptr == owner) return;
    std::lock_guard lock(m_impl->mutex);
    const auto found = m_impl->owners.find(owner);
    if (found == m_impl->owners.end()) return;
    found->second.faulted = true;
    found->second.accepting = false;
    found->second.fault = error.empty()
        ? "복구 불가능한 device error" : error;
}

bool RHISubmissionThread::IsCurrentThread() const
{
    std::lock_guard lock(m_impl->mutex);
    return m_impl->stats.running &&
        std::this_thread::get_id() == m_impl->threadId;
}

RHISubmissionThreadStats RHISubmissionThread::GetStats() const
{
    std::lock_guard lock(m_impl->mutex);
    RHISubmissionThreadStats result = m_impl->stats;
    result.pendingTasks = static_cast<uint32_t>(m_impl->queue.size()) +
        (nullptr != m_impl->runningOwner ? 1u : 0u);
    result.pendingRetirements = static_cast<uint32_t>(m_impl->retirements.size());
    for (const Impl::Entry& entry : m_impl->queue)
        if (EntryKind::RecordedBatch == entry.kind) ++result.pendingBatches;
    if (nullptr != m_impl->runningOwner &&
        EntryKind::RecordedBatch == m_impl->runningKind)
        ++result.pendingBatches;
    for (const Impl::Retirement& retirement : m_impl->retirements)
        if (EntryKind::RecordedBatch == retirement.kind) ++result.pendingBatches;
    return result;
}

RHISubmissionOwnerStats RHISubmissionThread::GetOwnerStats(
    const void* owner) const
{
    std::lock_guard lock(m_impl->mutex);
    return m_impl->GetOwnerStatsLocked(owner);
}

uint64_t RHISubmissionThread::GetOwnerGeneration(const void* owner) const
{
    return GetOwnerStats(owner).generation;
}

bool RHISubmissionThread::EnqueueRecordedBatch(const void* owner,
    IRHIDeviceResources& completionSource, RHIRecordedBatch&& batch,
    RHISubmissionTicket& outTicket, std::string& outError)
{
    IRHIParallelCommandPool* const pool = batch.m_pool;
    if (nullptr == pool)
    {
        outError = "RHIRecordedBatch에 command pool이 없다";
        return false;
    }
    const uint64_t generation = GetOwnerGeneration(owner);
    if (0 == generation || batch.GetBackendGeneration() != generation)
    {
        outError = "RHIRecordedBatch backend generation이 owner generation과 다르다";
        return false;
    }
    if (!pool->PrepareRecordedBatchSubmission(batch, outError)) return false;

    auto queuedBatch = std::make_shared<RHIRecordedBatch>(std::move(batch));
    const RHICompletionPoint completion = queuedBatch->GetCompletionPoint();
    if (!EnqueueInternal(owner, "recorded batch submit",
        [pool, queuedBatch](std::string& error)
        {
            return pool->SubmitRecordedBatch(*queuedBatch, error);
        }, outTicket, outError, completion,
        [&completionSource] { return completionSource.GetCompletedFenceValue(); },
        queuedBatch, EntryKind::RecordedBatch, false, generation))
    {
        return false;
    }
    outTicket.m_state->recordedBatch = std::move(queuedBatch);
    return true;
}

RHISubmissionThread& GetRHISubmissionThread()
{
    // 여러 render singleton이 CRT 정적 소멸에서 DeviceResources::Shutdown을
    // 호출한다. 함수-local 값 singleton이면 이것보다 늦게 만들어졌다는 이유로
    // 먼저 파괴되어, 뒤이어 온 owner drain이 죽은 mutex/thread를 만진다.
    // 프로세스 서비스의 저장소만 의도적으로 process-lifetime으로 두고 실제
    // worker는 client ref-count가 0이 될 때 ReleaseClient가 매번 join한다.
    static RHISubmissionThread* thread = new RHISubmissionThread();
    return *thread;
}

