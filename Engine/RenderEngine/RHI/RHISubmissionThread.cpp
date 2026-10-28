#include "../../Utility_Framework/Foundation/EngineStorage.h"
#include "../../Utility_Framework/Foundation/EngineNodeMap.h"
#include "../../Containers/ContainerAPI.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../Tools/build/ModuleAdapters/Creator.Core.LifetimeToken.h"
#else
import Creator.Core.LifetimeToken;
#endif
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../Tools/build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif
#include "RHISubmissionThread.h"
#include "../RenderThreadScheduling.h"
#include "../../EngineDiagnostics/ProfileScope.h"

#include "IRHIDeviceResources.h"
#include "RHIParallelCommandPool.h"
#include "RHIRecordedBatch.h"
#include "IRHICommandTranslator.h"
#include "RHIProfilingContext.h"
#include "RHIBackendExecution.h"

#include <cassert>
#include <stdio.h>
#include "../../Utility_Framework/Foundation/StandardLibrary.h"

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
    static_assert(std::atomic<bool>::is_always_lock_free,
        "RHI ticket ready polling requires a lock-free bool atomic");

    struct Preparation
    {
        RHISubmissionThread::Work work;
        RHICommandFailure failure;
        bool started{false};
        bool complete{false};
        bool success{false};
    } preparation;

    mutable std::mutex mutex;
    std::condition_variable wake;
    uint64_t sequence{ 0 };
    std::atomic<bool> ready{ false };
    bool success{ false };
    std::string label;
    std::string error;
    RHICommandFailure commandFailure;
    own::shared_owner<RHIRecordedBatch> recordedBatch;
    RHISubmissionIdentity identity;

    // Only the retained cache's sole owner may reset this storage. Completed
    // tickets held by any caller remain immutable and keep their own result.
    void Reset()
    {
        sequence = 0;
        ready.store(false, std::memory_order_relaxed);
        success = false;
        label.clear();
        error.clear();
        commandFailure.Reset();
        assert(!recordedBatch);
        identity = {};
        preparation.work = {};
        preparation.failure.Reset();
        preparation.started = preparation.complete = preparation.success = false;
    }

    void PublishCompletion(bool completedSuccessfully, std::string completionError)
    {
        {
            // 대기 진입과 같은 mutex로 게시해야 조건 확인 직후의 알림을 잃지 않는다.
            // 성공·실패·취소 모두 이 경로만 쓰므로 게시한 결과는 다시 바뀌지 않는다.
            std::lock_guard lock(mutex);
            if (ready.load(std::memory_order_relaxed))
            {
                return;
            }
            success = completedSuccessfully;
            error = std::move(completionError);
            if (success)
            {
                commandFailure.Reset();
            }
            else
            {
                commandFailure.message = error;
            }
            ready.store(true, std::memory_order_release);
        }
        wake.notify_all();
    }
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
        bool submissionBlocked{ false };
        bool gpuIdleVerified{ false };
        std::string submissionError;
        RHICommandFailure submissionFailure;
        std::string fault;
        uint64_t lastCompletedSequence{0};
    };

    struct Entry
    {
        uint64_t sequence{ 0 };
        uint64_t generation{ 0 };
        const void* owner{ nullptr };
        EntryKind kind{ EntryKind::Submission };
        Work work;
        own::shared_owner<RHISubmissionTicket::State> ticket;
        RHICompletionPoint retirementPoint{};
        CompletionQuery completionQuery;
        ce::lifetime_token lifetimeToken;
        uint64_t softwareBytes{};
        ce::profile_rhi_context rhiContext;
        RHISubmissionTicket::State::Preparation* preparation{nullptr};
    };

    struct Retirement
    {
        uint64_t sequence{ 0 };
        uint64_t generation{ 0 };
        const void* owner{ nullptr };
        EntryKind kind{ EntryKind::Submission };
        RHICompletionPoint point{};
        CompletionQuery completionQuery;
        ce::lifetime_token lifetimeToken;
        bool failedSubmission{ false };
        bool releaseReady{ false };
        ce::profile_rhi_context rhiContext;
    };

    mutable std::mutex mutex;
    std::condition_variable workWake;
    std::condition_variable preparationWake;
    std::condition_variable spaceWake;
    std::condition_variable drainWake;
    // Admission is bounded to three entries. Retain its backing capacity rather
    // than allocating a deque block for each large Entry on every enqueue.
    ce::dynamic_array<Entry> queue;
    static constexpr size_t kRetainedTickets = 64;
    ce::fixed_array<own::shared_owner<RHISubmissionTicket::State>, kRetainedTickets> ticketStorage;
    ce::dynamic_array<Retirement> retirements;
    ce::segmented_deque<ce::pair<const void*, RHICommandFailure>> failures;
    ce::engine_node_map<const void*, OwnerState> owners;
    std::thread thread;
    std::thread preparationThread;
    const void* preparingOwner{nullptr};
    std::thread::id threadId{};
    const void* runningOwner{ nullptr };
    uint64_t runningGeneration{ 0 };
    EntryKind runningKind{ EntryKind::Submission };
    uint32_t clientCount{ 0 };
    uint64_t nextSequence{ 1 };
    bool stopRequested{ false };
    bool abandonRetirements{ false };
    RHISubmissionThreadStats stats{};
    uint64_t softwareByteCapacity{RHISubmissionThread::kSoftwareByteCapacity};

    Impl() { queue.reserve(RHISubmissionThread::kQueueCapacity); }

    own::shared_owner<RHISubmissionTicket::State> AcquireTicketLocked(bool cacheable)
    {
        // Native batch tickets expose an owning payload to callers. Let their
        // last external/worker reference release that payload outside this
        // mutex instead of retaining it in the result cache past device drain.
        if (!cacheable)
        {
            return own::make_shared<RHISubmissionTicket::State>();
        }
        own::shared_owner<RHISubmissionTicket::State>* empty{};
        for (auto& state : ticketStorage)
        {
            if (!state)
            {
                if (!empty)
                {
                    empty = &state;
                }
                continue;
            }
            if (state.use_count() == 1)
            {
                // use_count is a relaxed load in own::shared_owner. Acquire
                // the last released reader before reusing its result storage.
                // State has no exposed weak handles that could acquire it now.
                std::atomic_thread_fence(std::memory_order_acquire);
                state->Reset();
                return state;
            }
        }
        auto state = own::make_shared<RHISubmissionTicket::State>();
        if (empty)
        {
            *empty = state;
        }
        return state;
    }

    size_t ExecutableEntryLocked() const
    {
        ce::fixed_array<const void*, RHISubmissionThread::kQueueCapacity> blocked{};
        size_t blockedCount = 0;
        for (size_t index = 0; index < queue.size(); ++index)
        {
            const Entry& entry = queue[index];
            if (std::find(blocked.begin(), blocked.begin() + blockedCount, entry.owner) !=
                blocked.begin() + blockedCount)
            {
                continue;
            }
            if (!entry.preparation || entry.preparation->complete)
            {
                return index;
            }
            blocked[blockedCount++] = entry.owner;
        }
        return queue.size();
    }

    bool HasPreparationLocked() const
    {
        return std::any_of(queue.begin(), queue.end(),
                           [](const Entry& entry) { return entry.preparation && !entry.preparation->started; });
    }

    void RunPreparations()
    {
        const ce::render::scheduling_scope scheduling{ce::render::thread_role::translation};
        ce::profiler().register_thread("[RHITranslation]", ce::track_kind::command_thread, 2);
        for (;;)
        {
            own::shared_owner<RHISubmissionTicket::State> preparingTicket;
            RHISubmissionTicket::State::Preparation* preparation{};
            ce::profile_rhi_context context;
            {
                std::unique_lock lock(mutex);
                preparationWake.wait(lock, [this] { return stopRequested || HasPreparationLocked(); });
                const auto found = std::find_if(queue.begin(), queue.end(), [](const Entry& entry)
                                                { return entry.preparation && !entry.preparation->started; });
                if (found == queue.end())
                {
                    if (stopRequested)
                    {
                        break;
                    }
                    continue;
                }
                preparingTicket = found->ticket;
                preparation = found->preparation;
                preparation->started = true;
                preparingOwner = found->owner;
                context = found->rhiContext;
            }
            std::string error;
            {
                const ce::profile_rhi_context_scope profile(context);
                RHIBackendExecutionScope backend;
                const RHICommandFailureScope diagnostic(preparation->failure);
                try
                {
                    preparation->success = preparation->work(error);
                }
                catch (const std::exception& exception)
                {
                    error = exception.what();
                }
                catch (...)
                {
                    error = "Unknown asynchronous RHI translation failure";
                }
                if (!preparation->success)
                {
                    preparation->failure.message = error.empty() ? "Asynchronous RHI translation failed" : error;
                }
            }
            // No software-command capture survives the credit release. The
            // separate native batch/retirement owners retain all GPU resources.
            preparation->work = {};
            {
                std::lock_guard lock(mutex);
                preparation->complete = true;
                preparingOwner = nullptr;
                ++stats.completedTranslations;
                stats.failedTranslations += preparation->success ? 0u : 1u;
                for (Entry& entry : queue)
                {
                    if (entry.preparation == preparation)
                    {
                        stats.pendingSoftwareBytes -= entry.softwareBytes;
                        entry.softwareBytes = 0;
                        break;
                    }
                }
            }
            spaceWake.notify_all();
            workWake.notify_all();
            drainWake.notify_all();
        }
        ce::profiler().unregister_thread();
    }

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
            result.submissionBlocked = state->second.submissionBlocked;
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
            if (retirement.owner != owner || retirement.releaseReady)
            {
                continue;
            }
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
                return retirement.owner == owner && !retirement.releaseReady;
            });
    }

    bool HasPendingRetirementsLocked() const
    {
        return std::any_of(retirements.begin(), retirements.end(),
            [](const Retirement& retirement) { return !retirement.releaseReady; });
    }

    void PollRetirementsLocked()
    {
        // raw backend를 캡처한 조회의 수명을 owner 해체와 직렬화하므로 이 잠금을 유지한다.
        for (Retirement& retirement : retirements)
        {
            if (retirement.releaseReady)
            {
                continue;
            }
            // A failed native submission does not prove which commands executed.
            // Its reservation becoming observable cannot reopen the cache by itself.
            // Lifecycle GPU-idle/device-loss handling explicitly releases this token.
            if (retirement.failedSubmission)
            {
                continue;
            }
            const uint64_t completed = retirement.completionQuery ? retirement.completionQuery() : 0;
            if (completed < retirement.point.value)
            {
                continue;
            }
            retirement.releaseReady = true;
            ++stats.retired;
            drainWake.notify_all();
        }
    }

    void Run()
    {
        const ce::render::scheduling_scope scheduling{ce::render::thread_role::submission};
        if (scheduling.error())
        {
            std::fprintf(stderr, "[RHI scheduling] Games registration failed error=%u\n", scheduling.error());
        }
        ce::profiler().register_thread("[RHIThread]", ce::track_kind::command_thread, 1);
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
                const auto canExecuteOrStop = [this]
                {
                    return ExecutableEntryLocked() != queue.size() ||
                           (stopRequested && queue.empty() && (!HasPendingRetirementsLocked() || abandonRetirements));
                };
                if (!HasPendingRetirementsLocked())
                {
                    workWake.wait(lock, canExecuteOrStop);
                }
                else
                {
                    // 미완료 GPU retirement 자체는 깨어날 이유가 아니다. 새 제출은
                    // 즉시 처리하되 완료 조회는 기한까지 쉬어 빈 큐의 busy-poll을 막는다.
                    // 정상 종료도 retirement가 남으면 같은 기한으로 drain을 진행한다.
                    workWake.wait_for(lock, std::chrono::milliseconds(1), canExecuteOrStop);
                }
                PollRetirementsLocked();
                const size_t executable = ExecutableEntryLocked();
                if (executable != queue.size())
                {
                    const auto position = queue.begin() + executable;
                    entry = std::move(*position);
                    queue.erase(position);
                    runningOwner = entry.owner;
                    runningGeneration = entry.generation;
                    runningKind = entry.kind;
                    hasEntry = true;
                    spaceWake.notify_one();
                }
                else if (stopRequested && queue.empty() && (!HasPendingRetirementsLocked() || abandonRetirements))
                {
                    break;
                }
            }

            if (!hasEntry)
            {
                continue;
            }

            std::string error;
            bool success = false;
            RHICommandFailure& commandFailure = entry.ticket->commandFailure;
            commandFailure.stage = EntryKind::Lifecycle == entry.kind ? RHICommandFailureStage::Retirement
                                                                      : RHICommandFailureStage::Submission;
            const RHICommandFailureScope diagnostic(commandFailure);
            const ce::profile_rhi_context_scope rhiContext(entry.rhiContext);
            try
            {
                ce::profile_rhi_scope execute{ce::marker<"RHIExecuteWork">()};
                if (entry.preparation && !entry.preparation->success)
                {
                    commandFailure = entry.preparation->failure;
                    error = commandFailure.message;
                }
                else
                {
                    success = entry.work && entry.work(error);
                }
            }
            catch (const std::exception& exception)
            {
                error = std::string("RHI submission exception: ") + exception.what();
            }
            catch (...)
            {
                error = "RHI submission unknown exception";
            }
            if (!success && error.empty())
            {
                error = entry.ticket->label + " 실패";
            }
            if (!success)
            {
                commandFailure.message = error;
            }
            // CPU command arenas are no longer needed after translation. Release
            // the work captures before returning their admission credits; native
            // resources remain pinned by the separate GPU retirement token.
            entry.work = {};
            {
                std::lock_guard lock(mutex);
                stats.pendingSoftwareBytes -= entry.softwareBytes;
                if (entry.softwareBytes)
                {
                    spaceWake.notify_all();
                }
                ++stats.executed;
                if (!success)
                {
                    ++stats.failed;
                    failures.emplace_back(entry.owner, commandFailure);
                    OwnerState& owner = owners[entry.owner];
                    owner.submissionBlocked = true;
                    owner.gpuIdleVerified = false;
                    owner.submissionError = error;
                    owner.submissionFailure = commandFailure;
                    owner.accepting = false;
                }
                if (entry.retirementPoint.IsValid() &&
                    entry.completionQuery && (entry.lifetimeToken ||
                        EntryKind::RecordedBatch == entry.kind))
                {
                    retirements.push_back(Retirement{entry.sequence, entry.generation, entry.owner, entry.kind,
                                                     entry.retirementPoint, std::move(entry.completionQuery),
                                                     std::move(entry.lifetimeToken), !success, false,
                                                     entry.rhiContext});
                }
                OwnerState& completedOwner = owners[entry.owner];
                if (entry.sequence <= completedOwner.lastCompletedSequence)
                {
                    ++stats.orderingErrors;
                }
                completedOwner.lastCompletedSequence = entry.sequence;
                stats.lastCompletedSequence = (std::max)(stats.lastCompletedSequence, entry.sequence);
                runningOwner = nullptr;
                runningGeneration = 0;
                runningKind = EntryKind::Submission;
                PollRetirementsLocked();
                drainWake.notify_all();
            }
            // Acquire of a completed ticket must also observe quarantine and the
            // registered GPU lifetime token, not an intermediate bookkeeping state.
            entry.ticket->PublishCompletion(success, error);
        }

        ce::profiler().unregister_thread();
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

