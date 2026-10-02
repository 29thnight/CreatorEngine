#include "../../Engine/Physics/PhysicsScene.h"
#include "../../Engine/Physics/PhysicsTestHooks.h"
#if !CE_SHIPPING
#include "../../Engine/EngineDiagnostics/ProfileScope.h"
#include "../../Engine/EngineDiagnostics/ProfileCaptureFile.h"
#endif
#include <atomic>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <ranges>
#include <thread>

using namespace ce::physics;
static_assert(!std::is_copy_constructible_v<command>);
static_assert(std::is_nothrow_move_constructible_v<command_payload>);
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

command request(PhysicsScene& scene, std::uint64_t tick, std::uint32_t producer, std::uint64_t sequence,
                command_payload payload)
{
    return {{scene.status().identity, tick_id{tick}, producer, sequence}, std::move(payload)};
}

body_definition definition(float x = 0, float y = 10)
{
    std::array shapes{ShapeInstance{shape_id{20}}};
    body_desc desc;
    desc.kind = body_kind::dynamic;
    desc.initial_pose.position = {x, y, 0};
    desc.shapes = shapes;
    desc.mass = 2;
    desc.linear_damping = 0;
    desc.angular_damping = 0;
    auto owned = body_definition::copy(desc);
    check(bool(owned), "owned body definition");
    return std::move(*owned); // The original array dies here.
}

void step(PhysicsScene& scene, float seconds = 1.f / 60)
{
    check(bool(scene.begin_step(seconds)), "begin step");
    check(scene.status().phase == scene_phase::simulating, "visible simulation phase");
    check(bool(scene.finish_step()), "finish step");
    check(scene.status().phase == scene_phase::idle, "publication returns to idle phase");
    const auto snapshot = scene.latest_snapshot();
    check(snapshot && snapshot->step_succeeded && snapshot->scene == scene.status().identity &&
              snapshot->tick == scene.status().last_tick,
          "snapshot publication identity");
    check(snapshot->statistics.unresolved_identities == 0, "all callback identities resolve");
    check(scene.status().sdk_errors == 0, "no SDK reports on valid P3 path");
#if !CE_SHIPPING
    ce::profiler().publish_frame(frame++);
#endif
}

bool has_event(const tick_snapshot& snapshot, event_kind kind, body_handle body)
{
    return std::ranges::any_of(snapshot.events, [&](const auto& event) {
        return event.kind == kind && (event.first.body == body || event.second.body == body);
    });
}

