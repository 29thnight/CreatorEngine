// Standalone TR0 provenance, display association and scoped exclusion regression.
#include "../../Engine/RenderEngine/Render/Temporal/TemporalMeasurementProvenance.h"
#include "../../Engine/RenderEngine/Render/Temporal/TemporalRuntimeControl.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../build/ModuleAdapters/Creator.Contracts.h"
#else
import Creator.Contracts;
#endif

int main()
{
    TemporalMeasurementProvenance frame;
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((!frame.IsValid() && !frame.IsGoldenEligible()));
        },
        "!frame.IsValid() && !frame.IsGoldenEligible()");
    frame.frameKind = TemporalMeasuredFrameKind::Real;
    frame.realFrameId = 17; frame.viewId = 4; frame.sceneEpoch = 2;
    frame.renderExtent = frame.displayExtent = {1920, 1080};
    frame.resolutionState = TemporalResolutionState::Native;
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((frame.IsValid() && !frame.IsGoldenEligible()));
        },
        "frame.IsValid() && !frame.IsGoldenEligible()");
    frame.nativeGateActive = true;
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((frame.IsGoldenEligible()));
        },
        "frame.IsGoldenEligible()");
    frame.publicationFrameId = 29;
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((frame.MatchesRealDisplay(4, 29, 1920, 1080)));
        },
        "frame.MatchesRealDisplay(4, 29, 1920, 1080)");
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((!frame.MatchesRealDisplay(4, 30, 1920, 1080)));
        },
        "!frame.MatchesRealDisplay(4, 30, 1920, 1080)");
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((!frame.MatchesRealDisplay(5, 29, 1920, 1080)));
        },
        "!frame.MatchesRealDisplay(5, 29, 1920, 1080)");
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((!frame.MatchesRealDisplay(4, 29, 1280, 720)));
        },
        "!frame.MatchesRealDisplay(4, 29, 1280, 720)");
    auto missingScene = frame;
    missingScene.sceneEpoch = 0;
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((!missingScene.MatchesRealDisplay(4, 29, 1920, 1080)));
        },
        "!missingScene.MatchesRealDisplay(4, 29, 1920, 1080)");
    auto mutation = frame;
    mutation.renderExtent.width = 0;
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((!mutation.IsValid() && !mutation.IsGoldenEligible()));
        },
        "!mutation.IsValid() && !mutation.IsGoldenEligible()");
    mutation = frame;
    mutation.generatedOrdinal = 1;
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((!mutation.IsValid() && !mutation.IsGoldenEligible()));
        },
        "!mutation.IsValid() && !mutation.IsGoldenEligible()");
    mutation.frameKind = TemporalMeasuredFrameKind::Generated;
    mutation.frameGenerator = TemporalProvider::Fsr;
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((mutation.IsValid() && !mutation.IsGoldenEligible()));
        },
        "mutation.IsValid() && !mutation.IsGoldenEligible()");
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((!mutation.MatchesRealDisplay(4, 29, 1920, 1080)));
        },
        "!mutation.MatchesRealDisplay(4, 29, 1920, 1080)");
    mutation = frame;
    mutation.upscaler = TemporalProvider::Dlss;
    mutation.resolutionState = TemporalResolutionState::Reconstructed;
    mutation.renderExtent = {1280, 720};
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((mutation.IsValid() && !mutation.IsGoldenEligible()));
        },
        "mutation.IsValid() && !mutation.IsGoldenEligible()");
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((mutation.MatchesRealDisplay(4, 29, 1920, 1080)));
        },
        "mutation.MatchesRealDisplay(4, 29, 1920, 1080)");
    mutation = frame;
    mutation.spatialMode = SpatialScalingMode::NisScale;
    mutation.resolutionState = TemporalResolutionState::SpatialScaled;
    mutation.renderExtent = {1280, 720};
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((mutation.IsValid() && !mutation.IsGoldenEligible()));
        },
        "mutation.IsValid() && !mutation.IsGoldenEligible()");
    mutation = frame;
    mutation.spatialMode = SpatialScalingMode::NisSharpen;
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((mutation.IsValid() && !mutation.IsGoldenEligible()));
        },
        "mutation.IsValid() && !mutation.IsGoldenEligible()");
    mutation = frame;
    mutation.deepDvcApplied = true;
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((mutation.IsValid() && !mutation.IsGoldenEligible()));
        },
        "mutation.IsValid() && !mutation.IsGoldenEligible()");

    mutation = frame;
    mutation.publicationFrameId = 29;
    mutation.testFaultActive = true;
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((!mutation.IsValid() && !mutation.IsGoldenEligible()));
        },
        "!mutation.IsValid() && !mutation.IsGoldenEligible()");
    mutation.InvalidateAcceptanceEvidence();
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((!mutation.IsValid() && !mutation.IsGoldenEligible()));
        },
        "!mutation.IsValid() && !mutation.IsGoldenEligible()");
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((mutation.realFrameId == frame.realFrameId && mutation.publicationFrameId == 29));
        },
        "mutation.realFrameId == frame.realFrameId && mutation.publicationFrameId == 29");
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((mutation.viewId == frame.viewId && mutation.sceneEpoch == frame.sceneEpoch));
        },
        "mutation.viewId == frame.viewId && mutation.sceneEpoch == frame.sceneEpoch");
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>(
                (mutation.renderExtent == frame.renderExtent && mutation.displayExtent == frame.displayExtent));
        },
        "mutation.renderExtent == frame.renderExtent && mutation.displayExtent == frame.displayExtent");
    // Runtime native suppression remains factual even when acceptance proof is unknown.
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((mutation.nativeGateActive));
        },
        "mutation.nativeGateActive");
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((mutation.testFaultActive));
        },
        "mutation.testFaultActive");
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((mutation.frameKind == TemporalMeasuredFrameKind::Unknown));
        },
        "mutation.frameKind == TemporalMeasuredFrameKind::Unknown");
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((mutation.resolutionState == TemporalResolutionState::Unknown));
        },
        "mutation.resolutionState == TemporalResolutionState::Unknown");
    mutation.spatialMode = SpatialScalingMode::NisSharpen;
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((!mutation.IsValid() && !mutation.IsGoldenEligible()));
        },
        "!mutation.IsValid() && !mutation.IsGoldenEligible()");

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
        ::ce::contracts::enforce_that(
            [&]() -> bool {
                return static_cast<bool>((effective.nativeCaptureExclusionActive && !effective.settings.enabled));
            },
            "effective.nativeCaptureExclusionActive && !effective.settings.enabled");
        ::ce::contracts::enforce_that(
            [&]() -> bool {
                return static_cast<bool>((effective.settings.requestedUpscaler == TemporalProvider::None));
            },
            "effective.settings.requestedUpscaler == TemporalProvider::None");
        ::ce::contracts::enforce_that(
            [&]() -> bool {
                return static_cast<bool>((effective.settings.requestedFrameGenerator == TemporalProvider::None));
            },
            "effective.settings.requestedFrameGenerator == TemporalProvider::None");
        ::ce::contracts::enforce_that(
            [&]() -> bool {
                return static_cast<bool>((effective.settings.spatialPost.nisMode == SpatialScalingMode::Off));
            },
            "effective.settings.spatialPost.nisMode == SpatialScalingMode::Off");
        ::ce::contracts::enforce_that(
            [&]() -> bool {
                return static_cast<bool>((!effective.settings.spatialPost.deepDvcEnabled));
            },
            "!effective.settings.spatialPost.deepDvcEnabled");
        ::ce::contracts::enforce_that(
            [&]() -> bool {
                return static_cast<bool>((effective.requestedSettings == intent));
            },
            "effective.requestedSettings == intent");
        ::ce::contracts::enforce_that(
            [&]() -> bool {
                return static_cast<bool>((effective.historyResetGeneration > before.historyResetGeneration));
            },
            "effective.historyResetGeneration > before.historyResetGeneration");
        {
            TemporalNativeCaptureExclusion inner;
            intent.requestedUpscaler = TemporalProvider::XeSS;
            runtime.Request(intent); // Concurrent intent must survive both releases.
        }
        ::ce::contracts::enforce_that(
            [&]() -> bool {
                return static_cast<bool>((runtime.Snapshot().nativeCaptureExclusionActive));
            },
            "runtime.Snapshot().nativeCaptureExclusionActive");
        ::ce::contracts::enforce_that(
            [&]() -> bool {
                return static_cast<bool>((runtime.Snapshot().requestedSettings == intent));
            },
            "runtime.Snapshot().requestedSettings == intent");
    }
    const auto restored = runtime.Snapshot();
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((!restored.nativeCaptureExclusionActive && restored.settings == intent));
        },
        "!restored.nativeCaptureExclusionActive && restored.settings == intent");
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((restored.historyResetGeneration > before.historyResetGeneration + 1));
        },
        "restored.historyResetGeneration > before.historyResetGeneration + 1");
    runtime.Request(saved);
}
