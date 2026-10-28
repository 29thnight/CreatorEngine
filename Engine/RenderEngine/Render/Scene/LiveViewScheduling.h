#include "../../../Containers/ContainerAPI.h"
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

enum class LiveViewUpdateMode : uint8_t
{
    Automatic,
    Continuous,
    FixedRate,
    OnDemand,
};

struct LiveViewUpdatePolicy
{
    LiveViewUpdateMode mode{LiveViewUpdateMode::Automatic};
    uint32_t framesPerSecond{};
    // 0은 표시 대상의 기본 우선순위를 쓴다. 큰 값부터 승인한다.
    uint32_t priority{};
    uint32_t maximumDeferralMilliseconds{250};

    bool operator==(const LiveViewUpdatePolicy&) const = default;
};

struct LiveViewScheduleRequest
{
    uint64_t viewId{};
    uint64_t historyRevision{};
    uint64_t sceneEpoch{};
    uint64_t resizeGeneration{};
    uint64_t contentRevision{};
    uint64_t cameraCutRevision{};
    LiveViewUpdatePolicy policy{};
    // Editor=0, Game=1, MaterialPreview=2. Host에서 명시한 표시 대상이다.
    uint32_t target{};
    bool force{};
};

// 픽셀 작업만 승인한다. Scene의 수명 delta 소비와 GPU/Host lease 회수는
// 이 정책 밖에서 항상 진행한다. 성공한 native 제출만 갱신 주기를 소비한다.
class LiveViewScheduler
{
  public:
    static constexpr uint32_t Capacity = 3;
    static constexpr uint32_t GpuBudget = 2;

    struct Plan
    {
        ce::fixed_array<uint32_t, Capacity> order{};
        ce::fixed_array<bool, Capacity> due{};
        uint32_t count{};
        uint64_t scheduledAt{};
    };

    static LiveViewUpdatePolicy ResolvePolicy(const LiveViewScheduleRequest& request)
    {
        auto policy = request.policy;
        if (policy.mode == LiveViewUpdateMode::Automatic)
        {
            policy.mode = request.target == 2   ? LiveViewUpdateMode::OnDemand
                          : request.target == 0 ? LiveViewUpdateMode::FixedRate
                                                : LiveViewUpdateMode::Continuous;
            if (policy.mode == LiveViewUpdateMode::FixedRate && policy.framesPerSecond == 0)
            {
                policy.framesPerSecond = 60;
            }
        }
        if (policy.priority == 0)
        {
            policy.priority = request.target == 1 ? 300 : request.target == 0 ? 200 : 100;
        }
        return policy;
    }

    bool IsDue(const LiveViewScheduleRequest& request, uint64_t now) const
    {
        if (request.viewId == 0 || request.target >= Capacity)
        {
            return false;
        }
        const auto& state = m_states[request.target];
        if (request.force || !state.submitted || !Matches(state.request, request))
        {
            return true;
        }
        const auto policy = ResolvePolicy(request);
        if (policy.mode == LiveViewUpdateMode::OnDemand)
        {
            return false;
        }
        if (policy.mode != LiveViewUpdateMode::FixedRate || policy.framesPerSecond == 0)
        {
            return true;
        }
        const uint64_t interval = 1000000000ull / policy.framesPerSecond;
        // Accept bounded host-tick jitter without moving the fixed-rate deadline.
        const uint64_t tolerance = (std::min)(interval / 16, uint64_t{1000000});
        if (now < state.submittedAt)
        {
            return true;
        }
        return now - state.submittedAt >= interval - tolerance &&
               (now >= state.nextDueAt || state.nextDueAt - now <= tolerance);
    }

