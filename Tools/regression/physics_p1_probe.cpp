#include "../../Engine/Physics/PhysicsScene.h"
#include "../../Engine/Physics/PhysicsTestHooks.h"
#include "../../Engine/EngineDiagnostics/ProfileService.h"
#include "../../Engine/EngineDiagnostics/ProfileScope.h"
#include "../../Engine/EngineDiagnostics/ProfileCaptureFile.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <thread>
#include <algorithm>
#include <vector>
#include <ranges>
#include <type_traits>
#include <map>
#include <array>

using namespace ce::physics;
static_assert(!std::is_convertible_v<body_handle, character_handle>);
static_assert(!std::is_move_constructible_v<PhysicsScene>);
int main(int argc, char** argv)
{
    int checks = 0;
    const auto check = [&](bool value, std::source_location location = std::source_location::current()) {
        ++checks;
        if (!value)
        {
            std::cerr << "check failed " << checks << " line " << location.line() << '\n';
            std::exit(1);
        }
    };

    auto& profiler = ce::profiler();
    const auto wait_recording_finalized = [&] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (profiler.recording_status().state != ce::recording_state::finalized &&
               profiler.recording_status().state != ce::recording_state::failed &&
               std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));

        check(profiler.recording_status().state == ce::recording_state::finalized);
    };
    profiler.initialize();
    profiler.register_thread("PhysicsProbe", ce::track_kind::game_thread);
    profiler.record(1);
    profiler.wait_until_idle(); // Recording admission is asynchronous.
    profiler.publish_frame(0);
    check(!body_handle{});
    check(bool(body_handle{scene_id{1}, 0, 1}));
    for (auto config : {scene_config{{0, 0, 0}, 257}, scene_config{{0, 0, 0}, 1, 0},
                        scene_config{{0, (std::numeric_limits<float>::quiet_NaN)(), 0}, 1}})
    {
        auto bad = PhysicsScene::create(config);
        check(!bad && bad.error().code == error_code::invalid_argument);
        check(bad.error().location.line() != 0);
    }
    std::uint64_t tasks = 0;
    for (auto point : {test::failure_point::foundation, test::failure_point::physics, test::failure_point::dispatcher,
                       test::failure_point::scene, test::failure_point::allocation})
    {
        test::next_failure.store(point);
        auto failed = PhysicsScene::create(scene_config{{0, 0, 0}, 1});
        check(!failed &&
              failed.error().code == (point == test::failure_point::allocation ? error_code::out_of_memory
                                                                               : error_code::backend_initialization));
        auto recovered = PhysicsScene::create(scene_config{{0, 0, 0}, 1});
        check(bool(recovered) && (*recovered)->status().sdk_errors == 0);
    }
    scene_id previous;
    for (int repetition = 0; repetition < 8; ++repetition)
    {
        auto first = PhysicsScene::create(scene_config{{0, -9.81f, 0}, 2, 1});
        check(bool(first));
        auto scene = std::move(*first);
        check(scene->status().identity != previous);
        previous = scene->status().identity;
        check(!scene->status().gpu_requested && scene->status().backend == execution_backend::cpu);
        auto second = PhysicsScene::create(scene_config{{0, 0, 0}, 1});
        check(bool(second) && (*second)->status().identity != scene->status().identity);
        check(!scene->finish_step());
        check(!scene->begin_step(0.f));
        check(!scene->begin_step((std::numeric_limits<float>::infinity)()));
        std::jthread wrong_thread([&] {
            auto wrong = scene->begin_step(1.f / 60);
            check(!wrong && wrong.error().code == error_code::wrong_phase);
        });
        wrong_thread.join();
        for (int step = 0; step < 10; ++step)
        {
            ce::profile_scope tick{ce::marker<"PhysicsProbe.Tick">()};
            check(bool(scene->begin_step(1.f / 60)));
            check(!scene->begin_step(1.f / 60));
            check(bool(scene->finish_step()));
        }
        check(scene->status().sdk_errors == 0);
        check(scene->status().submitted_tasks == scene->status().completed_tasks);
        tasks += scene->status().completed_tasks;
        check(bool(scene->begin_step(1.f / 60)));
        scene.reset(); // Must fetch and drain outstanding SDK work before releasing resources.
        check(bool((*second)->begin_step(1.f / 60)) && bool((*second)->finish_step()));
        second->reset();
        profiler.publish_frame(repetition + 1);
    }
    check(tasks > 0);
    std::atomic<int> concurrency_failures{0};
    {
        std::vector<std::jthread> callers;
        for (int worker = 0; worker < 4; ++worker)
            callers.emplace_back([&] {
                profiler.register_thread("ConcurrentSceneOwner", ce::track_kind::game_thread);
                for (int iteration = 0; iteration < 20; ++iteration)
                {
                    auto scene = PhysicsScene::create(scene_config{{0, 0, 0}, 1});
                    if (!scene || !(*scene)->begin_step(1.f / 60) || !(*scene)->finish_step() ||
                        (*scene)->status().sdk_errors)
                        ++concurrency_failures;
                }
                profiler.unregister_thread();
            });
    }
    check(concurrency_failures == 0);
    const scene_config gpu_config{{0, -9.81f, 0}, 2, 1024, execution_preference::prefer_gpu};
    {
        test::next_failure.store(test::failure_point::cuda_context);
        auto fallback = PhysicsScene::create(gpu_config);
        check(bool(fallback));
        const auto status = (*fallback)->status();
        check(status.gpu_requested && status.gpu_unavailable && status.backend == execution_backend::cpu);
        check(status.gpu_fallback == gpu_fallback_reason::context_unavailable);
        check(!status.gpu_dynamics && !status.gpu_broadphase);
        check(bool((*fallback)->begin_step(1.f / 60)) && bool((*fallback)->finish_step()));
        check((*fallback)->status().sdk_errors == 0);
    }

    bool gpu_verified = false;
    gpu_fallback_reason actual_fallback = gpu_fallback_reason::none;
    {
        auto selected = PhysicsScene::create(gpu_config);
        check(bool(selected));
        const auto status = (*selected)->status();
        actual_fallback = status.gpu_fallback;
        gpu_verified = status.backend == execution_backend::gpu;
        check(status.gpu_requested);
        check(gpu_verified ? (!status.gpu_unavailable && status.gpu_dynamics && status.gpu_broadphase)
                           : (status.gpu_unavailable && !status.gpu_dynamics && !status.gpu_broadphase));
        for (int step = 0; step < 10; ++step)
            check(bool((*selected)->begin_step(1.f / 60)) && bool((*selected)->finish_step()));
        check((*selected)->status().submitted_tasks == (*selected)->status().completed_tasks);
        if (gpu_verified)
            check((*selected)->status().sdk_errors == 0);
        check(bool((*selected)->begin_step(1.f / 60))); // GPU in-flight destruction also fetches before CUDA release.
    }

    if (gpu_verified)
    {
        test::next_failure.store(test::failure_point::gpu_scene);
        auto fallback = PhysicsScene::create(gpu_config);
        check(bool(fallback));
        const auto status = (*fallback)->status();
        check(status.gpu_fallback == gpu_fallback_reason::scene_rejected);
        check(status.backend == execution_backend::cpu && !status.gpu_dynamics && !status.gpu_broadphase);
        check(bool((*fallback)->begin_step(1.f / 60)) && bool((*fallback)->finish_step()));
        check((*fallback)->status().sdk_errors == 0);
    }
    // Keep the SDK workers alive and idle: destruction must not be what makes
    // their last task visible or turns an incomplete capture into a complete one.
    auto live_scene = PhysicsScene::create(scene_config{{0, -9.81f, 0}, 2});
    check(bool(live_scene));

    ce::capture_session_ptr baseline_capture;
    for (std::uint32_t iteration = 0; iteration < 20; ++iteration)
    {
        const auto capture_frame = iteration == 0 ? 9u : 20 + iteration;
        if (iteration != 0)
        {
            profiler.record(20 + iteration);
            profiler.wait_until_idle();
        }

        const auto previous_tasks = (*live_scene)->status().completed_tasks;
        check(bool((*live_scene)->begin_step(1.f / 60)));
        check(bool((*live_scene)->finish_step()));
        const auto status = (*live_scene)->status();
        check(status.submitted_tasks == status.completed_tasks);
        check(status.last_tick.value == iteration + 1);

        // No sleep, synthetic worker event, or worker teardown before freeze.
        profiler.publish_frame(capture_frame);
        profiler.pause();
        profiler.wait_until_idle();
        wait_recording_finalized();
        const auto live_capture = profiler.capture();
        const auto diagnostics = profiler.summary();
        if (!live_capture || !live_capture->complete() || live_capture->unacked_streams() != 0)
            std::cerr << "iteration=" << iteration << " state=" << static_cast<int>(profiler.state())
                      << " capture=" << bool(live_capture) << " unacked="
                      << (live_capture ? live_capture->unacked_streams() : 0)
                      << " dropped=" << diagnostics.dropped_events << " counters=" << diagnostics.dropped_counters
                      << " late=" << diagnostics.late_spans_dropped
                      << " gpu=" << diagnostics.pause_pending_gpu_submissions << '/' << diagnostics.pause_failed_gpu_submissions << '\n';
        check(live_capture && live_capture->complete() && live_capture->unacked_streams() == 0);
        if (iteration == 0)
            baseline_capture = live_capture;

        const auto* frame = live_capture->find_frame(capture_frame);
        check(frame && std::ranges::any_of(frame->events, [&](const auto& event) {
            return live_capture->marker(event.marker).name == "Physics.PhysXTask";
        }));
        for (const auto* name : {"PhysicsTick", "Physics.SimulateSubmit", "Physics.FetchWait",
                                 "Physics.TaskSubmit", "Physics.PhysXTask", "Physics.PhysXTaskRun",
                                 "Physics.PhysXTaskRelease", "Physics.TaskComplete"})
        {
            check(frame && std::ranges::any_of(frame->events, [&](const auto& event) {
                const bool task = std::string_view(name).starts_with("Physics.Task") ||
                                  std::string_view(name).starts_with("Physics.PhysXTask");
                return live_capture->marker(event.marker).name == name &&
                       event.cpu.session == status.identity.value && event.cpu.tick == status.last_tick.value &&
                       (task ? event.cpu.task != 0 : event.cpu.task == 0) && event.submission == 0 && event.queue == 0;
            }));
        }
        std::map<std::uint64_t, std::array<unsigned, 5>> task_flow;
        for (const auto& event : frame->events)
        {
            if (event.cpu.session != status.identity.value || event.cpu.tick != status.last_tick.value ||
                event.cpu.task == 0)
                continue;

            const auto& name = live_capture->marker(event.marker).name;
            if (name == "Physics.TaskSubmit")
                ++task_flow[event.cpu.task][0];
            else if (name == "Physics.PhysXTask")
                ++task_flow[event.cpu.task][1];
            else if (name == "Physics.TaskComplete")
                ++task_flow[event.cpu.task][2];
            else if (name == "Physics.PhysXTaskRun")
                ++task_flow[event.cpu.task][3];
            else if (name == "Physics.PhysXTaskRelease")
                ++task_flow[event.cpu.task][4];
        }

        check(task_flow.size() == status.completed_tasks - previous_tasks);
        check(std::ranges::all_of(task_flow, [](const auto& item) {
            return item.second == std::array<unsigned, 5>{1, 1, 1, 1, 1};
        }));
        check(std::ranges::all_of(live_capture->frames(), [](const auto& value) {
            return value.dropped_events == 0;
        }));
    }

    profiler.record(40);
    profiler.wait_until_idle();
    live_scene->reset();
    profiler.publish_frame(40);
    profiler.pause();
    profiler.wait_until_idle();
    wait_recording_finalized();
    const auto destruction_capture = profiler.capture();
    check(destruction_capture && destruction_capture->complete() && destruction_capture->unacked_streams() == 0);
    check(std::ranges::any_of(destruction_capture->frames() | std::views::transform([](const auto& frame) -> const auto& { return frame.events; }) | std::views::join,
        [&](const auto& event) { return destruction_capture->marker(event.marker).name == "Physics.SceneDestroy"; }));

    const auto capture = baseline_capture;
    check(bool(capture));
    check(capture->complete() && capture->unacked_streams() == 0);
    for (const auto* name : {"Physics.SceneCreate", "Physics.SceneDestroy", "Physics.SimulateSubmit",
                             "Physics.FetchWait", "Physics.PhysXTask", "Physics.GpuInitialize", "Physics.CpuInitialize"})
    {
        check(std::ranges::any_of(capture->markers(), [name](const auto& marker) { return marker.name == name; }));
        auto events = capture->frames() |
                      std::views::transform([](const auto& frame) -> const auto& { return frame.events; }) |
                      std::views::join;
        check(
            std::ranges::any_of(events, [&](const auto& event) { return capture->marker(event.marker).name == name; }));
    }
    check(std::ranges::all_of(capture->frames(), [](const auto& frame) { return frame.dropped_events == 0; }));
    if (argc == 2)
    {
        check(bool(ce::save_capture(*capture, std::filesystem::path(argv[1]))));
        const auto reopened = ce::load_capture(std::filesystem::path(argv[1]));
        check(reopened && (*reopened)->total_events() == capture->total_events());
        for (std::size_t index = 0; reopened && index < capture->frames().size(); ++index)
        {
            const auto& original = capture->frames()[index].events;
            const auto& saved = (*reopened)->frames()[index].events;
            check(original.size() == saved.size());
            for (std::size_t event = 0; event < original.size(); ++event)
                check(original[event].cpu.session == saved[event].cpu.session &&
                      original[event].cpu.tick == saved[event].cpu.tick &&
                      original[event].cpu.task == saved[event].cpu.task);
        }
        check(std::ranges::any_of((*reopened)->threads(), [](const auto& thread) {
            return thread.kind == ce::track_kind::physics_worker;
        }));
    }
    check(profiler.summary().abandoned_streams == 0);
    profiler.unregister_thread();
    profiler.shutdown();
    std::cout << "{\"result\":\"PHYSICS_P1_OK\",\"checks\":" << checks << ",\"sdk_tasks\":" << tasks
              << ",\"gpu_verified\":" << (gpu_verified ? "true" : "false")
              << ",\"gpu_fallback\":" << static_cast<int>(actual_fallback) << "}\n";
}
