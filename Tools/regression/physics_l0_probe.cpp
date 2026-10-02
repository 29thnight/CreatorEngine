#include "../../Engine/Physics/PhysicsScene.h"
#include "../../Engine/Physics/PhysicsCollisionPolicy.h"
#if !CE_SHIPPING
#include "../../Engine/EngineDiagnostics/ProfileScope.h"
#include "../../Engine/EngineDiagnostics/ProfileCaptureFile.h"
#endif
#include <array>
#include <iostream>
#include <ranges>
#include <thread>

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
        std::cerr << label << '\n';
        std::exit(1);
    }
}

void publish()
{
#if !CE_SHIPPING
    ce::profiler().publish_frame(frame++);
#endif
}

bool exercise(execution_preference execution)
{
    ce::layers::LayerCatalog catalog;
    const auto moving = catalog.Add("Moving");
    check(bool(moving), "common layer defined");
    const auto layers = catalog.Snapshot();
    PhysicsCollisionPolicy policy;
    scene_config config;
    config.workers = 2;
    config.execution = execution;
    auto made = PhysicsScene::create(config);
    check(bool(made), "SDK scene created");
    auto& scene = **made;
    const bool gpu = scene.status().gpu_dynamics && scene.status().gpu_broadphase;
    ShapeInstance floor_shape;
    floor_shape.form = box_geometry{{10, .5f, 10}};
    floor_shape.filter = policy.Snapshot()->Filter(*layers, ce::layers::default_layer).value();
    body_desc floor_desc;
    floor_desc.shapes = std::span(&floor_shape, 1);
    const auto floor = scene.create_body(floor_desc);
    check(bool(floor), "floor uses common layer filter");
    ShapeInstance box;
    box.filter = policy.Snapshot()->Filter(*layers, *moving).value();
    body_desc dynamic;
    dynamic.kind = body_kind::dynamic;
    dynamic.initial_pose.position = {0, 3, 0};
    dynamic.shapes = std::span(&box, 1);
    const auto body = scene.create_body(dynamic);
    check(bool(body), "moving body uses common filter");

    auto tick = [&] {
        check(bool(scene.begin_step(1.f / 60)) && bool(scene.finish_step()), "physics tick succeeds");
        publish();
    };
    for (int i = 0; i < 100; ++i)
        tick();
    check(scene.read_body(*body)->transform.position.y > .8f, "allowed common layers collide");

    check(bool(policy.Set(*layers, *moving, ce::layers::default_layer, false)), "disable common collision pair");
    const auto blocked = policy.Snapshot()->Filter(*layers, *moving).value();
    check(bool(scene.set_shape_filter(*body, box.id, blocked)), "existing SDK shape refiltered");
    check(bool(scene.set_velocity(*body, {0, -1, 0}, {})), "wake body after policy change");
    for (int i = 0; i < 60; ++i)
        tick();
    check(scene.read_body(*body)->transform.position.y < -2, "existing collision pair is actually disabled");

    check(bool(scene.set_pose(*body, dynamic.initial_pose)) && bool(scene.set_velocity(*body, {}, {})),
          "reset body without changing handle");
    check(bool(policy.Set(*layers, *moving, ce::layers::default_layer, true)), "reenable common collision pair");
    check(bool(scene.set_shape_filter(*body, box.id, policy.Snapshot()->Filter(*layers, *moving).value())),
          "reenable SDK pair without body replacement");
    for (int i = 0; i < 100; ++i)
        tick();
    check(scene.read_body(*body)->transform.position.y > .8f, "re-enabled common pair collides");

    std::array<query_hit, 8> hits;
    query_filter query;
    query.layers = layers->Find(*moving)->slot.Mask();
    const auto selected = scene.raycast({0, 8, 0}, {0, -1, 0}, 20, hits, query);
    check(selected && selected->required_capacity == 1 && hits[0].body == *body, "query mask uses common slot space");
    auto hidden = policy.Snapshot()->Filter(*layers, *moving).value();
    hidden.query_layers = 0;
    check(bool(scene.set_shape_filter(*body, box.id, hidden)), "update query visibility in idle SDK");
    check(scene.raycast({0, 8, 0}, {0, -1, 0}, 20, hits, query)->required_capacity == 0,
          "idle query sees new filter immediately before next tick");
    check(!scene.set_shape_filter(*body, shape_id{999}, blocked), "unknown shape identity rejected");
    check(bool(scene.begin_step(1.f / 60)), "in-flight filter test begins");
    check(!scene.set_shape_filter(*body, box.id, blocked), "in-flight filter write rejected");
    check(bool(scene.finish_step()), "in-flight filter test drained");
    bool foreign_rejected = false;
    std::jthread worker([&] { foreign_rejected = !scene.set_shape_filter(*body, box.id, blocked); });
    worker.join();
    check(foreign_rejected, "foreign filter mutation rejected");
    check(bool(scene.destroy_body(*body)) && !scene.set_shape_filter(*body, box.id, blocked),
          "retired SDK body cannot change filters");
    publish();
    return gpu;
}
} // namespace

int main(int argc, char** argv)
{
#if !CE_SHIPPING
    auto& profiler = ce::profiler();
    profiler.initialize();
    profiler.register_thread("PhysicsL0Probe", ce::track_kind::game_thread);
    profiler.record(1);
#endif
    exercise(execution_preference::cpu);
    const bool gpu = exercise(execution_preference::prefer_gpu);
#if !CE_SHIPPING
    publish();
    profiler.pause();
    profiler.wait_until_idle();
    const auto capture = profiler.capture();
    check(capture && capture->complete() && capture->unacked_streams() == 0, "filter capture complete");
    for (const char* name : {"Physics.FilterCommit", "Physics.Refilter"})
        check(std::ranges::any_of(capture->frames(),
                                  [&](const auto& value) {
                                      return std::ranges::any_of(value.events, [&](const auto& event) {
                                          return capture->marker(event.marker).name == name;
                                      });
                                  }),
              "filter execution hierarchy captured");
    if (argc == 2)
        check(bool(ce::save_capture(*capture, argv[1])), "save filter capture");
    profiler.unregister_thread();
    profiler.shutdown();
#else
    (void)argc;
    (void)argv;
#endif
    std::cout << "{\"result\":\"PHYSICS_L0_OK\",\"checks\":" << checks
              << ",\"gpu_verified\":" << (gpu ? "true" : "false") << "}\n";
}
