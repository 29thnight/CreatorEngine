#include "../../Engine/SceneRuntime/ScenePhysicsSimulation.h"
#include "../../Engine/SceneRuntime/SimulationSessionPolicy.h"
#if !CE_SHIPPING
#include "../../Engine/EngineDiagnostics/ProfileScope.h"
#include "../../Engine/EngineDiagnostics/ProfileCaptureFile.h"
#endif
#include <iostream>
#include <cmath>
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
    if (value)
        return;
    std::cerr << "check failed " << checks << ": " << label << '\n';
    std::exit(1);
}

body_definition definition(body_kind kind, float y)
{
    body_definition output;
    output.properties.kind = kind;
    output.properties.initial_pose.position = {0, y, 0};
    output.shapes.emplace_back();
    return output;
}

void publish()
{
#if !CE_SHIPPING
    ce::profiler().publish_frame(frame++);
#endif
}

void exercise_layers(execution_preference execution)
{
    ProjectLayerSettings project;
    ce::layers::layer_id moving;
    check(bool(project.Change([&](auto& catalog, auto&) -> ce::layers::result<void> {
              const auto added = catalog.Add("Moving");
              if (!added)
                  return std::unexpected(added.error());
              moving = *added;
              return {};
          })),
          "project common layer created");

    ScenePhysicsSimulation session;
    auto floor = definition(body_kind::static_body, 0);
    floor.shapes.front().form = box_geometry{{10, .5f, 10}};
    const auto ground = session.Register(std::move(floor), true);
    const auto body = session.Register(definition(body_kind::dynamic, 3), true);
    const auto disabled = session.Register(definition(body_kind::static_body, -30), false);
    check(ground && body && disabled, "layer session authoring registered");
    std::array assignments{ScenePhysicsSimulation::layer_assignment{*ground, ce::layers::default_layer},
                           ScenePhysicsSimulation::layer_assignment{*body, moving},
                           ScenePhysicsSimulation::layer_assignment{*disabled, moving}};
    check(bool(session.CommitLayers(*project.Snapshot(), assignments)), "editor definitions receive project filters");
    scene_config config;
    config.workers = 2;
    config.execution = execution;
    check(bool(session.Start(config)), "layer session starts");
    const auto identity = session.Handle(*body);
    auto tick = [&] {
        check(bool(session.Advance(ScenePhysicsSimulation::fixed_seconds)), "layer policy tick succeeds");
        publish();
    };
    for (int i = 0; i < 100; ++i)
        tick();
    check(session.Read(*body)->transform.position.y > .8f, "project allowed pair actually collides");

    const auto slotBeforeRename = project.Snapshot()->catalog.Find(moving)->slot;
    check(bool(project.Change([&](auto& catalog, auto&) { return catalog.Rename(moving, "RenamedMoving"); }))
              && bool(session.CommitLayers(*project.Snapshot(), assignments)), "layer rename commits by stable ID");
    check(project.Snapshot()->catalog.Find(moving)->slot == slotBeforeRename && session.Handle(*body) == identity,
          "rename preserves slot and SDK body identity");

    const auto before = project.Snapshot();
    check(bool(project.Change([&](auto& catalog, auto& policy) {
              return policy.Set(*catalog.Snapshot(), moving, ce::layers::default_layer, false);
          })),
          "project publishes blocked pair");
    const auto blocked = project.Snapshot();
    auto malformed = *blocked;
    malformed.policy.matrix[0] = 2;
    check(!session.CommitLayers(malformed, assignments) && session.LayerRevision() == before->revision,
          "invalid policy cannot publish a revision");
    auto invalid = assignments;
    invalid.back().layer = ce::layers::layer_id{999};
    check(!session.CommitLayers(*blocked, invalid), "last unknown layer rejects entire batch");
    invalid = assignments;
    invalid.back().binding = *body;
    check(!session.CommitLayers(*blocked, invalid), "duplicate binding rejected before mutation");
    check(!session.CommitLayers(*blocked, std::span(assignments).first(2)), "incomplete membership rejected");
    check(session.LayerRevision() == before->revision && session.Handle(*body) == identity,
          "failed preparation preserves revision and SDK identity");

    check(bool(session.Runtime()->begin_step(1.f / 60)), "layer commit in-flight test begins");
    check(!session.CommitLayers(*blocked, assignments), "in-flight policy mutation refused");
    check(bool(session.Runtime()->finish_step()), "layer in-flight test drained");
    bool foreign = false;
    std::jthread worker([&] { foreign = !session.CommitLayers(*blocked, assignments); });
    worker.join();
    check(foreign, "foreign project commit refused");
    check(bool(session.CommitLayers(*blocked, assignments)) && session.LayerRevision() == blocked->revision,
          "all body filters acknowledge coherent project revision");
    check(session.Handle(*body) == identity && bool(session.SetVelocity(*body, {0, -1, 0}, {})),
          "policy commit preserves body identity and motion controls");
    for (int i = 0; i < 60; ++i)
        tick();
    check(session.Read(*body)->transform.position.y < -2, "project blocked pair falls through ground");
    check(!session.CommitLayers(*before, assignments), "older project revision refused");

    check(bool(session.Runtime()->set_pose(identity, definition(body_kind::dynamic, 3).properties.initial_pose)) &&
              bool(session.SetVelocity(*body, {}, {})),
          "reset body for reenabled policy");
    check(bool(project.Change([&](auto& catalog, auto& policy) {
              return policy.Set(*catalog.Snapshot(), moving, ce::layers::default_layer, true);
          })) &&
              bool(session.CommitLayers(*project.Snapshot(), assignments)),
          "common pair reenabled as batch");
    for (int i = 0; i < 100; ++i)
        tick();
    check(session.Read(*body)->transform.position.y > .8f, "reenabled project pair collides again");

    std::array<query_hit, 8> hits;
    query_filter query;
    query.layers = project.Snapshot()->catalog.Find(moving)->slot.Mask();
    check(session.Runtime()->raycast({0, 8, 0}, {0, -1, 0}, 20, hits, query)->required_capacity == 1,
          "runtime query selects common layer slot");
    assignments[1].layer = ce::layers::default_layer;
    check(bool(session.CommitLayers(*project.Snapshot(), assignments)), "entity membership change at same revision");
    check(session.Runtime()->raycast({0, 8, 0}, {0, -1, 0}, 20, hits, query)->required_capacity == 0,
          "query sees changed membership before next tick");
    check(bool(session.SetEnabled(*disabled, true)), "disabled definition creates body with committed filter");
    check(session.Runtime()->raycast({0, -25, 0}, {0, -1, 0}, 10, hits, query)->required_capacity == 1,
          "reenabled body retains its common layer filter");

    // Force the second SDK write to fail after the first succeeds; the first must roll back.
    check(bool(session.Runtime()->destroy_body(session.Handle(*disabled))), "induce stale SDK target");
    assignments[1].layer = moving;
    assignments[2].layer = ce::layers::default_layer;
    check(!session.CommitLayers(*project.Snapshot(), assignments), "SDK failure rolls back previous filter writes");
    check(session.Runtime()->raycast({0, 8, 0}, {0, -1, 0}, 20, hits, query)->required_capacity == 0 &&
              session.Handle(*body) == identity,
          "rollback preserves query filter and identity");
    check(bool(session.Stop()), "layer session drains before restoration");
    check(bool(project.Restore(*before)), "editor layer policy restored with new epoch");
    assignments[1].layer = moving;
    assignments[2].layer = moving;
    check(bool(session.CommitLayers(*project.Snapshot(), assignments)) && bool(session.Start(config)),
          "restored policy starts a fresh session");
    check(session.Handle(*body).scene != identity.scene, "Play restart changes SDK scene identity");
    check(bool(session.Stop()), "restored layer session stops");
    publish();
}