bool character_tests(execution_preference execution)
{
    scene_config config{{0, -9.81f, 0}, 2};
    config.execution = execution;
    auto created = PhysicsScene::create(config);
    check(bool(created), "CCT scene");
    auto scene = std::move(*created);
    const bool gpu = scene->status().backend == execution_backend::gpu;
    const auto box = [&](math::vector3 position, math::vector3 extent, bool sensor = false, std::uint32_t belongs = 1) {
        ShapeInstance shape{shape_id{1}};
        shape.form = box_geometry{extent};
        shape.sensor = sensor;
        shape.filter.belongs_to = belongs;
        body_desc desc;
        desc.initial_pose.position = position;
        desc.shapes = std::span(&shape, 1);
        const auto body = scene->create_body(desc);
        check(bool(body), "CCT obstacle body");
        return *body;
    };
    const auto floor = box({0, -.5f, 0}, {30, .5f, 30});
    character_desc desc;
    desc.position = {0, 3, 0};
#if defined(CE_PHYSICS_TESTING)
    test::next_failure.store(test::failure_point::character_manager);
    check(!scene->create_character(desc), "CCT manager failure rollback");
    test::next_failure.store(test::failure_point::character_creation);
    check(!scene->create_character(desc), "created SDK controller rollback");
#endif
    const auto character = scene->create_character(desc);
    check(bool(character), "independent capsule creation");
    check(std::abs(scene->read_character(*character)->position.y - 3) < 1e-5f, "CCT center position uses metres");
    const auto invalid_before = scene->read_character(*character)->position;
    check(!scene->move_character(*character, {{1, 0, 0}, 0}), "invalid CCT elapsed time rejected");
    check(!scene->move_character(*character, {{NAN, 0, 0}, 1}), "invalid CCT displacement rejected");
    check(!scene->teleport_character(*character, {INFINITY, 0, 0}), "invalid CCT teleport rejected");
    check(scene->read_character(*character)->position == invalid_before, "invalid movement preserves pose");
    auto invalid_desc = desc;
    invalid_desc.radius = 0;
    check(!scene->create_character(invalid_desc), "invalid CCT radius rejected");
    invalid_desc = desc;
    invalid_desc.slope_limit_cosine = 2;
    check(!scene->create_character(invalid_desc), "invalid CCT slope cosine rejected");
    auto move = scene->move_character(*character, {{0, -4, 0}, 1.f / 60});
    check(move && move->below && std::abs(move->foot_position.y) < .01f, "capsule lands on floor with below flag");
    check(move->actual_displacement.y < -1 && move->actual_displacement.y > -3,
          "resolved displacement owns actual collision result");
    auto wall = box({3, 2, 0}, {.5f, 2, 3});
    move = scene->move_character(*character, {{5, -.05f, 0}, 1.f / 60});
    check(move && move->sides && move->position.x < 2.1f && move->position.x > 1.8f,
          "CCT wall stops horizontal movement");
    check(bool(scene->destroy_body(wall)), "remove wall for controller cache invalidation");
    check(bool(scene->teleport_character(*character, {0, 1.05f, 0})), "explicit CCT teleport");
    const auto ceiling = box({0, 3, 0}, {3, .25f, 3});
    move = scene->move_character(*character, {{0, 5, 0}, 1.f / 60});
    check(move && move->above && move->position.y < 1.8f, "capsule ceiling reports above");
    check(bool(scene->destroy_body(ceiling)), "remove ceiling");
    check(bool(scene->teleport_character(*character, {0, 1.05f, 0})), "reset CCT after ceiling");
    const auto sensor = box({2, 2, 0}, {.25f, 2, 3}, true);
    const auto filtered = box({3, 2, 0}, {.25f, 2, 3}, false, 2);
    check(bool(scene->destroy_character(*character)), "destroy first character");
    desc.position = {0, 1.05f, 0};
    desc.collides_with = 1;
    const auto replacement = scene->create_character(desc);
    check(replacement && replacement->slot == character->slot && replacement->generation != character->generation,
          "CCT generation changes on slot reuse");
    check(!scene->read_character(*character) && !scene->destroy_character(*character),
          "stale CCT rejected after reuse");
    move = scene->move_character(*replacement, {{5, -.05f, 0}, 1.f / 60});
    check(move && move->position.x > 4.9f, "CCT ignores sensor and symmetric-filtered body");
    check(bool(scene->destroy_body(sensor)) && bool(scene->destroy_body(filtered)), "remove filtered obstacles");
    check(bool(scene->teleport_character(*replacement, {0, 1.05f, 0})), "reset for CCT pairs");
    desc.position = {2, 1.05f, 0};
    const auto other = scene->create_character(desc);
    check(bool(other), "second independent capsule");
    move = scene->move_character(*replacement, {{4, -.05f, 0}, 1.f / 60});
    check(move && move->sides && move->position.x < 1.1f, "CCT vs CCT collision");
    check(bool(scene->destroy_character(*other)), "destroy second capsule");
    desc.belongs_to = 2;
    const auto filtered_character = scene->create_character(desc);
    check(bool(filtered_character), "filtered CCT pair creation");
    check(bool(scene->teleport_character(*replacement, {0, 1.05f, 0})), "reset for filtered CCT pair");
    move = scene->move_character(*replacement, {{4, -.05f, 0}, 1.f / 60});
    check(move && move->position.x > 3.9f, "symmetric CCT pair filtering");
    check(bool(scene->destroy_character(*filtered_character)), "release filtered CCT");
    desc.belongs_to = 1;
    check(bool(scene->teleport_character(*replacement, {0, 1.05f, 0})), "reset for low step");
    const auto low_step = box({2, .1f, 0}, {.5f, .1f, 3});
    move = scene->move_character(*replacement, {{4, -.05f, 0}, 1.f / 60});
    check(move && move->position.x > 3.9f, "configured CCT step offset climbs low obstacle");
    check(bool(scene->destroy_body(low_step)), "remove low step");
    check(bool(scene->teleport_character(*replacement, {0, 1.05f, 0})), "reset for high step");
    const auto high_step = box({2, .4f, 0}, {.5f, .4f, 3});
    move = scene->move_character(*replacement, {{4, -.05f, 0}, 1.f / 60});
    check(move && move->sides && move->position.x < 1.1f, "configured CCT step offset rejects high obstacle");
    check(bool(scene->destroy_body(high_step)), "remove high step");
    std::array<query_hit, 4> query_hits;
    auto queried = scene->raycast({-2, 1.05f, 0}, {1, 0, 0}, 5, query_hits);
    check(queried && queried->written == 0, "body query excludes private CCT proxy");
    auto foreign = PhysicsScene::create(scene_config{{0, 0, 0}, 1});
    check(foreign && !(*foreign)->read_character(*replacement), "foreign CCT handle rejected");
    std::atomic<bool> owner_rejected{false};
    std::jthread wrong_owner([&] { owner_rejected = !scene->move_character(*replacement, {{1, 0, 0}, 1}); });
    wrong_owner.join();
    check(owner_rejected, "CCT owner thread enforced");
    check(bool(scene->teleport_character(*replacement, {0, 1.05f, 0})), "reset for command movement");
    check(bool(scene->submit_command(
              request(*scene, 1, 0, 1, move_character_command{*replacement, character_move{{1, -.05f, 0}, 1.f / 60}}))),
          "owned CCT move command");
    step(*scene);
    auto snapshot = scene->latest_snapshot();
    check(snapshot->characters.size() == 1 && snapshot->characters[0].character == *replacement &&
              snapshot->characters[0].state.position.x > .99f && snapshot->characters[0].state.below,
          "completed CCT state published with tick");
    check(snapshot->events.empty(), "CCT proxy never leaks into body contact events");
    const auto retained = snapshot;
    check(bool(scene->begin_step(1.f / 60)), "CCT in-flight phase");
    check(!scene->move_character(*replacement, {{1, 0, 0}, 1}) && !scene->read_character(*replacement) &&
              !scene->destroy_character(*replacement),
          "CCT SDK work blocked during simulation");
    check(bool(scene->finish_step()), "CCT fetch phase");
    check(bool(scene->destroy_character(*replacement)) && bool(scene->destroy_character(*replacement)),
          "CCT destroy is idempotent before reuse");
    desc.position = {8, 3, 0};
    check(bool(scene->submit_command(request(*scene, 3, 0, 2, create_character_command{desc}))), "CCT create command");
    step(*scene);
    const auto command_character = scene->latest_snapshot()->commands[0].character_created;
    check(bool(command_character), "CCT command publishes typed created handle");
    check(
        bool(scene->submit_command(request(*scene, 4, 0, 3, teleport_character_command{command_character, {9, 4, 0}}))),
        "CCT teleport command");
    step(*scene);
    check(scene->latest_snapshot()->characters[0].state.position.x == 9, "CCT teleport command publishes result");
    check(bool(scene->submit_command(request(*scene, 5, 0, 4, destroy_character_command{command_character}))),
          "CCT destroy command");
    step(*scene);
    check(scene->latest_snapshot()->commands[0].character_retired == command_character &&
              scene->latest_snapshot()->characters.empty(),
          "CCT destroy outcome and snapshot");
    for (int i = 0; i < 64; ++i)
    {
        const auto handle = scene->create_character(desc);
        check(handle && scene->destroy_character(*handle), "CCT repeated create/release");
    }
#if defined(CE_PHYSICS_TESTING)
    test::next_failure.store(test::failure_point::character_generation_limit);
    const auto last = scene->create_character(desc);
    check(last && last->generation == UINT32_MAX && scene->destroy_character(*last), "CCT maximum generation release");
    const auto next = scene->create_character(desc);
    check(next && next->slot != last->slot, "CCT generation exhausted slot retired");
#endif
    check(scene->status().sdk_errors == 0, "valid CCT path has no SDK error reports");
    check(bool(scene->begin_step(1.f / 60)), "CCT in-flight scene teardown");
    scene.reset();
    check(retained->characters.size() == 1 && retained->characters[0].state.position.x > .99f,
          "CCT snapshot outlives controller manager and scene");
    (void)floor;
    return gpu;
}

