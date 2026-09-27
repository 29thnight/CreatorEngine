#pragma once
#include <chrono>
#include <array>
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include "AnimTaskList.h"

// Opt-in, owner-thread sample spanning animation update and proxy publication.
// Worker durations overlap owner wait and each other; never add them to frame time.
// Existing wait/worker totals combine the update and execute groups. Worker span
// includes their intervening barrier; jobs is the bounded update chunk count.
struct AnimationFrameMetrics
{
    double m_prepareUs{}, m_submitUs{}, m_waitUs{}, m_workerSumUs{}, m_workerSpanUs{};
    double m_updatePassWaitUs{}, m_executePassWaitUs{};
    double m_updatePassWorkerSumUs{}, m_executePassWorkerSumUs{};
    double m_publishUs{}, m_socketUs{}, m_updateUs{}, m_syncUs{}, m_renderCommitUs{}, m_paletteUs{};
    std::uint64_t m_jobs{}, m_evaluatedAnimators{}, m_validBones{}, m_localWrites{}, m_paletteCopies{}, m_paletteBytes{}, m_paletteArenaAllocations{};
    std::uint64_t m_updatePassJobs{}, m_executePassJobs{};
    std::uint64_t m_poseStorageGrowths{};
    double m_budgetUs{}, m_predictedPoseUs{}, m_measuredPoseUs{};
    std::uint64_t m_budgetEligible{}, m_budgetDegraded{}, m_budgetOverrun{};
    std::array<std::uint64_t, 8> m_qualityStageCounts{};
};

// Owner-published, immutable live HUD data. The selected recipe is copied
// after both worker groups join; execution ordinals are recorded by the
// executor itself, rather than inferred from the final quality level.
struct AnimationHudTask
{
    std::uint32_t index{};
    animation::task_kind kind{};
    std::uint32_t dependencyA{ animation::invalid_task };
    std::uint32_t dependencyB{ animation::invalid_task };
    std::uint32_t outputSlot{ animation::invalid_task };
    int clipIndex{ -1 };
    std::uint8_t sampleSlot{};
    bool reachable{};
    std::uint32_t executionOrder{ animation::invalid_task };
    std::string bufferOwner{};
};

struct AnimationHudAnimator
{
    std::uint64_t id{};
    std::string name{};
    animation::quality_stage stage{ animation::quality_stage::l0 };
    std::string reason{};
    double predictedUs{};
    double measuredUs{};
    float projectedHeight{};
    bool visible{};
    bool evaluated{};
    bool interpolated{};
};

struct AnimationHudSnapshot
{
    std::uint64_t frame{};
    std::uint64_t registered{};
    std::uint64_t evaluated{};
    std::uint64_t degraded{};
    double budgetUs{};
    double predictedUs{};
    double measuredUs{};
    std::array<std::uint64_t, 8> stages{};
    std::vector<AnimationHudAnimator> animators{};
    std::uint64_t selectedAnimatorId{};
    std::vector<AnimationHudTask> tasks{};
    std::uintptr_t workerPosePool{};
    std::uintptr_t workerCurrentStorage{};
    std::uintptr_t instancePoseStorage{};
    std::size_t workerPoseBuffers{};
    std::size_t instancePoseBuffers{};
};

class AnimationMeasurementScope
{
public:
    using Clock = std::chrono::steady_clock; // MSVC steady_clock uses QPC.
    explicit AnimationMeasurementScope(AnimationFrameMetrics& sample);
    ~AnimationMeasurementScope();
    AnimationMeasurementScope(const AnimationMeasurementScope&) = delete;
    AnimationMeasurementScope& operator=(const AnimationMeasurementScope&) = delete;
    static AnimationFrameMetrics* Current();
    static double Microseconds(Clock::time_point begin, Clock::time_point end)
    { return std::chrono::duration<double, std::micro>(end - begin).count(); }
private:
    AnimationFrameMetrics* m_previous{};
};
