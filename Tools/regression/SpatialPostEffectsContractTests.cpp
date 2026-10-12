// Authored source-only fixture. Not compiled or executed for this change.
// No SDK acceptance, GPU lifetime, latency, performance or pixel claims.
#include "../../Engine/RenderEngine/Render/Temporal/SpatialPostEffects.h"
#include "../../Engine/RenderEngine/Render/Temporal/TemporalUpscalerHost.h"
#include <cassert>
#include <limits>

int main()
{
    SpatialPostSettings options;
    assert(options.nisMode == SpatialScalingMode::Off && !options.deepDvcEnabled);
    assert(ValidateSpatialPostSettings(options));
    options.nisMode = SpatialScalingMode::NisScale;
    options.nisRenderScale = 0.5f;
    assert(ValidateSpatialPostSettings(options));
    options.nisRenderScale = 1.f;
    assert(ValidateSpatialPostSettings(options));
    options.nisRenderScale = 0.49f;
    assert(!ValidateSpatialPostSettings(options));
    options = {};
    options.nisSharpness = std::numeric_limits<float>::quiet_NaN();
    assert(!ValidateSpatialPostSettings(options));
    options = {};
    options.deepDvcIntensity = std::numeric_limits<float>::infinity();
    assert(!ValidateSpatialPostSettings(options));
    options = {};
    options.deepDvcSaturationBoost = -0.1f;
    assert(!ValidateSpatialPostSettings(options));
    options = {};
    options.nisMode = static_cast<SpatialScalingMode>(255);
    assert(!ValidateSpatialPostSettings(options));

    TemporalRuntimeSettings before;
    before.requestedUpscaler = TemporalProvider::Dlss;
    auto after = before;
    after.reflexMode = TemporalLatencyMode::OnPlusBoost;
    after.requestedFrameGenerator = TemporalProvider::Fsr;
    after.spatialPost.nisMode = SpatialScalingMode::NisSharpen;
    after.spatialPost.nisSharpness = 0.75f;
    after.spatialPost.deepDvcEnabled = true;
    after.spatialPost.deepDvcIntensity = 0.8f;
    assert(SameTemporalReconstructionSettings(before, after));
    after.quality = TemporalQuality::NativeAA;
    assert(!SameTemporalReconstructionSettings(before, after));
    after = before;
    after.enabled = false;
    assert(!SameTemporalReconstructionSettings(before, after));
    before.requestedUpscaler = TemporalProvider::None;
    after = before;
    after.enabled = false;
    assert(SameTemporalReconstructionSettings(before, after));
    SpatialPostSnapshot pending;
    assert(!pending.observed && pending.realFrameId == 0);
    assert(pending.activeNisMode == SpatialScalingMode::Off && !pending.deepDvcApplied);
}