void exercise_policy()
{
    int captures = 0, starts = 0, stops = 0, restores = 0, discards = 0;
    auto capture = [&] {
        ++captures;
        return false;
    };
    auto start = [&] {
        ++starts;
        return true;
    };
    auto stop = [&] {
        ++stops;
        return true;
    };
    auto restore = [&] {
        ++restores;
        return false;
    };
    auto discard = [&] { ++discards; };
    SimulationSessionPolicy runtime;
    check(runtime.Begin(capture, start, discard), "Player starts without editor backup");
    check(runtime.End(stop, restore, discard), "Player stops without authoring restore");
    check(captures == 0 && restores == 0 && discards == 0 && starts == 1 && stops == 1,
          "Player never invokes editor document operations");

    SimulationSessionPolicy editor(SimulationSessionPolicy::mode::editor_restore);
    check(!editor.Begin(capture, start, discard) && starts == 1, "failed backup blocks Editor start");
    check(!editor.Begin([] { return true; }, [] { return false; }, discard) && discards == 1,
          "failed Editor start discards unused backup");
    bool threw = false;
    try
    {
        editor.Begin([] { return true; }, []() -> bool { throw 7; }, discard);
    }
    catch (...)
    {
        threw = true;
    }
    check(threw && discards == 2, "exceptional Editor start discards unused backup");
    check(!editor.End([] { return false; }, restore, discard) && restores == 0, "failed SDK stop prevents restore");
    check(!editor.End(stop, restore, discard) && discards == 2, "failed restore retains original backup");
    check(editor.End(stop, [] { return true; }, discard) && discards == 3, "successful restore retry discards backup");
}