    Plan BuildPlan(const ce::fixed_array<LiveViewScheduleRequest, Capacity>& requests, uint32_t count, uint64_t now)
    {
        Plan plan;
        plan.scheduledAt = now;
        plan.count = (std::min)(count, Capacity);
        ce::fixed_array<bool, Capacity> overdue{};
        for (uint32_t index = 0; index < plan.count; ++index)
        {
            plan.order[index] = index;
            const auto& request = requests[index];
            plan.due[index] = IsDue(request, now);
            if (request.target >= Capacity)
            {
                continue;
            }
            auto& state = m_states[request.target];
            if (!plan.due[index])
            {
                state.waiting = false;
                continue;
            }
            // 갱신 입력이 계속 바뀌더라도 대기 시작 시각을 밀지 않는다.
            // 한 credit만 반복해서 열릴 때에도 낮은 우선순위 뷰가 진행한다.
            if (!state.waiting || state.waitingViewId != request.viewId)
            {
                state.waiting = true;
                state.waitingViewId = request.viewId;
                state.waitingSince = now;
            }
            const auto policy = ResolvePolicy(request);
            overdue[index] = policy.maximumDeferralMilliseconds != 0 && now >= state.waitingSince &&
                             now - state.waitingSince >= uint64_t{policy.maximumDeferralMilliseconds} * 1000000ull;
        }
        const auto before = [&](uint32_t left, uint32_t right)
        {
            if (plan.due[left] != plan.due[right])
            {
                return plan.due[left];
            }
            if (overdue[left] != overdue[right])
            {
                return overdue[left];
            }
            if (overdue[left] && requests[left].target < Capacity && requests[right].target < Capacity)
            {
                const auto leftWait = m_states[requests[left].target].waitingSince;
                const auto rightWait = m_states[requests[right].target].waitingSince;
                if (leftWait != rightWait)
                {
                    return leftWait < rightWait;
                }
            }
            return ResolvePolicy(requests[left]).priority > ResolvePolicy(requests[right]).priority;
        };
        // 뷰 상한은 셋이다. stable_sort의 임시 heap 없이 안정 순서를 만든다.
        for (uint32_t index = 1; index < plan.count; ++index)
        {
            const uint32_t candidate = plan.order[index];
            uint32_t position = index;
            while (position != 0 && before(candidate, plan.order[position - 1]))
            {
                plan.order[position] = plan.order[position - 1];
                --position;
            }
            plan.order[position] = candidate;
        }
        return plan;
    }

    void MarkSubmitted(const LiveViewScheduleRequest& request, uint64_t now)
    {
        if (request.target >= Capacity || request.viewId == 0)
        {
            return;
        }
        auto& state = m_states[request.target];
        const auto policy = ResolvePolicy(request);
        if (policy.mode == LiveViewUpdateMode::FixedRate && policy.framesPerSecond != 0)
        {
            const uint64_t interval = (std::max)(uint64_t{1}, 1000000000ull / policy.framesPerSecond);
            if (!state.submitted || !Matches(state.request, request) || request.force || now < state.submittedAt)
            {
                state.nextDueAt = now + interval;
            }
            else if (now >= state.nextDueAt)
            {
                // Skip missed deadlines; never queue a burst of catch-up frames.
                state.nextDueAt += ((now - state.nextDueAt) / interval + 1) * interval;
            }
            else
            {
                state.nextDueAt += interval;
            }
        }
        state.request = request;
        state.submittedAt = now;
        state.submitted = true;
        state.waiting = false;
    }

    void Reset() { m_states = {}; }

    static bool HasGpuCredit(uint32_t pending) { return pending < GpuBudget; }

  private:
    struct State
    {
        LiveViewScheduleRequest request{};
        uint64_t submittedAt{};
        uint64_t nextDueAt{};
        uint64_t waitingSince{};
        uint64_t waitingViewId{};
        bool submitted{};
        bool waiting{};
    };

    static bool Matches(const LiveViewScheduleRequest& left, const LiveViewScheduleRequest& right)
    {
        return left.viewId == right.viewId && left.historyRevision == right.historyRevision &&
               left.sceneEpoch == right.sceneEpoch && left.resizeGeneration == right.resizeGeneration &&
               left.contentRevision == right.contentRevision && left.cameraCutRevision == right.cameraCutRevision &&
               left.policy == right.policy;
    }

    ce::fixed_array<State, Capacity> m_states{};
};
