#pragma once

#include "CommandResult.h"
#include "Render/Temporal/TemporalRuntimeControl.h"

#include <charconv>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// Both existing command hosts use the same live control and value serializer.
// These commands neither create a test renderer nor manufacture SDK support.
namespace CommandCore
{
    namespace TemporalCommandDetail
    {
        inline const char* ProviderName(TemporalProvider value)
        {
            switch (value)
            {
            case TemporalProvider::None: return "none";
            case TemporalProvider::Fsr: return "fsr";
            case TemporalProvider::Dlss: return "dlss";
            case TemporalProvider::XeSS: return "xess";
            }
            return "unknown";
        }

        inline bool ParseProvider(std::string_view value, TemporalProvider& provider)
        {
            if (value == "none") { provider = TemporalProvider::None; }
            else if (value == "fsr") { provider = TemporalProvider::Fsr; }
            else if (value == "dlss") { provider = TemporalProvider::Dlss; }
            else if (value == "xess") { provider = TemporalProvider::XeSS; }
            else { return false; }
            return true;
        }

        inline bool ParseQuality(std::string_view value, TemporalQuality& quality)
        {
            if (value == "native-aa") { quality = TemporalQuality::NativeAA; }
            else if (value == "quality") { quality = TemporalQuality::Quality; }
            else if (value == "balanced") { quality = TemporalQuality::Balanced; }
            else if (value == "performance") { quality = TemporalQuality::Performance; }
            else if (value == "ultra-performance") { quality = TemporalQuality::UltraPerformance; }
            else { return false; }
            return true;
        }

        inline const char* QualityName(TemporalQuality value)
        {
            switch (value)
            {
            case TemporalQuality::NativeAA: return "native-aa";
            case TemporalQuality::Quality: return "quality";
            case TemporalQuality::Balanced: return "balanced";
            case TemporalQuality::Performance: return "performance";
            case TemporalQuality::UltraPerformance: return "ultra-performance";
            }
            return "unknown";
        }

        inline const char* TemporalAaName(TemporalProvider provider, TemporalQuality quality)
        {
            switch (provider)
            {
            case TemporalProvider::Fsr: return quality == TemporalQuality::NativeAA ? "fsr_native_aa" : "fsr_upscale";
            case TemporalProvider::Dlss: return quality == TemporalQuality::NativeAA ? "dlaa" : "dlss_upscale";
            case TemporalProvider::XeSS: return quality == TemporalQuality::NativeAA ? "xess_aa" : "xess_upscale";
            case TemporalProvider::None: return "none";
            }
            return "unknown";
        }

        inline const char* StatusName(TemporalStatus value)
        {
            switch (value)
            {
            case TemporalStatus::Success: return "success";
            case TemporalStatus::NotQueried: return "not_queried";
            case TemporalStatus::SdkNotBuilt: return "not_built";
            case TemporalStatus::RuntimeUnavailable: return "runtime_absent";
            case TemporalStatus::SdkVersionMismatch: return "sdk_version_mismatch";
            case TemporalStatus::BackendUnsupported: return "backend_unsupported";
            case TemporalStatus::FeatureUnsupported: return "hardware_or_feature_unsupported";
            case TemporalStatus::InvalidInput: return "input_missing_or_invalid";
            case TemporalStatus::NotInitialized: return "not_ready";
            case TemporalStatus::AlreadyInitialized: return "already_initialized";
            case TemporalStatus::SdkFailure: return "setup_or_execution_failed";
            case TemporalStatus::IntegrationRequired: return "integration_required";
            case TemporalStatus::EditorViewportForbidden: return "editor_viewport_forbidden";
            case TemporalStatus::ProjectionUnsupported: return "projection_unsupported";
            }
            return "unknown";
        }

        inline const char* ExecutionState(TemporalProvider requested, TemporalProvider active,
            const TemporalResult& result, bool observed, bool pending)
        {
            if (pending) { return "pending"; }
            if (!observed) { return "not_ready"; }
            if (active != TemporalProvider::None && result.IsSuccess()) { return "active"; }
            if (requested == TemporalProvider::None) { return "disabled"; }
            if (result.IsSuccess()) { return "ready_not_active"; }
            return StatusName(result.status);
        }