void exercise_transfer(execution_preference execution)
{
    scene_config config;
    config.workers = 2;
    config.execution = execution;
    ScenePhysicsSimulation source, destination;
    auto authored = definition(body_kind::dynamic, 20);
    authored.properties.linear_velocity = {3, 0, 0};
    authored.properties.angular_velocity = {0, 1, 0};
    auto id = source.Register(authored, true);
    check(id && bool(source.Start(config)), "source scene starts for travel");
    check(source.Advance(.05).value() == 3, "source simulates before travel");
    auto state = source.Read(*id);
    auto old = source.Handle(*id);
    check(bool(state), "read current persistent body before teardown");
    authored.properties.initial_pose = state->transform;
    authored.properties.linear_velocity = state->linear_velocity;
    authored.properties.angular_velocity = state->angular_velocity;
    check(bool(source.Stop()) && bool(source.Unregister(*id)), "old scene drains before transfer");
    auto next = destination.Register(authored, true);
    check(next && bool(destination.Start(config)), "destination scene starts without session backup");
    auto current = destination.Read(*next);
    check(destination.Handle(*next).scene != old.scene, "travel replaces scene scoped SDK handle");
    check(current->transform.position.y == state->transform.position.y &&
              current->linear_velocity.y == state->linear_velocity.y &&
              current->angular_velocity.y == state->angular_velocity.y,
          "travel preserves current pose and velocities");
    check(destination.Advance(.05).value() == 3 &&
              destination.Read(*next)->transform.position.y < state->transform.position.y,
          "destination continues physical motion");
    check(bool(destination.Stop()), "destination drains on shutdown");
}

void exercise_controls(execution_preference execution)
{
    ScenePhysicsSimulation session;
    auto authored = definition(body_kind::dynamic, 10);
    authored.properties.mass = 2;
    authored.properties.gravity_enabled = false;
    authored.properties.constraints = {axis_lock::y, axis_lock::all};
    auto id = session.Register(authored, true);
    check(bool(id), "register constrained authoring body");
    check(!session.SetVelocity(*id, {1, 2, 3}, {}) && !session.ApplyForce(*id, {1, 0, 0}, {}, force_mode::impulse),
          "editor refuses runtime control");
    scene_config config;
    config.workers = 2;
    config.execution = execution;
    check(bool(session.Start(config)), "constrained body starts");
    check(bool(session.SetVelocity(*id, {3, 7, 0}, {1, 2, 3})), "set current runtime velocity");
    check(bool(session.ApplyForce(*id, {4, 0, 0}, {}, force_mode::impulse)), "apply mass dependent impulse");

    check(session.Advance(.05).value() == 3, "advance constrained body");
    auto state = session.Read(*id);
    check(std::abs(state->linear_velocity.x - 5.f) < .001f, "impulse uses SDK mass after simulation");
    check(state->transform.position.x > .2f && std::abs(state->transform.position.y - 10.f) < .001f,
          "translation lock prevents motion on locked axis");
    check(std::abs(state->transform.rotation.x) < .001f && std::abs(state->transform.rotation.y) < .001f &&
              std::abs(state->transform.rotation.z) < .001f,
          "rotation locks prevent angular motion");
    check(!session.SetVelocity(*id, {std::numeric_limits<float>::quiet_NaN(), 0, 0}, {}), "invalid velocity rejected");
    check(bool(session.Runtime()->begin_step(1.f / 60)), "begin in flight control test");
    check(!session.SetVelocity(*id, {}, {}) && !session.ApplyForce(*id, {}, {}),
          "in flight SDK rejects direct mutation");
    check(bool(session.Runtime()->finish_step()), "fetch in flight control test");
    bool rejected = false;
    std::jthread worker([&] { rejected = !session.SetVelocity(*id, {}, {}) && !session.ApplyForce(*id, {}, {}); });
    worker.join();
    check(rejected, "foreign thread cannot control body");
    check(bool(session.SetEnabled(*id, false)), "disable controlled body");
    check(!session.SetVelocity(*id, {}, {}) && !session.ApplyForce(*id, {}, {}), "disabled body refuses control");
    check(bool(session.SetEnabled(*id, true)) && bool(session.Stop()), "reenable and stop controlled body");
    check(session.Read(*id)->linear_velocity.x == 0, "runtime control does not modify authoring velocity");
    check(bool(session.Unregister(*id)), "remove controlled body");
    check(session.SetVelocity(*id, {}, {}).error().code == error_code::stale_handle,
          "removed binding reports stale identity");
}

