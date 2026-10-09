#pragma once
#include "TemporalReconstruction.h"
#include "SpatialPostEffects.h"

enum class TemporalMeasuredFrameKind : uint8_t { Unknown, Real, Generated };
enum class TemporalResolutionState : uint8_t { Unknown, Native, Reconstructed, NativeFallback, SpatialScaled };

// Copy this value with the submitted/completed frame. Never sample a newer
// global runtime snapshot to annotate an older asynchronous GPU measurement.
struct TemporalMeasurementProvenance
{
    TemporalMeasuredFrameKind frameKind{ TemporalMeasuredFrameKind::Unknown };
    uint64_t realFrameId{ 0 }, viewId{ 0 }, sceneEpoch{ 0 };
    uint32_t generatedOrdinal{ 0 };
    TemporalExtent renderExtent, displayExtent;
    TemporalProvider upscaler{ TemporalProvider::None }, frameGenerator{ TemporalProvider::None };
    SpatialScalingMode spatialMode{ SpatialScalingMode::Off };
    bool deepDvcApplied{ false }; // SDK-accepted dispatch, not pixel/quality validation.
    TemporalResolutionState resolutionState{ TemporalResolutionState::Unknown };
    bool nativeGateActive{ false };
    uint64_t publicationFrameId{ 0 }; // Render publication identity, distinct from GT/SDK realFrameId.
    bool IsValid() const
    {
        return realFrameId != 0 && renderExtent.IsValid() && displayExtent.IsValid() &&
            resolutionState != TemporalResolutionState::Unknown &&
            spatialMode <= SpatialScalingMode::NisSharpen &&
            ((resolutionState == TemporalResolutionState::SpatialScaled) == (spatialMode == SpatialScalingMode::NisScale)) &&
            (resolutionState == TemporalResolutionState::Reconstructed
                ? upscaler != TemporalProvider::None
                : resolutionState == TemporalResolutionState::SpatialScaled
                ? upscaler == TemporalProvider::None && renderExtent.width <= displayExtent.width && renderExtent.height <= displayExtent.height
                : renderExtent == displayExtent && upscaler == TemporalProvider::None) &&
            ((frameKind == TemporalMeasuredFrameKind::Real && generatedOrdinal == 0) ||
             (frameKind == TemporalMeasuredFrameKind::Generated && generatedOrdinal != 0 &&
                frameGenerator != TemporalProvider::None));
    }
    bool IsGoldenEligible() const
    {
        return IsValid() && nativeGateActive && frameKind == TemporalMeasuredFrameKind::Real &&
            upscaler == TemporalProvider::None && frameGenerator == TemporalProvider::None &&
            spatialMode == SpatialScalingMode::Off && !deepDvcApplied &&
            renderExtent == displayExtent && resolutionState == TemporalResolutionState::Native;
    }
};

inline const char* TemporalMeasuredFrameKindName(TemporalMeasuredFrameKind value)
{
    return value == TemporalMeasuredFrameKind::Real ? "real" :
        value == TemporalMeasuredFrameKind::Generated ? "generated" : "unknown";
}
inline const char* TemporalResolutionStateName(TemporalResolutionState value)
{
    switch (value) {
    case TemporalResolutionState::Native: return "native";
    case TemporalResolutionState::Reconstructed: return "reconstructed";
    case TemporalResolutionState::NativeFallback: return "native-fallback";
    case TemporalResolutionState::SpatialScaled: return "spatial-scaled";
    default: return "unknown";
    }
}
inline const char* TemporalMeasuredProviderName(TemporalProvider value)
{
    switch (value) {
    case TemporalProvider::Fsr: return "fsr";
    case TemporalProvider::Dlss: return "dlss";
    case TemporalProvider::XeSS: return "xess";
    default: return "none";
    }
}
