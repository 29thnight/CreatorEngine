#include "IRHICommandTranslator.h"
#include "IRenderDeviceServices.h"
#include "RHIParallelCommandPool.h"

bool RHICommandTranslator::Translate(RHISoftwareBatch&& softwareBatch, const RHICommandTranslationOptions& options,
                                     RHITranslatedBatch& outBatch, RHICommandFailure& outFailure)
{
    RHISoftwareBatch input(std::move(softwareBatch));
    outFailure.Reset();
    outFailure.stage = RHICommandFailureStage::Translation;
    outFailure.recordingId = input.GetRecordingId();
    outFailure.backendGeneration = input.GetDesc().backendGeneration;
    if (!input.IsValid() || outBatch.IsValid() || !m_pool.IsInitialized() ||
        options.mode != RHICommandTranslationMode::Serial || options.maxTranslationJobs != 1 || !m_queue.IsValid() ||
        input.GetDesc().queue != m_queue || m_queue.kind != RHIQueueKind::Graphics)
    {
        outFailure.message =
            "Serial pool translation requires a sealed graphics batch, empty output and initialized pool.";
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
    for (const auto target : std::span(input.m_targetOrder.data(), input.m_targetOrder.size()))
    {
        if (target >= m_pool.GetWorkerCount())
        {
            outFailure.message = "Software target exceeds native command pool capacity.";
            return false;
        }
    }
    input.m_state = RHISoftwareBatchState::Translating;
    RHITranslatedBatch translated;
    std::string error;
    RHICommandFailure segmentFailure;
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
    try
    {
        for (const auto target : std::span(input.m_targetOrder.data(), input.m_targetOrder.size()))
        {
            outFailure.targetIndex = target;
            nativeOpened = true;
            if (!m_pool.OpenWorker(target, error))
            {
                outFailure.message = error;
                closeFailedRecording();
                return false;
            }
            uint64_t targetCommands{};
            for (const auto& segment :
                 std::span(input.m_targets[target].segments.data(), input.m_targets[target].segments.size()))
            {
                RHICommandTranslationStats segmentStats;
                auto& encoder = m_pool.AcquireEncoder(target);
                if (!segment->Replay(encoder, segmentStats, segmentFailure))
                {
                    outFailure.commandIndex = targetCommands + segmentFailure.commandIndex;
                    outFailure.commandType = segmentFailure.commandType;
                    outFailure.message = segmentFailure.message;
                    closeFailedRecording();
                    return false;
                }
                targetCommands += segmentStats.commandCount;
                translated.m_stats.commandCount += segmentStats.commandCount;
                if (translated.m_stats.nativeComputeDispatches == UINT64_MAX ||
                    segmentStats.nativeComputeDispatches == UINT64_MAX)
                {
                    translated.m_stats.nativeComputeDispatches = UINT64_MAX;
                }
                else
                {
                    translated.m_stats.nativeComputeDispatches += segmentStats.nativeComputeDispatches;
                }
            }
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
        if (!m_pool.FinalizeRecordedBatch(input.m_targetOrder, nativeDesc, translated.m_native, error))
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
