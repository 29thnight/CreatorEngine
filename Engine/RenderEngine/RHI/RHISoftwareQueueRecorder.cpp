#include "../../Utility_Framework/LifetimeToken.h"
#include "../../Utility_Framework/Ownership.h"
#include "RHISoftwareQueueRecorder.h"
#include "IRenderDeviceServices.h"
#include "RHITranslationWorkQueue.h"
#include "RHIProfilingContext.h"
#include "../../EngineDiagnostics/ProfileScope.h"

namespace
{
    constexpr uint32_t kMaximumTargets = 8;

    bool valid_translation_options(const RHICommandTranslationOptions& options)
    {
        return options.maxTranslationJobs != 0 && options.maxTranslationJobs <= kMaximumTargets &&
               ((options.mode == RHICommandTranslationMode::Serial && options.maxTranslationJobs == 1) ||
                options.mode == RHICommandTranslationMode::Parallel);
    }

    void accumulate_translation(RHICommandTranslationStats& destination, const RHICommandTranslationStats& source)
    {
        destination.commandCount += source.commandCount;
        destination.nativeComputeDispatches =
            destination.nativeComputeDispatches == UINT64_MAX || source.nativeComputeDispatches == UINT64_MAX
                ? UINT64_MAX
                : destination.nativeComputeDispatches + source.nativeComputeDispatches;
    }
} // namespace

