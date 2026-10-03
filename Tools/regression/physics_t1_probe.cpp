#if defined(CE_PHYSICS_WAKE_TEST)
extern "C" bool ce_physics_wake_probe();
#endif
#include "../../Engine/SceneRuntime/ScenePhysicsSimulation.h"
#include "../../Engine/SceneRuntime/PhysicsTransformPolicy.h"
#include <array>
#include <cmath>
#include <iostream>
#include <cstdlib>
#include <map>
#include <array>
#include "../../Engine/Physics/PhysicsTestHooks.h"
#if !CE_SHIPPING
#include "../../Engine/EngineDiagnostics/ProfileScope.h"
#include "../../Engine/EngineDiagnostics/ProfileCaptureFile.h"
#endif

using namespace ce::physics;
namespace {
int checks = 0;
void check(bool value, const char* message) {
    ++checks;
    if (!value) { std::cerr << "B2 failure: " << message << '\n'; std::exit(1); }
}
bool close(float a, float b) { return std::abs(a-b) < .001f; }
bool exercise(execution_preference preference) {
    ScenePhysicsSimulation session;
    body_definition moving;
    moving.properties.kind = body_kind::dynamic;
    moving.properties.gravity_enabled = false;
    moving.properties.linear_velocity = {6, 0, 0};
    moving.properties.initial_pose.position = {0, 10, 0};
    moving.shapes.push_back(ShapeInstance{});
    auto dynamic = session.Register(moving, true);
    check(bool(dynamic), "dynamic registration");
    auto fixed = moving;
    fixed.properties.kind = body_kind::static_body;
    fixed.properties.initial_pose.position = {0, -10, 0};
    auto stationary = session.Register(fixed, true);
    check(bool(stationary), "static registration");
    auto kinematic = fixed;
    kinematic.properties.kind = body_kind::kinematic;
    kinematic.properties.initial_pose.position = {0, 20, 0};
    auto target = session.Register(kinematic, true);
    check(bool(target), "kinematic registration");
    check(!session.SetPose(*dynamic, {}), "Editor rejects runtime pose");
    scene_config config;
    config.execution = preference;
    check(bool(session.Start(config)), "session start");
    bool gpu = session.Runtime()->status().backend == execution_backend::gpu;
    auto step = session.Advance(session.fixed_seconds);
    check(step && *step == 1, "one fixed tick");
    check(close(session.Read(*dynamic)->transform.position.x, .1f), "completed authoritative pose");
    check(close(session.RenderPose(*dynamic)->position.x, 0), "alpha zero uses previous pose");
    check(session.RenderPoses().size() == 1, "static body absent from render work");
    auto half = session.Advance(session.fixed_seconds*.5);
    check(half && *half == 0, "no tick frame");
    check(close(session.RenderPose(*dynamic)->position.x, .05f), "no tick frame interpolates midpoint");
    check(close(session.Read(*dynamic)->transform.position.x, .1f), "render interpolation does not alter SDK state");
    check(session.ChangedPoses().empty(), "no tick frame makes no Transform writes");
    pose jump{{4, 10, 0}, {0,0,0,1}};
    check(bool(session.SetPose(*dynamic, jump)), "dynamic teleport");
    check(close(session.RenderPose(*dynamic)->position.x, 4), "teleport resets history");
    pose ground{{3, -10, 0}, {0,0,0,1}};
    check(bool(session.SetPose(*stationary, ground)), "static explicit move");
    check(close(session.Read(*stationary)->transform.position.x, 3), "static query state updates immediately");
    pose goal{{2, 20, 0}, {0,0,0,1}};
    check(bool(session.SetKinematicTarget(*target, goal)), "kinematic target request");
    check(close(session.Read(*target)->transform.position.x, 0), "target is not completed state");
    auto multiple = session.Advance(session.fixed_seconds*2);
    check(multiple && *multiple == 2, "multi tick frame");
    check(close(session.Read(*target)->transform.position.x, 2), "kinematic target completed");
    check(close(session.RenderPose(*dynamic)->position.x, 4.15f), "multi tick keeps last two completed poses");
    check(!session.SetKinematicTarget(*dynamic, goal), "dynamic rejects kinematic target");
    check(bool(session.SetVelocity(*dynamic, {}, {})), "stop moving body");
    for (int i=0; i<200; ++i)
        check(bool(session.Advance(session.fixed_seconds)), "sleep settling tick");
    check(session.RenderPoses().empty(), "sleeping bodies leave render work list");
    check(bool(session.SetEnabled(*dynamic, false)), "disable");
    check(close(session.RenderPose(*dynamic)->position.x, session.Read(*dynamic)->transform.position.x), "disabled history resets");
    check(!session.SetPose(*dynamic, jump), "disabled rejects teleport");
    check(bool(session.Unregister(*dynamic)), "unregister");
    check(!session.RenderPose(*dynamic), "stale render binding rejects");
#if defined(CE_PHYSICS_TESTING)
    const auto failureBody = session.Register(moving, true);
    check(bool(failureBody) && bool(session.Advance(session.fixed_seconds)), "moving failure fixture");
    const auto beforeFailure = std::vector<ScenePhysicsSimulation::render_pose>(session.RenderPoses().begin(), session.RenderPoses().end());
    test::next_failure.store(test::failure_point::step_fetch);
    check(!session.Advance(session.fixed_seconds), "injected fetch failure");
    check(session.RenderPoses().size() == beforeFailure.size(), "failed overlap retains published list");
    for (std::size_t i = 0; i < beforeFailure.size(); ++i)
        check(session.RenderPoses()[i].binding == beforeFailure[i].binding &&
              session.RenderPoses()[i].previous.position == beforeFailure[i].previous.position &&
              session.RenderPoses()[i].current.position == beforeFailure[i].current.position,
              "failed overlap does not publish staged history");
#endif
    check(bool(session.Stop()), "stop");
    check(session.RenderPoses().empty() && session.InterpolationAlpha()==0, "Stop clears history and alpha");
    check(bool(session.Start(config)) && close(session.Read(*stationary)->transform.position.x,0), "replay restores authored static pose");
    check(bool(session.Stop()), "final teardown");
    return gpu;
}
}
void workload(execution_preference preference, std::uint32_t count)
{
    ScenePhysicsSimulation session;
    body_definition definition;
    definition.properties.kind = body_kind::dynamic;
    definition.properties.gravity_enabled = false;
    definition.properties.linear_velocity = {1, 0, 0};
    definition.properties.linear_damping = 0;
    definition.shapes.push_back(ShapeInstance{});
    for (std::uint32_t i = 0; i < count; ++i)
    {
        definition.properties.initial_pose.position = {float(i % 32) * 3, 10, float(i / 32) * 3};
        check(bool(session.Register(definition, true)), "workload register");
    }
    scene_config config;
    config.workers = 2;
    config.execution = preference;
    check(bool(session.Start(config)), "workload start");
    for (int i = 0; i < 60; ++i)
    {
        auto advanced = session.Advance(session.fixed_seconds);
        check(advanced && *advanced == 1 && session.RenderPoses().size() == count,
              "all active history candidates merge exactly once");
    }
    check(bool(session.Stop()), "workload stop");
}

