#include "../../Engine/Physics/PhysicsScene.h"
#include "../../Engine/Physics/PhysicsTestHooks.h"
#include "../../Engine/EngineDiagnostics/ProfileService.h"
#include "../../Engine/EngineDiagnostics/ProfileScope.h"
#include "../../Engine/EngineDiagnostics/ProfileCaptureFile.h"
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <ranges>
#include <thread>
#include <vector>

using namespace ce::physics;

namespace
{
int checks = 0;

void check(bool condition, std::source_location where = std::source_location::current())
{
    ++checks;
    if (!condition)
    {
        std::cerr << "P2 check failed at " << where.line() << '\n';
        std::exit(1);
    }
}

template<class T>
void rejected(const result<T>& value, error_code code)
{
    check(!value && value.error().code == code);
}

body_handle body(PhysicsScene& scene, body_kind kind, geometry form, math::vector3 position,
                 std::uint32_t collision_mask = ~0u, bool sensor = false)
{
    ShapeInstance shape;
    shape.form = std::move(form);
    shape.filter.collides_with = collision_mask;
    shape.sensor = sensor;
    auto value = scene.create_body(body_desc{kind, pose{position}, std::span(&shape, 1)});
    check(bool(value));
    return *value;
}

void step(PhysicsScene& scene, int count)
{
    for (auto index : std::views::iota(0, count))
    {
        (void)index;
        check(bool(scene.begin_step(1.f / 60)));
        check(bool(scene.finish_step()));
    }
}

const std::array<math::vector3, 8> cube = {{{-.5f, -.5f, -.5f},
                                            {.5f, -.5f, -.5f},
                                            {-.5f, .5f, -.5f},
                                            {.5f, .5f, -.5f},
                                            {-.5f, -.5f, .5f},
                                            {.5f, -.5f, .5f},
                                            {-.5f, .5f, .5f},
                                            {.5f, .5f, .5f}}};
} // namespace

