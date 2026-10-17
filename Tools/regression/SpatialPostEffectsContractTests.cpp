// Authored source-only fixture. Not compiled or executed for this change.
// No SDK acceptance, GPU lifetime, latency, performance or pixel claims.
#include "../../Engine/RenderEngine/Render/Temporal/SpatialPostEffects.h"
#include "../../Engine/RenderEngine/Render/Temporal/TemporalUpscalerHost.h"
#include "../../Engine/Utility_Framework/Contract.h"
#include "../../Engine/Utility_Framework/StandardLibrary.h"

int main()
{
    SpatialPostSettings options;
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((options.nisMode == SpatialScalingMode::Off && !options.deepDvcEnabled));
        },
        "options.nisMode == SpatialScalingMode::Off && !options.deepDvcEnabled");
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((ValidateSpatialPostSettings(options)));
        },
        "ValidateSpatialPostSettings(options)");
    options.nisMode = SpatialScalingMode::NisScale;
    options.nisRenderScale = 0.5f;
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((ValidateSpatialPostSettings(options)));
        },
        "ValidateSpatialPostSettings(options)");
    options.nisRenderScale = 1.f;
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((ValidateSpatialPostSettings(options)));
        },
        "ValidateSpatialPostSettings(options)");
    options.nisRenderScale = 0.49f;
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((!ValidateSpatialPostSettings(options)));
        },
        "!ValidateSpatialPostSettings(options)");
    options = {};
    options.nisSharpness = std::numeric_limits<float>::quiet_NaN();
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((!ValidateSpatialPostSettings(options)));
        },
        "!ValidateSpatialPostSettings(options)");
    options = {};
    options.deepDvcIntensity = std::numeric_limits<float>::infinity();
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((!ValidateSpatialPostSettings(options)));
        },
        "!ValidateSpatialPostSettings(options)");
    options = {};
    options.deepDvcSaturationBoost = -0.1f;
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((!ValidateSpatialPostSettings(options)));
        },
        "!ValidateSpatialPostSettings(options)");
    options = {};
    options.nisMode = static_cast<SpatialScalingMode>(255);
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((!ValidateSpatialPostSettings(options)));
        },
        "!ValidateSpatialPostSettings(options)");

    TemporalRuntimeSettings before;
    before.requestedUpscaler = TemporalProvider::Dlss;
    auto after = before;
    after.reflexMode = TemporalLatencyMode::OnPlusBoost;
    after.requestedFrameGenerator = TemporalProvider::Fsr;
    after.spatialPost.nisMode = SpatialScalingMode::NisSharpen;
    after.spatialPost.nisSharpness = 0.75f;
    after.spatialPost.deepDvcEnabled = true;
    after.spatialPost.deepDvcIntensity = 0.8f;
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((SameTemporalReconstructionSettings(before, after)));
        },
        "SameTemporalReconstructionSettings(before, after)");
    after.quality = TemporalQuality::NativeAA;
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((!SameTemporalReconstructionSettings(before, after)));
        },
        "!SameTemporalReconstructionSettings(before, after)");
    after = before;
    after.enabled = false;
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((!SameTemporalReconstructionSettings(before, after)));
        },
        "!SameTemporalReconstructionSettings(before, after)");
    before.requestedUpscaler = TemporalProvider::None;
    after = before;
    after.enabled = false;
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((SameTemporalReconstructionSettings(before, after)));
        },
        "SameTemporalReconstructionSettings(before, after)");
    SpatialPostSnapshot pending;
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((!pending.observed && pending.realFrameId == 0));
        },
        "!pending.observed && pending.realFrameId == 0");
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((pending.activeNisMode == SpatialScalingMode::Off && !pending.deepDvcApplied));
        },
        "pending.activeNisMode == SpatialScalingMode::Off && !pending.deepDvcApplied");
}
