#include "../../Engine/SceneRuntime/ScenePhysicsSimulation.h"
#if !CE_SHIPPING
#include "../../Engine/EngineDiagnostics/ProfileScope.h"
#include "../../Engine/EngineDiagnostics/ProfileCaptureFile.h"
#endif
#include <array>
#include <cmath>
#include <iostream>
#include <limits>

using namespace ce::physics;
namespace
{
int checks = 0;
std::uint32_t frame = 1;
void check(bool value, const char* label)
{
    ++checks;
    if (!value)
    {
        std::cerr << "check failed " << checks << ": " << label << '\n';
        std::exit(1);
    }
}
void tick(ScenePhysicsSimulation& scene, int count)
{
    for (int i = 0; i < count; ++i)
    {
        check(scene.Advance(ScenePhysicsSimulation::fixed_seconds).value() == 1, "completed fixed step");
#if !CE_SHIPPING
        ce::profiler().publish_frame(frame++);
#endif
    }
}
body_definition box(math::vector3 position, math::vector3 extent)
{
    body_definition result;
    result.properties.kind = body_kind::static_body;
    result.properties.initial_pose.position = position;
    result.shapes.emplace_back();
    result.shapes.front().form = box_geometry{extent};
    return result;
}
struct outcome
{
    character_state state;
    bool gpu;
};
outcome traverse(execution_preference execution, float angle, float limit, float stepHeight)
{
    ScenePhysicsSimulation scene;
    auto floor = scene.Register(box({0, -.5f, 0}, {30, .5f, 30}), true);
    check(bool(floor), "floor registration");
    ScenePhysicsSimulation::character_definition definition;
    definition.capsule.position = {-2, 1.05f, 0};
    definition.capsule.slope_limit_cosine = limit;
    definition.capsule.step_offset = .3f;
    auto character = scene.RegisterCharacter(definition, true);
    check(bool(character), "character registration");
    scene_config config;
    config.workers = 2;
    config.execution = execution;
    check(bool(scene.Start(config)), "session start");
    const bool gpu = scene.Runtime()->status().backend == execution_backend::gpu;
    if (angle != 0)
    {
        const float height = 10 * std::tan(angle);
        const std::array points{math::vector3{0, 0, -4}, math::vector3{0, 0, 4}, math::vector3{10, height, -4},
                                math::vector3{10, height, 4}};
        const std::array triangles{triangle_indices{0, 1, 2}, triangle_indices{1, 3, 2}};
        auto cooked = scene.Runtime()->cook_triangle_mesh(points, triangles);
        check(bool(cooked), "static ramp cooking");
        auto ramp = box({}, {});
        ramp.shapes.front().form = cooked_geometry{*cooked};
        check(bool(scene.Register(std::move(ramp), true)), "static triangle ramp registration");
    }
    else
    {
        check(bool(scene.Register(box({1, stepHeight / 2, 0}, {1, stepHeight / 2, 4}), true)), "step registration");
    }
    tick(scene, 30);
    check(bool(scene.SetCharacterVelocity(*character, {2, 0, 0})), "metres per second traversal input");
    tick(scene, 150);
    auto state = scene.ReadCharacter(*character)->collision;
    std::cerr << "traverse angle=" << angle << " limit=" << limit << " step=" << stepHeight << " x=" << state.position.x
              << " foot=" << state.foot_position.y << '\n';
    check(bool(scene.Stop()), "traversal stop");
    return {state, gpu};
}
bool exercise(execution_preference execution)
{
    constexpr float radians = 3.14159265358979323846f / 180;
    auto gentle = traverse(execution, 20 * radians, std::cos(45 * radians), 0);
    check(gentle.state.position.x > 2 && gentle.state.foot_position.y > .5f, "walkable static mesh slope climbed");
    auto steep = traverse(execution, 60 * radians, std::cos(45 * radians), 0);
    check(steep.state.position.x < 1 && steep.state.foot_position.y < 1, "nonwalkable static mesh slope blocks ascent");
    auto unrestricted = traverse(execution, 60 * radians, 0, 0);
    check(unrestricted.state.position.x > steep.state.position.x + .5f, "zero slope limit disables slope rejection");
    auto low = traverse(execution, 0, .70710678f, .2f);
    check(low.state.position.x > 2.8f, "configured step climbs low obstacle");
    auto high = traverse(execution, 0, .70710678f, .8f);
    check(high.state.position.x < .2f && high.state.foot_position.y < .1f,
          "high step blocked without capsule climbing");

    ScenePhysicsSimulation scene;
    ScenePhysicsSimulation::character_definition definition;
    definition.capsule.position = {0, 20, 0};
    auto character = scene.RegisterCharacter(definition, true);
    scene_config config;
    config.workers = 2;
    config.execution = execution;
    check(bool(character) && bool(scene.Start(config)), "forced movement session");
    auto handle = scene.CharacterHandle(*character);
    check(bool(scene.SetCharacterVelocity(*character, {6, 0, 0})), "explicit forced velocity replaces desired input");
    tick(scene, 30);
    auto state = scene.ReadCharacter(*character).value();
    check(std::abs(state.collision.position.x - 3) < .01f && std::abs(state.fall_velocity + 4.905f) < .01f,
          "velocity metres/second and gravity metres/second squared");
    check(bool(scene.SetCharacterVelocity(*character, {})), "caller ends forced velocity explicitly");
    tick(scene, 30);
    check(std::abs(scene.ReadCharacter(*character)->collision.position.x - 3) < .01f,
          "cancel stops horizontal movement");
    const auto before = scene.ReadCharacter(*character)->collision.position;
    auto rejected = scene.TeleportCharacter(*character, {NAN, 0, 0});
    check(!rejected && rejected.error().code == error_code::invalid_argument &&
              scene.ReadCharacter(*character)->collision.position == before,
          "invalid teleport preserves completed state");
    check(bool(scene.TeleportCharacter(*character, {100, 30, 0})) && scene.CharacterHandle(*character) == handle,
          "forced position preserves SDK identity");
    check(scene.ReadCharacter(*character)->fall_velocity == 0 && !scene.ReadCharacter(*character)->collision.below,
          "forced position resets falling and old contact result");
    tick(scene, 1);
    check(std::abs(scene.ReadCharacter(*character)->fall_velocity + .1635f) < .001f,
          "gravity restarts once after teleport");
    check(bool(scene.Stop()) && scene.ReadCharacter(*character)->collision.position.y == 20,
          "Stop restores authored state");
    return gentle.gpu && steep.gpu && unrestricted.gpu && low.gpu && high.gpu;
}
} // namespace
int main(int argc, char** argv)
{
#if !CE_SHIPPING
    auto& profiler = ce::profiler();
    profiler.initialize();
    profiler.register_thread("PhysicsC1Probe", ce::track_kind::game_thread);
    profiler.record(1);
#endif
    exercise(execution_preference::cpu);
    const bool gpu = exercise(execution_preference::prefer_gpu);
#if !CE_SHIPPING
    profiler.publish_frame(frame++);
    profiler.pause();
    profiler.wait_until_idle();
    auto capture = profiler.capture();
    check(capture && capture->complete(), "complete traversal capture");
    if (argc > 1)
        check(bool(ce::save_capture(*capture, argv[1])), "save traversal capture");
#endif
    std::cout << "{\"result\":\"PHYSICS_C1_OK\",\"checks\":" << checks
              << ",\"gpu_verified\":" << (gpu ? "true" : "false") << "}\n";
}