bool RHIQueueCommandTranslator::Translate(RHISoftwareBatch&& softwareBatch, const RHICommandTranslationOptions& options,
                                          RHITranslatedBatch& outBatch, RHICommandFailure& outFailure)
{
    RHISoftwareBatch input(std::move(softwareBatch));
    const auto rhiContext = MakeRHIProfileContext(input.GetDesc(), input.GetRecordingId());
    const ce::profile_rhi_context_scope context(rhiContext);
    ce::profile_rhi_scope profile{ce::marker<"RHICommandTranslate">()};
    outFailure.Reset();
    outFailure.stage = RHICommandFailureStage::Translation;
    outFailure.recordingId = input.GetRecordingId();
    outFailure.backendGeneration = input.GetDesc().backendGeneration;
    const auto workers = m_recorder.GetWorkerCount();
    if (!input.IsValid() || outBatch.IsValid() || !m_queue || !m_queue->GetIdentity().IsValid() ||
        input.GetDesc().queue != m_queue->GetIdentity() || !valid_translation_options(options) ||
        input.GetTargetCount() > (std::max)(1u, (std::min)(workers, kMaximumTargets)))
    {
        outFailure.message = "Owned translation requires a sealed batch, matching queue and valid target policy.";
        return false;
    }
    auto& services = m_recorder.DeviceServices();
    const auto recordingCurrent = [&]
    {
        return input.GetDesc().uploadRecordingId && input.GetDesc().descriptorVersion &&
               input.GetDesc().uploadRecordingId == services.GetCurrentUploadRecordingId() &&
               input.GetDesc().descriptorVersion == services.GetDescriptorVersionToken();
    };
    if (!recordingCurrent())
    {
        outFailure.message = "Owned translation has stale upload or descriptor recording.";
        return false;
    }
    const auto order = std::span(input.m_storage->targetOrder.data(), input.m_storage->targetOrder.size());
    for (const auto target : order)
    {
        if (target >= input.m_storage->activeTargetCount)
        {
            outFailure.message = "Owned translation references an inactive software target.";
            return false;
        }
        if (!workers && input.m_storage->targets[target].activeSegments > 1)
        {
            outFailure.message = "Serial-only native recorder cannot reset logical encoder state between segments.";
            return false;
        }
    }
    struct alignas(64) TargetResult
    {
        RHICommandTranslationStats stats;
        RHICommandFailure failure;
        bool succeeded{};
    };
    std::array<TargetResult, kMaximumTargets> results;
    uint32_t jobs = 1;
    if (options.mode == RHICommandTranslationMode::Parallel && order.size() > 1)
    {
        const auto budget = options.minCommandsPerJob
                                ? input.GetRecordingStats().commandCount / options.minCommandsPerJob
                                : order.size();
        jobs = static_cast<uint32_t>(
            (std::max)(uint64_t{1}, (std::min)({uint64_t(options.maxTranslationJobs), uint64_t(workers),
                                                uint64_t(order.size()), budget})));
    }
    std::array<uint64_t, kMaximumTargets> costs{};
    if (jobs > 1)
    {
        for (size_t index = 0; index < order.size(); ++index)
        {
            const auto& target = input.m_storage->targets[order[index]];
            for (const auto& segment : std::span(target.segments.data(), target.activeSegments))
            {
                costs[index] += (std::min)(segment->GetCommandCount(), UINT64_MAX - costs[index]);
            }
        }
    }
    translation_work_queue<kMaximumTargets> pending(std::span(costs.data(), jobs > 1 ? order.size() : 0));
    RHITranslatedBatch translated;
    translated.m_provenance = MakeRHICommandProvenance(rhiContext);
    translated.m_stats.translationJobCount = jobs;
    ce::pooled_owner<IRHIQueueRecording> native;
    std::string error;
    const auto replayTarget = [&](size_t index, RHIEncoder* serialEncoder)
    {
        auto targetContext = rhiContext;
        targetContext.target_index = order[index];
        const ce::profile_rhi_context_scope context(targetContext);
        ce::profile_rhi_scope targetProfile{ce::marker<"RHICommandTranslate.Target">()};
        auto& result = results[index];
        result.succeeded = true;
        try
        {
            const auto& target = input.m_storage->targets[order[index]];
            for (const auto& segment : std::span(target.segments.data(), target.activeSegments))
            {
                RHICommandTranslationStats stats;
                // Finish on the owned recorder preserves dense target order.
                // Map original software targetOrder to those dense native slots.
                auto& encoder =
                    serialEncoder ? *serialEncoder : native->AcquireTranslationEncoder(static_cast<uint32_t>(index));
                if (!segment->Replay(encoder, stats, result.failure))
                {
                    result.failure.commandIndex += result.stats.commandCount;
                    result.succeeded = false;
                    break;
                }
                accumulate_translation(result.stats, stats);
            }
        }
        catch (const std::exception& exception)
        {
            result.succeeded = false;
            result.failure.message = exception.what();
        }
        catch (...)
        {
            result.succeeded = false;
            result.failure.message = "Unknown owned target translation exception.";
        }
    };
#if !CE_SHIPPING
    const auto cpuContext = ce::current_cpu_context;
#endif
    const auto replay = [&](uint32_t)
    {
#if !CE_SHIPPING
        const ce::profile_context_scope context(cpuContext);
#endif
        const ce::profile_rhi_context_scope contextScope(rhiContext);
        ce::profile_rhi_scope profile{ce::marker<"RHISoftwareTranslate.Targets">()};
        if (jobs == 1)
        {
            for (size_t index = 0; index < order.size(); ++index)
            {
                replayTarget(index, nullptr);
            }
        }
        else
        {
            for (auto index = pending.claim(); index < order.size(); index = pending.claim())
            {
                replayTarget(index, nullptr);
            }
        }
    };
    const auto checkResults = [&]
    {
        for (size_t index = 0; index < order.size(); ++index)
        {
            const auto& result = results[index];
            if (!result.succeeded)
            {
                outFailure.targetIndex = order[index];
                outFailure.commandIndex = result.failure.commandIndex;
                outFailure.commandType = result.failure.commandType;
                outFailure.message = result.failure.message;
                return false;
            }
            accumulate_translation(translated.m_stats, result.stats);
        }
        return true;
    };
    try
    {
        input.m_state = RHISoftwareBatchState::Translating;
        if (!workers)
        {
            if (!m_recorder.Record(
                    m_queue,
                    [&](RHIEncoder& encoder)
                    {
                        if (!order.empty())
                        {
                            replayTarget(0, &encoder);
                        }
                    },
                    input.GetDesc().lifetimeToken, translated.m_owned, error))
            {
                outFailure.message = error;
                return false;
            }
            if (!checkResults())
            {
                return false;
            }
        }
        else
        {
            if (!m_recorder.BeginRecording(m_queue, (std::max)(1u, static_cast<uint32_t>(order.size())),
                                           input.GetDesc().lifetimeToken, native, error) ||
                !native)
            {
                outFailure.message = error.empty() ? "Native recorder returned no owned recording." : error;
                return false;
            }
            if (jobs > 1)
            {
                ce::profile_rhi_scope join{ce::marker<"RHICommandTranslateJoin">()};
                m_recorder.RunParallel([&replay](uint32_t index) { replay(index); }, jobs);
            }
            else
            {
                replay(0);
            }
            if (!checkResults())
            {
                return false;
            }
            if (!native->Finish(translated.m_owned, error))
            {
                outFailure.message = error;
                return false;
            }
        }
        if (!recordingCurrent() || !translated.m_owned ||
            translated.m_owned->GetQueueIdentity() != m_queue->GetIdentity() || !translated.m_owned->GetRecordingId())
        {
            outFailure.message = "Owned translation returned foreign storage or changed recording versions.";
            return false;
        }
        outBatch = std::move(translated);
        outFailure.Reset();
        return true;
    }
    catch (const std::exception& exception)
    {
        outFailure.message = exception.what();
    }
    catch (...)
    {
        outFailure.message = "Unknown owned translation exception.";
    }
    return false;
}