void telemetry_tests()
{
    auto created = PhysicsScene::create(scene_config{{0, 0, 0}, 2});
    check(bool(created), "telemetry scene");
    auto scene = std::move(*created);
    auto owned = definition();
    const auto body = scene->create_body(owned.view());
    check(bool(body), "telemetry body");
    const auto character = scene->create_character(character_desc{});
    check(bool(character), "telemetry character");
    check(bool(scene->set_velocity(*body, {1, 0, 0}, {})) && bool(scene->set_pose(*body, {{0, 10, 0}})),
          "repeated changes to one body");
    std::array<query_hit, 0> no_storage;
    const auto queried = scene->raycast({-2, 10, 0}, {1, 0, 0}, 5, no_storage);
    check(queried && queried->truncated, "telemetry query overflow");
    step(*scene);
    auto statistics = scene->latest_snapshot()->statistics;
    check(statistics.bodies == 1 && statistics.shapes == 1 && statistics.characters == 1 &&
              statistics.changed_bodies == 1 && statistics.changed_shapes == 1 && statistics.changed_characters == 1,
          "O(1) counts and distinct modified handle counts");
    check(statistics.queries == 1 && statistics.query_hits == 1 && statistics.query_overflows == 1 &&
              statistics.query_scratch_peak_bytes > 0,
          "query execution and caller overflow counted since prior publication");
    check(statistics.workers == 2 && statistics.tasks_submitted == statistics.tasks_completed &&
              statistics.active_bodies == 1 && statistics.active_shapes == 1,
          "worker/task/active shape metrics reflect real work");
    check(statistics.tick_buffer_bytes > 0 && statistics.tick_buffer_peak_bytes >= statistics.tick_buffer_bytes,
          "API tick storage capacity in bytes");
    step(*scene);
    statistics = scene->latest_snapshot()->statistics;
    check(statistics.changed_bodies == 0 && statistics.changed_characters == 0 && statistics.queries == 0 &&
              statistics.query_scratch_peak_bytes > 0,
          "interval deltas reset while lifetime high-water remains");
    check(bool(scene->destroy_body(*body)) && bool(scene->destroy_character(*character)), "telemetry owners destroyed");
    step(*scene);
    statistics = scene->latest_snapshot()->statistics;
    check(statistics.bodies == 0 && statistics.shapes == 0 && statistics.characters == 0 &&
              statistics.changed_bodies == 1 && statistics.changed_characters == 1,
          "destroyed generations contribute changes without remaining in gauges");
}