        // Decimal strings preserve full uint64 identities in JavaScript clients.
        inline CommandData Identity(uint64_t value)
        {
            return CommandData::String(std::to_string(value));
        }

        inline CommandData ResultData(const TemporalResult& result)
        {
            auto data = CommandData::Object();
            data.Set("status", CommandData::String(StatusName(result.status)));
            data.Set("nativeCode", CommandData::Int(result.nativeCode));
            return data;
        }

        inline CommandData ExtentData(const TemporalExtent& extent)
        {
            auto data = CommandData::Object();
            data.Set("width", CommandData::Int(extent.width));
            data.Set("height", CommandData::Int(extent.height));
            return data;
        }

        inline CommandData SnapshotData(const TemporalRuntimeSnapshot& snapshot,
            TemporalPresentationTarget target)
        {
            auto data = CommandData::Object();
            data.Set("schema", CommandData::String("temporal.live.v1"));
            data.Set("source", CommandData::String("production_runtime"));
            data.Set("target", CommandData::String(target == TemporalPresentationTarget::PlayerSwapchain
                ? "player_swapchain" : "editor_viewport"));
            data.Set("viewId", Identity(snapshot.viewId));
            data.Set("sceneEpoch", Identity(snapshot.sceneEpoch));
            data.Set("observedPresentationTarget", CommandData::String(
                snapshot.presentationTarget == TemporalPresentationTarget::PlayerSwapchain ? "player_swapchain" :
                snapshot.presentationTarget == TemporalPresentationTarget::EditorViewport ? "editor_viewport" : "unbound"));
            data.Set("nativeCaptureExclusionActive", CommandData::Bool(snapshot.nativeCaptureExclusionActive));
            data.Set("effectiveRequestedUpscaler", CommandData::String(ProviderName(snapshot.settings.requestedUpscaler)));
            data.Set("effectiveRequestedUpscaleQuality", CommandData::String(QualityName(snapshot.settings.quality)));
            data.Set("effectiveRequestedFrameGenerator", CommandData::String(ProviderName(snapshot.settings.requestedFrameGenerator)));
            data.Set("rendererObserved", CommandData::Bool(snapshot.rendererObserved));
            data.Set("playerObserved", CommandData::Bool(snapshot.playerObserved));
            data.Set("requestedGeneration", Identity(snapshot.requestedGeneration));
            data.Set("observedGeneration", Identity(snapshot.observedGeneration));
            data.Set("playerObservedGeneration", Identity(snapshot.playerObservedGeneration));
            data.Set("historyResetGeneration", Identity(snapshot.historyResetGeneration));
            data.Set("requestedUpscaler", CommandData::String(ProviderName(snapshot.requestedSettings.requestedUpscaler)));
            data.Set("requestedUpscaleQuality", CommandData::String(QualityName(snapshot.requestedSettings.quality)));
            data.Set("requestedFrameGenerator", CommandData::String(ProviderName(snapshot.requestedSettings.requestedFrameGenerator)));
            data.Set("selectedUpscaler", CommandData::String(ProviderName(snapshot.selectedUpscaler)));
            data.Set("selectedFrameGenerator", CommandData::String(ProviderName(snapshot.selectedFrameGenerator)));
            data.Set("activeUpscaler", CommandData::String(ProviderName(snapshot.activeUpscaler)));
            data.Set("activeFrameGenerator", CommandData::String(ProviderName(snapshot.activeFrameGenerator)));
            data.Set("aaObserved", CommandData::Bool(snapshot.aaObserved));
            data.Set("fxaaRequested", CommandData::Bool(snapshot.fxaaRequested));
            data.Set("fxaaApplied", CommandData::Bool(snapshot.fxaaApplied));
            data.Set("temporalAaApplied", CommandData::Bool(snapshot.temporalAaApplied));
            data.Set("activeUpscaleQuality", snapshot.aaObserved && snapshot.temporalAaApplied
                ? CommandData::String(QualityName(snapshot.observedUpscaleQuality)) : CommandData{});
            data.Set("activeAaMethod", snapshot.aaObserved ? CommandData::String(snapshot.temporalAaApplied
                ? TemporalAaName(snapshot.activeUpscaler, snapshot.observedUpscaleQuality)
                : snapshot.fxaaApplied ? "fxaa" : "none") : CommandData{});
            data.Set("upscaleState", CommandData::String(ExecutionState(snapshot.settings.requestedUpscaler,
                snapshot.activeUpscaler, snapshot.lastUpscaleResult, snapshot.rendererObserved,
                snapshot.observedGeneration < snapshot.requestedGeneration)));
            data.Set("frameGenerationState", CommandData::String(target == TemporalPresentationTarget::EditorViewport
                ? "editor_viewport_forbidden" : ExecutionState(snapshot.settings.requestedFrameGenerator,
                    snapshot.activeFrameGenerator, snapshot.lastFrameGenerationResult, snapshot.playerObserved,
                    snapshot.playerObservedGeneration < snapshot.requestedGeneration)));
            data.Set("requestedUpscaleResult", ResultData(snapshot.requestedUpscaleResult));
            data.Set("requestedFrameGenerationResult", ResultData(snapshot.requestedFrameGenerationResult));
            data.Set("upscaleResult", ResultData(snapshot.lastUpscaleResult));
            data.Set("frameGenerationResult", ResultData(snapshot.lastFrameGenerationResult));
            data.Set("diagnostic", CommandData::String(snapshot.diagnostic));
            data.Set("lastRealFrameId", Identity(snapshot.lastRealFrameId));
            data.Set("frameKind", CommandData::String(snapshot.lastRealFrameId != 0 ? "real" : "unobserved"));
            data.Set("renderExtent", ExtentData(snapshot.frame.renderExtent));
            data.Set("displayExtent", ExtentData(snapshot.frame.displayExtent));
            data.Set("cameraValid", CommandData::Bool(snapshot.frame.camera.valid));
            data.Set("depthInverted", CommandData::Bool(snapshot.frame.depthInverted));
            data.Set("depthInfinite", CommandData::Bool(snapshot.frame.depthInfinite));
            data.Set("highDynamicRange", CommandData::Bool(snapshot.frame.highDynamicRange));
            data.Set("preExposure", CommandData::Double(snapshot.frame.preExposure));
            data.Set("requestedInterpolatedFrameCount", CommandData::Int(snapshot.settings.interpolatedFrameCount));
            data.Set("historyRevision", Identity(snapshot.frame.historyRevision));
            data.Set("reset", CommandData::Bool(snapshot.frame.reset));
            data.Set("jitterX", CommandData::Double(snapshot.frame.jitterX));
            data.Set("jitterY", CommandData::Double(snapshot.frame.jitterY));
            data.Set("simulationDeltaMilliseconds", CommandData::Double(snapshot.frame.frameTimeMilliseconds));
            data.Set("motionVectorScaleX", CommandData::Double(snapshot.frame.motionVectorScaleX));
            data.Set("motionVectorScaleY", CommandData::Double(snapshot.frame.motionVectorScaleY));
            data.Set("motionConvention", CommandData::String("current_to_previous_pixels_unjittered_top_left"));
            data.Set("motionVectorsValid", CommandData::Bool(snapshot.motionVectorsValid));
            auto motion = CommandData::Object();
            motion.Set("static", CommandData::Bool(snapshot.motionStaticValid));
            motion.Set("skinned", CommandData::Bool(snapshot.motionSkinnedValid));
            motion.Set("instanced", CommandData::Bool(snapshot.motionInstancedValid));
            motion.Set("decal", CommandData::Bool(snapshot.motionDecalValid));
            motion.Set("alpha", CommandData::Bool(snapshot.motionAlphaValid));
            data.Set("motionCoverage", std::move(motion));
            data.Set("renderSubmittedFrameId", Identity(snapshot.renderSubmittedFrameId));
            data.Set("renderGpuCompletedFrameId", Identity(snapshot.renderGpuCompletedFrameId));
            data.Set("cpuPresentReturnedFrameId", Identity(snapshot.cpuPresentReturnedFrameId));
            data.Set("sdkFinalConsumedFrameId", Identity(snapshot.sdkFinalConsumedFrameId));
            data.Set("realPresentationCount", Identity(snapshot.realPresentationCount));
            auto generatedIdentity = CommandData::Object();
            generatedIdentity.Set("realFrameId", Identity(snapshot.generatedRealFrameId));
            generatedIdentity.Set("ordinal", CommandData::Int(snapshot.generatedOrdinal));
            data.Set("generatedFrameId", snapshot.generatedRealFrameId && snapshot.generatedOrdinal ? std::move(generatedIdentity) : CommandData{});
            data.Set("generatedFrameIdentityNote", CommandData::String("identity is the source real-frame ID plus SDK-output ordinal; neither counter is a frame ID"));
            data.Set("generatedSubmissionCount", Identity(snapshot.generatedSubmissionCount));
            data.Set("generatedPresentationCount", Identity(snapshot.generatedPresentationCount));
            data.Set("latencyMarkerRealFrameId", Identity(snapshot.latencyMarkerRealFrameId));
            data.Set("latencyProvider", CommandData::String(snapshot.latencyProvider));
            data.Set("latencyResult", ResultData(snapshot.latencyResult));
            data.Set("inputToPhotonMilliseconds", CommandData{});
            data.Set("completionNote", CommandData::String("zero means unobserved; CPU present return, GPU completion and SDK final consumption are distinct"));
            auto capabilities = CommandData::Array();
            for (const auto& capability : snapshot.capabilities)
            {
                auto item = CommandData::Object();
                item.Set("provider", CommandData::String(ProviderName(capability.provider)));
                item.Set("graphicsApi", CommandData::String(capability.backend == TemporalBackend::DX12 ? "dx12" : "vulkan"));
                item.Set("sdkVersion", CommandData::String(capability.sdkVersion ? capability.sdkVersion : "unknown"));
                item.Set("sdkRevision", CommandData::String(capability.sdkRevision ? capability.sdkRevision : "unknown"));
                item.Set("upscalerRuntimeImplementation", CommandData::String(capability.upscalerImplementation));
                item.Set("frameGeneratorRuntimeImplementation", CommandData::String(capability.frameGeneratorImplementation));
                item.Set("upscaling", ResultData(capability.upscaling));
                item.Set("frameGeneration", ResultData(capability.frameGeneration));
                item.Set("maxInterpolatedFrames", CommandData::Int(capability.maxInterpolatedFrames));
                capabilities.Append(std::move(item));
            }
            data.Set("capabilities", std::move(capabilities));
            return data;
        }
    }