class RHISoftwareQueueCommandBatch final : public IRHIQueueRecording, public IRHIQueueCommandBatch
{
  public:
    enum class State
    {
        Empty,
        Recording,
        Sealed,
        Consumed
    };

    explicit RHISoftwareQueueCommandBatch(const IRHISoftwareCommandValidator& validator) : m_recorder(validator) {}
    RHIQueueIdentity GetQueueIdentity() const override { return m_identity; }
    uint64_t GetRecordingId() const override { return m_recordingId; }
    RHICommandProvenance GetCommandProvenance(uint32_t target) const override
    {
        if (m_state.load(std::memory_order_acquire) != State::Recording || target >= m_targets)
        {
            return {};
        }
        return MakeRHICommandProvenance(MakeRHIProfileContext(m_desc, m_recordingId, target));
    }
    RHIEncoder& AcquireEncoder(uint32_t target) override
    {
        if (m_state.load(std::memory_order_acquire) != State::Recording)
        {
            throw std::logic_error("Software queue recording is not writable.");
        }
        return m_recorder.AcquireEncoder(target);
    }
    bool Finish(ce::pooled_owner<IRHIQueueCommandBatch>& batch, std::string& error) override;
    bool Begin(RHISoftwareQueueState* domain, IRenderDeviceServices& services, const RHIQueueIdentity& identity,
               uint32_t targets, RHISoftwareBatchDesc desc, const RHISoftwareQueueOptions& options, std::string& error);
    void ReleaseCpuStorage() noexcept;
    void Reset() noexcept
    {
        // A pooled object must not retain the expired control block: its deleter
        // owns the pool state until the last weak reference also disappears.
        m_lease.reset();
        ReleaseCpuStorage();
        m_identity = {};
        m_services = nullptr;
        m_domain = nullptr;
        m_targets = 0;
        m_recordingId = 0;
        m_backendGeneration = 0;
        m_state.store(State::Empty, std::memory_order_relaxed);
    }

  private:
    friend class RHISoftwareCommandQueue;
    friend struct RHISoftwareQueueState;
    ce::weak_pooled_owner<RHISoftwareQueueCommandBatch> m_lease;
    RHISoftwareRecorder m_recorder;
    RHISoftwareBatch m_batch;
    RHISoftwareBatchDesc m_desc;
    RHIQueueIdentity m_identity;
    IRenderDeviceServices* m_services{};
    RHISoftwareQueueState* m_domain{};
    uint64_t m_reservedArenaBytes{};
    uint64_t m_recordingId{};
    uint64_t m_backendGeneration{};
    uint32_t m_targets{};
    std::atomic<State> m_state{State::Empty};
};

struct RHISoftwareQueueState : own::enable_owner_from_this<RHISoftwareQueueState>
{
    static constexpr size_t kRetainedRecordings = 64;
    own::shared_owner<IRHIQueueRecorder> m_nativeRecorder;
    own::shared_owner<const IRHISoftwareCommandValidator> m_validator;
    RHISoftwareQueueOptions m_options;
    RHISoftwareBatchDesc m_frameDesc;
    uint64_t m_pendingArenaBytes{};
    std::mutex m_recordingMutex;
    std::recursive_mutex m_submissionMutex;
    std::array<RHISoftwareQueueCommandBatch*, kRetainedRecordings> m_idle{};
    ~RHISoftwareQueueState()
    {
        for (auto* batch : m_idle)
        {
            ce::render_delete<RHISoftwareQueueCommandBatch>{}(batch);
        }
    }