void snapshot_pool_tests()
{
    scene_config config{{0, 0, 0}, 1};
    config.snapshot_capacity = 2;
    auto result = PhysicsScene::create(config);
    check(bool(result), "bounded snapshot scene");
    auto scene = std::move(*result);
    step(*scene);
    auto first = scene->latest_snapshot();
    const std::weak_ptr<const tick_snapshot> old_control = first;
    step(*scene);
    auto second = scene->latest_snapshot();
    check(second->statistics.snapshot_buffers == 2, "snapshot pool reaches two buffers");
    const auto exhausted = scene->begin_step(1.f / 60);
    check(!exhausted && exhausted.error().code == error_code::capacity_exceeded &&
              scene->status().last_tick.value == 2 && scene->status().phase == scene_phase::idle,
          "retained readers apply backpressure before closing input");
    first.reset();
    check(old_control.expired(), "released snapshot control block expires");
    second.reset();
    for (int i = 0; i < 20; ++i)
    {
        step(*scene);
        check(scene->latest_snapshot()->statistics.snapshot_buffers == 2 && old_control.expired(),
              "warm buffer reuse remains bounded and never revives weak readers");
    }
    auto retained = scene->latest_snapshot();
    scene.reset();
    check(retained->step_succeeded && retained->tick.value == 22,
          "snapshot remains valid after its pool and scene are destroyed");
}

void command_tests()
{
    scene_config config{{0, 0, 0}, 2};
    config.command_capacity = 4;
    auto result = PhysicsScene::create(config);
    check(bool(result), "command scene");
    auto scene = std::move(*result);
    check(!scene->latest_snapshot(), "no invented pre-step snapshot");
    const std::array<math::vector3, 8> points{{{-.5f, -.5f, -.5f},
                                               {.5f, -.5f, -.5f},
                                               {-.5f, .5f, -.5f},
                                               {.5f, .5f, -.5f},
                                               {-.5f, -.5f, .5f},
                                               {.5f, -.5f, .5f},
                                               {-.5f, .5f, .5f},
                                               {.5f, .5f, .5f}}};
    auto cooked = scene->cook_convex(points);
    check(bool(cooked), "cooked command asset");
    const std::weak_ptr<const CollisionGeometry> retained_asset = *cooked;
    auto owned = definition();
    owned.shapes[0].form = cooked_geometry{std::move(*cooked)};
    auto creation = request(*scene, 1, 0, 1, create_body_command{std::move(owned)});
    check(bool(scene->submit_command(std::move(creation))), "owned create enqueue");
    check(!retained_asset.expired(), "queued command owns cooked asset after producer drops it");
    check(!scene->submit_command(request(*scene, 1, 0, 1, create_body_command{definition()})),
          "duplicate reserved sequence");
    auto invalid = request(*scene, 1, 256, 1, destroy_body_command{});
    check(!scene->submit_command(std::move(invalid)), "producer bounds");
    invalid = request(*scene, 0, 0, 2, destroy_body_command{});
    check(!scene->submit_command(std::move(invalid)), "tick zero rejected");
    invalid = request(*scene, 1, 0, 0, destroy_body_command{});
    check(!scene->submit_command(std::move(invalid)), "sequence zero rejected");
    invalid = request(*scene, 1, 0, 2, destroy_body_command{});
    invalid.stamp.scene.value++;
    auto foreign = scene->submit_command(std::move(invalid));
    check(!foreign && foreign.error().code == error_code::wrong_scene, "foreign command rejected");
    check(!scene->begin_step(0) && scene->status().last_tick.value == 0, "invalid step keeps input open");
#if defined(CE_PHYSICS_TESTING)
    test::next_failure.store(test::failure_point::snapshot_allocation);
    auto allocation = scene->begin_step(1.f / 60);
    check(!allocation && allocation.error().code == error_code::out_of_memory && scene->status().last_tick.value == 0,
          "publication OOM consumes neither tick nor command");
#endif
    step(*scene);
    const auto first = scene->latest_snapshot();
    check(first->commands.size() == 1 && !first->commands[0].failure && first->commands[0].created,
          "create outcome supplies handle");
    check(first->statistics.command_rejections == 5 && first->commands[0].waited_ticks == 1 &&
              first->statistics.max_command_wait_ticks == 1,
          "input rejection and residence metrics");
    const auto body = first->commands[0].created;
    check(scene->read_body(body)->mass == 2 && first->statistics.commands_applied == 1, "owned payload applied");
    auto late = scene->submit_command(request(*scene, 1, 0, 2, destroy_body_command{body}));
    check(!late && late.error().code == error_code::late_command, "closed tick rejected");
    auto consumed = scene->submit_command(request(*scene, 2, 0, 1, destroy_body_command{body}));
    check(!consumed && consumed.error().code == error_code::duplicate_command, "consumed sequence rejected");

    check(bool(scene->submit_command(request(*scene, 2, 1, 1, set_velocity_command{body, {2, 0, 0}, {}}))),
          "AI first arrival");
    check(bool(scene->submit_command(request(*scene, 2, 0, 3, set_velocity_command{body, {1, 0, 0}, {}}))),
          "main later sequence arrives early");
    check(bool(scene->submit_command(request(*scene, 2, 0, 2, set_velocity_command{body, {.5f, 0, 0}, {}}))),
          "main earlier sequence arrives late");
    check(bool(scene->submit_command(request(*scene, 4, 0, 4, set_velocity_command{body, {4, 0, 0}, {}}))),
          "future tick held");
    auto full = scene->submit_command(request(*scene, 3, 2, 1, destroy_body_command{body}));
    check(!full && full.error().code == error_code::capacity_exceeded, "bounded backpressure");
    check(bool(scene->begin_step(1.f / 60)), "input closure");
    std::atomic<bool> future_accepted{false}, late_rejected{false};
    {
        std::jthread producer([&] {
            future_accepted =
                bool(scene->submit_command(request(*scene, 3, 2, 1, set_velocity_command{body, {3, 0, 0}, {}})));
            auto late_result = scene->submit_command(request(*scene, 2, 3, 1, destroy_body_command{body}));
            late_rejected = !late_result && late_result.error().code == error_code::late_command;
        });
    }
    check(future_accepted && late_rejected, "producer works during simulate without touching SDK");
    check(!scene->read_body(body), "owner read blocked while simulating");
    check(bool(scene->finish_step()), "finish ordered commands");
    auto ordered = scene->latest_snapshot();
    check(ordered->statistics.command_overflows >= 1 && ordered->statistics.command_rejections >= 3,
          "bounded queue rejection is counted independently from failed commits");
    check(ordered->commands.size() == 3 && ordered->commands[0].stamp.sequence == 2 &&
              ordered->commands[1].stamp.sequence == 3 && ordered->commands[2].stamp.producer == 1,
          "stable tick/producer/sequence order");
    check(std::abs(scene->read_body(body)->linear_velocity.x - 2.f) < 1e-5f,
          "AI priority follows main regardless of arrival");
    step(*scene);
    check(scene->latest_snapshot()->commands.size() == 1 && scene->read_body(body)->linear_velocity.x == 3,
          "tick 3 future command applies exactly then");
    step(*scene);
    check(scene->latest_snapshot()->commands.size() == 1 && scene->read_body(body)->linear_velocity.x == 4,
          "tick 4 command not pulled forward");

    auto bad_definition = definition();
    bad_definition.properties.mass = -1;
    check(bool(scene->submit_command(request(*scene, 5, 0, 5, create_body_command{std::move(bad_definition)}))),
          "invalid payload remains an explicit commit outcome");
    check(bool(scene->submit_command(request(*scene, 5, 0, 6, destroy_body_command{body}))),
          "valid command after failed command");
    step(*scene);
    const auto deleted = scene->latest_snapshot();
    check(deleted->commands.size() == 2 && deleted->commands[0].failure && !deleted->commands[1].failure &&
              deleted->statistics.commands_failed == 1 && deleted->statistics.commands_applied == 1,
          "per-command failure does not roll back independent successors");
    check(!scene->read_body(body) && first->commands[0].created == body && first->tick.value == 1,
          "stale handle and retained immutable snapshot");
    check(retained_asset.expired(), "retired actor releases cooked asset after fetch, snapshots keep no SDK owner");
    scene.reset();
    check(first->commands[0].created == body && deleted->commands[1].stamp.tick.value == 5, "snapshot outlives scene");
}

