#include "../../Engine/RenderEngine/Render/Scene/LiveViewScheduling.h"

#include <cstdio>

namespace
{
    unsigned failures{};

    void Expect(bool condition, const char* message)
    {
        if (!condition)
        {
            std::fprintf(stderr, "FAIL: %s\n", message);
            ++failures;
        }
    }

    std::array<LiveViewScheduleRequest, LiveViewScheduler::Capacity> Requests()
    {
        std::array<LiveViewScheduleRequest, LiveViewScheduler::Capacity> result{};
        for (uint32_t index = 0; index < result.size(); ++index)
        {
            result[index].viewId = index + 1;
            result[index].target = index;
            result[index].sceneEpoch = 1;
            result[index].resizeGeneration = 1;
        }
        return result;
    }
}

int main()
{
    LiveViewScheduler scheduler;
    auto requests = Requests();
    constexpr uint64_t start = 1000000000;
    auto plan = scheduler.BuildPlan(requests, 3, start);
    Expect(plan.count == 3 && plan.order[0] == 1, "Game has priority over Scene and preview");
    Expect(plan.due[0] && plan.due[1] && plan.due[2], "initial outputs are all due");
    Expect(LiveViewScheduler::HasGpuCredit(0) && LiveViewScheduler::HasGpuCredit(1),
        "a single free credit admits one view without reserving a pair");
    Expect(!LiveViewScheduler::HasGpuCredit(2) && !LiveViewScheduler::HasGpuCredit(3),
        "view policy cannot increase the shared GPU budget");

    scheduler.MarkSubmitted(requests[0], start);
    scheduler.MarkSubmitted(requests[1], start);
    scheduler.MarkSubmitted(requests[2], start);
    plan = scheduler.BuildPlan(requests, 3, start + 1000000);
    Expect(!plan.due[0] && plan.due[1] && !plan.due[2],
        "Scene cadence and preview dirtiness are independent of Game");
    Expect(scheduler.IsDue(requests[0], start + 16666667), "default Scene cadence reaches 60 Hz");
    Expect(!scheduler.IsDue(requests[2], start + 1000000000), "unchanged preview stays cached");

    ++requests[2].contentRevision;
    Expect(scheduler.IsDue(requests[2], start + 2000000), "dirty preview retries until a submission succeeds");
    Expect(scheduler.IsDue(requests[2], start + 3000000), "failed or skipped attempts do not consume dirtiness");
    scheduler.MarkSubmitted(requests[2], start + 3000000);
    Expect(!scheduler.IsDue(requests[2], start + 4000000), "successful preview consumes its exact revision");
    ++requests[2].historyRevision;
    Expect(scheduler.IsDue(requests[2], start + 4000000), "camera history changes invalidate cached output");
    scheduler.MarkSubmitted(requests[2], start + 4000000);
    ++requests[2].sceneEpoch;
    Expect(scheduler.IsDue(requests[2], start + 5000000), "scene replacement invalidates cached output");
    scheduler.MarkSubmitted(requests[2], start + 5000000);
    ++requests[2].resizeGeneration;
    Expect(scheduler.IsDue(requests[2], start + 6000000), "resize invalidates cached output");
    scheduler.MarkSubmitted(requests[2], start + 6000000);
    ++requests[2].cameraCutRevision;
    Expect(scheduler.IsDue(requests[2], start + 7000000), "explicit cuts invalidate cached output");
    scheduler.MarkSubmitted(requests[2], start + 7000000);
    requests[2].force = true;
    Expect(scheduler.IsDue(requests[2], start + 7000001), "diagnostic captures bypass update throttles");
    requests[2].force = false;

    scheduler.Reset();
    requests = Requests();
    requests[0].policy.mode = LiveViewUpdateMode::FixedRate;
    requests[0].policy.framesPerSecond = 30;
    scheduler.MarkSubmitted(requests[0], start);
    Expect(!scheduler.IsDue(requests[0], start + 20000000), "Host can set Scene to 30 Hz");
    Expect(scheduler.IsDue(requests[0], start + 33333334), "30 Hz becomes due after its interval");
    Expect(scheduler.IsDue(requests[0], start - 1), "clock rollback cannot permanently stall a view");

    // 매 틱 credit이 하나뿐이어도 계속 바뀌는 Game 입력이 다른 뷰를 굶기지 않는다.
    scheduler.Reset();
    requests = Requests();
    plan = scheduler.BuildPlan(requests, 3, start);
    scheduler.MarkSubmitted(requests[1], start);
    for (uint32_t tick = 1; tick < 25; ++tick)
    {
        ++requests[1].contentRevision;
        plan = scheduler.BuildPlan(requests, 3, start + tick * 10000000ull);
        Expect(plan.order[0] == 1, "Game remains first inside the bounded deferral window");
        scheduler.MarkSubmitted(requests[1], start + tick * 10000000ull);
    }
    plan = scheduler.BuildPlan(requests, 3, start + 250000000);
    Expect(plan.order[0] == 0, "overdue Scene receives the next credit");
    scheduler.MarkSubmitted(requests[0], start + 250000000);
    plan = scheduler.BuildPlan(requests, 3, start + 260000000);
    Expect(plan.order[0] == 2, "overdue preview then makes progress");

    requests[0].viewId = 0;
    requests[1].target = 99;
    plan = scheduler.BuildPlan(requests, 100, start + 270000000);
    Expect(plan.count == LiveViewScheduler::Capacity && !plan.due[0] && !plan.due[1],
        "invalid identities and oversized counts stay within bounded storage");
    scheduler.Reset();
    Expect(scheduler.IsDue(requests[2], start), "backend rebuild invalidates previous admissions");
    std::printf("LiveViewScheduling: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
