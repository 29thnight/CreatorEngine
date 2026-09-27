#pragma once
#include "AnimationDiagnostics.h"
#include <cstdint>
#include <string>
#include <vector>
namespace RenderTest
{
    struct AnimationBaselineReport
    {
        std::size_t m_actors{}, m_bones{}, m_sceneBones{}, m_skinnedMeshes{}, m_workers{};
        int m_qualityStage{ -1 };
        double m_budgetMs{};
        std::uint32_t m_lowDetailBoneCount{};
        std::vector<AnimationFrameMetrics> m_frames;
    };
    // CPU baseline only: commit real palette snapshots, discard batches after timing.
    bool RunAnimationBaselineProbe(const std::string& modelPath, std::size_t actors,
        int qualityStage,
        AnimationBaselineReport& report, std::string& error);
    bool RunAnimationBudgetProbe(const std::string& modelPath, std::size_t actors,
        double budgetMs, AnimationBaselineReport& report, std::string& error);
}
