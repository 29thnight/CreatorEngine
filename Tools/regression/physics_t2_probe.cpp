#include "../../Engine/Physics/PhysicsScene.h"
#if !CE_SHIPPING
#include "../../Engine/EngineDiagnostics/ProfileScope.h"
#include "../../Engine/EngineDiagnostics/ProfileCaptureFile.h"
#endif
#include <atomic>
#include <barrier>
#include <iostream>
#include <thread>
#include <ranges>

using namespace ce::physics;
namespace
{
int checks = 0;
void check(bool value, const char* message)
{
    ++checks;
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}

bool run(execution_preference execution)
{
    scene_config config;
    config.execution = execution;
    config.gravity = {};
    config.command_capacity = 128;
    config.workers = 2;
    auto created = PhysicsScene::create(config);
    check(bool(created), "create scene");
    auto scene = std::move(*created);
    const bool gpu = scene->status().backend == execution_backend::gpu;
    auto acquired = scene->channel();
    check(bool(acquired), "owner channel acquisition");
    const auto channel = *acquired;
    const auto identity = channel.identity();
    check(identity == scene->status().identity && !channel.is_closed(), "channel identity");

    std::array shapes{ShapeInstance{shape_id{1}}};
    body_desc desc;
    desc.kind = body_kind::dynamic;

    desc.shapes = shapes;
    desc.linear_damping = 0;
    auto body = scene->create_body(desc);
    check(bool(body), "create dynamic body");
    const auto handle = *body;
    std::array<query_hit, 4> hit_storage{};
    std::array requests{
        query_request{ray_query{{0, 5, 0}, {0, -1, 0}, 10}, hit_storage},
        query_request{ray_query{{0, 5, 0}, {}, 10}, {}},
        query_request{overlap_query{sphere_geometry{1.f}, {}}, {}},
        query_request{ray_query{{100, 5, 0}, {0, -1, 0}, 10}, {}}
    };
    std::array<result<query_result>, 4> answers{};
    check(bool(scene->query_batch(requests, answers)), "batch execution");
    check(answers[0] && answers[0]->written == 1, "batch ray hits latest created shape");
    check(!answers[1] && answers[1].error().code == error_code::invalid_argument, "individual error");
    check(answers[2] && answers[2]->required_capacity == 1 && answers[2]->truncated, "zero capacity overflow");
    check(answers[3] && answers[3]->required_capacity == 0, "continue after invalid request");
    check(!scene->query_batch(requests, std::span{answers}.first(1)), "result capacity rejected");
    check(bool(scene->query_batch({}, {})), "empty batch");
    check(bool(scene->set_pose(handle, {{20, 0, 0}, {}})), "mutation after read window");
    check(bool(scene->query_batch(requests, answers)) && answers[0] && answers[0]->written == 0,
          "query structure reflects changed pose");
    std::array sweep_requests{query_request{sweep_query{sphere_geometry{0.25f}, {{0, 5, 0}, {}},
                                                        {0, -1, 0}, 10}, hit_storage}};
    std::array<result<query_result>, 1> sweep_answers{};
    check(bool(scene->set_pose(handle, {})), "restore test body");
    check(bool(scene->query_batch(sweep_requests, sweep_answers)) && sweep_answers[0] &&
          sweep_answers[0]->written == 1, "mixed geometry sweep dispatch");

    std::atomic<bool> owner_rejected{false};
    std::jthread wrong_owner([&] {
        auto invalid = scene->channel();
        check(!scene->query_batch(requests, answers), "foreign thread batch rejected");
        auto mutation = scene->set_velocity(handle, {}, {});
        owner_rejected = !invalid && invalid.error().code == error_code::wrong_phase &&
                         !mutation && mutation.error().code == error_code::wrong_phase;
    });
    wrong_owner.join();
    check(owner_rejected, "job cannot acquire capabilities or mutate SDK");

    std::barrier gate{9};
    std::atomic<int> accepted{0};
    std::vector<std::jthread> jobs;
    for (std::uint32_t producer = 0; producer < 8; ++producer)
        jobs.emplace_back([channel, handle, producer, &gate, &accepted] {
            gate.arrive_and_wait();
            // Reverse arrival within each producer; completion must use logical order.
            for (std::uint64_t sequence = 8; sequence > 0; --sequence)
                if (channel.submit({{channel.identity(), tick_id{1}, producer, sequence},
                                    set_velocity_command{handle, {float(producer * 10 + sequence), 0, 0}, {}}}))
                    ++accepted;
        });
    gate.arrive_and_wait();
    jobs.clear();
    check(accepted == 64, "all independent producers accepted");
    check(bool(scene->begin_step(1.f/60)), "commit and simulate");
    check(!scene->query_batch(requests, answers), "in-flight batch rejected");
    check(bool(scene->finish_step()), "finish simulation");
    const auto retained = channel.latest_snapshot();
    check(retained && retained->step_succeeded && retained->commands.size() == 64, "owned immutable publication");
    for (std::size_t i = 0; i < retained->commands.size(); ++i)
    {
        const auto& outcome = retained->commands[i];
        check(outcome.stamp.producer == i / 8 && outcome.stamp.sequence == i % 8 + 1 && !outcome.failure,
              "deterministic producer/sequence merge");
    }
    check(std::abs(scene->read_body(handle)->linear_velocity.x - 78.f) < 1e-4f, "last logical velocity wins");
    auto duplicate = channel.submit({{identity, tick_id{2}, 7, 8}, set_velocity_command{handle, {}, {}}});
    check(!duplicate && duplicate.error().code == error_code::duplicate_command, "consumed sequence rejected");
    auto late = channel.submit({{identity, tick_id{1}, 9, 1}, set_velocity_command{handle, {}, {}}});
    check(!late && late.error().code == error_code::late_command, "closed tick rejected");
    auto foreign = identity;
    ++foreign.value;
    auto wrong_scene = channel.submit({{foreign, tick_id{2}, 9, 1}, set_velocity_command{handle, {}, {}}});
    check(!wrong_scene && wrong_scene.error().code == error_code::wrong_scene, "foreign scene rejected");

    check(bool(scene->begin_step(1.f/60)), "in flight before owner teardown");
    std::barrier teardown_gate{2};
    std::atomic<bool> safe{true};
    std::atomic<int> racing_submissions{0};
    std::jthread survivor([channel, handle, &teardown_gate, &safe, &racing_submissions] {
        teardown_gate.arrive_and_wait();
        std::uint64_t sequence = 1;
        while (!channel.is_closed())
        {
            auto snapshot = channel.latest_snapshot();
            if (!snapshot || !snapshot->step_succeeded || snapshot->scene != channel.identity()) safe = false;
            auto submitted = channel.submit({{channel.identity(), tick_id{3}, 10, sequence++},
                                            set_velocity_command{handle, {}, {}}});
            if (!submitted && submitted.error().code != error_code::capacity_exceeded &&
                submitted.error().code != error_code::wrong_phase) safe = false;
            ++racing_submissions;
        }
        for (int i = 0; i < 1000; ++i)
        {
            auto rejected = channel.submit({{channel.identity(), tick_id{3}, 10, sequence++},
                                           set_velocity_command{handle, {}, {}}});
            if (rejected || rejected.error().code != error_code::wrong_phase) safe = false;
        }
    });
    teardown_gate.arrive_and_wait();
    while (racing_submissions == 0) std::this_thread::yield();
    scene.reset(); // Owner closes the independent mailbox and drains in-flight SDK work.
    survivor.join();
    check(safe && channel.is_closed(), "channel rejects safely across destruction");
    check(retained->tick.value == 1 && retained->commands.size() == 64, "reader retained old publication after SDK destruction");
    const auto final = channel.latest_snapshot();
    check(final && final->tick.value == 2 && final->step_succeeded, "in-flight final publication survives scene destruction");
    PhysicsSceneChannel empty;
    check(empty.is_closed() && !empty.identity().value && !empty.latest_snapshot() &&
          !empty.submit({{identity, tick_id{3}, 0, 1}, set_velocity_command{handle, {}, {}}}), "empty channel rejected");
    auto replay = PhysicsScene::create(config);
    check(bool(replay), "create independent replay");
    const auto fresh = (*replay)->channel();
    check(fresh && !fresh->is_closed() && fresh->identity() != identity, "new session has a new channel identity");
    auto stale = fresh->submit({{identity, tick_id{1}, 0, 1}, set_velocity_command{handle, {}, {}}});
    check(!stale && stale.error().code == error_code::wrong_scene, "old session request cannot target replay");
    replay->reset();
    check(fresh->is_closed() && channel.is_closed(), "both lifetimes close independently");
#if !CE_SHIPPING
    ce::profiler().publish_frame(gpu ? 2 : 1);
#endif
    return gpu;
}
}

int main(int argc, char** argv)
{
#if !CE_SHIPPING
    auto& profiler = ce::profiler();
    profiler.initialize();
    profiler.register_thread("Physics T2 owner");
    profiler.record(1);
#endif
    run(execution_preference::cpu);
    const bool gpu = run(execution_preference::prefer_gpu);
#if !CE_SHIPPING
    profiler.publish_frame(3);
    profiler.pause();
    profiler.wait_until_idle();
    const auto capture = profiler.capture();
    check(capture && capture->complete() && capture->unacked_streams() == 0, "capture drain includes solver workers");
    check(std::ranges::any_of(capture->frames(), [&](const auto& frame) {
        return std::ranges::any_of(frame.events, [&](const auto& event) {
            return capture->marker(event.marker).name == "Physics.QueryStructureUpdate";
        });
    }), "query update appears in the profiler hierarchy");
    if (argc == 2) check(bool(ce::save_capture(*capture, argv[1])), "save capture");
    profiler.unregister_thread();
    profiler.shutdown();
#else
    (void)argc; (void)argv;
#endif
    std::cout << "{\"result\":\"PHYSICS_T2_OK\",\"checks\":" << checks << ",\"gpu_verified\":" << (gpu ? "true" : "false") << "}\n";
}