RHISubmissionIdentity RHISubmissionTicket::GetIdentity() const
{
    return m_state ? m_state->identity : RHISubmissionIdentity{};
}

bool RHISubmissionTicket::IsComplete() const
{
    return m_state && m_state->ready.load(std::memory_order_acquire);
}

bool RHISubmissionTicket::GetCommandFailure(RHICommandFailure& outFailure) const
{
    outFailure.Reset();
    if (!m_state || !m_state->ready.load(std::memory_order_acquire) || m_state->success)
    {
        return false;
    }
    outFailure = m_state->commandFailure;
    return true;
}

const RHIRecordedBatch* RHISubmissionTicket::GetRecordedBatch() const
{
    return m_state ? m_state->recordedBatch.operator->() : nullptr;
}

RHIRecordedBatch* RHISubmissionTicket::GetRecordedBatch()
{
    return m_state ? m_state->recordedBatch.operator->() : nullptr;
}

RHISubmissionThread::RHISubmissionThread(uint64_t softwareByteCapacity) : m_impl(own::make_unique<Impl>())
{
    m_impl->softwareByteCapacity = (std::max)(uint64_t{1}, softwareByteCapacity);
}

RHISubmissionThread::~RHISubmissionThread()
{
    Impl& impl = *m_impl;
    ce::dynamic_array<Impl::Retirement> completed;
    {
        std::unique_lock lock(impl.mutex);
        impl.stats.accepting = false;
        impl.stopRequested = true;
        impl.workWake.notify_all();
        impl.preparationWake.notify_all();
        impl.spaceWake.notify_all();
        // 정상 진행 중인 작업은 끝낸다. 완료되지 않은 자원을 조용히 폐기하지 않는다.
        impl.drainWake.wait(lock,
                            [&impl]
                            {
                                if (!impl.queue.empty() || impl.runningOwner != nullptr ||
                                    impl.preparingOwner != nullptr)
                                {
                                    return false;
                                }
                                bool uncertain = false;
                                for (Impl::Retirement& retirement : impl.retirements)
                                {
                                    if (retirement.releaseReady)
                                    {
                                        continue;
                                    }
                                    const auto owner = impl.owners.find(retirement.owner);
                                    if (owner != impl.owners.end())
                                    {
                                        if (owner->second.faulted || owner->second.gpuIdleVerified)
                                        {
                                            retirement.releaseReady = true;
                                        }
                                        else
                                        {
                                            uncertain = uncertain || owner->second.submissionBlocked;
                                        }
                                    }
                                }
                                return uncertain || !impl.HasPendingRetirementsLocked();
                            });
        if (impl.HasPendingRetirementsLocked())
        {
            std::fputs("Fatal RHI teardown invariant: forced service destruction retains uncertain accepted "
                "native work without verified GPU idle/device loss or an owner-retention path\n", stderr);
            std::fflush(stderr);
            std::terminate();
        }
        completed.swap(impl.retirements);
    }
    impl.workWake.notify_all();
    impl.preparationWake.notify_all();
    impl.spaceWake.notify_all();
    impl.drainWake.notify_all();
    if (impl.thread.joinable())
    {
        impl.thread.join();
    }
    if (impl.preparationThread.joinable())
    {
        impl.preparationThread.join();
    }
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
            impl.preparationThread = std::thread([&impl] { impl.RunPreparations(); });
            impl.thread = std::thread([&impl] { impl.Run(); });
        }
        catch (const std::exception& exception)
        {
            outError = std::string("RHI thread 생성 실패: ") + exception.what();
            impl.stopRequested = true;
            impl.preparationWake.notify_all();
            lock.unlock();
            if (impl.preparationThread.joinable())
            {
                impl.preparationThread.join();
            }
            return false;
        }
        impl.workWake.wait(lock, [&impl] { return impl.stats.running; });
    }
    ownerState.registered = true;
    ownerState.accepting = true;
    ownerState.transitioning = false;
    ownerState.faulted = false;
    ownerState.fault.clear();
    ownerState.submissionBlocked = false;
    ownerState.gpuIdleVerified = false;
    ownerState.submissionError.clear();
    ownerState.submissionFailure.Reset();
    ownerState.lastCommand = RHILifecycleCommand::None;
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
    CollectCompletedLifetimes(owner);

    Impl& impl = *m_impl;
    bool shouldJoin = false;
    {
        std::lock_guard lock(impl.mutex);
        const auto found = impl.owners.find(owner);
        if (found != impl.owners.end())
        {
            if (found->second.submissionBlocked && !found->second.gpuIdleVerified && !found->second.faulted)
            {
                std::fprintf(stderr, "Fatal RHI invariant: releasing owner %p after uncertain native submission "
                    "without verified GPU idle or device loss: %s\n", owner,
                    found->second.submissionError.c_str());
                std::fflush(stderr);
                std::terminate();
            }
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
        impl.preparationWake.notify_all();
        impl.spaceWake.notify_all();
        impl.thread.join();
        impl.preparationThread.join();
    }
}