bool exercise(execution_preference execution)
{
    ScenePhysicsSimulation session;
    auto floor_definition = definition(body_kind::static_body, -1);
    floor_definition.shapes.front().form = box_geometry{{10, .5f, 10}};
    const auto floor = session.Register(std::move(floor_definition), true);
    auto initial = definition(body_kind::dynamic, 8);
    initial.properties.linear_velocity = {1, 0, 0};
    const auto body = session.Register(std::move(initial), true);
    check(floor && body, "editor membership without SDK actors");
    check(!session.IsRunning() && !session.Handle(*body), "editor has no SDK scene/body");
    check(session.Advance(1).value() == 0 && session.Read(*body)->transform.position.y == 8, "editor cannot simulate");

    auto edited = definition(body_kind::dynamic, 10);
    edited.properties.linear_velocity = {2, 0, 0};
    check(bool(session.Define(*body, std::move(edited))), "authoring edit before Play");
    scene_config config;
    config.workers = 2;
    config.execution = execution;
    bool gpu = false;
    const int repetitions = execution == execution_preference::cpu ? 32 : 4;
    scene_id previous{};
    for (int iteration = 0; iteration < repetitions; ++iteration)
    {
        check(bool(session.Start(config)), "atomic Play preparation");
        check(session.IsRunning() && bool(session.Handle(*body)), "Play creates current session handle");
        const auto current = session.Runtime()->status().identity;
        check(current != previous, "new Play never revives old session identity");
        previous = current;
        gpu |= session.Runtime()->status().backend == execution_backend::gpu;
        check(session.Read(*body)->transform.position.y == 10, "replay uses authoring position");
        check(session.Read(*body)->linear_velocity.x == 2, "replay resets initial velocity");
        check(session.Read(*body)->inertia.x > 0 && session.Read(*body)->inertia.y > 0,
              "SDK mass properties visible before first tick");
        check(!session.Start(config) && !session.Define(*body, definition(body_kind::dynamic, 100)),
              "duplicate Play and authoring mutation rejected");
        check(session.Advance(0).value() == 0, "zero time cannot tick");
        check(session.Advance(ScenePhysicsSimulation::fixed_seconds / 2).value() == 0,
              "partial fixed tick accumulates");
        check(session.Advance(ScenePhysicsSimulation::fixed_seconds / 2).value() == 1,
              "accumulated fixed tick runs once");
        for (int tick = 0; tick < 45; ++tick)
        {
            check(session.Advance(ScenePhysicsSimulation::fixed_seconds).value() == 1, "fixed physics tick");
            check(session.ChangedPoses().size() == 1, "publish only active dynamic body");
            publish();
        }
        check(session.Read(*body)->transform.position.y < 9, "actual gravity simulation");
        check(bool(session.SetEnabled(*body, false)), "disable runtime body");
        const auto disabled = *session.Read(*body);
        check(!session.Handle(*body), "disabled runtime handle invalidated");
        check(session.Advance(.05).value() == 3 && session.ChangedPoses().empty(), "disabled body cannot simulate");
        check(bool(session.SetEnabled(*body, true)), "reenable runtime body");
        check(session.Runtime()->read_body(session.Handle(*body))->linear_velocity.y == disabled.linear_velocity.y,
              "reenable preserves current velocity");
        check(session.Advance(1).value() == 4 && session.DroppedSeconds() > .9, "bounded catchup discards excess time");
        check(session.ChangedPoses().size() == 1, "multiple ticks publish one final pose per body");

        const auto spawned = session.Register(definition(body_kind::dynamic, 20), true);
        check(spawned && bool(session.Handle(*spawned)), "Play-time spawn gets current SDK actor");
        check(bool(session.Unregister(*spawned)) && bool(session.Unregister(*spawned)),
              "lifecycle unregister idempotent");
        check(!session.Read(*spawned), "removed membership cannot be read");
        const auto old_handle = session.Handle(*body);
        const auto snapshot = session.Runtime()->latest_snapshot();
        check(bool(session.Runtime()->begin_step(1.f / 60)), "in-flight step before Stop");
        check(bool(session.Stop()), "Stop drains in-flight simulation");
        check(!session.IsRunning() && !session.Runtime() && !session.Handle(*body), "Stop removes all SDK owners");
        check(session.Read(*body)->transform.position.y == 10 && session.Read(*body)->transform.position.x == 0,
              "Stop restores authoring pose");
        check(session.Read(*body)->linear_velocity.x == 2 && session.Read(*body)->linear_velocity.y == 0,
              "Stop restores initial velocity");
        check(session.ChangedPoses().size() == 2 && session.DroppedSeconds() == 0,
              "Stop resets publication and scheduling state");
        check(snapshot && snapshot->scene == old_handle.scene && snapshot->step_succeeded,
              "reader-held immutable values survive Stop");
        check(bool(session.Stop()), "repeated Stop is safe");
        publish();
    }

    auto bad = definition(body_kind::dynamic, 7);
    bad.properties.mass = -1;
    const auto invalid = session.Register(std::move(bad), true);
    check(bool(invalid), "editor may hold invalid authoring for diagnostics");
    check(!session.Start(config) && !session.IsRunning(), "failed Play publishes no partial session");
    check(!session.Handle(*body) && session.Read(*body)->transform.position.y == 10,
          "failed Play preserves editor state");
    check(bool(session.Unregister(*invalid)) && bool(session.Start(config)), "corrected Play recovers");
    check(!session.Advance(-1) && !session.Advance(std::numeric_limits<double>::infinity()), "invalid time rejected");
    check(session.Advance((std::numeric_limits<double>::max)()).value() == 4,
          "huge finite time cannot overflow accumulator");
    check(session.Advance((std::numeric_limits<double>::max)()).value() == 4 && std::isfinite(session.DroppedSeconds()),
          "discard accounting saturates without infinity");
    bool rejected = false;
    std::jthread worker([&] { rejected = !session.Stop() && !session.Read(*body) && !session.Advance(.1); });
    worker.join();
    check(rejected && session.IsRunning(), "foreign thread cannot stop or mutate the scene owner");
    check(bool(session.Stop()), "final Stop");
    publish();
    return gpu;
}
} // namespace

