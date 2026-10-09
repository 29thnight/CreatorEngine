#include "ProfilerRenderingBridge.h"

#include "EditorWindowNames.h"
#include "EditorWindowRegistry.h"
#include "Render/Scene/EnhancedSceneRenderer.h"

#include <algorithm>
#include <string_view>

namespace editor
{
    namespace rendering_bridge
    {
        std::string bounded_text(std::string_view value, std::size_t maximum, bool& truncated)
        {
            std::size_t size = (std::min)(value.size(), maximum);
            if (size < value.size())
            {
                truncated = true;
                // Do not publish a partial UTF-8 code point at the byte limit.
                while (size > 0 && (static_cast<unsigned char>(value[size]) & 0xc0) == 0x80)
                {
                    --size;
                }
            }
            std::string result(value.substr(0, size));
            for (char& character : result)
            {
                if (character == '\0')
                {
                    character = '?';
                    truncated = true;
                }
            }
            return result;
        }
    }

    ce::profiler_viewer::diagnostics::rendering_snapshot capture_rendering_diagnostics()
    {
        using namespace ce::profiler_viewer::diagnostics;
        static_assert(kEnhancedLiveDisplayTargetCount == rendering_view_count);
        const auto source = EnhancedSceneRenderer::GetLiveDebugSnapshot();
        const auto displays = EnhancedSceneRenderer::GetLiveDisplaySnapshot();
        rendering_snapshot result;
        result.backend = source.backend == EnhancedLiveBackend::Vulkan
            ? rendering_backend::vulkan : rendering_backend::dx12;
        result.enabled = source.enabled;
        result.pipelineReady = source.pipelineReady;
        result.width = source.width;
        result.height = source.height;
        result.framesRendered = source.framesRendered;
        result.framesIdle = source.framesIdle;
        result.framesInFlight = source.framesInFlight;
        result.consumedFrameId = source.consumedFrameId;
        result.drawCount = source.drawCount;
        result.batchCount = source.batchCount;
        result.preparedMeshletBatchCount = source.preparedMeshletBatchCount;
        result.indexedIndirectSupported = source.indexedIndirectSupported;
        result.nonIndexedIndirectSupported = source.nonIndexedIndirectSupported;
        result.preparedGpuCandidates = source.preparedGpuCandidates;
        result.preparedGpuCompactedBins = source.preparedGpuCompactedBins;
        result.preparedGpuPreservedBins = source.preparedGpuPreservedBins;
        result.preparedGpuConservativeCandidates = source.preparedGpuConservativeCandidates;
        result.meshletFallback = rendering_bridge::bounded_text(source.meshletFallback,
            maximum_rendering_text_bytes, result.textTruncated);
        result.currentFrameOcclusion = source.currentFrameOcclusion;
        result.occlusionFallback = rendering_bridge::bounded_text(source.occlusionFallback,
            maximum_rendering_text_bytes, result.textTruncated);
        result.skinningFallback = rendering_bridge::bounded_text(source.skinningFallback,
            maximum_rendering_text_bytes, result.textTruncated);
        result.decalCount = source.decalCount;
        result.decalBatchCount = source.decalBatchCount;
        for (std::size_t index = 0; index < rendering_view_count; ++index)
        {
            const auto& sourceShadow = source.shadow[index];
            auto& shadow = result.shadow[index];
            shadow.valid = sourceShadow.valid;
            shadow.hasDirectionalLight = sourceShadow.hasDirectionalLight;
            shadow.lightIndex = sourceShadow.lightIndex;
            shadow.lightDirection = sourceShadow.lightDirection;
            shadow.shadowDistance = sourceShadow.shadowDistance;
            shadow.slopeScale = sourceShadow.slopeScale;
            shadow.casterCandidates = sourceShadow.casterCandidates;
            shadow.gpuVisibilityActive = sourceShadow.gpuVisibilityActive;
            shadow.gpuSubmittedCandidates = sourceShadow.gpuSubmittedCandidates;
            shadow.gpuSubmittedBins = sourceShadow.gpuSubmittedBins;
            for (std::size_t cascadeIndex = 0; cascadeIndex < shadow.cascades.size(); ++cascadeIndex)
            {
                const auto& sourceCascade = sourceShadow.cascades[cascadeIndex];
                shadow.cascades[cascadeIndex] = {
                    sourceCascade.splitDepth, sourceCascade.radius, sourceCascade.worldTexel,
                    sourceCascade.depthSpan, sourceCascade.constantBias, sourceCascade.graphCasters,
                };
            }
            const auto& view = displays.targets[index];
            result.views[index] = { view.key.viewId, view.completedFrameId,
                view.completedWidth, view.completedHeight, view.ready };
        }
        result.cpuMs = source.cpuMs;
        result.gpuMs = source.gpuMs;
        const auto provenance = [](const auto& p) {
            rendering_temporal_provenance value;
            value.frameKind = static_cast<std::uint8_t>(p.frameKind);
            value.resolutionState = static_cast<std::uint8_t>(p.resolutionState);
            value.upscaler = static_cast<std::uint8_t>(p.upscaler);
            value.frameGenerator = static_cast<std::uint8_t>(p.frameGenerator);
            value.realFrameId = p.realFrameId; value.viewId = p.viewId; value.sceneEpoch = p.sceneEpoch;
            value.generatedOrdinal = p.generatedOrdinal;
            value.renderWidth = p.renderExtent.width; value.renderHeight = p.renderExtent.height;
            value.displayWidth = p.displayExtent.width; value.displayHeight = p.displayExtent.height;
            value.nativeGateActive = p.nativeGateActive;
            value.publicationFrameId = p.publicationFrameId;
            value.spatialProvenanceAvailable = p.IsValid();
            value.spatialMode = static_cast<std::uint8_t>(p.spatialMode);
            value.deepDvcApplied = p.deepDvcApplied;
            return value;
        };
        result.temporalProvenance = provenance(source.temporalProvenance);
        result.gpuTemporalProvenance = provenance(source.gpuTemporalProvenance);
        result.gpuCollects = source.gpuCollects;
        result.gpuCollectMismatches = source.gpuCollectMismatches;
        result.gpuQueryOverflowPasses = source.gpuQueryOverflowPasses;
        result.lastGpuFrameId = source.lastGpuFrameId;
        result.lastGpuSubmissionId = source.lastGpuSubmissionId;
        result.lastGpuViewId = source.lastGpuViewId;
        result.lastGpuCollectError = rendering_bridge::bounded_text(source.lastGpuCollectError,
            maximum_rendering_text_bytes, result.textTruncated);
        result.graveyardCount = static_cast<std::uint64_t>(source.graveyardCount);
        result.lastError = rendering_bridge::bounded_text(source.lastError,
            maximum_rendering_text_bytes, result.textTruncated);
        const auto passCount = (std::min)(source.passTimings.size(), maximum_rendering_passes);
        result.omittedPassTimings = source.passTimings.size() - passCount;
        result.passTimings.reserve(passCount);
        for (std::size_t index = 0; index < passCount; ++index)
        {
            const auto& timing = source.passTimings[index];
            result.passTimings.push_back({ rendering_bridge::bounded_text(timing.name,
                maximum_rendering_name_bytes, result.textTruncated), timing.milliseconds });
        }
        const auto messageCount = (std::min)(source.validationMessages.size(), maximum_rendering_messages);
        result.omittedValidationMessages = source.validationMessages.size() - messageCount;
        result.validationMessages.reserve(messageCount);
        for (std::size_t index = 0; index < messageCount; ++index)
        {
            result.validationMessages.push_back(rendering_bridge::bounded_text(source.validationMessages[index],
                maximum_rendering_message_bytes, result.textTruncated));
        }
        return result;
    }

    bool apply_rendering_command(ce::profiler_viewer::diagnostics::rendering_command command)
    {
        using ce::profiler_viewer::diagnostics::rendering_command;
        if (command != rendering_command::open_render_pass)
        {
            return false;
        }
        // Window registry mutation and focus happen on its UI thread. The engine
        // owner queues this known action instead of invoking ImGui from pump().
        return queue_window_request(EditorWindowName::kRenderPass, window_request::focus);
    }
}