bool RHISubmissionThread::Enqueue(const void* owner, const char* label, Work work,
    RHISubmissionTicket& outTicket, std::string& outError,
    RHICompletionPoint retirementPoint, CompletionQuery completionQuery,
    ce::lifetime_token lifetimeToken)
{
    if (!IsCurrentThread())
    {
        CollectCompletedLifetimes(owner);
    }
    return EnqueueInternal(owner, label, std::move(work), outTicket, outError,
        retirementPoint, std::move(completionQuery), std::move(lifetimeToken),
        EntryKind::Submission, false, 0);
}

bool RHISubmissionThread::EnqueueInternal(const void* owner, const char* label, Work work,
                                          RHISubmissionTicket& outTicket, std::string& outError,
                                          RHICompletionPoint retirementPoint, CompletionQuery completionQuery,
                                          ce::lifetime_token lifetimeToken, EntryKind kind, bool allowTransition,
                                          uint64_t expectedGeneration,
                                          own::shared_owner<RHIRecordedBatch> recordedBatch,
                                          RHISubmissionIdentity identity, uint64_t softwareBytes,
                                          uint64_t commandRecordingId, Work preparation)
{
    if (nullptr == owner || !work)
    {
        outError = "RHI submission owner/work가 없다";
        return false;
    }
    if (lifetimeToken && (!retirementPoint.IsValid() || !completionQuery))
    {
        outError = "자원 수명 토큰에는 실제 완료점과 완료 질의가 필요하다";
        return false;
    }

    Impl& impl = *m_impl;
    if (softwareBytes > impl.softwareByteCapacity)
    {
        outError = "Software command arenas exceed the RHI admission byte capacity.";
        return false;
    }
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
    const uint64_t admittedGeneration = ownerState.generation;
    const auto exceedsSoftwareBytes = [&]
    { return softwareBytes > impl.softwareByteCapacity - impl.stats.pendingSoftwareBytes; };
    const auto lacksCapacity = [&] { return impl.queue.size() >= kQueueCapacity || exceedsSoftwareBytes(); };
    const auto ownerAccepts = [&]
    {
        return ownerState.registered && ownerState.generation == admittedGeneration &&
               (allowTransition || (ownerState.accepting && !ownerState.faulted && !ownerState.transitioning));
    };
    if (lacksCapacity() && impl.stats.accepting && ownerAccepts())
    {
        ce::profile_rhi_scope admissionWait{softwareBytes ? ce::marker<"RHISoftwareAdmissionWait">()
                                                          : ce::marker<"RHIAdmissionWait">()};
        ++impl.stats.saturationWaits;
        impl.stats.softwareSaturationWaits += exceedsSoftwareBytes() ? 1u : 0u;
        const auto waitStarted = std::chrono::steady_clock::now();
        while (lacksCapacity() && impl.stats.accepting && ownerAccepts())
        {
            impl.spaceWake.wait(lock);
        }
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
    // 용량 대기는 mutex를 놓는다. 그 사이 닫히거나 교체된 owner에 오래된 작업을
    // 다시 넣으면 취소·drain이 끝난 뒤에도 backend를 호출할 수 있어 재검증한다.
    if (!ownerState.registered || ownerState.generation != admittedGeneration ||
        ((!ownerState.accepting || ownerState.faulted || ownerState.transitioning) && !allowTransition))
    {
        outError = ownerState.faulted && !ownerState.fault.empty()
            ? ownerState.fault
            : "RHI submission 용량 대기 중 owner의 수락 상태/generation이 바뀌었다";
        return false;
    }

    auto state = impl.AcquireTicketLocked(!recordedBatch);
    state->label = label ? label : "RHI submission";
    auto* prepared = preparation ? &state->preparation : nullptr;
    if (prepared)
    {
        prepared->work = std::move(preparation);
    }
    const uint64_t sequence = impl.nextSequence;
    state->sequence = sequence;
    // worker가 즉시 완료해도 release 게시 뒤 ticket의 결과 포인터를 바꾸지 않는다.
    state->identity = recordedBatch
        ? RHISubmissionIdentity{recordedBatch->GetFrameId(), recordedBatch->GetRecordingId(),
                                recordedBatch->GetCompletionPoint()}
        : identity;
    state->recordedBatch = std::move(recordedBatch);
    state->commandFailure.stage = RHICommandFailureStage::Admission;
    state->commandFailure.recordingId = commandRecordingId ? commandRecordingId : state->identity.recordingId;
    state->commandFailure.backendGeneration = ownerState.generation;
    if (prepared)
    {
        prepared->failure = state->commandFailure;
        prepared->failure.stage = RHICommandFailureStage::Translation;
    }
    impl.queue.push_back(Impl::Entry{sequence, ownerState.generation, owner, kind, std::move(work), state,
                                     retirementPoint, std::move(completionQuery), std::move(lifetimeToken),
                                     softwareBytes, ce::current_rhi_context, prepared});
    impl.stats.pendingSoftwareBytes += softwareBytes;
    impl.stats.peakSoftwareBytes = (std::max)(impl.stats.peakSoftwareBytes, impl.stats.pendingSoftwareBytes);
    ++impl.nextSequence;
    ownerState.gpuIdleVerified = false;
    if (state->recordedBatch)
    {
        // 이 잠금을 놓기 전에는 worker가 꺼낼 수 없다. admission의 유일한 경계다.
        state->recordedBatch->m_admitted = true;
    }
    ++impl.stats.enqueued;
    impl.stats.maxQueueDepth = (std::max)(impl.stats.maxQueueDepth,
        static_cast<uint32_t>(impl.queue.size()));
    outTicket.m_state = std::move(state);
    lock.unlock();
    impl.workWake.notify_one();
    impl.preparationWake.notify_one();
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
    ce::profile_scope profile{ce::marker<"RHITicketWait">()};
    const auto state = ticket.m_state;
    if (!state)
    {
        outError = "유효하지 않은 RHI submission ticket";
        return false;
    }
    if (!state->ready.load(std::memory_order_acquire))
    {
        std::unique_lock lock(state->mutex);
        state->wake.wait(lock, [&state]
        {
            return state->ready.load(std::memory_order_acquire);
        });
    }
    // acquire로 완료를 본 뒤 결과는 불변이며, 지역 shared_ptr가 읽는 동안 수명을 잡는다.
    if (!state->success)
    {
        outError = state->error;
    }
    return state->success;
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
    CollectCompletedLifetimes(owner);
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
        const auto found = impl.owners.find(owner);
        const bool blocked = found != impl.owners.end() && found->second.submissionBlocked;
        return !impl.HasSubmissionLocked(owner) &&
            (blocked || !impl.HasRetirementLocked(owner));
    });
    lock.unlock();
    CollectCompletedLifetimes(owner);
    return !ConsumeFailure(owner, outError);
}

