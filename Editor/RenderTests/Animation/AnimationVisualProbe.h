#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace RenderTest
{
    struct AnimationVisualReport
    {
        std::string modelId, markerMeshId, bone;
        std::array<float, 3> socketPosition{};
        std::uint32_t paletteDigest{};
        std::size_t skinnedMeshes{};
    };
    // Commandlet fixture only. Paused Play mode holds the simulation while the
    // ordinary renderer consumes the pose published by AnimationJob.
    bool RunAnimationVisualProbe(const std::string& action, const std::string& modelPath,
        AnimationVisualReport& report, std::string& error);
}
