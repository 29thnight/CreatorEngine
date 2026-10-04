#pragma once
#include "../../Engine/Physics/PhysicsScene.h"

// Exercise the same public contract in diagnostics and Shipping executables.
template<class Check>
bool verify_physics_handle_lifetime(Check&& verify)
{
    using namespace ce::physics;

    const auto check = [&](bool valid, std::source_location where = std::source_location::current()) {
        return verify(valid, where);
    };

    const auto rejects = [&](const auto& value, error_code code,
                             std::source_location where = std::source_location::current()) {
        return check(!value && value.error().code == code, where);
    };

    const auto character_rejects = [&](PhysicsScene& scene, character_handle handle, error_code code) {
        return rejects(scene.read_character(handle), code) &&
               rejects(scene.move_character(handle, character_move{{1, 0, 0}}), code) &&
               rejects(scene.teleport_character(handle, {10, 0, 0}), code) &&
               rejects(scene.set_character_filter(handle, 1, ~0u), code);
    };

    auto created = PhysicsScene::create(scene_config{{0, 0, 0}, 1});
    if (!check(bool(created)))
        return false;

    auto scene = std::move(*created);
    auto character = scene->create_character(character_desc{});
    if (!check(bool(character)) || !check(bool(scene->destroy_character(*character))))
        return false;

    // Repeating retirement is intentionally idempotent until the slot is reused.
    if (!check(bool(scene->destroy_character(*character))) ||
        !character_rejects(*scene, *character, error_code::stale_handle))
        return false;

    auto replacement = scene->create_character(character_desc{});
    if (!check(bool(replacement)) || !check(*replacement != *character) ||
        !character_rejects(*scene, *character, error_code::stale_handle) ||
        !rejects(scene->destroy_character(*character), error_code::stale_handle))
        return false;

    // Rejected stale commands must leave the replacement controller unchanged.
    auto state = scene->read_character(*replacement);
    if (!check(state && state->position.x == 0 && state->position.y == 2))
        return false;

    character_desc invalid;
    invalid.radius = 0;
    if (!rejects(scene->create_character(invalid), error_code::invalid_argument) ||
        !check(bool(scene->read_character(*replacement))))
        return false;

    ShapeInstance shape;
    shape.form = box_geometry{};
    auto body = scene->create_body(body_desc{body_kind::static_body, pose{}, std::span(&shape, 1)});
    if (!check(bool(body)))
        return false;

    // Values remain safe to retain after the entire owning scene has been destroyed.
    scene.reset();
    auto next = PhysicsScene::create(scene_config{{0, 0, 0}, 1});
    if (!check(bool(next)) || !character_rejects(**next, *replacement, error_code::wrong_scene) ||
        !rejects((*next)->destroy_character(*replacement), error_code::wrong_scene) ||
        !rejects((*next)->read_body(*body), error_code::wrong_scene) ||
        !rejects((*next)->destroy_body(*body), error_code::wrong_scene))
        return false;

    auto recovered = (*next)->create_character(character_desc{});
    return check(bool(recovered)) && check(bool((*next)->read_character(*recovered)));
}