void concurrent_tests()
{
    auto result = PhysicsScene::create(scene_config{{0, 0, 0}, 4});
    check(bool(result), "concurrent scene");
    auto scene = std::move(*result);
    const auto desc = definition();
    auto created = scene->create_body(desc.view());
    check(bool(created), "concurrent body");
    const auto body = *created;
    std::atomic<unsigned> failures{0};
    {
        std::vector<std::jthread> producers;
        for (std::uint32_t producer = 0; producer < 4; ++producer)
            producers.emplace_back([&, producer] {
                for (std::uint64_t sequence = 25; sequence > 0; --sequence)
                    if (!scene->submit_command(
                            request(*scene, 1, producer, sequence,
                                    set_velocity_command{body, {float(100 * producer + sequence), 0, 0}, {}})))
                        ++failures;
            });
    }
    check(failures == 0, "100 concurrent owned commands accepted");
    step(*scene);
    const auto retained = scene->latest_snapshot();
    check(retained->commands.size() == 100 && scene->read_body(body)->linear_velocity.x == 325,
          "concurrent arrival does not change deterministic merge order");
    std::atomic<std::uint64_t> observations{0};
    {
        std::jthread reader([&](std::stop_token stop) {
            while (!stop.stop_requested())
            {
                const auto snapshot = scene->latest_snapshot();
                if (!snapshot)
                    continue;
                ++observations;
                for (const auto& outcome : snapshot->commands)
                    if (outcome.stamp.tick != snapshot->tick || outcome.failure)
                        ++failures;
            }
        });
        while (observations == 0)
            std::this_thread::yield();
        for (std::uint64_t tick = 2; tick <= 20; ++tick)
        {
            check(bool(scene->submit_command(
                      request(*scene, tick, 0, 25 + tick, set_velocity_command{body, {float(tick), 0, 0}, {}}))),
                  "next concurrent publication");
            step(*scene);
        }
        reader.request_stop();
    }
    check(failures == 0 && observations > 0 && retained->commands.size() == 100 && retained->tick.value == 1,
          "concurrent readers observe whole immutable snapshots");
}