    bool ReserveArenaBytes(uint64_t bytes)
    {
        std::lock_guard lock(m_recordingMutex);
        if (bytes > m_options.maxPendingArenaBytes - m_pendingArenaBytes)
        {
            return false;
        }
        m_pendingArenaBytes += bytes;
        return true;
    }
    void ReturnArenaBytes(uint64_t bytes) noexcept
    {
        std::lock_guard lock(m_recordingMutex);
        m_pendingArenaBytes -= bytes;
    }

    template<class F>
    bool Execute(F&& submit, std::string& error)
    {
        auto* const commandFailure = RHICommandFailureScope::Current();
#if !CE_SHIPPING
        const auto cpuContext = ce::current_cpu_context;
#endif
        // Never hold a mutex while waiting for a different submission owner.
        // Nested queue calls inside that owner's graph transaction reuse its lock.
        const auto dispatch = [&](std::string& failure)
        {
            const RHICommandFailureScope diagnostic(commandFailure ? commandFailure : RHICommandFailureScope::Current(),
                                                    true);
#if !CE_SHIPPING
            const ce::profile_context_scope context(cpuContext);
#endif
            std::lock_guard lock(m_submissionMutex);
            return submit(failure);
        };
        // Keep the ABI std::function capture to one pointer even when profiling
        // adds context. The synchronous backend callback never outlives dispatch.
        return m_nativeRecorder->ExecuteSubmission([&dispatch](std::string& failure) { return dispatch(failure); },
                                                   error);
    }
    ce::pooled_owner<RHISoftwareQueueCommandBatch> Acquire(RHISoftwareBatchDesc& desc)
    {
        RHISoftwareQueueCommandBatch* value{};
        {
            std::lock_guard lock(m_recordingMutex);
            desc = m_frameDesc;
            for (auto& slot : m_idle)
            {
                if (slot)
                {
                    value = std::exchange(slot, nullptr);
                    break;
                }
            }
        }
        if (!value)
        {
            auto* memory = ce::render_allocator<RHISoftwareQueueCommandBatch>{}.allocate(1);
            try
            {
                value = std::construct_at(memory, *m_validator);
            }
            catch (...)
            {
                ce::render_allocator<RHISoftwareQueueCommandBatch>{}.deallocate(memory, 1);
                throw;
            }
        }
        auto lease = ce::pooled_owner<RHISoftwareQueueCommandBatch>(
            value,
            [state = shared_from_this()](RHISoftwareQueueCommandBatch* batch)
            {
                // Last CPU alias releases the graph/packet owners even when the
                // recording was cancelled or never submitted. Idle storage is empty.
                batch->Reset();
                RHISoftwareQueueCommandBatch* returned = batch;
                std::lock_guard lock(state->m_recordingMutex);
                for (auto& slot : state->m_idle)
                {
                    if (!slot)
                    {
                        slot = std::exchange(returned, nullptr);
                        break;
                    }
                }
                ce::render_delete<RHISoftwareQueueCommandBatch>{}(returned);
            },
            ce::render_allocator<std::byte>{});
        lease->m_lease = lease;
        return lease;
    }
};

void RHISoftwareQueueCommandBatch::ReleaseCpuStorage() noexcept
{
    m_recorder.CancelRecording();
    m_batch = {};
    m_desc = {};
    // Credit returns only after all software storage has been released. A
    // consumed batch may still have CPU aliases and an independent GPU owner.
    if (const auto bytes = std::exchange(m_reservedArenaBytes, uint64_t{}))
    {
        m_domain->ReturnArenaBytes(bytes);
    }
}

