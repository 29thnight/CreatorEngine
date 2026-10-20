#pragma once

#include "RHISoftwareBatch.h"
#include "RHICommandProvenance.h"
#include "../../EngineDiagnostics/ProfileRhiContext.h"

inline ce::profile_rhi_context MakeRHIProfileContext(const RHICommandProvenance& source)
{
    ce::profile_rhi_context context;
    context.recording_id = source.recordingId;
    context.source_engine_frame = source.sourceEngineFrameAvailable ? source.sourceEngineFrame : 0;
    context.source_engine_frame_available = source.sourceEngineFrameAvailable;
    context.frame_id = source.frameId;
    context.view_id = source.renderViewId;
    context.device_incarnation = source.queue.deviceGeneration;
    context.queue_id = source.queue.queueId;
    context.queue_kind = source.queue.IsValid() ? static_cast<uint8_t>(source.queue.kind) : uint8_t{0};
    context.target_index = source.targetIndex;
    context.backend_generation = source.backendGeneration;
    return context;
}

inline RHICommandProvenance MakeRHICommandProvenance(const ce::profile_rhi_context& context)
{
    RHICommandProvenance result;
    result.recordingId = context.recording_id;
    result.sourceEngineFrame = context.source_engine_frame;
    result.sourceEngineFrameAvailable = context.source_engine_frame_available;
    result.frameId = context.frame_id;
    result.renderViewId = context.view_id;
    result.queue = {context.device_incarnation, context.queue_id, static_cast<RHIQueueKind>(context.queue_kind)};
    result.targetIndex = context.target_index;
    result.backendGeneration = context.backend_generation;
    return result;
}

inline ce::profile_rhi_context MakeRHIProfileContext(const RHISoftwareBatchDesc& desc, uint64_t recordingId,
                                                     uint32_t targetIndex = UINT32_MAX)
{
    ce::profile_rhi_context context;
    context.recording_id = recordingId;
    context.source_engine_frame = desc.sourceEngineFrameAvailable ? desc.sourceEngineFrame : 0;
    context.source_engine_frame_available = desc.sourceEngineFrameAvailable;
    context.frame_id = desc.frameId;
    context.view_id = desc.renderViewId;
    context.device_incarnation = desc.queue.deviceGeneration;
    context.queue_id = desc.queue.queueId;
    context.queue_kind = desc.queue.IsValid() ? static_cast<uint8_t>(desc.queue.kind) : uint8_t{0};
    context.target_index = targetIndex;
    context.backend_generation = desc.backendGeneration;
    return context;
}
