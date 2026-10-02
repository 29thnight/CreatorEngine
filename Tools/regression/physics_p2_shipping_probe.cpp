#include "../../Engine/Physics/PhysicsScene.h"
#include <array>
#include <cmath>
#include <iostream>

using namespace ce::physics;

int main()
{
    int checks = 0;
    const auto check = [&](bool value) {
        ++checks;
        return value;
    };
    auto created = PhysicsScene::create(scene_config{{0, -9.81f, 0}, 2, 1024, execution_preference::prefer_gpu});
    if (!check(bool(created)))
        return 1;
    auto scene = std::move(*created);
    const auto initial_sdk_errors = scene->status().sdk_errors;
    const std::array<math::vector3, 8> cube = {{{-.5f, -.5f, -.5f},
                                                {.5f, -.5f, -.5f},
                                                {-.5f, .5f, -.5f},
                                                {.5f, .5f, -.5f},
                                                {-.5f, -.5f, .5f},
                                                {.5f, -.5f, .5f},
                                                {-.5f, .5f, .5f},
                                                {.5f, .5f, .5f}}};
    auto cooked = scene->cook_convex(cube);
    if (!check(bool(cooked)))
        return 2;
    ShapeInstance floor;
    floor.form = box_geometry{{5, .5f, 5}};
    auto ground = scene->create_body(body_desc{body_kind::static_body, pose{{0, -.5f, 0}}, std::span(&floor, 1)});
    ShapeInstance shape;
    shape.form = cooked_geometry{*cooked};
    auto dynamic = scene->create_body(body_desc{body_kind::dynamic, pose{{0, 5, 0}}, std::span(&shape, 1)});
    if (!check(bool(ground) && bool(dynamic)))
        return 3;
    cooked->reset();
    shape.form = box_geometry{};
    for (int step = 0; step < 180; ++step)
        if (!check(bool(scene->begin_step(1.f / 60))) || !check(bool(scene->finish_step())))
            return 4;
    auto state = scene->read_body(*dynamic);
    if (!check(state && std::abs(state->transform.position.y - .5f) <= .12f))
        return 5;
    std::array<query_hit, 1> hit;
    auto query = scene->raycast({0, 10, 0}, {0, -1, 0}, 20, hit);
    if (!check(query && query->truncated && query->required_capacity == 2 && query->written == 1))
        return 6;
    const bool gpu = scene->status().backend == execution_backend::gpu;
    if (!check(scene->status().sdk_errors == initial_sdk_errors && scene->destroy_body(*dynamic) &&
               !scene->read_body(*dynamic)))
        return 7;
    scene.reset();
    // No diagnostics sources or library are linked in this executable.
    std::cout << "{\"result\":\"PHYSICS_P2_OK\",\"checks\":" << checks
              << ",\"gpu_verified\":" << (gpu ? "true" : "false") << "}\n";
}