bool event_tests(execution_preference execution)
{
    scene_config config{{0, -9.81f, 0}, 2, 1024, execution};
    auto result = PhysicsScene::create(config);
    check(bool(result), "event scene");
    auto scene = std::move(*result);
    const bool gpu = scene->status().backend == execution_backend::gpu;
    std::array floor_shapes{ShapeInstance{shape_id{10}, box_geometry{{5, .5f, 5}}}};
    body_desc floor_desc;
    floor_desc.initial_pose.position = {0, -.5f, 0};
    floor_desc.shapes = floor_shapes;
    const auto floor = scene->create_body(floor_desc);
    check(bool(floor), "floor");
    auto desc = definition(0, .45f);
    auto body_result = scene->create_body(desc.view());
    check(bool(body_result), "contact body");
    const auto body = *body_result;
    step(*scene);
    auto first = scene->latest_snapshot();
    check(has_event(*first, event_kind::contact_begin, body), "actual contact begin");
    check(first->statistics.required_events == first->events.size() && first->statistics.dropped_events == 0,
          "events exact capacity accounting");
    bool contacts = false;
    for (const auto& event : first->events)
    {
        if (event.kind != event_kind::contact_begin)
            continue;
        check(event.first.body == *floor && event.first.shape.value == 10 && event.second.body == body &&
                  event.second.shape.value == 20,
              "canonical body and shape IDs");
        check(event.contact_count > 0 && event.required_contacts >= event.contact_count, "owned contact manifold");
        for (auto index : std::views::iota(0u, event.contact_count))
            check(std::isfinite(event.contacts[index].position.y) && std::isfinite(event.contacts[index].normal.y),
                  "finite owned contact values");
        contacts = true;
    }
    check(contacts && !first->active_poses.empty(), "active dynamic pose collected after fetch");
    step(*scene);
    check(has_event(*scene->latest_snapshot(), event_kind::contact_persist, body), "awake contact persist");
    check(bool(scene->set_pose(body, pose{{0, 10, 0}})), "separate body");
    step(*scene);
    check(has_event(*scene->latest_snapshot(), event_kind::contact_end, body), "actual contact end");
    check(bool(scene->set_pose(body, pose{{0, .45f, 0}})), "restore contact");
    check(bool(scene->set_velocity(body, {}, {})), "reset contact velocity");
#if defined(CE_PHYSICS_TESTING)
    test::next_failure.store(test::failure_point::contact_storage_limit);
#endif
    step(*scene);
#if defined(CE_PHYSICS_TESTING)
    const auto limited = scene->latest_snapshot();
    check(limited->statistics.dropped_contacts > 0 &&
              std::ranges::any_of(limited->events,
                                  [](const auto& event) { return event.required_contacts > event.contact_count; }),
          "partial contact storage is explicit");
#endif
    const auto next_tick = scene->status().last_tick.value + 1;
    check(bool(scene->submit_command(request(*scene, next_tick, 0, 1, destroy_body_command{body}))),
          "delete contacting body");
    check(bool(scene->submit_command(request(*scene, next_tick, 1, 1, create_body_command{definition(0, .45f)}))),
          "create after delete in same commit");
    step(*scene);
    auto replaced = scene->latest_snapshot();
    const auto replacement = replaced->commands[1].created;
    check(replacement.slot == body.slot && replacement.generation != body.generation &&
              has_event(*replaced, event_kind::contact_end, body),
          "removed shape preserves old generation across slot reuse");
    check(!scene->read_body(body) && first->events.front().second.body == body,
          "deleted body and old event ownership remain distinct");
    const auto original_y = first->events.front().contacts[0].position.y;
    scene.reset();
    check(first->events.front().contacts[0].position.y == original_y && first->events.front().second.body == body,
          "contact values outlive SDK scene and retired shapes");
    return gpu;
}

void sensor_and_overflow_tests()
{
    auto result = PhysicsScene::create(scene_config{{0, 0, 0}, 2});
    check(bool(result), "sensor scene");
    auto scene = std::move(*result);
    std::array sensor_shapes{ShapeInstance{shape_id{11}, sphere_geometry{2}}};
    sensor_shapes[0].sensor = true;
    body_desc sensor_desc;
    sensor_desc.shapes = sensor_shapes;
    auto sensor = scene->create_body(sensor_desc);
    auto desc = definition(0, 0);
    auto body = scene->create_body(desc.view());
    check(sensor && body, "sensor bodies");
    step(*scene);
    auto entered = scene->latest_snapshot();
    check(has_event(*entered, event_kind::sensor_enter, *body) &&
              std::ranges::any_of(entered->events,
                                  [](const auto& event) { return event.first.sensor || event.second.sensor; }),
          "sensor enter has shape ownership");
    check(bool(scene->destroy_body(*sensor)), "delete occupied sensor while idle");
    step(*scene);
    check(has_event(*scene->latest_snapshot(), event_kind::sensor_exit, *sensor),
          "removed sensor exit preserves identity");

    scene_config small{{0, -9.81f, 0}, 2};
    small.event_capacity = 1;
    auto overflow = PhysicsScene::create(small);
    check(bool(overflow), "overflow scene");
    std::array floor_shapes{ShapeInstance{shape_id{1}, box_geometry{{10, .5f, 10}}}};
    body_desc floor;
    floor.shapes = floor_shapes;
    floor.initial_pose.position = {0, -.5f, 0};
    check(bool((*overflow)->create_body(floor)), "overflow floor");
    for (float x : {-2.f, 0.f, 2.f})
    {
        auto box = definition(x, .45f);
        check(bool((*overflow)->create_body(box.view())), "overflow contact body");
    }
    step(**overflow);
    const auto snapshot = (*overflow)->latest_snapshot();
    check(snapshot->events.size() == 1 && snapshot->statistics.dropped_events > 0 &&
              snapshot->statistics.required_events == snapshot->events.size() + snapshot->statistics.dropped_events,
          "bounded event collector reports every dropped event");
}

