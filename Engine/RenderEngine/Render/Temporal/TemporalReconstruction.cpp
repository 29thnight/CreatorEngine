#include "TemporalReconstruction.h"

#include <cmath>
#include <cstddef>

namespace
{
    bool IsPositiveFinite(float value)
    {
        return std::isfinite(value) && value > 0.0f;
    }

    TemporalResult FindCapability(TemporalProvider provider, TemporalBackend backend,
        std::span<const TemporalCapabilities> capabilities, bool frameGeneration)
    {
        if (provider == TemporalProvider::None) return { TemporalStatus::Success };
        TemporalResult result{ TemporalStatus::NotQueried };
        for (const auto& capability : capabilities)
        {
            if (capability.provider != provider || capability.backend != backend) continue;
            const auto candidate = frameGeneration ? capability.frameGeneration : capability.upscaling;
            // TU and FG adapters can contribute independent snapshots for one
            // provider. An unknown other axis must not hide a real query result.
            if (candidate.status == TemporalStatus::NotQueried) continue;
            if (result.status != TemporalStatus::NotQueried &&
                (result.status != candidate.status || result.nativeCode != candidate.nativeCode))
                return { TemporalStatus::InvalidInput }; // Conflicting/stale snapshots fail closed.
            result = candidate;
        }
        return result;
    }

    TemporalProvider SelectProvider(TemporalProvider requested, TemporalBackend backend,
        std::span<const TemporalCapabilities> capabilities, bool frameGeneration, TemporalResult& requestedResult)
    {
        requestedResult = FindCapability(requested, backend, capabilities, frameGeneration);
        if (requestedResult.IsSuccess()) return requested;
        // A failed optional vendor falls back to the source-available baseline,
        // then native rendering / real presentation. No cross-axis coupling.
        if (requested != TemporalProvider::Fsr &&
            FindCapability(TemporalProvider::Fsr, backend, capabilities, frameGeneration).IsSuccess())
            return TemporalProvider::Fsr;
        return TemporalProvider::None;
    }
}

TemporalResult ValidateTemporalFrame(const TemporalFrame& frame)
{
    if (frame.realFrameId == 0 || !frame.renderExtent.IsValid() || !frame.displayExtent.IsValid() ||
        frame.renderExtent.width > frame.displayExtent.width ||
        frame.renderExtent.height > frame.displayExtent.height ||
        !std::isfinite(frame.jitterX) || !std::isfinite(frame.jitterY) ||
        std::abs(frame.jitterX) > 0.5f || std::abs(frame.jitterY) > 0.5f ||
        !std::isfinite(frame.previousJitterX) || !std::isfinite(frame.previousJitterY) ||
        std::abs(frame.previousJitterX) > 0.5f || std::abs(frame.previousJitterY) > 0.5f ||
        !std::isfinite(frame.motionVectorScaleX) || !std::isfinite(frame.motionVectorScaleY) ||
        frame.motionVectorScaleX == 0.0f || frame.motionVectorScaleY == 0.0f ||
        !IsPositiveFinite(frame.frameTimeMilliseconds) ||
        (frame.camera.orthographicProjection ? !std::isfinite(frame.cameraNear) : !IsPositiveFinite(frame.cameraNear)) ||
        !std::isfinite(frame.cameraVerticalFov) || frame.cameraVerticalFov >= 3.141592654f ||
        (frame.camera.orthographicProjection ? frame.cameraVerticalFov < 0.0f : frame.cameraVerticalFov <= 0.0f) ||
        !IsPositiveFinite(frame.preExposure) || !IsPositiveFinite(frame.viewSpaceToMeters))
        return { TemporalStatus::InvalidInput };
    // Infinite projection uses depthInfinite instead of an IEEE infinity value;
    // providers translate it to their SDK's own near/far convention.
    if (!frame.depthInfinite &&
        ((frame.camera.orthographicProjection ? !std::isfinite(frame.cameraFar) : !IsPositiveFinite(frame.cameraFar)) ||
            frame.cameraFar <= frame.cameraNear))
        return { TemporalStatus::InvalidInput };
    return { TemporalStatus::Success };
}

