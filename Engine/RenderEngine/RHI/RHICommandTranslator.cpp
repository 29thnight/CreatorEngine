#include "IRHICommandTranslator.h"
#include "IRenderDeviceServices.h"
#include "RHIParallelCommandPool.h"
#include "RHITranslationWorkQueue.h"
#include "../../EngineDiagnostics/ProfileScope.h"

bool RHICommandTranslator::Translate(RHISoftwareBatch&& softwareBatch, const RHICommandTranslationOptions& options,
                                     RHITranslatedBatch& outBatch, RHICommandFailure& outFailure)
{
    RHISoftwareBatch input(std::move(softwareBatch));
    outFailure.Reset();
    outFailure.stage = RHICommandFailureStage::Translation;
    outFailure.recordingId = input.GetRecordingId();
    outFailure.backendGeneration = input.GetDesc().backendGeneration;
    if (!input.IsValid() || outBatch.IsValid() || !m_pool.IsInitialized() ||
        input.GetTargetCount() > IRHIParallelCommandPool::kMaxWorkers ||
        (options.mode != RHICommandTranslationMode::Serial && options.mode != RHICommandTranslationMode::Parallel) ||
        options.maxTranslationJobs == 0 || options.maxTranslationJobs > IRHIParallelCommandPool::kMaxWorkers ||
        (options.mode == RHICommandTranslationMode::Serial && options.maxTranslationJobs != 1) ||
        (options.mode == RHICommandTranslationMode::Parallel && !m_pool.SupportsParallelTranslation()) ||
        !m_queue.IsValid() || input.GetDesc().queue != m_queue || m_queue.kind != RHIQueueKind::Graphics)
    {
        outFailure.message =
            "Pool translation requires a sealed graphics batch, valid job policy and independent parallel targets.";
        return false;
    }
    uint64_t uploadRecordingId{};
    uint64_t descriptorVersion{};
    if (!m_pool.GetTranslationRecording(uploadRecordingId, descriptorVersion))
    {
        uploadRecordingId = m_services.GetCurrentUploadRecordingId();
        descriptorVersion = m_services.GetDescriptorVersionToken();
    }
    if (!input.GetDesc().uploadRecordingId || !input.GetDesc().descriptorVersion ||
        input.GetDesc().uploadRecordingId != uploadRecordingId ||
        input.GetDesc().descriptorVersion != descriptorVersion)
    {
        outFailure.message = "Upload or descriptor recording changed before serial translation.";
        return false;
    }
    for (const auto target : std::span(input.m_storage->targetOrder.data(), input.m_storage->targetOrder.size()))
    {
        if (target >= m_pool.GetWorkerCount())
        {
            outFailure.message = "Software target exceeds native command pool capacity.";
            return false;
        }
    }
    const auto order = std::span(input.m_storage->targetOrder.data(), input.m_storage->targetOrder.size());
    struct alignas(64) TargetResult
    {
        RHICommandTranslationStats stats;
        RHICommandFailure failure;
        bool succeeded{};
    };
    // Retain diagnostic strings on the coordinating thread. Jobs borrow
    // disjoint slots only until RunParallel joins, including on failure.
    static thread_local std::array<TargetResult, IRHIParallelCommandPool::kMaxWorkers> resultStorage;
    auto* const results = resultStorage.data();
    static thread_local bool resultsInUse{};
    if (resultsInUse)
    {
        outFailure.message = "Native translation cannot recursively borrow target results.";
        return false;
    }
    struct ResultLease
    {
        bool& active;
        ~ResultLease() { active = false; }
    } resultLease{resultsInUse};
    resultsInUse = true;
    for (size_t index = 0; index < order.size(); ++index)
    {
        results[index].stats = {};
        results[index].failure.Reset();
        results[index].succeeded = false;
    }
    uint32_t jobs = 1;
    if (options.mode == RHICommandTranslationMode::Parallel && order.size() > 1)
    {
        const auto commandCount = input.GetRecordingStats().commandCount;
        const auto budget = options.minCommandsPerJob ? commandCount / options.minCommandsPerJob : order.size();
        jobs = static_cast<uint32_t>((std::max)(
            uint64_t{1}, (std::min)({uint64_t(options.maxTranslationJobs), uint64_t(order.size()), budget})));
    }
    std::array<uint64_t, IRHIParallelCommandPool::kMaxWorkers> targetCosts{};
    if (jobs > 1)
    {
        for (size_t index = 0; index < order.size(); ++index)
        {
            const auto& target = input.m_storage->targets[order[index]];
            for (const auto& segment : std::span(target.segments.data(), target.activeSegments))
            {
                const auto commands = segment->GetCommandCount();
                targetCosts[index] += (std::min)(commands, UINT64_MAX - targetCosts[index]);
            }
        }
    }
    translation_work_queue<IRHIParallelCommandPool::kMaxWorkers> pendingTargets(
        std::span(targetCosts.data(), jobs > 1 ? order.size() : 0));
    input.m_state = RHISoftwareBatchState::Translating;
    RHITranslatedBatch translated;
    translated.m_stats.translationJobCount = jobs;
    std::string error;
    bool nativeOpened = false;
    const auto closeFailedRecording = [&]
    {
        if (nativeOpened)
        {
            std::string closeError;
            if (!m_pool.CloseAll(closeError))
            {
                outFailure.message += "; native close: " + closeError;
            }
        }
    };
    const auto accumulate = [](RHICommandTranslationStats& destination, const RHICommandTranslationStats& source)
    {
        destination.commandCount += source.commandCount;
        if (destination.nativeComputeDispatches == UINT64_MAX || source.nativeComputeDispatches == UINT64_MAX)
        {
            destination.nativeComputeDispatches = UINT64_MAX;
        }
        else
        {
            destination.nativeComputeDispatches += source.nativeComputeDispatches;
        }
    };
    const auto replayTarget = [&](size_t index)
    {
        const auto target = order[index];
        auto& result = results[index];
        try
        {
            result.succeeded = true;
            for (const auto& segment : std::span(input.m_storage->targets[target].segments.data(),
                                                 input.m_storage->targets[target].activeSegments))
            {
                RHICommandTranslationStats stats;
                auto& encoder = m_pool.AcquireTranslationEncoder(target);
                if (!segment->Replay(encoder, stats, result.failure))
                {
                    result.failure.commandIndex += result.stats.commandCount;
                    result.succeeded = false;
                    break;
                }
                accumulate(result.stats, stats);
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
            result.failure.message = "Unknown native target translation exception.";
        }
    };
#if !CE_SHIPPING
    const auto cpuContext = ce::current_cpu_context;
#endif
    const auto replayTargets = [&](uint32_t)
    {
#if !CE_SHIPPING
        const ce::profile_context_scope context(cpuContext);
#endif
        ce::profile_scope replay{ce::marker<"RHISoftwareTranslate.Targets">()};
        if (jobs == 1)
        {
            for (size_t index = 0; index < order.size(); ++index)
            {
                replayTarget(index);
            }
            return;
        }
        // 짧은 target을 끝낸 작업자가 남은 target을 맡는다. target 내부 segment와
        // 결과 슬롯은 끝까지 같은 작업자가 소유하고 제출·오류 보고는 원래 순서를 따른다.
        for (auto index = pendingTargets.claim(); index < order.size(); index = pendingTargets.claim())
        {
            replayTarget(index);
        }
    };
    try
    {
        // Allocator/list reset and final close stay on the RHI owner. A replay
        // job exclusively owns its target; only the owner combines results.
        for (const auto target : order)
        {
            outFailure.targetIndex = target;
            nativeOpened = true;
            if (!m_pool.OpenWorker(target, error))
            {
                outFailure.message = error;
                closeFailedRecording();
                return false;
            }
        }
        if (jobs > 1)
        {
            m_pool.RunParallel(replayTargets, jobs);
        }
        else
        {
            replayTargets(0);
        }
        for (size_t index = 0; index < order.size(); ++index)
        {
            const auto& result = results[index];
            if (!result.succeeded)
            {
                outFailure.targetIndex = order[index];
                outFailure.commandIndex = result.failure.commandIndex;
                outFailure.commandType = result.failure.commandType;
                outFailure.message = result.failure.message;
                closeFailedRecording();
                return false;
            }
            accumulate(translated.m_stats, result.stats);
        }
        if (m_pool.DrainEncoderDrops(error))
        {
            outFailure.message = "Native translation dropped commands: " + error;
            closeFailedRecording();
            return false;
        }
        RHIRecordedBatchDesc nativeDesc;
        nativeDesc.frameId = input.m_desc.frameId;
        nativeDesc.backendGeneration = input.m_desc.backendGeneration;
        nativeDesc.displayToken = input.m_desc.displayToken;
        nativeDesc.lifetimeToken = input.m_desc.lifetimeToken;
#if !CE_SHIPPING && CE_DX_TIMING_CAPTURE
        nativeDesc.captureContext = input.m_desc.captureContext;
#endif
        if (!m_pool.FinalizeRecordedBatch(order, nativeDesc, translated.m_native, error))
        {
            outFailure.message = error;
            closeFailedRecording();
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
        outFailure.message = "Unknown native translation exception.";
    }
    closeFailedRecording();
    return false;
}
