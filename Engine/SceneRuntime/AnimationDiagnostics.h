#pragma once
#include <chrono>
#include <cstdint>

// Opt-in, owner-thread sample spanning animation update and proxy publication.
// Worker durations overlap owner wait and each other; never add them to frame time.
struct AnimationFrameMetrics
{
    double m_prepareUs{}, m_submitUs{}, m_waitUs{}, m_workerSumUs{}, m_workerSpanUs{};
    double m_publishUs{}, m_socketUs{}, m_updateUs{}, m_syncUs{}, m_renderCommitUs{}, m_paletteUs{};
    std::uint64_t m_jobs{}, m_evaluatedAnimators{}, m_validBones{}, m_localWrites{}, m_paletteCopies{}, m_paletteBytes{};
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