void RHISubmissionThread::CollectCompletedLifetimes(const void* owner)
{
    if (IsCurrentThread())
    {
        throw std::logic_error("RHI lifetime collection requires the producer thread");
    }
    for (;;)
    {
        Impl::Retirement completed;
        {
            std::lock_guard lock(m_impl->mutex);
            const auto found = std::find_if(m_impl->retirements.begin(), m_impl->retirements.end(),
                [owner](const Impl::Retirement& retirement)
                {
                    return retirement.owner == owner && retirement.releaseReady;
                });
            if (found == m_impl->retirements.end())
            {
                return;
            }
            completed = std::move(*found);
            m_impl->retirements.erase(found);
        }
        // 추가 할당 없이 한 토큰씩 잠금 밖 producer에서 파괴한다.
        const ce::profile_rhi_context_scope rhiContext(completed.rhiContext);
        ce::profile_rhi_scope retire{ce::marker<"RHIBatchRetire">()};
        completed.lifetimeToken.reset();
    }
}

bool RHISubmissionThread::ConsumeFailure(const void* owner, std::string& outError)
{
    Impl& impl = *m_impl;
    std::lock_guard lock(impl.mutex);
    for (auto it = impl.failures.begin(); it != impl.failures.end(); ++it)
    {
        if (it->first != owner) continue;
        outError = std::move(it->second.message);
        impl.failures.erase(it);
        return true;
    }
    const auto found = impl.owners.find(owner);
    if (found != impl.owners.end() && found->second.submissionBlocked)
    {
        outError = found->second.submissionError;
        return true;
    }
    return false;
}