bool RHISoftwareQueueCommandBatch::Begin(RHISoftwareQueueState* domain, IRenderDeviceServices& services,
                                         const RHIQueueIdentity& identity, uint32_t targets, RHISoftwareBatchDesc desc,
                                         const RHISoftwareQueueOptions& options, std::string& error)
{
    desc.queue = identity;
    desc.uploadRecordingId = services.GetCurrentUploadRecordingId();
    desc.descriptorVersion = services.GetDescriptorVersionToken();
    if (!domain || !desc.backendGeneration || !desc.lifetimeToken || !desc.uploadRecordingId ||
        !desc.descriptorVersion ||
        !m_recorder.BeginRecording({identity, targets, options.maxCommandBytes, options.maxPayloadBytes}, error))
    {
        if (error.empty())
        {
            error = "Software queue recording requires frame identity, pinned owners and current resource versions.";
        }
        return false;
    }
    m_desc = std::move(desc);
    m_backendGeneration = m_desc.backendGeneration;
    m_recordingId = m_recorder.GetRecordingId();
    m_services = &services;
    m_domain = domain;
    m_identity = identity;
    m_targets = targets;
    m_state.store(State::Recording, std::memory_order_release);
    return true;
}

bool RHISoftwareQueueCommandBatch::Finish(ce::pooled_owner<IRHIQueueCommandBatch>& batch, std::string& error)
{
    batch.reset();
    error.clear();
    if (m_state.load(std::memory_order_acquire) != State::Recording ||
        m_services->GetCurrentUploadRecordingId() != m_desc.uploadRecordingId ||
        m_services->GetDescriptorVersionToken() != m_desc.descriptorVersion)
    {
        error = "Software queue recording is sealed or its upload/descriptor version changed.";
        return false;
    }
    std::array<uint32_t, kMaximumTargets> order{};
    size_t count{};
    for (uint32_t target = 0; target < m_targets; ++target)
    {
        if (m_recorder.HasRecorded(target))
        {
            order[count++] = target;
        }
    }
    if (!m_recorder.FinishRecording(std::span(order).first(count), m_desc, m_batch, error))
    {
        return false;
    }
    const auto bytes = m_batch.GetRecordingStats().allocatedBytes;
    if (!m_domain->ReserveArenaBytes(bytes))
    {
        ReleaseCpuStorage();
        m_state.store(State::Consumed, std::memory_order_release);
        error = "Owned software queue exceeds its pending arena byte limit.";
        return false;
    }
    m_reservedArenaBytes = bytes;
    m_recordingId = m_batch.GetRecordingId();
    m_desc = {};
    m_state.store(State::Sealed, std::memory_order_release);
    batch = m_lease.lock();
    return true;
}

RHISoftwareCommandQueue::RHISoftwareCommandQueue(ConstructionKey, own::shared_owner<RHISoftwareQueueState> state,
                                                 own::shared_owner<IRHICommandQueue> nativeQueue)
    : m_state(std::move(state)), m_native(std::move(nativeQueue))
{
}

RHIQueueIdentity RHISoftwareCommandQueue::GetIdentity() const
{
    return m_native->GetIdentity();
}

bool RHISoftwareCommandQueue::Signal(uint64_t value, RHITimelinePoint& point, std::string& error)
{
    point = {};
    error.clear();
    return m_state->Execute(
        [&](std::string& failure)
        {
            if (!m_native->Signal(value, point, failure))
            {
                point = {};
                return false;
            }
            if (!point.IsValid() || point.value != value || point.fence->GetProducer() != GetIdentity())
            {
                point = {};
                failure = "Native queue signal did not publish the requested timeline point.";
                return false;
            }
            return true;
        },
        error);
}

bool RHISoftwareCommandQueue::Wait(const RHITimelinePoint& point, std::string& error)
{
    error.clear();
    return m_state->Execute([&](std::string& failure) { return m_native->Wait(point, failure); }, error);
}

