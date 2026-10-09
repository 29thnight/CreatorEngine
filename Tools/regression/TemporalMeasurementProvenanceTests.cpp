// Source-only TR0 fixture. Not built or executed in this change.
// Link TemporalRuntimeControl.cpp when the user authorizes native validation.
#include "../../Engine/RenderEngine/Render/Temporal/TemporalMeasurementProvenance.h"
#include "../../Engine/RenderEngine/Render/Temporal/TemporalRuntimeControl.h"
#include <cassert>

int main()
{
    TemporalMeasurementProvenance frame;
    assert(!frame.IsValid() && !frame.IsGoldenEligible());
    frame.frameKind = TemporalMeasuredFrameKind::Real;
    frame.realFrameId = 17; frame.viewId = 4; frame.sceneEpoch = 2;
    frame.renderExtent = frame.displayExtent = {1920, 1080};
    frame.resolutionState = TemporalResolutionState::Native;
    assert(frame.IsValid() && !frame.IsGoldenEligible());
    frame.nativeGateActive = true;
    assert(frame.IsGoldenEligible());
    auto mutation = frame;
    mutation.renderExtent.width = 0;
    assert(!mutation.IsValid() && !mutation.IsGoldenEligible());
    mutation = frame;
    mutation.generatedOrdinal = 1;
    assert(!mutation.IsValid() && !mutation.IsGoldenEligible());
    mutation.frameKind = TemporalMeasuredFrameKind::Generated;
    mutation.frameGenerator = TemporalProvider::Fsr;
    assert(mutation.IsValid() && !mutation.IsGoldenEligible());
    mutation = frame;
    mutation.upscaler = TemporalProvider::Dlss;
    mutation.resolutionState = TemporalResolutionState::Reconstructed;
    mutation.renderExtent = {1280, 720};
    assert(mutation.IsValid() && !mutation.IsGoldenEligible());
    mutation = frame;
    mutation.spatialMode = SpatialScalingMode::NisScale;
    mutation.resolutionState = TemporalResolutionState::SpatialScaled;
    mutation.renderExtent = {1280, 720};
    assert(mutation.IsValid() && !mutation.IsGoldenEligible());
    mutation = frame;
    mutation.spatialMode = SpatialScalingMode::NisSharpen;
    assert(mutation.IsValid() && !mutation.IsGoldenEligible());
    mutation = frame;
    mutation.deepDvcApplied = true;
    assert(mutation.IsValid() && !mutation.IsGoldenEligible());

    mutation = frame;
    mutation.publicationFrameId = 29;
    mutation.testFaultActive = true;
    assert(!mutation.IsValid() && !mutation.IsGoldenEligible());
    mutation.InvalidateAcceptanceEvidence();
    assert(!mutation.IsValid() && !mutation.IsGoldenEligible());
    assert(mutation.realFrameId == frame.realFrameId && mutation.publicationFrameId == 29);
    assert(mutation.viewId == frame.viewId && mutation.sceneEpoch == frame.sceneEpoch);
    assert(mutation.renderExtent == frame.renderExtent && mutation.displayExtent == frame.displayExtent);
    // Runtime native suppression remains factual even when acceptance proof is unknown.
    assert(mutation.nativeGateActive);
    assert(mutation.testFaultActive);
    assert(mutation.frameKind == TemporalMeasuredFrameKind::Unknown);
    assert(mutation.resolutionState == TemporalResolutionState::Unknown);
    mutation.spatialMode = SpatialScalingMode::NisSharpen;
    assert(!mutation.IsValid() && !mutation.IsGoldenEligible());

    auto& runtime = TemporalRuntimeControl::Get();
    const auto saved = runtime.Snapshot().requestedSettings;
    auto intent = saved;
    intent.enabled = true;
    intent.requestedUpscaler = TemporalProvider::Fsr;
    intent.requestedFrameGenerator = TemporalProvider::Dlss;
    intent.spatialPost.nisMode = SpatialScalingMode::NisSharpen;
    intent.spatialPost.deepDvcEnabled = true;
    runtime.Request(intent);
    const auto before = runtime.Snapshot();
    {
        TemporalNativeCaptureExclusion outer;
        auto effective = runtime.Snapshot();
        assert(effective.nativeCaptureExclusionActive && !effective.settings.enabled);
        assert(effective.settings.requestedUpscaler == TemporalProvider::None);
        assert(effective.settings.requestedFrameGenerator == TemporalProvider::None);
        assert(effective.settings.spatialPost.nisMode == SpatialScalingMode::Off);
        assert(!effective.settings.spatialPost.deepDvcEnabled);
        assert(effective.requestedSettings == intent);
        assert(effective.historyResetGeneration > before.historyResetGeneration);
        {
            TemporalNativeCaptureExclusion inner;
            intent.requestedUpscaler = TemporalProvider::XeSS;
            runtime.Request(intent); // Concurrent intent must survive both releases.
        }
        assert(runtime.Snapshot().nativeCaptureExclusionActive);
        assert(runtime.Snapshot().requestedSettings == intent);
    }
    const auto restored = runtime.Snapshot();
    assert(!restored.nativeCaptureExclusionActive && restored.settings == intent);
    assert(restored.historyResetGeneration > before.historyResetGeneration + 1);
    runtime.Request(saved);
}
