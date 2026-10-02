#include "../../Engine/SceneRuntime/PhysicsShapeDefinition.h"
#include "../../Engine/SceneRuntime/ScenePhysicsSimulation.h"
#if !CE_SHIPPING
#include "../../Engine/EngineDiagnostics/ProfileScope.h"
#include "../../Engine/EngineDiagnostics/ProfileCaptureFile.h"
#endif
#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>

using namespace ce::physics;

namespace
{
int checks = 0;

void check(bool value, const char* message)
{
    ++checks;
    if (!value)
    {
        std::cerr << "B1 check failed: " << message << '\n';
        std::exit(1);
    }
}

std::array<PhysicsShapeDefinition, 3> compound()
{
    std::array<PhysicsShapeDefinition, 3> shapes;
    shapes[0].shapeId = 11;
    shapes[0].localPosition = {-3, 0, 0};
    shapes[1].shapeId = 22;
    shapes[1].kind = PhysicsShapeKind::sphere;
    shapes[1].localPosition = {3, 0, 0};
    shapes[1].staticFriction = .8f;
    shapes[2].shapeId = 33;
    shapes[2].kind = PhysicsShapeKind::capsule;
    shapes[2].localPosition = {0, 0, 3};
    shapes[2].sensor = true;
    return shapes;
}

void validate_authoring()
{
    auto source = compound();
    const auto built = BuildPhysicsShapes(source, {2, 2, 2}, {8, 16, 32}, body_kind::dynamic);
    check(built && built->size() == 3, "complete owned compound");
    source[0].halfExtent = {99, 99, 99};
    check(std::get<box_geometry>((*built)[0].form).half_extent.x == 1 &&
              std::get<sphere_geometry>((*built)[1].form).radius == 1 &&
              std::get<capsule_geometry>((*built)[2].form).half_height == 1,
          "primitive scaling and independent source lifetime");
    check((*built)[0].local_pose.position.x == -6 && (*built)[2].local_pose.position.z == 6 && (*built)[2].sensor &&
              (*built)[1].surface.static_friction == .8f && (*built)[0].filter.belongs_to == 8 &&
              (*built)[0].filter.collides_with == 16 && (*built)[0].filter.query_layers == 32,
          "local pose material sensor and inherited filter");

    const auto reject = [](auto shapes, math::vector3 scale = {1, 1, 1}, body_kind motion = body_kind::dynamic) {
        check(!BuildPhysicsShapes(shapes, scale, {}, motion), "invalid authoring rejected before publication");
    };
    reject(std::span<const PhysicsShapeDefinition>{});
    for (const auto invalid :
         {0.f, -1.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
    {
        source = compound();
        reject(source, {invalid, 1, 1});
        source[0].halfExtent.y = invalid;
        reject(source);
        source = compound();
        source[1].radius = invalid;
        reject(source);
        source = compound();
        source[2].radius = invalid;
        reject(source);
    }

    source = compound();
    source[2].shapeId = source[0].shapeId;
    reject(source);
    source[2].shapeId = 0;
    reject(source);
    source = compound();
    source[1].kind = static_cast<PhysicsShapeKind>(255);
    reject(source);
    source = compound();
    source[2].halfHeight = -.1f;
    reject(source);
    source = compound();
    source[1].restitution = 1.1f;
    reject(source);
    source = compound();
    source[0].localRotation = {0, 0, 0, 0};
    reject(source);
    source = compound();
    source[0].localPosition.x = std::numeric_limits<float>::infinity();
    reject(source);
    source = compound();
    source[0].localPosition.x = (std::numeric_limits<float>::max)();
    reject(source, {2, 2, 2});
    source = compound();
    source[0].halfExtent.x = (std::numeric_limits<float>::max)();
    reject(source, {2, 2, 2});
    source = compound();
    for (auto& value : source)
        value.sensor = true;
    reject(source);
    reject(source, {1, 1, 1}, body_kind::kinematic);
    check(bool(BuildPhysicsShapes(source, {1, 1, 1}, {}, body_kind::static_body)), "static sensor-only body allowed");
    reject(source, {1, 1, 1}, static_cast<body_kind>(255));

    source = compound();
    reject(std::span(source).subspan(1, 1), {1, 2, 1});
    reject(std::span(source).subspan(2, 1), {1, 2, 1}, body_kind::static_body);
    check(bool(BuildPhysicsShapes(std::span(source).first(1), {1, 2, 3}, {}, body_kind::dynamic)),
          "axis aligned box accepts nonuniform scale");
    source[0].localRotation = {0, .70710678f, 0, .70710678f};
    reject(std::span(source).first(1), {1, 2, 3});
    check(bool(BuildPhysicsShapes(std::span(source).first(1), {2, 2, 2}, {}, body_kind::dynamic)),
          "rotated box accepts uniform scale");
}

bool exercise(execution_preference execution)
{
    ScenePhysicsSimulation session;
    auto source = compound();
    auto shapes = BuildPhysicsShapes(source, {1, 1, 1}, {8, ~0u, 8}, body_kind::static_body);
    check(bool(shapes), "query authoring prepared");
    body_definition definition;
    definition.properties.kind = body_kind::static_body;
    definition.shapes = std::move(*shapes);
    const auto binding = session.Register(std::move(definition), true);
    check(bool(binding), "scene owns shape values");
    source = {}; // Runtime must not borrow the caller's definitions.

    scene_config config;
    config.workers = 2;
    config.execution = execution;
    check(bool(session.Start(config)), "compound Play starts");
    auto* runtime = session.Runtime();
    const bool gpu = runtime->status().backend == execution_backend::gpu;
    const auto body = session.Handle(*binding);
    std::array<query_hit, 4> hits;
    const auto ray = [&](math::vector3 origin, shape_id expected) {
        const auto result = runtime->raycast(origin, {0, -1, 0}, 10, hits, query_filter{8, true});
        check(result && result->written == 1 && hits[0].shape == expected && hits[0].body == body,
              "compound query resolves body and stable shape ID");
    };
    ray({-3, 5, 0}, shape_id{11});
    ray({3, 5, 0}, shape_id{22});
    ray({0, 5, 3}, shape_id{33});
    auto result = runtime->raycast({0, 5, 3}, {0, -1, 0}, 10, hits);
    check(result && result->written == 0, "sensor hidden by default query policy");
    result = runtime->raycast({3, 5, 0}, {0, -1, 0}, 10, hits, query_filter{4, true});
    check(result && result->written == 0, "entity layer mask inherited by every shape");
    check(!session.Define(*binding, {}) && session.Handle(*binding) == body, "Play rejects authoring mutation");
    check(bool(session.Stop()), "compound Stop releases SDK bodies");

    // Query visibility is independent from solidity and survives authoring rebuild.
    source = compound();
    source[1].queryEnabled = false;
    shapes = BuildPhysicsShapes(source, {1, 1, 1}, {}, body_kind::static_body);
    check(bool(shapes), "query disabled solid prepared");
    definition = {};
    definition.properties.kind = body_kind::static_body;
    definition.shapes = std::move(*shapes);
    check(bool(session.Define(*binding, std::move(definition))) && bool(session.Start(config)),
          "Editor definition replacement and second Play");
    result = session.Runtime()->raycast({3, 5, 0}, {0, -1, 0}, 10, hits);
    check(result && result->written == 0, "query-disabled solid absent from queries");
    check(bool(session.Advance(ScenePhysicsSimulation::fixed_seconds)) && bool(session.Stop()),
          "compound step and teardown");
    return gpu;
}
bool exercise_overrides(execution_preference execution)
{
    ce::layers::LayerCatalog catalog;
    const auto other = catalog.Add("Sensor");
    check(bool(other), "common override layer created");
    PhysicsCollisionPolicy policy;
    project_layer_snapshot settings{1, *catalog.Snapshot(), *policy.Snapshot()};
    settings.catalog.revision = settings.policy.revision = settings.revision;
    auto source = compound();
    source[1].layerOverride = other->value;
    source[2].layerOverride = other->value;
    check(!BuildPhysicsShapes(source, {1, 1, 1}, {}, body_kind::dynamic), "override requires project policy");
    auto built =
        BuildPhysicsShapes(source, {1, 1, 1}, {1, ~0u, 1}, body_kind::dynamic, MissingGeometryResolver{}, &settings);
    check(built && (*built)[0].filter.belongs_to == 1 && (*built)[1].filter.belongs_to == 2 && (*built)[2].sensor,
          "solid and sensor use stable common layer override");
    ScenePhysicsSimulation session;
    body_definition definition;
    definition.properties.gravity_enabled = false;
    definition.properties.initial_pose.position = {0, 2, 0};
    definition.properties.linear_velocity = {1, 0, 0};
    definition.shapes = *built;
    const auto binding = session.Register(definition, true);
    check(bool(binding), "override binding registered");
    scene_config config;
    config.execution = execution;
    config.workers = 2;
    check(bool(session.Start(config)), "override session starts");
    const bool gpu = session.Runtime()->status().backend == execution_backend::gpu;
    const std::array assignments{ScenePhysicsSimulation::layer_assignment{*binding, ce::layers::default_layer}};
    check(bool(session.CommitLayers(settings, assignments)), "override membership committed");
    std::array<query_hit, 4> hits;
    const auto query = [&](math::vector3 origin, std::uint32_t mask, std::uint32_t count) {
        auto ray = session.Runtime()->raycast(origin, {0, -1, 0}, 20, hits, query_filter{mask, true});
        check(ray && ray->written == count, "override query mask applied per shape");
    };
    query({3, 10, 0}, 1, 0);
    query({3, 10, 0}, 2, 1);
    query({-3, 10, 0}, 1, 1);
    check(bool(policy.Set(settings.catalog, *other, ce::layers::default_layer, false)), "common policy changed");
    settings.policy = *policy.Snapshot();
    settings.revision = settings.catalog.revision = settings.policy.revision = 2;
    check(bool(session.CommitLayers(settings, assignments)), "policy refresh preserves overrides");
    query({3, 10, 0}, 2, 1);
    auto retired = settings;
    retired.catalog.definitions[1]->retired = true;
    retired.revision = retired.catalog.revision = retired.policy.revision = 3;
    const auto before = session.Handle(*binding);
    check(!session.CommitLayers(retired, assignments) && session.Handle(*binding) == before &&
              session.LayerRevision() == 2,
          "retired override rejects entire policy transaction");
    check(bool(session.Advance(ScenePhysicsSimulation::fixed_seconds)), "advance before replacement");
    const auto state = session.Read(*binding).value();
    built =
        BuildPhysicsShapes(source, {2, 2, 2}, {1, ~0u, 1}, body_kind::dynamic, MissingGeometryResolver{}, &settings);
    definition.shapes = *built;
    check(bool(session.Replace(*binding, definition)), "idle scale and compound replacement");
    const auto replacement = session.Handle(*binding);
    check(replacement != before && !session.Runtime()->read_body(before), "old runtime handle retired");
    const auto after = session.Read(*binding).value();
    check(after.transform.position.x == state.transform.position.x &&
              after.transform.position.y == state.transform.position.y &&
              after.linear_velocity.x == state.linear_velocity.x,
          "replacement preserves simulated pose and velocity");
    query({6 + after.transform.position.x, 10, 0}, 2, 1);
    query({3 + after.transform.position.x, 10, 0}, 2, 0);
    auto unknown = source;
    unknown[2].layerOverride = 9999;
    check(!BuildPhysicsShapes(unknown, {1, 1, 1}, {}, body_kind::dynamic, MissingGeometryResolver{}, &settings),
          "unknown override rejected during authoring preflight");
    auto invalid = definition;
    std::get<box_geometry>(invalid.shapes[0].form).half_extent.x = -1;
    check(!session.Replace(*binding, invalid) && session.Handle(*binding) == replacement,
          "invalid replacement preserves old body");
    check(bool(session.Runtime()->begin_step(1.f / 60)), "begin in-flight replacement rejection");
    check(!session.Replace(*binding, definition) && session.Handle(*binding) == replacement,
          "in-flight replacement rejected");
    check(bool(session.Runtime()->finish_step()), "finish in-flight step");
    check(bool(session.SetEnabled(*binding, false)), "disable before replacement");
    check(!session.Replace(*binding, invalid) && !session.Handle(*binding), "disabled invalid definition rejected");
    check(bool(session.Replace(*binding, definition)) && !session.Handle(*binding),
          "disabled replacement retains disabled membership");
    check(bool(session.SetEnabled(*binding, true)), "replacement re-enable");
    check(bool(session.Advance(ScenePhysicsSimulation::fixed_seconds)) && bool(session.Stop()), "replacement teardown");
    check(!session.Replace(*binding, definition), "Editor rejects runtime replacement");
    return gpu;
}
} // namespace

int main(int argc, char** argv)
{
#if !CE_SHIPPING
    auto& profiler = ce::profiler();
    profiler.initialize();
    profiler.register_thread("PhysicsB1Probe", ce::track_kind::game_thread);
    profiler.record(1);
#endif
    validate_authoring();
    exercise(execution_preference::cpu);
    const bool gpu = exercise(execution_preference::prefer_gpu);
    exercise_overrides(execution_preference::cpu);
    check(exercise_overrides(execution_preference::prefer_gpu), "actual GPU override/replacement");
#if !CE_SHIPPING
    profiler.publish_frame(1);
    profiler.pause();
    profiler.wait_until_idle();
    const auto capture = profiler.capture();
    check(capture && capture->complete() && capture->unacked_streams() == 0, "compound profiling capture complete");
    if (argc == 2)
        check(bool(ce::save_capture(*capture, argv[1])), "compound capture saved");
    profiler.unregister_thread();
    profiler.shutdown();
#else
    (void)argc;
    (void)argv;
#endif
    std::cout << "{\"result\":\"PHYSICS_B1_OK\",\"checks\":" << checks
              << ",\"gpu_verified\":" << (gpu ? "true" : "false") << "}\n";
}
