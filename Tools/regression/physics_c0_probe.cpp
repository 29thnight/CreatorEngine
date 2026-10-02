#include "../../Engine/SceneRuntime/ScenePhysicsSimulation.h"
#if !CE_SHIPPING
#include "../../Engine/EngineDiagnostics/ProfileScope.h"
#include "../../Engine/EngineDiagnostics/ProfileCaptureFile.h"
#endif
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <thread>
#include <ranges>

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
void publish()
{
#if !CE_SHIPPING
    ce::profiler().publish_frame(frame++);
#endif
}
bool near(float a, float b)
{
    return std::abs(a - b) < .002f;
}
body_definition box(math::vector3 position, math::vector3 extent)
{
    body_definition output;
    output.properties.kind = body_kind::static_body;
    output.properties.initial_pose.position = position;
    output.shapes.emplace_back();
    output.shapes.front().form = box_geometry{extent};
    return output;
}
bool exercise(execution_preference execution)
{
    ProjectLayerSettings project;
    ce::layers::layer_id moving;
    check(bool(project.Change([&](auto& catalog, auto&) -> ce::layers::result<void> {
              auto created = catalog.Add("Character");
              if (!created)
                  return std::unexpected(created.error());
              moving = *created;
              return {};
          })),
          "common character layer created");
    ScenePhysicsSimulation session;
    auto floor = session.Register(box({0, -.5f, 0}, {100, .5f, 100}), true);
    auto wall = session.Register(box({3, 2, 0}, {.2f, 4, 5}), true);
    ScenePhysicsSimulation::character_definition definition;
    definition.capsule.position = {0, 3, 0};
    auto character = session.RegisterCharacter(definition, true);
    auto disabledDefinition = definition;
    disabledDefinition.capsule.position = {-5, 3, 0};
    auto disabled = session.RegisterCharacter(disabledDefinition, false);
    check(floor && wall && character && disabled, "body and independent character registration");
    std::array assignments{ScenePhysicsSimulation::layer_assignment{*floor, ce::layers::default_layer},
                           ScenePhysicsSimulation::layer_assignment{*wall, ce::layers::default_layer},
                           ScenePhysicsSimulation::layer_assignment{*character, moving},
                           ScenePhysicsSimulation::layer_assignment{*disabled, moving}};
    check(bool(session.CommitLayers(*project.Snapshot(), assignments)), "one body/character policy commit");
    check(!session.SetCharacterVelocity(*character, {1, 0, 0}) && !session.CharacterHandle(*character),
          "editor input refused without SDK owner");
    auto invalid = definition;
    invalid.capsule.step_offset = 100;
    check(!session.RegisterCharacter(invalid, true), "invalid capsule rejected before membership");
    scene_config config;
    config.workers = 2;
    config.execution = execution;
    check(bool(session.Start(config)), "Play starts all body and character owners transactionally");
    const bool gpu = session.Runtime()->status().backend == execution_backend::gpu;
    auto handle = session.CharacterHandle(*character);
    check(bool(handle) && !session.CharacterHandle(*disabled), "enabled membership creates only live controller");
    check(!session.DefineCharacter(*character, definition), "authoring frozen in Play");
    check(!session.SetCharacterVelocity(*character, {std::numeric_limits<float>::infinity(), 0, 0}),
          "nonfinite input rejected");
    check(session.Advance(ScenePhysicsSimulation::fixed_seconds / 2).value() == 0 &&
              session.ReadCharacter(*character)->collision.position.y == 3,
          "fractional frame performs no move");
    check(bool(session.SetCharacterVelocity(*character, {1, 0, 0})), "world velocity input accepted");
    check(session.Advance(ScenePhysicsSimulation::fixed_seconds / 2).value() == 1,
          "two half frames make one fixed move");
    auto state = session.ReadCharacter(*character).value();
    check(near(state.collision.position.x, 1.f / 60) && state.tick.value == 1,
          "m/s integrates exactly once per completed tick");
    check(near(state.collision.actual_displacement.x, 1.f / 60), "actual displacement reports collision move");
    check(session.Advance(1).value() == 4 && session.DroppedSeconds() > .9, "catchup budget applies to characters");
    check(near(session.ReadCharacter(*character)->collision.position.x, 5.f / 60),
          "four catchup ticks integrate input four times");
    check(bool(session.SetCharacterVelocity(*character, {})), "horizontal input stopped");
    const auto tick = [&] {
        check(session.Advance(ScenePhysicsSimulation::fixed_seconds).value() == 1, "fixed tick succeeds");
        publish();
    };
    for (int i = 0; i < 90; ++i)
        tick();
    state = session.ReadCharacter(*character).value();
    check(state.collision.below && std::abs(state.collision.foot_position.y) < .08f && state.fall_velocity == 0,
          "gravity grounds independent capsule and clears downward velocity");
    check(session.ChangedCharacters().size() == 1, "only enabled characters publish completed movement");

    const auto before = project.Snapshot();
    check(bool(project.Change([&](auto& catalog, auto& policy) {
              return policy.Set(*catalog.Snapshot(), moving, ce::layers::default_layer, false);
          })),
          "common project policy blocks character-body pair");
    auto malformed = assignments;
    malformed.back().layer = ce::layers::layer_id{999};
    check(!session.CommitLayers(*project.Snapshot(), malformed) && session.LayerRevision() == before->revision &&
              session.CharacterHandle(*character) == handle,
          "last invalid member cannot partially update character filter");
    check(bool(session.Runtime()->begin_step(1.f / 60)), "inflight owner test starts");
    check(!session.SetCharacterVelocity(*character, {}) && !session.TeleportCharacter(*character, {}) &&
              !session.CommitLayers(*project.Snapshot(), assignments),
          "inflight character writes refused");
    check(bool(session.Runtime()->finish_step()), "inflight test drained");
    check(bool(session.CommitLayers(*project.Snapshot(), assignments)) && session.CharacterHandle(*character) == handle,
          "refilter preserves character identity and position");
    for (int i = 0; i < 60; ++i)
        tick();
    check(session.ReadCharacter(*character)->collision.position.y < -2, "blocked character falls through floor");
    check(bool(project.Restore(*before)) && bool(session.CommitLayers(*project.Snapshot(), assignments)),
          "coherent layer policy restored");
    check(bool(session.TeleportCharacter(*character, {0, 3, 0})), "idle teleport succeeds");
    check(session.ReadCharacter(*character)->fall_velocity == 0 && !session.ReadCharacter(*character)->collision.below,
          "teleport clears old collision and accumulated fall velocity");
    check(bool(session.SetCharacterVelocity(*character, {2, 0, 0})), "wall input set");
    for (int i = 0; i < 180; ++i)
        tick();
    state = session.ReadCharacter(*character).value();
    check(state.collision.sides && state.collision.position.x < 2.4f && state.collision.position.x > 2,
          "wall collision constrains actual movement");
    check(bool(session.SetCharacterEnabled(*character, false)) && !session.CharacterHandle(*character),
          "disable retires SDK controller");
    check(!session.SetCharacterVelocity(*character, {}), "disabled movement rejected");
    tick();
    check(near(session.ReadCharacter(*character)->collision.position.x, state.collision.position.x),
          "disabled character does not integrate");
    check(bool(session.SetCharacterEnabled(*character, true)) && session.CharacterHandle(*character) != handle,
          "enable resumes retained position with new controller generation");
    check(!session.Runtime()->read_character(handle), "retired SDK generation remains stale");
    bool foreign = false;
    std::jthread worker([&] {
        foreign = !session.ReadCharacter(*character) && !session.SetCharacterVelocity(*character, {}) &&
                  !session.TeleportCharacter(*character, {}) && !session.SetCharacterEnabled(*character, false) &&
                  !session.UnregisterCharacter(*character) && !session.CharacterHandle(*character);
    });
    worker.join();
    check(foreign, "all character state/control boundaries enforce owner before map access");
    handle = session.CharacterHandle(*character);
    check(bool(session.Stop()) && !session.CharacterHandle(*character), "Stop destroys controller before restoration");
    state = session.ReadCharacter(*character).value();
    check(state.collision.position.y == 3 && state.collision.position.x == 0 && state.desired_velocity.x == 0 &&
              state.fall_velocity == 0 && state.tick.value == 0,
          "Stop restores authored state and clears runtime input/results");
    check(bool(session.Start(config)) && session.CharacterHandle(*character).scene != handle.scene &&
              !session.Runtime()->read_character(handle),
          "restart receives a new scene epoch");
    check(bool(session.UnregisterCharacter(*character)) && bool(session.UnregisterCharacter(*character)) &&
              !session.ReadCharacter(*character),
          "unregister is idempotent and wrapper binding retires");
    check(bool(session.Stop()), "final stop");
    publish();
    return gpu;
}
} // namespace
int main(int argc, char** argv)
{
#if !CE_SHIPPING
    auto& profiler = ce::profiler();
    profiler.initialize();
    profiler.register_thread("PhysicsC0Probe", ce::track_kind::game_thread);
    profiler.record(1);
#endif
    exercise(execution_preference::cpu);
    const bool gpu = exercise(execution_preference::prefer_gpu);
#if !CE_SHIPPING
    publish();
    profiler.pause();
    profiler.wait_until_idle();
    auto capture = profiler.capture();
    check(capture && capture->complete(), "complete character hierarchy capture");
    for (const auto name : {"Physics.CharacterFixedStep", "Physics.CharacterMovement", "Physics.CharacterFilterCommit"})
        check(std::ranges::any_of(capture->frames(),
                                  [&](const auto& frame) {
                                      return std::ranges::any_of(frame.events, [&](const auto& event) {
                                          return capture->marker(event.marker).name == name;
                                      });
                                  }),
              "character profiler marker executed");
    check(std::ranges::any_of(capture->frames(),
                              [&](const auto& frame) {
                                  return std::ranges::any_of(frame.events, [&](const auto& parent) {
                                      return capture->marker(parent.marker).name == "Physics.CharacterFixedStep" &&
                                             std::ranges::any_of(frame.events, [&](const auto& child) {
                                                 return capture->marker(child.marker).name ==
                                                            "Physics.CharacterMovement" &&
                                                        child.thread_slot == parent.thread_slot &&
                                                        child.depth > parent.depth &&
                                                        child.tick_begin >= parent.tick_begin &&
                                                        child.tick_end <= parent.tick_end;
                                             });
                                  });
                              }),
          "SDK movement nested under fixed-step hierarchy");
    if (argc == 2)
        check(bool(ce::save_capture(*capture, argv[1])), "capture saved");
    profiler.unregister_thread();
    profiler.shutdown();
#else
    (void)argc;
    (void)argv;
#endif
    std::cout << "{\"result\":\"PHYSICS_C0_OK\",\"checks\":" << checks
              << ",\"gpu_verified\":" << (gpu ? "true" : "false") << "}\n";
}