void force_and_failure_tests()
{
    auto result = PhysicsScene::create(scene_config{{0, 0, 0}, 1});
    check(bool(result), "force scene");
    auto scene = std::move(*result);
    auto desc = definition();
    auto body = scene->create_body(desc.view());
    check(bool(body), "force body");
    std::uint64_t tick = 1;
    for (const auto [mode, value, expected] :
         {std::tuple{force_mode::force, 2.f, 1.f / 60}, std::tuple{force_mode::impulse, 2.f, 1.f},
          std::tuple{force_mode::acceleration, 3.f, .05f}, std::tuple{force_mode::velocity_change, 4.f, 4.f}})
    {
        check(bool(scene->set_velocity(*body, {}, {})), "clear velocity");
        check(bool(scene->submit_command(
                  request(*scene, tick, 0, tick, apply_force_command{*body, {value, 0, 0}, {}, mode}))),
              "typed force enqueue");
        step(*scene);
        check(std::abs(scene->read_body(*body)->linear_velocity.x - expected) < 1e-4f,
              "force modes preserve units without command coalescing");
        ++tick;
    }
    auto invalid = scene->apply_force(*body, {0, 0, 0}, {}, static_cast<force_mode>(255));
    check(!invalid && invalid.error().code == error_code::invalid_argument, "invalid force mode rejected");

    const auto original = *body;
    auto replacement = definition(11, 10);
    replacement.properties.mass = -1;
    check(bool(scene->submit_command(request(*scene, tick, 0, tick, replace_body_command{original, replacement}))),
          "replacement validation failure enqueue");
    step(*scene);
    check(scene->latest_snapshot()->commands[0].failure && scene->read_body(original)->mass == 2,
          "failed replacement keeps complete original body");
    ++tick;
#if defined(CE_PHYSICS_TESTING)
    replacement.properties.mass = 3;
    test::next_failure.store(test::failure_point::body_shape);
    check(bool(scene->submit_command(request(*scene, tick, 0, tick, replace_body_command{original, replacement}))),
          "partial replacement failure enqueue");
    step(*scene);
    check(scene->latest_snapshot()->commands[0].failure && scene->read_body(original)->mass == 2,
          "partial SDK construction failure keeps original handle and actor");
    ++tick;
#endif
    replacement.properties.mass = 3;
    replacement.shapes[0].id = shape_id{77};
    replacement.shapes[0].form = sphere_geometry{1};
    replacement.shapes[0].surface.restitution = .25f;
    replacement.shapes[0].filter.query_layers = 8;
    check(bool(scene->submit_command(
              request(*scene, tick, 0, tick, replace_body_command{original, std::move(replacement)}))),
          "complete shape/material/filter replacement enqueue");
    step(*scene);
    const auto replaced = scene->latest_snapshot();
    check(!replaced->commands[0].failure && replaced->commands[0].retired == original &&
              replaced->commands[0].created && !scene->read_body(original),
          "replacement remaps handle explicitly");
    body = replaced->commands[0].created;
    check(scene->read_body(*body)->mass == 3 && std::abs(scene->read_body(*body)->inertia.x - 1.2f) < 1e-4f,
          "replacement recomputes mass and sphere inertia");
    std::array<query_hit, 4> hits;
    auto query = scene->raycast({9, 10, 0}, {1, 0, 0}, 4, hits, query_filter{8});
    check(query && query->written == 1 && hits[0].body == *body && hits[0].shape.value == 77,
          "replacement owns new shape ID and query filter");
    query = scene->raycast({9, 10, 0}, {1, 0, 0}, 4, hits, query_filter{1});
    check(query && query->written == 0, "old query layer is removed atomically");
    ++tick;

    auto kinematic_definition = definition();
    kinematic_definition.properties.kind = body_kind::kinematic;
    const auto kinematic = scene->create_body(kinematic_definition.view());
    check(bool(kinematic), "kinematic body for command");
    check(bool(scene->submit_command(request(*scene, scene->status().last_tick.value + 1, 1, 1,
                                             set_kinematic_target_command{*kinematic, pose{{20, 10, 0}}}))),
          "kinematic target command");
    step(*scene);
    check(std::abs(scene->read_body(*kinematic)->transform.position.x - 20.f) < 1e-5f,
          "kinematic target applied before simulate");
    check(!scene->apply_force(*kinematic, {1, 0, 0}, {}), "force never mutates kinematic actor");
#if defined(CE_PHYSICS_TESTING)
    tick = scene->status().last_tick.value + 1;
    const auto retained = scene->latest_snapshot();
    check(bool(scene->submit_command(request(*scene, tick, 0, tick, set_velocity_command{*body, {7, 0, 0}, {}}))),
          "failure step command");
    check(bool(scene->submit_command(request(*scene, tick + 1, 2, 1, create_body_command{definition()}))),
          "future request before terminal failure");
    check(bool(scene->begin_step(1.f / 60)), "failure step begin");
    test::next_failure.store(test::failure_point::step_fetch);
    const auto failed = scene->finish_step();
    const auto diagnostic = scene->latest_snapshot();
    check(!failed && failed.error().code == error_code::backend_initialization && diagnostic &&
              !diagnostic->step_succeeded && diagnostic->failure && diagnostic->commands.size() == 2 &&
              diagnostic->commands[1].cancelled && diagnostic->statistics.commands_cancelled == 1 &&
              diagnostic->active_poses.empty(),
          "fetch failure publishes explicit diagnostics, never valid poses");
    check(retained->step_succeeded && retained->tick.value == tick - 1, "failed step never mutates prior snapshot");
    check(scene->status().failed && !scene->begin_step(1.f / 60) && !scene->read_body(*body),
          "terminal failure blocks further SDK work");
    check(scene->status().phase == scene_phase::failed, "terminal failure phase is visible");
    check(!scene->submit_command(request(*scene, tick + 2, 2, 2, create_body_command{definition()})),
          "terminal failure blocks new producers");
#endif
}
} // namespace