bool RHISubmissionThread::ConsumeFailure(const void* owner, RHICommandFailure& outFailure)
{
    outFailure.Reset();
    Impl& impl = *m_impl;
    std::lock_guard lock(impl.mutex);
    for (auto it = impl.failures.begin(); it != impl.failures.end(); ++it)
    {
        if (it->first != owner)
        {
            continue;
        }
        outFailure = std::move(it->second);
        impl.failures.erase(it);
        return true;
    }
    const auto found = impl.owners.find(owner);
    if (found != impl.owners.end() && found->second.submissionBlocked)
    {
        outFailure = found->second.submissionFailure;
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
        state.submissionBlocked = true;
        state.submissionError = outError;
        if (!ticket.GetCommandFailure(state.submissionFailure))
        {
            state.submissionFailure.stage = RHICommandFailureStage::Retirement;
            state.submissionFailure.backendGeneration = state.generation;
            state.submissionFailure.message = outError;
        }
        state.accepting = false;
        ++impl.stats.lifecycleFailures;
        return false;
    }

    std::unique_lock lock(impl.mutex);
    ++impl.stats.drainCount;
    // native GPU-idle가 성공했다. 실패한 제출의 C가 영원히 signal되지 않아도
    // 이 명시적 경계에서는 토큰을 놓을 수 있다. 완료 fence를 꾸미지는 않는다.
    for (Impl::Retirement& retirement : impl.retirements)
    {
        if (retirement.owner == owner)
        {
            retirement.releaseReady = true;
        }
    }
    impl.workWake.notify_all();
    impl.drainWake.wait(lock, [&impl, owner]
    {
        return !impl.HasSubmissionLocked(owner) &&
            !impl.HasRetirementLocked(owner);
    });

    impl.failures.erase(std::remove_if(impl.failures.begin(), impl.failures.end(),
        [owner](const auto& failure) { return failure.first == owner; }), impl.failures.end());

    Impl::OwnerState& state = impl.owners[owner];
    state.gpuIdleVerified = true;
    ++state.generation;
    ++state.lifecycleCommands;
    ++state.drainCommands;
    state.lastCommand = command;
    state.transitioning = false;
    state.accepting = !state.submissionBlocked && RHILifecycleCommand::BackendShutdown != command;
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
        state.submissionBlocked = true;
        state.submissionError = outError;
        state.submissionFailure.Reset();
        state.submissionFailure.stage = RHICommandFailureStage::Retirement;
        state.submissionFailure.backendGeneration = state.generation;
        state.submissionFailure.message = outError;
        state.accepting = false;
        ++impl.stats.lifecycleFailures;
        return false;
    }
    lock.unlock();
    CollectCompletedLifetimes(owner);
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
    ce::dynamic_array<own::shared_owner<RHISubmissionTicket::State>> cancelled;
    ce::dynamic_array<Impl::Entry> cancelledEntries;
    uint64_t cancelledSoftwareBytes{};
    std::unique_lock lock(impl.mutex);
    const auto found = impl.owners.find(owner);
    if (found == impl.owners.end() || !found->second.registered)
    {
        outError = "device-error owner가 등록돼 있지 않다";
        return false;
    }
    if (!found->second.faulted)
    {
        outError = "native device-loss 확인 없이 제출 수명을 폐기할 수 없다";
        return false;
    }
    cancelled.reserve(impl.queue.size());
    cancelledEntries.reserve(impl.queue.size());
    Impl::OwnerState& state = found->second;
    outResult.previousGeneration = state.generation;
    state.accepting = false;
    state.transitioning = true;
    state.faulted = true;

    for (auto it = impl.queue.begin(); it != impl.queue.end();)
    {
        if (it->owner != owner)
        {
            ++it;
            continue;
        }
        cancelled.push_back(it->ticket);
        cancelledSoftwareBytes += it->softwareBytes;
        cancelledEntries.push_back(std::move(*it));
        it = impl.queue.erase(it);
    }
    impl.spaceWake.notify_all();
    impl.drainWake.wait(lock, [&impl, owner] { return impl.runningOwner != owner && impl.preparingOwner != owner; });
    for (Impl::Retirement& retirement : impl.retirements)
    {
        if (retirement.owner == owner)
        {
            retirement.releaseReady = true;
        }
    }
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

    cancelledEntries.clear();
    if (cancelledSoftwareBytes)
    {
        std::lock_guard releaseLock(impl.mutex);
        impl.stats.pendingSoftwareBytes -= cancelledSoftwareBytes;
        impl.spaceWake.notify_all();
    }
    for (const auto& ticketState : cancelled)
    {
        ticketState->preparation.work = {};
        if (ticketState->recordedBatch)
        {
            ticketState->recordedBatch->m_state = RHIRecordedBatchState::SubmissionFailed;
        }
        ticketState->PublishCompletion(false, "device error로 제출이 폐기됐다");
    }
    CollectCompletedLifetimes(owner);
    // 실행할 entry 없이 취소만 끝나도 queue/drain 대기자가 새 상태를 확인해야 한다.
    impl.workWake.notify_all();
    impl.drainWake.notify_all();
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
    result.pendingRetirements = 0;
    for (const Impl::Entry& entry : m_impl->queue)
        if (EntryKind::RecordedBatch == entry.kind) ++result.pendingBatches;
    if (nullptr != m_impl->runningOwner &&
        EntryKind::RecordedBatch == m_impl->runningKind)
        ++result.pendingBatches;
    for (const Impl::Retirement& retirement : m_impl->retirements)
    {
        if (!retirement.releaseReady)
        {
            ++result.pendingRetirements;
            if (EntryKind::RecordedBatch == retirement.kind)
            {
                ++result.pendingBatches;
            }
        }
    }
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

