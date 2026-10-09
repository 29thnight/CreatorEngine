#pragma once
#include "../../MaterialGraphSceneInput.h"
#include "../Temporal/TemporalUpscalerHost.h"
#include "../Temporal/TemporalMeasurementProvenance.h"
#include "../Temporal/SpatialPostEffectsHost.h"
#include <algorithm>
#include <bit>
#include <map>
#include <tuple>

// Submission-committed, per logical view history. Preparing/aborting a frame does
// not advance either the jitter sequence or previous object/skin/camera inputs.
struct EnhancedTemporalViewState
{
    TemporalUpscalerHost upscaler;
    SpatialPostEffectsHost spatialPost;
    TemporalFrame frame;
    TemporalMeasurementProvenance provenance;
    FrameCameraSnapshot rasterCamera;
    own::shared_owner<const material_graph::SceneViewInput> previousInput;
    using DecalKey = std::tuple<uint64_t, uint64_t>;
    std::map<DecalKey, math::matrix4x4> previousDecals, pendingDecals;
    using SpriteKey=std::tuple<uint64_t,uint64_t,uint64_t>;
    struct SpriteHistory
    {
        math::matrix4x4 world;math::vector4 uv;math::color color;
        uint64_t textureIdentity{};bool signedDistance{},enableDepth{};int canvasOrder{},layerOrder{};
    };
    std::map<SpriteKey,SpriteHistory> previousSprites,pendingSprites;
    math::matrix4x4 previousViewProjection{math::matrix4x4::identity()};
    uint64_t viewId{}, sceneEpoch{}, revision{}, settingsGeneration{}, resetGeneration{}, jitterIndex{};
    uint64_t submittedFrame{};
    float previousSeconds{}, pendingSeconds{}, previousJitterX{}, previousJitterY{};
    TemporalExtent previousRender, previousDisplay;
    TemporalQuality quality{ TemporalQuality::Quality };
    TemporalRuntimeSettings reconstructionSettings;
    TemporalProvider previousProvider{ TemporalProvider::None };
    // One writer per callback, read only after the graph's recording joins.
    // These observations never choose whether another callback records FXAA.
    bool temporalAaApplied{ false }, fxaaRequested{ true }, fxaaApplied{ false };