    inline CommandResult ExecuteTemporalCommand(const std::vector<std::string>& parts,
        TemporalPresentationTarget target)
    {
        using namespace TemporalCommandDetail;
        if (parts.empty())
        {
            return InvalidArguments("A temporal command name is required");
        }
        auto& control = TemporalRuntimeControl::Get();
        const auto snapshot = control.Snapshot();
        auto settings = snapshot.requestedSettings;
        const auto& command = parts[0];
        uint64_t generation = 0;
        if (command == "temporal.upscale" || command == "temporal.fg")
        {
            const bool upscale = command == "temporal.upscale";
            if (parts.size() == 1)
            {
                return Ok("Live state; support and request acknowledgement do not prove activation", SnapshotData(snapshot, target));
            }
            TemporalProvider provider;
            if (parts.size() > (upscale ? 3u : 2u) || !ParseProvider(parts[1], provider))
            {
                return InvalidArguments(command + " [none|fsr|dlss|xess]" +
                    (upscale ? " [native-aa|quality|balanced|performance|ultra-performance]" : ""));
            }
            if (!upscale && target == TemporalPresentationTarget::EditorViewport && provider != TemporalProvider::None)
            {
                return PreconditionFailed("temporal.editor_viewport_forbidden", "Frame generation is forbidden for Editor viewports");
            }
            if (upscale)
            {
                settings.requestedUpscaler = provider;
                if (parts.size() == 3 && !ParseQuality(parts[2], settings.quality))
                {
                    return InvalidArguments("Quality must be native-aa, quality, balanced, performance or ultra-performance");
                }
            }
            else
            {
                settings.requestedFrameGenerator = provider;
            }
            settings.enabled = settings.requestedUpscaler != TemporalProvider::None ||
                settings.requestedFrameGenerator != TemporalProvider::None;
            generation = control.Request(settings);
        }
        else if (command == "temporal.reset")
        {
            if (parts.size() != 1)
            {
                return InvalidArguments("temporal.reset takes no arguments");
            }
            generation = control.RequestHistoryReset();
        }
        else if (command == "temporal.runtime")
        {
            if (parts.size() < 2 || parts.size() > 3)
            {
                return InvalidArguments("temporal.runtime <absolute-trusted-directory> [dlss-project-id]");
            }
            const auto directory = std::filesystem::u8path(parts[1]);
            std::error_code error;
            if (!directory.is_absolute() || !std::filesystem::is_directory(directory, error) || error)
            {
                return InvalidArguments("Runtime directory must be an existing absolute trusted directory");
            }
            settings.runtimeDirectory = directory.wstring();
            if (parts.size() == 3)
            {
                settings.dlssProjectId = parts[2];
            }
            generation = control.Request(settings);
        }
        else
        {
            if (command != "temporal.status" && command != "temporal.support" &&
                command != "temporal.metadata" && command != "temporal.motion" &&
                command != "temporal.latency" && command != "temporal.fallback")
            {
                return InvalidArguments("Unknown temporal command", "command.unknown");
            }
            uint64_t expected = 0;
            if (parts.size() > 1)
            {
                if (command != "temporal.status" || parts.size() != 2)
                {
                    return InvalidArguments(command + " takes no arguments");
                }
                const auto& value = parts[1];
                const auto parsed = std::from_chars(value.data(), value.data() + value.size(), expected);
                if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || expected == 0)
                {
                    return InvalidArguments("temporal.status generation must be a positive uint64 decimal identity");
                }
            }
            auto data = SnapshotData(snapshot, target);
            const bool rendererAcknowledged = snapshot.rendererObserved && snapshot.observedGeneration >= expected;
            const bool playerAcknowledged = snapshot.playerObserved && snapshot.playerObservedGeneration >= expected;
            data.Set("rendererAcknowledged", CommandData::Bool(rendererAcknowledged));
            data.Set("playerAcknowledged", CommandData::Bool(playerAcknowledged));
            const bool acknowledged = rendererAcknowledged &&
                (target != TemporalPresentationTarget::PlayerSwapchain || playerAcknowledged);
            data.Set("acknowledged", CommandData::Bool(acknowledged));
            if (expected > snapshot.requestedGeneration)
            {
                return InvalidArguments("The requested generation has not been issued by this process");
            }
            if (expected != 0 && snapshot.requestedGeneration > expected)
            {
                data.Set("requestState", CommandData::String("superseded"));
                return Ok("A newer request superseded this generation; its settings are not an exact acknowledgement", std::move(data));
            }
            data.Set("requestState", CommandData::String(acknowledged ? "acknowledged" : "pending"));
            if (expected != 0 && !acknowledged)
            {
                return Ok("The live frame consumers have not acknowledged this generation", std::move(data));
            }
            return Ok("Live diagnostic snapshot; this is not a pixel, generated-frame or latency acceptance result", std::move(data));
        }
        auto data = SnapshotData(control.Snapshot(), target);
        data.Set("receiptGeneration", Identity(generation));
        data.Set("requestState", CommandData::String("queued"));
        return Ok("Request queued; poll temporal.status with receiptGeneration for live-frame acknowledgement", std::move(data));
    }
}