int main(int argc, char** argv)
{
    auto& profiler = ce::profiler();
    profiler.initialize();
    profiler.register_thread("PhysicsP2Owner", ce::track_kind::game_thread);
    profiler.record(1);
    profiler.publish_frame(1);
    bool gpu_verified = false;
    std::shared_ptr<const CollisionGeometry> retained;

    for (auto preference : {execution_preference::cpu, execution_preference::prefer_gpu})
    {
        auto created = PhysicsScene::create(scene_config{{0, -9.81f, 0}, 2, 1024, preference});
        check(bool(created));
        auto scene = std::move(*created);
        const auto initial_sdk_errors = scene->status().sdk_errors;
        gpu_verified |= scene->status().backend == execution_backend::gpu;

        auto convex = scene->cook_convex(cube);
        check(bool(convex) && (*convex)->kind() == geometry_kind::convex && (*convex)->gpu_compatible());
        const std::array<math::vector3, 4> plane = {{{-2, 0, -2}, {2, 0, -2}, {-2, 0, 2}, {2, 0, 2}}};
        const std::array<triangle_indices, 2> indices = {{{0, 2, 1}, {1, 2, 3}}};
        auto mesh = scene->cook_triangle_mesh(plane, indices);
        check(bool(mesh) && (*mesh)->kind() == geometry_kind::triangle_mesh);
        const std::array<std::int16_t, 9> heights{};
        auto terrain = scene->cook_heightfield(heightfield_desc{3, 3, heights});
        check(bool(terrain) && (*terrain)->kind() == geometry_kind::heightfield);
        retained = *convex;

        const auto floor = body(*scene, body_kind::static_body, box_geometry{{30, .5f, 30}}, {0, -.5f, 0});
        const auto box = body(*scene, body_kind::dynamic, box_geometry{}, {0, 5, 0});
        const auto sphere = body(*scene, body_kind::dynamic, sphere_geometry{}, {3, 5, 0});
        const auto capsule = body(*scene, body_kind::dynamic, capsule_geometry{.5f, .5f}, {6, 5, 0});
        const auto hull = body(*scene, body_kind::dynamic, cooked_geometry{*convex}, {9, 5, 0});
        const auto filtered = body(*scene, body_kind::dynamic, sphere_geometry{}, {12, 5, 0}, 0);
        const auto sensor = body(*scene, body_kind::static_body, box_geometry{}, {15, 5, 0}, ~0u, true);
        const auto through_sensor = body(*scene, body_kind::dynamic, sphere_geometry{.25f}, {15, 7, 0});
        const auto triangle_floor = body(*scene, body_kind::static_body, cooked_geometry{*mesh}, {0, 2, 8});
        const auto mesh_drop = body(*scene, body_kind::dynamic, sphere_geometry{}, {0, 7, 8});
        const auto terrain_floor = body(*scene, body_kind::static_body, cooked_geometry{*terrain}, {4, 2, 8});
        const auto terrain_drop = body(*scene, body_kind::dynamic, sphere_geometry{}, {5, 7, 9});
        (void)triangle_floor;
        (void)terrain_floor;

        std::array<ShapeInstance, 2> compound;
        compound[0].id = shape_id{17};
        compound[0].form = box_geometry{};
        compound[0].local_pose.position = {-1, 0, 0};
        compound[0].surface.restitution = .2f;
        compound[0].filter.query_layers = 2;
        compound[1].id = shape_id{93};
        compound[1].form = sphere_geometry{.25f};
        compound[1].local_pose.position = {1, 0, 0};
        compound[1].sensor = true;
        compound[1].filter.query_layers = 4;
        auto compound_handle = scene->create_body(body_desc{body_kind::kinematic, pose{{20, 5, 0}}, compound, 7.f});
        check(bool(compound_handle));
        auto compound_state = scene->read_body(*compound_handle);
        check(bool(compound_state) && std::abs(compound_state->mass - 7.f) < 1e-5f);
        check(compound_state->inertia.x > 0 && compound_state->inertia.y > 0 && compound_state->inertia.z > 0);
        check(std::abs(compound_state->inertia.y - 7.f / 6) < .001f);
        check(std::abs(compound_state->center_of_mass.position.x + 1.f) < .001f);
        check(bool(scene->set_kinematic_target(*compound_handle, pose{{20, 6, 0}})));
        rejected(scene->set_kinematic_target(box, pose{}), error_code::invalid_argument);
        rejected(scene->set_velocity(floor, {}, {}), error_code::invalid_argument);

        ShapeInstance locked_shape;
        body_desc locked_desc{body_kind::dynamic, pose{{25, 5, 0}}, std::span(&locked_shape, 1)};
        locked_desc.constraints.translation = axis_lock::y;
        locked_desc.constraints.rotation = axis_lock::x | axis_lock::z;
        auto locked = scene->create_body(locked_desc);
        check(bool(locked));
        body_desc floating_desc{body_kind::dynamic, pose{{25, 8, 0}}, std::span(&locked_shape, 1)};
        floating_desc.gravity_enabled = false;
        floating_desc.linear_damping = 2.f;
        floating_desc.linear_velocity = {1, 0, 0};
        auto floating = scene->create_body(floating_desc);
        check(bool(floating));
        locked_desc.constraints.translation = static_cast<axis_lock>(8);
        rejected(scene->create_body(locked_desc), error_code::invalid_argument);
        locked_desc.constraints.translation = axis_lock::none;
        locked_desc.linear_damping = -1.f;
        rejected(scene->create_body(locked_desc), error_code::invalid_argument);

        step(*scene, 180);
        for (auto [handle, height] :
             std::array{std::pair{box, .5f}, std::pair{sphere, .5f}, std::pair{capsule, 1.f}, std::pair{hull, .5f},
                        std::pair{mesh_drop, 2.5f}, std::pair{terrain_drop, 2.5f}})
        {
            auto state = scene->read_body(handle);
            check(bool(state) && std::abs(state->transform.position.y - height) < .12f);
        }
        check(scene->read_body(filtered)->transform.position.y < -10.f);
        check(scene->read_body(through_sensor)->transform.position.y < 1.f);
        check(std::abs(scene->read_body(*compound_handle)->transform.position.y - 6.f) < .01f);
        check(std::abs(scene->read_body(*locked)->transform.position.y - 5.f) < .01f);
        const auto floating_state = scene->read_body(*floating);
        check(bool(floating_state) && std::abs(floating_state->transform.position.y - 8.f) < .01f &&
              std::abs(floating_state->linear_velocity.x) < .01f);
        check(scene->status().sdk_errors == initial_sdk_errors);

        std::array<query_hit, 16> hits;
        auto ray = scene->raycast({0, 10, 0}, {0, -1, 0}, 20, hits);
        check(bool(ray) && !ray->truncated && ray->required_capacity == 2 && ray->written == 2);
        check(std::ranges::any_of(std::span(hits).first(ray->written), [box](auto hit) {
            return hit.body == box && hit.shape == shape_id{1} && hit.has_location && hit.normal.y > .9f;
        }));
        auto nohit = scene->raycast({100, 10, 100}, {0, -1, 0}, 20, hits);
        check(bool(nohit) && nohit->written == 0 && nohit->required_capacity == 0 && !nohit->truncated);
        auto overflow = scene->raycast({0, 10, 0}, {0, -1, 0}, 20, std::span(hits).first(1));
        check(bool(overflow) && overflow->truncated && overflow->written == 1 && overflow->required_capacity == 2);
        auto count_only = scene->raycast({0, 10, 0}, {0, -1, 0}, 20, {});
        check(bool(count_only) && count_only->truncated && count_only->written == 0 &&
              count_only->required_capacity == 2);
        auto ignored = scene->raycast({0, 10, 0}, {0, -1, 0}, 20, hits, query_filter{~0u, false, box});
        check(bool(ignored) && ignored->written == 1 && hits[0].body == floor);

        auto sensor_hidden = scene->overlap(sphere_geometry{.1f}, pose{{15, 5, 0}}, hits);
        check(bool(sensor_hidden) && sensor_hidden->written == 0);
        auto sensor_visible = scene->overlap(sphere_geometry{.1f}, pose{{15, 5, 0}}, hits, query_filter{~0u, true});
        check(bool(sensor_visible) && sensor_visible->written == 1 && hits[0].body == sensor && !hits[0].has_location);
        auto shape_query = scene->overlap(sphere_geometry{.1f}, pose{{21, 6, 0}}, hits, query_filter{4, true});
        check(bool(shape_query) && shape_query->written == 1 && hits[0].body == *compound_handle &&
              hits[0].shape == shape_id{93});
        auto sweep = scene->sweep(sphere_geometry{.25f}, pose{{0, 10, 0}}, {0, -1, 0}, 20, hits);
        check(bool(sweep) && sweep->written == 2);
        check(std::ranges::any_of(std::span(hits).first(sweep->written),
                                  [box](auto hit) { return hit.body == box && hit.distance > 8.f; }));
        auto convex_overlap = scene->overlap(cooked_geometry{*convex}, pose{{0, .5f, 0}}, hits);
        check(bool(convex_overlap) && convex_overlap->written >= 1);

        rejected(scene->sweep(cooked_geometry{*mesh}, pose{}, {0, -1, 0}, 10, hits), error_code::unsupported_geometry);
        rejected(scene->raycast({}, {0, -2, 0}, 10, hits), error_code::invalid_argument);
        rejected(scene->cook_heightfield(heightfield_desc{3, 4, heights}), error_code::invalid_argument);
        auto invalid_indices = indices;
        invalid_indices[0].a = 99;
        rejected(scene->cook_triangle_mesh(plane, invalid_indices), error_code::invalid_argument);

        auto invalid = compound;
        invalid[1].id = invalid[0].id;
        rejected(scene->create_body(body_desc{body_kind::dynamic, pose{}, invalid}), error_code::invalid_argument);
        invalid = compound;
        invalid[0].surface.restitution = 2;
        rejected(scene->create_body(body_desc{body_kind::dynamic, pose{}, invalid}), error_code::invalid_argument);
        invalid = compound;
        invalid[0].form = cooked_geometry{*mesh};
        rejected(scene->create_body(body_desc{body_kind::dynamic, pose{}, invalid}), error_code::unsupported_geometry);
        invalid[0].form = cooked_geometry{*terrain};
        rejected(scene->create_body(body_desc{body_kind::dynamic, pose{}, invalid}), error_code::unsupported_geometry);
        invalid[0].form = box_geometry{{-1, 1, 1}};
        rejected(scene->create_body(body_desc{body_kind::static_body, pose{}, invalid}), error_code::invalid_argument);

        test::next_failure.store(test::failure_point::body_shape);
        rejected(scene->create_body(body_desc{body_kind::static_body, pose{{40, 0, 0}}, compound}),
                 error_code::backend_initialization);
        auto rolled_back = scene->overlap(box_geometry{{3, 3, 3}}, pose{{40, 0, 0}}, hits, query_filter{~0u, true});
        check(bool(rolled_back) && rolled_back->written == 0);

        const auto old = body(*scene, body_kind::static_body, sphere_geometry{}, {40, 0, 0});
        check(bool(scene->destroy_body(old)));
        check(bool(scene->destroy_body(old))); // Idempotent before slot reuse.
        rejected(scene->read_body(old), error_code::stale_handle);
        const auto replacement = body(*scene, body_kind::static_body, sphere_geometry{}, {40, 0, 0});
        check(old.slot == replacement.slot && old.generation != replacement.generation);
        rejected(scene->destroy_body(old), error_code::stale_handle);
        rejected(scene->set_pose(old, pose{}), error_code::stale_handle);
        rejected(scene->raycast({}, {0, -1, 0}, 10, hits, query_filter{~0u, false, old}), error_code::stale_handle);
        test::next_failure.store(test::failure_point::body_generation_limit);
        const auto retired = body(*scene, body_kind::static_body, sphere_geometry{}, {45, 0, 0});
        check(retired.generation == UINT32_MAX);
        check(bool(scene->destroy_body(retired)));
        const auto fresh = body(*scene, body_kind::static_body, sphere_geometry{}, {45, 0, 0});
        check(fresh.slot != retired.slot);
        rejected(scene->read_body(retired), error_code::stale_handle);
        auto other = PhysicsScene::create(scene_config{{0, -9.81f, 0}, 1});
        check(bool(other));
        rejected((*other)->read_body(replacement), error_code::wrong_scene);
        rejected((*other)->raycast({}, {0, -1, 0}, 10, hits, query_filter{~0u, false, replacement}),
                 error_code::wrong_scene);
        std::jthread foreign([&] { rejected(scene->read_body(box), error_code::wrong_phase); });
        foreign.join();
        check(bool(scene->begin_step(1.f / 60)));
        rejected(scene->destroy_body(box), error_code::wrong_phase);
        rejected(scene->raycast({}, {0, -1, 0}, 10, hits), error_code::wrong_phase);
        rejected(scene->cook_convex(cube), error_code::wrong_phase);
        check(bool(scene->finish_step()));

        // More hits than the SDK scratch chunk; exact count must survive multiple callbacks.
        for (auto index : std::views::iota(0, 70))
            body(*scene, body_kind::static_body, sphere_geometry{.25f}, {50, static_cast<float>(index), 0});
        auto many = scene->raycast({50, 80, 0}, {0, -1, 0}, 100, std::span(hits).first(3));
        check(bool(many) && many->required_capacity == 70 && many->written == 3 && many->truncated);

        // Cooked geometry must outlive both caller references and the scene that cooked it.
        auto shared_shape = ShapeInstance{};
        shared_shape.form = cooked_geometry{*convex, {2, 1, 1}};
        auto shared =
            (*other)->create_body(body_desc{body_kind::dynamic, pose{{0, 5, 0}}, std::span(&shared_shape, 1)});
        check(bool(shared));
        scene.reset();
        convex->reset();
        shared_shape.form = box_geometry{};
        step(**other, 2);
        check((*other)->read_body(*shared)->transform.position.y < 5.f);
        check(bool((*other)->destroy_body(*shared)));
        profiler.publish_frame(preference == execution_preference::cpu ? 2 : 3);
    }

    // Retained asset keeps Foundation alive across a complete scene lifetime gap.
    {
        auto scene = PhysicsScene::create(scene_config{{0, 0, 0}, 1});
        check(bool(scene));
        body(**scene, body_kind::static_body, cooked_geometry{retained}, {});
        retained.reset();
        const std::array<math::vector3, 4> degenerate = {{{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {3, 0, 0}}};
        rejected((*scene)->cook_convex(degenerate), error_code::cooking_failed);
        auto recovered = (*scene)->cook_convex(cube);
        check(bool(recovered));
    }

    profiler.publish_frame(4);
    profiler.pause();
    profiler.wait_until_idle();
    auto capture = profiler.capture();
    check(bool(capture) && capture->complete() && capture->unacked_streams() == 0);
    check(std::ranges::all_of(capture->frames(), [](const auto& frame) { return frame.dropped_events == 0; }));
    for (const char* name :
         {"Physics.CookConvex", "Physics.CookTriangleMesh", "Physics.CookHeightfield", "Physics.BodyCreate",
          "Physics.BodyDestroy", "Physics.Raycast", "Physics.Sweep", "Physics.Overlap"})
    {
        auto events = capture->frames() |
                      std::views::transform([](const auto& frame) -> const auto& { return frame.events; }) |
                      std::views::join;
        check(
            std::ranges::any_of(events, [&](const auto& event) { return capture->marker(event.marker).name == name; }));
    }
    if (argc == 2)
        check(bool(ce::save_capture(*capture, std::filesystem::path(argv[1]))));
    profiler.unregister_thread();
    profiler.shutdown();
    std::cout << "{\"result\":\"PHYSICS_P2_OK\",\"checks\":" << checks
              << ",\"gpu_verified\":" << (gpu_verified ? "true" : "false") << "}\n";
}
