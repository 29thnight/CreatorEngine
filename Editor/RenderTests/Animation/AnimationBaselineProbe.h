#pragma once
#include "AnimationDiagnostics.h"
#include <string>
#include <vector>
namespace RenderTest
{
    struct AnimationBaselineReport
    {
        std::size_t m_actors{}, m_bones{}, m_sceneBones{}, m_skinnedMeshes{}, m_workers{};
        std::vector<AnimationFrameMetrics> m_frames;
    };
    // CPU baseline only: commit real palette snapshots, discard batches after timing.
    bool RunAnimationBaselineProbe(const std::string& modelPath, std::size_t actors,
        AnimationBaselineReport& report, std::string& error);
}