bool RHISubmissionThread::EnqueueRecordedBatch(const void* owner, IRHIDeviceResources& completionSource,
                                               RHIRecordedBatch&& batch, RHISubmissionTicket& outTicket,
                                               std::string& outError, uint64_t commandRecordingId)
{
    IRHIParallelCommandPool* const pool = batch.m_pool;
    if (nullptr == pool)
    {
        outError = "RHIRecordedBatch에 command pool이 없다";
        return false;
    }
    own::shared_owner<RHIRecordedBatch> queuedBatch;
    RHIRecordingAdmissionGuard admission([&]
    {
        pool->RejectPreparedRecordedBatch(queuedBatch ? *queuedBatch : batch);
    });
    CollectCompletedLifetimes(owner);
    const uint64_t generation = GetOwnerGeneration(owner);
    if (0 == generation || batch.GetBackendGeneration() != generation)
    {
        outError = "RHIRecordedBatch backend generation이 owner generation과 다르다";
        return false;
    }
    if (!pool->PrepareRecordedBatchSubmission(batch, outError))
    {
        return false;
    }

    queuedBatch = own::make_shared<RHIRecordedBatch>(std::move(batch));
    const RHICompletionPoint completion = queuedBatch->GetCompletionPoint();
    // 완료 뒤에도 남는 ticket은 식별 정보만 보관한다. 그래프 자원은 retirement가 소유한다.
    auto lifetimeToken = std::move(queuedBatch->m_lifetimeToken);
    if (!EnqueueInternal(
            owner, "recorded batch submit",
            [pool, queuedBatch](std::string& error)
            {
#if !CE_SHIPPING && CE_DX_TIMING_CAPTURE
                const ce::dx_capture::submission_context_scope captureContext(queuedBatch->GetCaptureContext());
#endif
                RHICommandFailureScope::SetStage(RHICommandFailureStage::Submission);
                return pool->SubmitRecordedBatch(*queuedBatch, error);
            },
            outTicket, outError, completion, [&completionSource] { return completionSource.GetCompletedFenceValue(); },
            std::move(lifetimeToken), EntryKind::RecordedBatch, false, generation, queuedBatch, {}, 0,
            commandRecordingId))
    {
        return false;
    }
    admission.Accept();
    pool->AcceptPreparedRecordedBatch(*queuedBatch, outTicket);
    return true;
}

