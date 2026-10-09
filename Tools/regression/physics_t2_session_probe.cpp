#include "../../Engine/SceneRuntime/ScenePhysicsSimulation.h"
#include "../../Engine/SceneRuntime/PhysicsTransformPolicy.h"
#include <array>
#include <cmath>
#include <iostream>
#include <cstdlib>
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
void publish_profile_frame() {
#if !CE_SHIPPING
    static std::uint32_t frame = 0;
    ce::profiler().publish_frame(++frame);
#endif
}

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
    kinematic.shapes.front().form = box_geometry{{2, .25f, .25f}};
    auto target = session.Register(kinematic, true);
    check(bool(target), "kinematic registration");
    check(!session.SetPose(*dynamic, {}), "Editor rejects runtime pose");
    std::array<query_hit, 8> hits{};
    std::array requests{query_request{ray_query{{0, 30, 0}, {0, -1, 0}, 50}, hits},
                        query_request{overlap_query{sphere_geometry{1}, {{0, -10, 0}, {}}}, {}}};
    std::array<result<query_result>, 2> answers;
    check(!session.QueryBatch(requests, answers), "editor mode has no query runtime");
    scene_config config;
    config.execution = preference;
    check(bool(session.Start(config)), "session start");
    bool gpu = session.Runtime()->status().backend == execution_backend::gpu;
    check(bool(session.QueryBatch(requests, answers)) && answers[0] && answers[0]->written == 3,
          "session mixed batch resolves three registered bodies");
    check(answers[1] && answers[1]->required_capacity == 1 && answers[1]->truncated,
          "session batch overflow");
    for (const auto& hit : std::span{hits}.first(answers[0]->written))
        check(bool(session.Binding(hit.body)), "query hits resolve registered ownership");
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
    pose goal{{2, 20, 0}, math::quaternion_from_axis_angle(math::vector3::unit_y(), .785398163f)};
    std::array<query_hit, 8> rotatedHits{};
    std::array rotatedRequests{query_request{
        overlap_query{sphere_geometry{.1f}, {{3.1f, 20, -1.1f}, {0, 0, 0, 1}}}, rotatedHits}};
    std::array<result<query_result>, 1> rotatedAnswers;
    check(bool(session.QueryBatch(rotatedRequests, rotatedAnswers)) && rotatedAnswers[0] &&
              rotatedAnswers[0]->written == 0,
          "future rotated target does not alter pre-step query structure");
    check(bool(session.SetKinematicTarget(*target, goal)), "kinematic target request");
    check(close(session.Read(*target)->transform.position.x, 0), "target is not completed state");
    auto multiple = session.Advance(session.fixed_seconds*2);
    check(multiple && *multiple == 2, "multi tick frame");
    check(close(session.Read(*target)->transform.position.x, 2), "kinematic target completed");
    check(bool(session.QueryBatch(rotatedRequests, rotatedAnswers)) && rotatedAnswers[0] &&
              rotatedAnswers[0]->written == 1,
          "rotated kinematic target updates overlap structure after completed tick");
    check(session.Binding(rotatedHits[0].body) && *session.Binding(rotatedHits[0].body) == *target,
          "rotated query preserves registered kinematic owner");
    const auto kinematicHit = rotatedHits[0].body;
    check(bool(session.SetEnabled(*target, false)), "disable queried kinematic");
    check(bool(session.QueryBatch(rotatedRequests, rotatedAnswers)) && rotatedAnswers[0] &&
              rotatedAnswers[0]->written == 0,
          "disabled kinematic disappears from committed query structure");
    check(!session.Binding(kinematicHit), "disabled SDK hit does not resolve a live binding");
    check(bool(session.SetEnabled(*target, true)), "re-enable queried kinematic");
    check(close(session.RenderPose(*dynamic)->position.x, 4.15f), "multi tick keeps last two completed poses");
    check(!session.SetKinematicTarget(*dynamic, goal), "dynamic rejects kinematic target");
    check(bool(session.SetVelocity(*dynamic, {}, {})), "stop moving body");
    for (int i=0; i<200; ++i)
    {
        check(bool(session.Advance(session.fixed_seconds)), "sleep settling tick");
        publish_profile_frame();
    }
    check(session.RenderPoses().empty(), "sleeping bodies leave render work list");
    check(bool(session.SetEnabled(*dynamic, false)), "disable");
    check(close(session.RenderPose(*dynamic)->position.x, session.Read(*dynamic)->transform.position.x), "disabled history resets");
    check(!session.SetPose(*dynamic, jump), "disabled rejects teleport");
    check(bool(session.Unregister(*dynamic)), "unregister");
    check(!session.RenderPose(*dynamic), "stale render binding rejects");
    check(bool(session.Stop()), "stop");
    check(session.RenderPoses().empty() && session.InterpolationAlpha()==0, "Stop clears history and alpha");
    check(bool(session.Start(config)) && close(session.Read(*stationary)->transform.position.x,0), "replay restores authored static pose");
    check(bool(session.Stop()), "final teardown");
    check(bool(session.Stop()), "query session stop");
    check(!session.QueryBatch(requests, answers), "query rejected after stop");
    check(bool(session.Start(config)), "query session restart");
    check(bool(session.QueryBatch(requests, answers)), "query after restart");
    check(bool(session.Stop()), "query session final stop");
    return gpu;
}
}
int main(int argc, char** argv) {
#if !CE_SHIPPING
    auto& profiler = ce::profiler();
    profiler.initialize();
    profiler.register_thread("PhysicsB2Probe", ce::track_kind::game_thread);
    profiler.record(1);
    profiler.wait_until_idle();
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
#if !CE_SHIPPING
    publish_profile_frame();
    profiler.pause();
    profiler.wait_until_idle();
    const auto capture = profiler.capture();
    check(capture && capture->complete() && capture->unacked_streams()==0, "profiling capture complete");
    for (const auto name : {"Physics.QueryBatch", "Physics.QueryStructureUpdate"})
        check(std::ranges::any_of(capture->frames(), [&](const auto& frame) {
            return std::ranges::any_of(frame.events, [&](const auto& event) {
                return capture->marker(event.marker).name == name && event.cpu.session != 0;
            });
        }), "query profiler scene identity");
    if(argc==2)
        check(bool(ce::save_capture(*capture,argv[1])), "profiling capture saved");
    profiler.unregister_thread();
    profiler.shutdown();
#else
    (void)argc;
    (void)argv;
#endif
    std::cout << "{\"result\":\"PHYSICS_T2_SESSION_OK\",\"checks\":" << checks
              << ",\"gpu_verified\":" << (gpu ? "true" : "false") << "}\n";
}