int main(int argc, char** argv) {
#if !CE_SHIPPING
    auto& profiler = ce::profiler();
    profiler.initialize();
    profiler.register_thread("PhysicsB2Probe", ce::track_kind::game_thread);
    profiler.record(1);
#endif
    const auto rotation = math::quaternion_from_axis_angle(math::vector3::unit_y(), .5f);
    const auto parent = math::compose(math::vector3{2,2,2}, rotation, math::vector3{3,4,5});
    const auto local = math::compose(math::vector3{1,1,1}, math::quaternion{0,0,0,1}, math::vector3{1,0,0});
    auto captured = CapturePhysicsTransform(local*parent);
    check(captured && close(captured->scale.x,2), "rotated scaled parent accepted");
    const auto shear = math::compose(math::vector3{1,1,1}, rotation, math::vector3{}) *
                       math::compose(math::vector3{2,1,1}, math::quaternion{0,0,0,1}, math::vector3{});
    check(!CapturePhysicsTransform(shear), "parent induced shear rejected");
    exercise(execution_preference::cpu);
    const auto gpu = exercise(execution_preference::prefer_gpu);
    for (const auto count : {64u, 256u, 1024u})
    {
        workload(execution_preference::cpu, count);
        workload(execution_preference::prefer_gpu, count);
    }
#if !CE_SHIPPING
    profiler.publish_frame(1);
    profiler.pause();
    profiler.wait_until_idle();
    const auto capture = profiler.capture();
    check(capture && capture->complete() && capture->unacked_streams()==0, "profiling capture complete");
    std::map<std::pair<std::uint64_t, std::uint64_t>, std::array<const ce::profile_event*, 3>> stages;
    std::vector<const ce::profile_event*> workers;
    for (const auto& frame : capture->frames())
        for (const auto& event : frame.events)
        {
            const auto name = capture->marker(event.marker).name;
            if (name == "Physics.PhysXTask") workers.push_back(&event);
            const auto index = name == "Physics.SimulateSubmit" ? 0 :
                               (name == "Physics.InFlightRenderPrepare" || name == "Physics.RenderPrepare") ? 1 : name == "Physics.FetchWait" ? 2 : -1;
            if (index >= 0) stages[{event.cpu.session, event.cpu.tick}][index] = &event;
        }
    for (const auto name : {"Physics.FetchResults", "Physics.DispatcherDrain",
                            "Physics.SnapshotStatistics", "Physics.RenderMerge"})
    {
        bool observed = false;
        for (const auto& frame : capture->frames())
            for (const auto& event : frame.events)
                if (capture->marker(event.marker).name == name)
                {
                    observed = true;
                    check(event.cpu.session != 0 && event.cpu.tick != 0 && event.tick_end >= event.tick_begin,
                          "cost span has scene/tick identity");
                }
        check(observed, "cost separation marker recorded");
    }

    std::array<double, 3> milliseconds{};
    std::size_t measured = 0, concurrent = 0;
    for (const auto& [identity, stage] : stages)
    {
        if (!stage[0] || !stage[1] || !stage[2]) continue;
        const bool overlapped = capture->marker(stage[1]->marker).name == "Physics.InFlightRenderPrepare";
        check(overlapped ? (stage[0]->tick_end <= stage[1]->tick_begin && stage[1]->tick_end <= stage[2]->tick_begin)
                         : (stage[0]->tick_end <= stage[2]->tick_begin && stage[2]->tick_end <= stage[1]->tick_begin),
              "same tick submit then independent preparation then fetch wait");
        check(stage[0]->thread_slot == stage[1]->thread_slot && stage[1]->thread_slot == stage[2]->thread_slot,
              "preparation remains on owner thread");
        for (std::size_t i = 0; i < 3; ++i)
            milliseconds[i] += capture->milliseconds(stage[i]->tick_end-stage[i]->tick_begin);
        ++measured;
        concurrent += overlapped && std::ranges::any_of(workers, [&](const auto* worker) {
            return worker->cpu.session == identity.first && worker->cpu.tick == identity.second &&
                   worker->cpu.task != 0 && worker->thread_slot != stage[1]->thread_slot &&
                   worker->tick_begin < stage[1]->tick_end && stage[1]->tick_begin < worker->tick_end;
        });
    }
    check(measured > 0, "real correlated pipeline spans");
    check(concurrent == 0, "production preparation never competes with solver workers");
    check(!workers.empty(), "real SDK worker spans remain captured");
    std::cerr << "pipeline ticks=" << measured << " concurrent=" << concurrent
              << " submitMs=" << milliseconds[0] << " prepareMs=" << milliseconds[1]
              << " fetchWaitMs=" << milliseconds[2] << '\n';
    if(argc==2)
        check(bool(ce::save_capture(*capture,argv[1])), "profiling capture saved");
    profiler.unregister_thread();
    profiler.shutdown();
#else
    (void)argc;
    (void)argv;
#endif
#if defined(CE_PHYSICS_WAKE_TEST)
    check(ce_physics_wake_probe(), "dispatcher chain, fan-out, saturation and nested waiting");
#endif

    std::cout << "{\"result\":\"PHYSICS_T1_OK\",\"checks\":" << checks
              << ",\"gpu_verified\":" << (gpu ? "true" : "false") << "}\n";
}
