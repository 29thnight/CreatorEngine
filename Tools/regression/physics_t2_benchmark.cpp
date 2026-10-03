#include "../../Engine/Physics/PhysicsScene.h"
#include "../../Engine/EngineDiagnostics/ProfileScope.h"
#include "../../Engine/EngineDiagnostics/ProfileCaptureFile.h"
#include <chrono>
#include <iostream>
#include <numeric>
#include <ranges>

using namespace ce::physics;

int main(int argc, char** argv)
{
    if (argc != 6) return 2;
    const bool gpu = std::string_view(argv[1]) == "gpu";
    const bool batch = std::string_view(argv[2]) == "batch";
    const bool recording = std::string_view(argv[3]) == "on";
    const auto count = std::stoul(argv[4]);
    if (!count || count > 256) return 2;

    auto& profiler = ce::profiler();
    profiler.initialize();
    profiler.register_thread("T2 query owner", ce::track_kind::game_thread);
    scene_config config;
    config.execution = gpu ? execution_preference::prefer_gpu : execution_preference::cpu;
    config.workers = 2;
    auto created = PhysicsScene::create(config);
    if (!created) return 3;
    auto scene = std::move(*created);
    if (gpu && scene->status().backend != execution_backend::gpu) return 4;

    std::array shapes{ShapeInstance{shape_id{1}}};
    body_desc desc;
    desc.kind = body_kind::static_body;
    desc.shapes = shapes;
    for (unsigned i = 0; i < 1024; ++i)
    {
        desc.initial_pose.position = {float(i % 32) * 3, 0, float(i / 32) * 3};
        if (!scene->create_body(desc)) return 5;
    }
    if (!scene->begin_step(1.f / 60) || !scene->finish_step()) return 6;

    std::vector<std::array<query_hit, 8>> scalarHits(count), batchHits(count);
    std::vector<query_request> requests;
    std::vector<result<query_result>> answers(count), baseline(count);
    for (unsigned i = 0; i < count; ++i)
    {
        const math::vector3 point{float(i % 32) * 3, 5, float(i / 32) * 3};
        query_input input;
        if (i % 3 == 0) input = ray_query{point, {0, -1, 0}, 10};
        else if (i % 3 == 1) input = sweep_query{sphere_geometry{.25f}, {point, {}}, {0, -1, 0}, 10};
        else input = overlap_query{sphere_geometry{.75f}, {{point.x, 0, point.z}, {}}};
        requests.push_back({input, batchHits[i]});
    }

    auto scalar = [&] {
        ce::profile_context_scope context{{scene->status().identity.value, scene->status().last_tick.value, 0}};
        for (unsigned i = 0; i < count; ++i)
            baseline[i] = std::visit([&](const auto& query) -> result<query_result> {
                using T = std::decay_t<decltype(query)>;
                if constexpr (std::is_same_v<T, ray_query>)
                    return scene->raycast(query.origin, query.direction, query.distance, scalarHits[i]);
                else if constexpr (std::is_same_v<T, sweep_query>)
                    return scene->sweep(query.form, query.origin, query.direction, query.distance, scalarHits[i]);
                else return scene->overlap(query.form, query.origin, scalarHits[i]);
            }, requests[i].input);
    };
    scalar();
    if (!scene->query_batch(requests, answers)) return 7;
    for (unsigned i = 0; i < count; ++i)
    {
        if (!baseline[i] || !answers[i] || answers[i]->written != 1 || baseline[i]->written != 1 ||
            answers[i]->required_capacity != baseline[i]->required_capacity ||
            batchHits[i][0].body != scalarHits[i][0].body || batchHits[i][0].shape != scalarHits[i][0].shape ||
            std::abs(batchHits[i][0].distance - scalarHits[i][0].distance) > 1e-5f) return 8;
    }

    auto execute = [&] { if (batch) { if (!scene->query_batch(requests, answers)) std::exit(9); } else scalar(); };
    for (int i = 0; i < 60; ++i) execute();
    if (recording) profiler.record(1);
    std::vector<double> samples;
    for (int i = 0; i < 600; ++i)
    {
        const auto start = std::chrono::steady_clock::now();
        execute();
        samples.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count());
        if (recording) profiler.publish_frame(i + 1);
    }

    std::uint64_t parents = 0, updates = 0, queries = 0, violations = 0;
    if (recording)
    {
        profiler.pause();
        profiler.wait_until_idle();
        const auto capture = profiler.capture();
        if (!capture || !capture->complete() || capture->unacked_streams() || capture->dropped_counters()) return 10;
        std::vector<ce::profile_event> parentEvents;
        for (const auto& frame : capture->frames())
        {
            if (frame.dropped_events) return 11;
            for (const auto& event : frame.events)
                if (capture->marker(event.marker).name == "Physics.QueryBatch") parentEvents.push_back(event);
        }
        for (const auto& frame : capture->frames())
            for (const auto& event : frame.events)
            {
                const auto name = capture->marker(event.marker).name;
                if (name == "Physics.QueryBatch") ++parents;
                else if (name == "Physics.QueryStructureUpdate") ++updates;
                else if (name == "Physics.Raycast" || name == "Physics.Sweep" || name == "Physics.Overlap") ++queries;
                else continue;
                if (event.cpu.session != scene->status().identity.value || event.cpu.tick != 1 || event.cpu.task) ++violations;
                if (batch && name != "Physics.QueryBatch" && !std::ranges::any_of(parentEvents, [&](const auto& parent) {
                    return parent.thread_slot == event.thread_slot && parent.depth + 1 == event.depth &&
                        parent.tick_begin <= event.tick_begin && parent.tick_end >= event.tick_end;
                })) ++violations;
            }
        if (violations || queries != count * 600 || (batch && (parents != 600 || updates != 600)) ||
            !ce::save_capture(*capture, argv[5])) return 12;
    }
    const auto mean = std::accumulate(samples.begin(), samples.end(), 0.0) / samples.size();
    std::ranges::sort(samples);
    std::cout << "{\"backend\":\"" << (gpu ? "gpu" : "cpu") << "\",\"mode\":\"" << (batch ? "batch" : "scalar")
              << "\",\"profile\":" << (recording ? "true" : "false") << ",\"requests\":" << count
              << ",\"samples\":600,\"meanUs\":" << mean << ",\"p99Us\":" << samples[593]
              << ",\"parity\":true,\"parents\":" << parents << ",\"updates\":" << updates
              << ",\"queries\":" << queries << ",\"hierarchyViolations\":" << violations << "}\n";
    scene.reset();
    profiler.unregister_thread();
    profiler.shutdown();
}