TemporalResult ValidateTemporalCamera(const TemporalCamera& camera)
{
    if (!camera.valid || !IsPositiveFinite(camera.aspectRatio)) return { TemporalStatus::InvalidInput };
    const auto validMatrix = [](const std::array<float, 16>& matrix)
    {
        bool nonzero = false;
        for (float value : matrix)
        {
            if (!std::isfinite(value)) return false;
            nonzero = nonzero || value != 0.0f;
        }
        return nonzero;
    };
    if (!validMatrix(camera.viewMatrix) || !validMatrix(camera.projectionMatrix) ||
        !validMatrix(camera.cameraViewToClip) || !validMatrix(camera.clipToCameraView) ||
        !validMatrix(camera.clipToPreviousClip) || !validMatrix(camera.previousClipToClip))
        return { TemporalStatus::InvalidInput };
    for (size_t index = 0; index < camera.position.size(); ++index)
        if (!std::isfinite(camera.position[index]) || !std::isfinite(camera.up[index]) ||
            !std::isfinite(camera.right[index]) || !std::isfinite(camera.forward[index]))
            return { TemporalStatus::InvalidInput };
    const auto dot = [](const std::array<float, 3>& left, const std::array<float, 3>& right)
    {
        return left[0] * right[0] + left[1] * right[1] + left[2] * right[2];
    };
    constexpr float kBasisTolerance = 0.001f;
    if (std::abs(dot(camera.up, camera.up) - 1.0f) > kBasisTolerance ||
        std::abs(dot(camera.right, camera.right) - 1.0f) > kBasisTolerance ||
        std::abs(dot(camera.forward, camera.forward) - 1.0f) > kBasisTolerance ||
        std::abs(dot(camera.up, camera.right)) > kBasisTolerance ||
        std::abs(dot(camera.up, camera.forward)) > kBasisTolerance ||
        std::abs(dot(camera.right, camera.forward)) > kBasisTolerance)
        return { TemporalStatus::InvalidInput };
    return { TemporalStatus::Success };
}

TemporalResult ValidateTemporalUpscaleInputs(const TemporalUpscaleInputs& inputs)
{
    if (inputs.frame.camera.orthographicProjection) return { TemporalStatus::ProjectionUnsupported };
    const auto frameResult = ValidateTemporalFrame(inputs.frame);
    if (!frameResult.IsSuccess()) return frameResult;
    if (!inputs.color.IsValid() || !inputs.depth.IsValid() || !inputs.motionVectors.IsValid() ||
        !inputs.output.IsValid() || inputs.output == inputs.color || inputs.output == inputs.depth ||
        inputs.output == inputs.motionVectors || inputs.output == inputs.exposure ||
        inputs.output == inputs.reactiveMask || inputs.output == inputs.transparencyMask ||
        inputs.output == inputs.responsiveMask)
        return { TemporalStatus::InvalidInput };
    return { TemporalStatus::Success };
}

TemporalResult ValidateTemporalFrameGenerationConfig(const TemporalFrameGenerationConfig& config)
{
    if (config.target == TemporalPresentationTarget::EditorViewport)
        return { TemporalStatus::EditorViewportForbidden };
    if ((config.transferFunction != TemporalTransferFunction::SRGB &&
            config.transferFunction != TemporalTransferFunction::PQ &&
            config.transferFunction != TemporalTransferFunction::Linear) ||
        config.target != TemporalPresentationTarget::PlayerSwapchain || !config.displayExtent.IsValid() ||
        config.interpolatedFrameCount == 0 || !std::isfinite(config.minLuminance) ||
        config.minLuminance < 0.0f || !IsPositiveFinite(config.maxLuminance) ||
        config.maxLuminance <= config.minLuminance)
        return { TemporalStatus::InvalidInput };
    return { TemporalStatus::Success };
}

TemporalResult ValidateTemporalFrameGenerationInputs(const TemporalFrameGenerationInputs& inputs)
{
    if (inputs.frame.camera.orthographicProjection) return { TemporalStatus::ProjectionUnsupported };
    const auto frameResult = ValidateTemporalFrame(inputs.frame);
    if (!frameResult.IsSuccess()) return frameResult;
    if (!inputs.lifetimeToken || !inputs.hudlessColor.IsValid() || !inputs.depth.IsValid() ||
        !inputs.motionVectors.IsValid() || inputs.hudlessColor == inputs.depth ||
        inputs.hudlessColor == inputs.motionVectors || inputs.depth == inputs.motionVectors ||
        inputs.uiColor == inputs.hudlessColor || inputs.uiColor == inputs.depth ||
        inputs.uiColor == inputs.motionVectors)
        return { TemporalStatus::InvalidInput };
    return { TemporalStatus::Success };
}

TemporalSelection SelectTemporalProviders(TemporalProvider requestedUpscaler,
    TemporalProvider requestedFrameGenerator, TemporalBackend backend,
    std::span<const TemporalCapabilities> capabilities)
{
    TemporalSelection result;
    result.upscaler = SelectProvider(requestedUpscaler, backend, capabilities, false, result.requestedUpscaler);
    result.frameGenerator = SelectProvider(requestedFrameGenerator, backend, capabilities, true,
        result.requestedFrameGenerator);
    return result;
}

TemporalJitterOffset SampleTemporalJitter(uint64_t sequenceIndex, uint64_t seed)
{
    const auto halton = [](uint64_t index, uint32_t base)
    {
        float value = 0.0f, factor = 1.0f;
        for (; index; index /= base)
        {
            factor /= static_cast<float>(base);
            value += factor * static_cast<float>(index % base);
        }
        return value;
    };
    const uint64_t index = ((sequenceIndex % 1024) + (seed % 1024)) % 1024 + 1;
    return { halton(index, 2) - 0.5f, halton(index, 3) - 0.5f };
}