bool RHISubmissionThread::EnqueueSoftwareBatch(const void* owner, IRHIDeviceResources& completionSource,
    own::shared_owner<IRHIParallelCommandPool> pool, own::shared_owner<IRHICommandTranslator> translator,
    RHISoftwareBatch&& batch, RHISubmissionTicket& outTicket, std::string& outError)
{
    if (IsCurrentThread() || !pool || !translator || !batch.IsValid())
    {
        outError = "Software submission requires a producer, sealed batch, frozen pool and translator.";
        return false;
    }
    const uint64_t softwareBytes = batch.GetRecordingStats().allocatedBytes;
    const uint64_t commandRecordingId = batch.GetRecordingId();
    const ce::profile_rhi_context_scope rhiContext(MakeRHIProfileContext(batch.GetDesc(), commandRecordingId));
    if (softwareBytes > m_impl->softwareByteCapacity)
    {
        // Reject before reserving a native completion or advancing upload state.
        outError = "Software command arenas exceed the RHI admission byte capacity.";
        return false;
    }
    CollectCompletedLifetimes(owner);
    const uint64_t generation = GetOwnerGeneration(owner);
    uint64_t recordingId{};
    uint64_t descriptorVersion{};
    if (!generation || generation != batch.GetDesc().backendGeneration ||
        !pool->GetTranslationRecording(recordingId, descriptorVersion) ||
        recordingId != batch.GetDesc().uploadRecordingId || descriptorVersion != batch.GetDesc().descriptorVersion)
    {
        outError = "Software submission resource recording or owner generation does not match.";
        return false;
    }
    RHICompletionPoint completion;
    RHIRecordingAdmissionGuard admission([&]
    {
        if (completion.IsValid())
        {
            pool->RejectPreparedCommands(recordingId, completion);
        }
    });
    if (!pool->PrepareRecordedCommands(pool->GetCurrentFrameSlot(), completion, outError) || !completion.IsValid())
    {
        return false;
    }
    const RHISubmissionIdentity identity{batch.GetDesc().frameId, recordingId, completion};
    struct SoftwareSubmission
    {
        own::shared_owner<IRHIParallelCommandPool> pool;
        own::shared_owner<IRHICommandTranslator> translator;
        ce::lifetime_token resources;
        RHISoftwareBatch software;
        RHITranslatedBatch translated;
        RHISubmissionIdentity identity;
        ce::cpu_span_context cpuContext{};
    };
    // The queue owns this aggregate through preparation, native submit and the
    // proven GPU retirement. Separate shared wrappers and owning std::function
    // captures would allocate several control blocks for the same lifetime.
    auto lifetime = own::make_shared<SoftwareSubmission>();
    lifetime->pool = pool;
    lifetime->translator = translator;
    lifetime->resources = batch.GetDesc().lifetimeToken;
    lifetime->software = std::move(batch);
    lifetime->identity = identity;
#if !CE_SHIPPING
    lifetime->cpuContext = ce::current_cpu_context;
#endif
    auto* const submitted = lifetime.operator->();
    Work preparation = [submitted](std::string& error)
    {
        auto& software = submitted->software;
        // A custom translator may throw before consuming the input. Arena
        // credits must still follow CPU storage, not this GPU lifetime owner.
        struct ReleaseSoftware
        {
            RHISoftwareBatch& batch;
            ~ReleaseSoftware() { batch = {}; }
        } release{software};
        RHICommandFailureScope::SetIdentity(software.GetRecordingId(), software.GetDesc().backendGeneration);
        RHICommandFailureScope::SetStage(RHICommandFailureStage::Translation);
#if !CE_SHIPPING && CE_DX_TIMING_CAPTURE
        const ce::dx_capture::submission_context_scope context(software.GetDesc().captureContext);
#endif
        const ce::profile_context_scope recordingContext(submitted->cpuContext);
        RHICommandFailure failure;
        {
            ce::profile_rhi_scope translate{ce::marker<"RHISoftwareTranslate">()};
            RHICommandTranslationOptions options;
            if (submitted->pool->SupportsParallelTranslation())
            {
                options.mode = RHICommandTranslationMode::Parallel;
                options.maxTranslationJobs =
                    (std::min)(IRHIParallelCommandPool::kMaxWorkers, submitted->pool->GetWorkerCount());
                options.minCommandsPerJob = 256;
            }
            if (!submitted->translator->Translate(std::move(software), options, submitted->translated, failure))
            {
                RHICommandFailureScope::Report(failure);
                error = failure.message;
                return false;
            }
        }
        return true;
    };
    if (!EnqueueInternal(
            owner, "software batch submit",
            [submitted](std::string& error)
            {
                auto& native = submitted->translated.m_native;
                native.m_recordingId = submitted->identity.recordingId;
                native.m_completion = submitted->identity.completion;
                native.m_admitted = true;
#if !CE_SHIPPING && CE_DX_TIMING_CAPTURE
                const ce::dx_capture::submission_context_scope context(native.GetCaptureContext());
#endif
                RHICommandFailureScope::SetStage(RHICommandFailureStage::Submission);
                ce::profile_rhi_scope nativeSubmit{ce::marker<"RHINativeSubmit">()};
                return submitted->pool->SubmitRecordedBatch(native, error);
            },
            outTicket, outError, completion, [&completionSource] { return completionSource.GetCompletedFenceValue(); },
            std::move(lifetime), EntryKind::RecordedBatch, false, generation, {}, identity, softwareBytes,
            commandRecordingId, std::move(preparation)))
    {
        return false;
    }
    admission.Accept();
    pool->AcceptPreparedCommands(completion, outTicket);
    return true;
}

bool RHISubmissionThread::EnqueueTranslatedBatch(const void* owner,
    IRHIDeviceResources& completionSource, RHITranslatedBatch&& batch,
    RHISubmissionTicket& outTicket, std::string& outError)
{
    if (batch.GetKind() != RHITranslatedBatchKind::ParallelPool)
    {
        outError = "Translated submission requires a native parallel-pool batch.";
        return false;
    }
    const ce::profile_rhi_context_scope rhiContext(MakeRHIProfileContext(batch.m_provenance));
    return EnqueueRecordedBatch(owner, completionSource, std::move(batch.m_native), outTicket, outError,
                                batch.m_provenance.recordingId);
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