bool RHISoftwareCommandQueue::Submit(const ce::pooled_owner<IRHIQueueCommandBatch>& batch, uint64_t value,
                                     RHITimelinePoint& point, std::string& error)
{
    point = {};
    error.clear();
    const auto software = ce::dynamic_pooled_cast<RHISoftwareQueueCommandBatch>(batch);
    RHICommandFailureScope::SetStage(RHICommandFailureStage::Admission);
    if (software)
    {
        RHICommandFailureScope::SetIdentity(software->m_recordingId, software->m_backendGeneration);
    }
    if (!software || software->m_domain != m_state.operator->() || software->GetQueueIdentity() != GetIdentity() ||
        value == 0 || value == UINT64_MAX)
    {
        error = "Software queue submission requires its own sealed batch and a valid signal value.";
        return false;
    }
    try
    {
        return m_state->Execute(
            [&](std::string& failure)
            {
                RHICommandFailureScope::SetStage(RHICommandFailureStage::Admission);
                RHICommandFailureScope::SetIdentity(software->m_recordingId, software->m_backendGeneration);
                auto expected = RHISoftwareQueueCommandBatch::State::Sealed;
                if (!software->m_state.compare_exchange_strong(expected, RHISoftwareQueueCommandBatch::State::Consumed,
                                                               std::memory_order_acq_rel, std::memory_order_acquire))
                {
                    failure = "Software queue batch has already been consumed.";
                    return false;
                }
                const ce::profile_rhi_context_scope rhiContext(
                    MakeRHIProfileContext(software->m_batch.GetDesc(), software->m_recordingId));
#if !CE_SHIPPING && CE_DX_TIMING_CAPTURE
                const auto captureContext = software->m_batch.GetDesc().captureContext;
                const ce::dx_capture::submission_context_scope capture(captureContext);
#endif
                // No retry can replay a prefix after any translation/submission
                // failure. Native Submit owns retirement, including unfenced failure.
                RHIQueueCommandTranslator translator(*m_state->m_nativeRecorder, m_native);
                RHITranslatedBatch translated;
                RHICommandFailure translationFailure;
                RHICommandFailureScope::SetStage(RHICommandFailureStage::Translation);
                bool succeeded{};
                {
                    struct ReleaseCpuStorage
                    {
                        RHISoftwareQueueCommandBatch& batch;
                        ~ReleaseCpuStorage() { batch.ReleaseCpuStorage(); }
                    } release{*software};
                    succeeded = translator.Translate(std::move(software->m_batch), m_state->m_options.translation,
                                                     translated, translationFailure);
                }
                if (!succeeded)
                {
                    RHICommandFailureScope::Report(translationFailure);
                    failure = translationFailure.message;
                    return false;
                }
                RHICommandFailureScope::SetStage(RHICommandFailureStage::Submission);
                ce::profile_rhi_scope submitProfile{ce::marker<"RHINativeSubmit">()};
                if (!m_native->Submit(translated.m_owned, value, point, failure))
                {
                    point = {};
                    return false;
                }
                if (!point.IsValid() || point.value != value || point.fence->GetProducer() != GetIdentity())
                {
                    RHICommandFailureScope::SetExecutionUncertain(true);
                    point = {};
                    failure = "Native queue accepted a batch without the requested issued timeline point.";
                    return false;
                }
                return true;
            },
            error);
    }
    catch (const std::exception& exception)
    {
        point = {};
        error = exception.what();
    }
    catch (...)
    {
        point = {};
        error = "Unknown software queue submission exception.";
    }
    return false;
}

size_t RHISoftwareCommandQueue::CollectCompleted()
{
    return m_native->CollectCompleted();
}
size_t RHISoftwareCommandQueue::GetPendingBatchCount() const
{
    return m_native->GetPendingBatchCount();
}

RHISoftwareQueueRecorder::RHISoftwareQueueRecorder(own::shared_owner<IRHIQueueRecorder> nativeRecorder,
                                                   own::shared_owner<const IRHISoftwareCommandValidator> validator,
                                                   RHISoftwareQueueOptions options, job_scheduler& scheduler)
    : IRHIQueueRecorder(scheduler), m_state(ce::make_render_shared<RHISoftwareQueueState>())
{
    if (!nativeRecorder || !validator || !options.maxCommandBytes || !options.maxPayloadBytes ||
        !options.maxPendingArenaBytes || !valid_translation_options(options.translation))
    {
        throw std::invalid_argument("Software queue adapter requires backend owners and a valid recording policy.");
    }
    m_state->m_nativeRecorder = std::move(nativeRecorder);
    m_state->m_validator = std::move(validator);
    m_state->m_options = options;
}

RHISoftwareQueueRecorder::~RHISoftwareQueueRecorder() = default;
IRenderDeviceServices& RHISoftwareQueueRecorder::DeviceServices() const
{
    return m_state->m_nativeRecorder->DeviceServices();
}
uint32_t RHISoftwareQueueRecorder::GetWorkerCount() const
{
    return (std::min)(kMaximumTargets, m_state->m_nativeRecorder->GetWorkerCount());
}