int main(int argc, char** argv)
{
#if !CE_SHIPPING
    auto& profiler = ce::profiler();
    profiler.initialize();
    profiler.register_thread("PhysicsP3Probe", ce::track_kind::game_thread);
    profiler.record(1);
#endif
    character_tests(execution_preference::cpu);
    const bool cct_gpu_verified = character_tests(execution_preference::prefer_gpu);
    telemetry_tests();
    snapshot_pool_tests();
    command_tests();
    concurrent_tests();
    event_tests(execution_preference::cpu);
    const bool gpu_verified = event_tests(execution_preference::prefer_gpu) && cct_gpu_verified;
    sensor_and_overflow_tests();
    force_and_failure_tests();
#if !CE_SHIPPING
    profiler.publish_frame(frame++);
    profiler.pause();
    profiler.wait_until_idle();
    const auto capture = profiler.capture();
    check(capture && capture->complete() && capture->unacked_streams() == 0, "complete P3 capture");
    for (const auto* name :
         {"Physics.CommandCommit", "Physics.EventCollect", "Physics.ActivePoseCollect", "Physics.SnapshotPublish",
          "Physics.CharacterMovement", "Physics.CharacterPoseCollect", "Physics.Counters"})
        check(std::ranges::any_of(capture->frames(),
                                  [&](const auto& frame) {
                                      return std::ranges::any_of(frame.events, [&](const auto& event) {
                                          return capture->marker(event.marker).name == name && event.cpu.session != 0 &&
                                                 event.cpu.tick != 0;
                                      });
                                  }),
              "actual P3 hierarchy with tick ownership");
    check(capture->dropped_counters() == 0, "physics counters have no collector loss");
    bool owned_samples = false, failed_sample = false;
    for (const auto& frame : capture->frames())
        for (const auto& sample : frame.counters)
        {
            const auto* descriptor = ce::find_counter(capture->counter_descriptors(), sample.id);
            if (!descriptor || descriptor->category != ce::counter_category::physics)
                continue;
            owned_samples = true;
            check(sample.cpu.session != 0 && sample.cpu.tick != 0 && sample.cpu.task == 0,
                  "physics sample belongs to one scene and tick");
            failed_sample |= sample.id == ce::profile_counter_id::physics_step_failed && sample.value == 1;
        }
    check(owned_samples, "real physics counter samples captured");
#if defined(CE_PHYSICS_TESTING)
    check(failed_sample, "terminal fetch failure remains visible in counters");
#endif
    const auto decoded = ce::decode_capture(ce::encode_capture(*capture));
    check(bool(decoded), "physics counters reload from ceprof");
    if (decoded)
    {
        bool preserved = (*decoded)->frames().size() == capture->frames().size();
        for (std::size_t i = 0; preserved && i < capture->frames().size(); ++i)
        {
            const auto& before = capture->frames()[i].counters;
            const auto& after = (*decoded)->frames()[i].counters;
            preserved &= before.size() == after.size();
            for (std::size_t j = 0; preserved && j < before.size(); ++j)
                preserved &= before[j].id == after[j].id && before[j].value == after[j].value &&
                             before[j].cpu.session == after[j].cpu.session && before[j].cpu.tick == after[j].cpu.tick;
        }
        check(preserved, "all physical counter values and ownership survive reload");
    }
    if (argc == 2)
        check(bool(ce::save_capture(*capture, argv[1])), "save P3 capture");
    profiler.unregister_thread();
    profiler.shutdown();
#else
    (void)argc;
    (void)argv;
#endif
    std::cout << "{\"result\":\"PHYSICS_P3_OK\",\"checks\":" << checks
              << ",\"gpu_verified\":" << (gpu_verified ? "true" : "false") << "}\n";
}