int main(int argc, char** argv)
{
#if !CE_SHIPPING
    auto& profiler = ce::profiler();
    profiler.initialize();
    profiler.register_thread("PhysicsB0Probe", ce::track_kind::game_thread);
    profiler.record(1);
#endif
    exercise_policy();
    exercise_controls(execution_preference::cpu);
    exercise_controls(execution_preference::prefer_gpu);
    exercise_transfer(execution_preference::cpu);
    exercise_transfer(execution_preference::prefer_gpu);
    exercise(execution_preference::cpu);
    const bool gpu = exercise(execution_preference::prefer_gpu);
    exercise_layers(execution_preference::cpu);
    exercise_layers(execution_preference::prefer_gpu);
#if !CE_SHIPPING
    publish();
    profiler.pause();
    profiler.wait_until_idle();
    const auto capture = profiler.capture();
    check(capture && capture->complete() && capture->unacked_streams() == 0, "complete Play/Stop profiling capture");
    for (const char* name : {"Physics.PlayStart", "Physics.PlayStop", "Physics.LayerPolicyCommit", "Physics.Refilter"})
        check(std::ranges::any_of(capture->frames(),
                                  [&](const auto& value) {
                                      return std::ranges::any_of(value.events, [&](const auto& event) {
                                          return capture->marker(event.marker).name == name;
                                      });
                                  }),
              "Play/Stop hierarchy captured");
    check(std::ranges::any_of(capture->frames(),
                              [&](const auto& capturedFrame) {
                                  return std::ranges::any_of(capturedFrame.events, [&](const auto& parent) {
                                      if (capture->marker(parent.marker).name != "Physics.LayerPolicyCommit")
                                          return false;
                                      return std::ranges::any_of(capturedFrame.events, [&](const auto& child) {
                                          return capture->marker(child.marker).name == "Physics.Refilter" &&
                                                 parent.thread_slot == child.thread_slot &&
                                                 child.depth > parent.depth && child.tick_begin >= parent.tick_begin &&
                                                 child.tick_end <= parent.tick_end;
                                      });
                                  });
                              }),
          "refilter is nested under project policy commit");
    if (argc == 2)
        check(bool(ce::save_capture(*capture, argv[1])), "save Play/Stop capture");
    profiler.unregister_thread();
    profiler.shutdown();
#else
    (void)argc;
    (void)argv;
#endif
    std::cout << "{\"result\":\"PHYSICS_B0_OK\",\"checks\":" << checks
              << ",\"gpu_verified\":" << (gpu ? "true" : "false") << "}\n";
}