    TemporalResult Begin(IRHIDeviceResources& resources, TemporalBackend backend,
        const TemporalRuntimeSnapshot& control, const FrameCameraSnapshot& camera,
        uint64_t realFrame, uint64_t logicalView, uint64_t scene, uint64_t history,
        TemporalExtent display, float deltaSeconds, float totalSeconds, bool nativeOnly, bool toneMappedSdr)
    {
        auto settings = control.settings;
        if (nativeOnly)
        {
            settings.requestedUpscaler = TemporalProvider::None;
        }
        quality = settings.quality;
        temporalAaApplied = fxaaApplied = false;
        const auto configured = upscaler.Configure(resources, backend, settings,
            control.requestedGeneration, display, false, camera.isOrthographic);
        if (!configured.IsSuccess())
        {
            return configured;
        }
        const auto spatialConfigured = spatialPost.Configure(resources, backend, settings,
            control.requestedGeneration, display, upscaler.Provider() != TemporalProvider::None, toneMappedSdr, nativeOnly);
        if (!spatialConfigured.IsSuccess())
        {
            return spatialConfigured;
        }
        const auto render = spatialPost.RenderExtent(upscaler.RenderExtent());
        if (!render.IsValid()) return {TemporalStatus::InvalidInput};
        const bool reset = !submittedFrame || realFrame <= submittedFrame ||
            viewId != logicalView || sceneEpoch != scene || revision != history ||
            previousRender != render || previousDisplay != display ||
            !SameTemporalReconstructionSettings(reconstructionSettings, settings) ||
            previousProvider != upscaler.Provider() || resetGeneration != control.historyResetGeneration;
        // Keep the reset pending through deferred preparation or an aborted
        // recording. Only Commit establishes a valid previous-frame history.
        if (reset) { submittedFrame = 0; previousInput.reset(); previousDecals.clear(); previousSprites.clear(); jitterIndex = 0; }
        viewId = logicalView; sceneEpoch = scene; revision = history;
        settingsGeneration = control.requestedGeneration; resetGeneration = control.historyResetGeneration;
        reconstructionSettings = settings;
        pendingSeconds = totalSeconds;
        frame = {};
        frame.realFrameId = realFrame; frame.historyRevision = history + control.historyResetGeneration;
        frame.renderExtent = render; frame.displayExtent = display; frame.reset = reset;
        frame.motionVectorScaleX = frame.motionVectorScaleY = 1.f;
        frame.frameTimeMilliseconds = 1000.f * std::clamp(reset ? deltaSeconds : totalSeconds - previousSeconds, 0.0001f, 1.f);
        frame.cameraNear = camera.nearPlane; frame.cameraFar = camera.farPlane;
        frame.cameraVerticalFov = math::radians(camera.fov);
        const auto current = camera.view * camera.projection;
        const auto currentInverse = math::try_inverse(current);
        if (!currentInverse) return {TemporalStatus::InvalidInput};
        const auto previous = reset ? current : previousViewProjection;
        const auto previousInverse = math::try_inverse(previous);
        if (!previousInverse) return {TemporalStatus::InvalidInput};
        auto& output = frame.camera;
        output.viewMatrix = std::bit_cast<std::array<float,16>>(camera.view);
        output.projectionMatrix = std::bit_cast<std::array<float,16>>(camera.projection);
        output.cameraViewToClip = output.projectionMatrix;
        output.clipToCameraView = std::bit_cast<std::array<float,16>>(camera.inverseProjection);
        output.clipToPreviousClip = std::bit_cast<std::array<float,16>>(*currentInverse * previous);
        output.previousClipToClip = std::bit_cast<std::array<float,16>>(*previousInverse * current);
        output.position = {camera.eyePosition.x,camera.eyePosition.y,camera.eyePosition.z};
        output.up = {camera.up.x,camera.up.y,camera.up.z};
        output.right = {camera.right.x,camera.right.y,camera.right.z};
        output.forward = {camera.forward.x,camera.forward.y,camera.forward.z};
        output.aspectRatio = float(display.width)/float(display.height);
        output.orthographicProjection = camera.isOrthographic;
        output.valid = true;
        rasterCamera = camera;
        if (upscaler.Provider() != TemporalProvider::None)
        {
            const auto jitter=SampleTemporalJitter(jitterIndex);
            frame.jitterX=jitter.x;frame.jitterY=jitter.y;
            for (unsigned row=0; row<4; ++row)
            {
                rasterCamera.projection.m[row][0] += 2.f * frame.jitterX / float(render.width) * camera.projection.m[row][3];
                rasterCamera.projection.m[row][1] -= 2.f * frame.jitterY / float(render.height) * camera.projection.m[row][3];
            }
            rasterCamera.inverseProjection = math::inverse(rasterCamera.projection);
        }
        frame.previousJitterX = reset ? frame.jitterX : previousJitterX;
        frame.previousJitterY = reset ? frame.jitterY : previousJitterY;
        provenance = {};
        provenance.frameKind = TemporalMeasuredFrameKind::Real;
        provenance.realFrameId = realFrame; provenance.viewId = logicalView; provenance.sceneEpoch = scene;
        provenance.renderExtent = render; provenance.displayExtent = display;
        provenance.upscaler = upscaler.Provider();
        provenance.frameGenerator = TemporalProvider::None; // renderer never generates frames
        provenance.nativeGateActive = control.nativeCaptureExclusionActive;
        provenance.resolutionState = spatialPost.Scales() ? TemporalResolutionState::SpatialScaled :
            upscaler.Provider() != TemporalProvider::None ? TemporalResolutionState::Reconstructed :
            settings.requestedUpscaler != TemporalProvider::None ? TemporalResolutionState::NativeFallback : TemporalResolutionState::Native;
        return configured;
    }
    void Commit(own::shared_owner<const material_graph::SceneViewInput> input)
    {
        previousInput = std::move(input);
        previousViewProjection = std::bit_cast<math::matrix4x4>(frame.camera.viewMatrix) *
            std::bit_cast<math::matrix4x4>(frame.camera.projectionMatrix);
        previousRender = frame.renderExtent; previousDisplay = frame.displayExtent;
        previousProvider = upscaler.Provider();
        const auto spatial = spatialPost.Snapshot();
        provenance.spatialMode = spatial.activeNisMode;
        provenance.deepDvcApplied = spatial.deepDvcApplied;
        submittedFrame = frame.realFrameId; previousSeconds = pendingSeconds;
        previousJitterX = frame.jitterX; previousJitterY = frame.jitterY;
        previousDecals = std::move(pendingDecals); previousSprites = std::move(pendingSprites); ++jitterIndex;
    }
    void Invalidate() { submittedFrame = 0; previousInput.reset(); previousDecals.clear(); previousSprites.clear(); }

    // Call only after successful graph recording and submission. In particular,
    // Configure success is not evidence that the current SDK evaluation ran.
    void PublishSubmitted(TemporalRuntimeSnapshot& snapshot) const
    {
        snapshot.observedGeneration = settingsGeneration;
        snapshot.viewId = viewId; snapshot.sceneEpoch = sceneEpoch; snapshot.frame = frame;
        snapshot.selectedUpscaler = upscaler.Provider();
        snapshot.activeUpscaler = temporalAaApplied ? upscaler.Provider() : TemporalProvider::None;
        snapshot.lastUpscaleResult = upscaler.LastResult();
        snapshot.requestedUpscaleResult = upscaler.RequestedResult();
        snapshot.renderSubmittedFrameId = snapshot.lastRealFrameId = frame.realFrameId;
        snapshot.observedUpscaleQuality = quality;
        snapshot.aaObserved = true;
        snapshot.temporalAaApplied = temporalAaApplied;
        snapshot.fxaaRequested = fxaaRequested; snapshot.fxaaApplied = fxaaApplied;
        snapshot.spatialPost = spatialPost.Snapshot();
        for (const auto& capability : upscaler.Capabilities())
        {
            auto found = std::find_if(snapshot.capabilities.begin(), snapshot.capabilities.end(),
                [&](const auto& old) { return old.provider == capability.provider && old.backend == capability.backend; });
            if (found == snapshot.capabilities.end())
            {
                snapshot.capabilities.push_back(capability);
            }
            else
            {
                found->upscaling = capability.upscaling;
                found->upscalerImplementation = capability.upscalerImplementation;
            }
        }
    }
};