void RHISoftwareQueueRecorder::SetFrameDesc(const RHISoftwareBatchDesc& desc)
{
    std::lock_guard lock(m_state->m_recordingMutex);
    m_state->m_frameDesc = desc;
    m_state->m_frameDesc.lifetimeToken.reset();
    m_state->m_frameDesc.queue = {};
}

bool RHISoftwareQueueRecorder::WrapQueue(const own::shared_owner<IRHICommandQueue>& nativeQueue,
                                         own::shared_owner<IRHICommandQueue>& queue, std::string& error)
{
    const auto input = nativeQueue;
    queue.reset();
    error.clear();
    if (!input || !input->GetIdentity().IsValid())
    {
        error = "Software queue wrapper requires an identified native queue.";
        return false;
    }
    if (const auto wrapped = own::dynamic_owner_cast<RHISoftwareCommandQueue>(input))
    {
        if (wrapped->m_state.operator->() != m_state.operator->())
        {
            error = "A software queue belongs to another recording domain.";
            return false;
        }
        queue = wrapped;
        return true;
    }
    queue = own::make_shared<RHISoftwareCommandQueue>(RHISoftwareCommandQueue::ConstructionKey{}, m_state, input);
    return true;
}

bool RHISoftwareQueueRecorder::Record(const own::shared_owner<IRHICommandQueue>& queue,
                                      const std::function<void(RHIEncoder&)>& commands, ce::lifetime_token token,
                                      ce::pooled_owner<IRHIQueueCommandBatch>& batch, std::string& error)
{
    batch.reset();
    error.clear();
    if (!commands)
    {
        error = "Software queue recording requires a callback.";
        return false;
    }
    ce::pooled_owner<IRHIQueueRecording> recording;
    if (!BeginRecording(queue, 1, std::move(token), recording, error))
    {
        return false;
    }
    try
    {
        {
            const ce::profile_rhi_context_scope rhiContext(MakeRHIProfileContext(recording->GetCommandProvenance(0)));
            ce::profile_rhi_scope profile{ce::marker<"RHISoftwareRecord">()};
            commands(recording->AcquireEncoder(0));
        }
        return recording->Finish(batch, error);
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
    }
    catch (...)
    {
        error = "Software queue callback threw an unknown exception.";
    }
    return false;
}

bool RHISoftwareQueueRecorder::BeginRecording(const own::shared_owner<IRHICommandQueue>& queue, uint32_t targetCount,
                                              ce::lifetime_token token, ce::pooled_owner<IRHIQueueRecording>& recording,
                                              std::string& error)
{
    recording.reset();
    error.clear();
    const auto wrapped = own::dynamic_owner_cast<RHISoftwareCommandQueue>(queue);
    if (!wrapped || wrapped->m_state.operator->() != m_state.operator->() || !token || !targetCount ||
        targetCount > (std::max)(1u, GetWorkerCount()))
    {
        error = "Software recording requires this domain's queue, pinned owner and available targets.";
        return false;
    }
    RHISoftwareBatchDesc desc;
    auto result = m_state->Acquire(desc);
    desc.lifetimeToken = std::move(token);
    if (!result->Begin(m_state.operator->(), DeviceServices(), queue->GetIdentity(), targetCount, std::move(desc),
                       m_state->m_options, error))
    {
        return false;
    }
    recording = std::move(result);
    return true;
}

bool RHISoftwareQueueRecorder::ExecuteSubmission(const std::function<bool(std::string&)>& submit, std::string& error)
{
    if (!submit)
    {
        error = "Software queue transaction requires a callback.";
        return false;
    }
    return m_state->Execute(submit, error);
}

RHIQueueCapabilities RHISoftwareQueueFactory::QueryQueueCapabilities() const
{
    return m_native.QueryQueueCapabilities();
}
bool RHISoftwareQueueFactory::CreateQueue(RHIQueueKind kind, own::shared_owner<IRHICommandQueue>& queue,
                                          std::string& error)
{
    queue.reset();
    own::shared_owner<IRHICommandQueue> native;
    return m_native.CreateQueue(kind, native, error) && m_recorder.WrapQueue(native, queue, error);
}
