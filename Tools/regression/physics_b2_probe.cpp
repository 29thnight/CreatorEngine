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
    check(bool(session.Stop()), "stop");
    check(session.RenderPoses().empty() && session.InterpolationAlpha()==0, "Stop clears history and alpha");
    check(bool(session.Start(config)) && close(session.Read(*stationary)->transform.position.x,0), "replay restores authored static pose");
    check(bool(session.Stop()), "final teardown");
    return gpu;
}
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
    unsigned roundedHomogeneous = 0;
    for (unsigned pose = 1; pose <= 128; ++pose)
    {
        const auto rotation = math::quaternion_from_axis_angle(math::vector3::unit_y(), pose * .017f);
        const auto bodyWorld = math::compose(math::vector3{1.3f, 2.1f, .7f}, rotation, math::vector3{1, 2, 3});
        const auto interpolated = math::compose(math::vector3{1.3f, 2.1f, .7f}, rotation, math::vector3{4, 5, 6});
        const auto oldTransport = local * bodyWorld * math::inverse(bodyWorld) * interpolated;
        roundedHomogeneous += oldTransport.m[3][3] != 1.f;
        const auto rendered = PhysicsRenderDescendantMatrix(local * bodyWorld, bodyWorld, interpolated);
        check(rendered.m[0][3] == 0.f && rendered.m[1][3] == 0.f &&
              rendered.m[2][3] == 0.f && rendered.m[3][3] == 1.f, "descendant render matrix remains exactly affine");
        check(PhysicsTransformsNear(rendered, local * interpolated, 1e-4f), "descendant retains local offset and scale");
    }
    std::cerr << "[physics.render.affine] roundedLegacy=" << roundedHomogeneous << " poses=128\n";
    const auto singular = math::compose(math::vector3{0, 1, 1}, rotation, math::vector3{});
    check(PhysicsTransformsNear(PhysicsRenderDescendantMatrix(local, singular, parent), local),
          "singular parent preserves current world matrix");
    exercise(execution_preference::cpu);
    const auto gpu = exercise(execution_preference::prefer_gpu);
#if !CE_SHIPPING
    profiler.publish_frame(1);
    profiler.pause();
    profiler.wait_until_idle();
    const auto capture = profiler.capture();
    check(capture && capture->complete() && capture->unacked_streams()==0, "profiling capture complete");
    if(argc==2)
        check(bool(ce::save_capture(*capture,argv[1])), "profiling capture saved");
    profiler.unregister_thread();
    profiler.shutdown();
#else
    (void)argc;
    (void)argv;
#endif
    std::cout << "{\"result\":\"PHYSICS_B2_OK\",\"checks\":" << checks
              << ",\"gpu_verified\":" << (gpu ? "true" : "false") << "}\n";
}
