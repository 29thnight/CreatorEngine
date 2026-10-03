#include "../../Engine/SceneRuntime/ScenePhysicsSimulation.h"
#include "../../Engine/EngineDiagnostics/ProfileScope.h"
#include "../../Engine/EngineDiagnostics/ProfileCaptureFile.h"
#include <chrono>
#include <iostream>
#include <numeric>
#include <ranges>
#include <map>
#include <sstream>
#include <tuple>
#include <thread>
#include <cmath>
#include <unordered_set>


// Owner spans are inclusive wall durations. Worker work and queue latency are
// separate diagnostics, never added to the owner wall-clock total.
static std::string profile_costs(const ce::capture_session& capture)
{
    using tick_key = std::pair<std::uint64_t, std::uint64_t>;
    using task_key = std::tuple<std::uint64_t, std::uint64_t, std::uint64_t>;
    std::map<std::string, std::map<tick_key, double>> costs;
    std::map<task_key, ce::profile_tick> submits, completes;
    std::vector<const ce::profile_event*> workers;
    std::map<tick_key, std::map<std::string, const ce::profile_event*>> owners;

    for (const auto& frame : capture.frames())
        for (const auto& event : frame.events)
        {
            const auto name = capture.marker(event.marker).name;
            if (!event.cpu.session || !event.cpu.tick)
                continue;

            if (name == "Physics.TaskSubmit")
                submits[{event.cpu.session, event.cpu.tick, event.cpu.task}] = event.tick_begin;
            else if (name == "Physics.TaskComplete")
                completes[{event.cpu.session, event.cpu.tick, event.cpu.task}] = event.tick_begin;
            else if (name == "Physics.PhysXTask")
                workers.push_back(&event);

            if (event.cpu.task == 0 && !ce::has_flag(event.flags, ce::event_flags::instant) && event.tick_end >= event.tick_begin)
                owners[{event.cpu.session, event.cpu.tick}][std::string(name)] = &event;

            if (name.starts_with("Physics.") && !ce::has_flag(event.flags, ce::event_flags::instant) && event.tick_end >= event.tick_begin)
                costs[std::string(name)][{event.cpu.session, event.cpu.tick}] +=
                    capture.milliseconds(event.tick_end - event.tick_begin) * 1000;
        }

    std::vector<double> queues;
    std::size_t unmatched = 0, unmatchedCompletions = 0;
    for (const auto* worker : workers)
    {
        const auto done = completes.find({worker->cpu.session, worker->cpu.tick, worker->cpu.task});
        if (done == completes.end() || done->second < worker->tick_end) ++unmatchedCompletions;
        const auto found = submits.find({worker->cpu.session, worker->cpu.tick, worker->cpu.task});
        if (found == submits.end() || found->second > worker->tick_begin)
            ++unmatched;
        else
            queues.push_back(capture.milliseconds(worker->tick_begin - found->second) * 1000);
    }

    std::size_t foreignTasks = 0, inlineTasks = 0;
    for (const auto* task : workers)
    {
        const auto tick = owners.find({task->cpu.session, task->cpu.tick});
        if (tick == owners.end()) continue;
        const auto owner = tick->second.find("Physics.FetchWait");
        if (owner == tick->second.end()) continue;
        if (task->thread_slot == owner->second->thread_slot)
            ++inlineTasks;
        else
            ++foreignTasks;
    }

    std::size_t hierarchyViolations = 0;
    for (const auto& [identity, stages] : owners)
    {
        const auto parent = stages.find("Physics.FetchWait");
        if (parent == stages.end()) continue;

        for (const auto name : {"Physics.FetchResults", "Physics.DispatcherDrain"})
        {
            const auto child = stages.find(name);
            if (child == stages.end() || child->second->thread_slot != parent->second->thread_slot ||
                child->second->depth != parent->second->depth + 1 ||
                child->second->tick_begin < parent->second->tick_begin ||
                child->second->tick_end > parent->second->tick_end)
                ++hierarchyViolations;
        }
    }

    std::ostringstream result;
    result << "{\"sdkTasks\":" << workers.size() << ",\"workerTasks\":" << foreignTasks
           << ",\"inlineTasks\":" << inlineTasks << ",\"unmatchedTasks\":" << unmatched
           << ",\"unmatchedCompletions\":" << unmatchedCompletions
           << ",\"completionTasks\":" << completes.size()
           << ",\"hierarchyViolations\":" << hierarchyViolations
           << ",\"captureMemoryBytes\":" << capture.memory_bytes();
    if (!queues.empty())
    {
        std::ranges::sort(queues);
        result << ",\"queueMeanUs\":" << std::accumulate(queues.begin(), queues.end(), 0.0) / queues.size()
               << ",\"queueP99Us\":" << queues[(queues.size() * 99 + 99) / 100 - 1];
    }

    result << ",\"inclusivePerTick\":{";
    bool first = true;
    for (const auto& [name, ticks] : costs)
    {
        std::vector<double> values;
        for (const auto& [identity, value] : ticks)
            values.push_back(value);

        std::ranges::sort(values);
        if (!first) result << ',';
        first = false;
        result << '\"' << name << "\":{\"ticks\":" << values.size()
               << ",\"meanUs\":" << std::accumulate(values.begin(), values.end(), 0.0) / values.size()
               << ",\"p99Us\":" << values[(values.size() * 99 + 99) / 100 - 1] << '}';
    }
    result << "}}";
    return result.str();
}

int main(int argc, char** argv)
{
    if (argc != 5 && argc != 6 && argc != 7) return 2;
    const bool gpu = std::string_view(argv[1]) == "gpu";
    const auto active = static_cast<unsigned>(std::stoul(argv[2]));
    const bool recording = std::string_view(argv[3]) == "on";
    const auto requestedWorkers = argc >= 6 ? static_cast<unsigned>(std::stoul(argv[5])) : 2u;
    const std::string_view workload = argc == 7 ? argv[6] : "free";
    const bool stacks = workload == "stack" || workload == "stack-unconstrained";
    const bool contacts = workload == "contact" || stacks;
    const bool small = workload == "small";
    const unsigned bodies = small ? active : std::max(1024u, active);
    if (active > 4096 || requestedWorkers > 256 || (workload != "free" && !contacts && !small)) return 2;
    auto& profiler = ce::profiler();
    ce::profiler_config profilerConfig;
    profilerConfig.memory_budget = 512ull * 1024ull * 1024ull;
    profiler.initialize(profilerConfig);
    profiler.register_thread("T1 benchmark owner", ce::track_kind::game_thread);
    ScenePhysicsSimulation session;
    ce::physics::body_definition definition;
    definition.properties.kind = ce::physics::body_kind::dynamic;
    definition.properties.gravity_enabled = contacts;
    definition.properties.linear_damping = 0;
    if (stacks && workload != "stack-unconstrained")
    {
        // A repeatable solver load: preserve upright columns while permitting X/Y motion.
        definition.properties.constraints.rotation = ce::physics::axis_lock::all;
        definition.properties.constraints.translation = ce::physics::axis_lock::z;
    }
    definition.shapes.push_back(ce::physics::ShapeInstance{});
    definition.shapes.front().surface = {0, 0, 0};
    if (contacts)
    {
        ce::physics::body_definition floor;
        floor.properties.kind = ce::physics::body_kind::static_body;
        floor.properties.initial_pose.position = {0, -.5f, 0};
        floor.shapes.push_back(ce::physics::ShapeInstance{});
        floor.shapes.front().form = ce::physics::box_geometry{{256, .5f, 256}};
        floor.shapes.front().surface = {0, 0, 0};
        if (!session.Register(floor, true)) return 3;
    }

    if (stacks && (active != bodies || active % 4)) return 2;

    for (unsigned i = 0; i < bodies; ++i)
    {
        definition.properties.initial_pose.position = {float(i%64)*3, contacts ? .5f : (i < active ? 100.f : 10.f),float(i/64)*3};
        if (stacks)
        {
            const auto column = i / 4;
            definition.properties.initial_pose.position = {float(column % 32) * 3, .5f + float(i % 4), float(column / 32) * 3};
        }
        definition.properties.linear_velocity = {i < active ? 1.f : 0.f,0,0};
        if (!session.Register(definition,true)) return 3;
    }
    ce::physics::scene_config config;
    config.workers = requestedWorkers;
    config.event_capacity = bodies * 8;
    config.execution = gpu ? ce::physics::execution_preference::prefer_gpu : ce::physics::execution_preference::cpu;
    if (!session.Start(config) || (gpu && session.Runtime()->status().backend != ce::physics::execution_backend::gpu)) return 4;
    for (int i=0; i<60; ++i) if(!session.Advance(session.fixed_seconds)) return 5;
    if(recording) profiler.record(1);
    std::vector<double> samples;
    samples.reserve(240);
    std::uint64_t contactTicks = 0, minContacts = UINT64_MAX;
    std::uint64_t dynamicContactTicks = 0, minDynamicPairs = UINT64_MAX;
    double minMeanHeight = 5;
    for (int i=0; i<240; ++i)
    {
        const auto start=std::chrono::steady_clock::now();
        const auto result=session.Advance(session.fixed_seconds);
        const auto finish=std::chrono::steady_clock::now();
        if(!result || *result != 1) return 6;
        samples.push_back(std::chrono::duration<double,std::micro>(finish-start).count());
        const auto snapshot = session.Runtime()->latest_snapshot();
        if (snapshot->statistics.active_bodies != active || snapshot->statistics.dropped_events ||
            snapshot->statistics.dropped_contacts || snapshot->statistics.unresolved_identities) return 9;
        if (contacts)
        {
            minContacts = std::min(minContacts, snapshot->statistics.contacts_stored);
            contactTicks += snapshot->statistics.contacts_stored >= active;
        }
        if (stacks)
        {
            std::unordered_set<std::uint32_t> dynamicSlots;
            double heightSum = 0;
            for (const auto& item : snapshot->active_poses)
            {
                const auto& position = item.state.transform.position;
                if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z) ||
                    position.y < .2f || position.y > 5.f)
                {
                    std::cerr << "stack bounds: body=" << item.body.slot << " height=" << position.y << '\n';
                    return 10;
                }
                dynamicSlots.insert(item.body.slot);
                heightSum += position.y;
            }
            const double meanHeight = heightSum / active;
            minMeanHeight = std::min(minMeanHeight, meanHeight);
            if (meanHeight < 1.5)
            {
                std::cerr << "stack mean height=" << meanHeight << '\n';
                return 10;
            }

            std::uint64_t dynamicPairs = 0;
            for (const auto& event : snapshot->events)
                dynamicPairs += event.contact_count > 0 && dynamicSlots.contains(event.first.body.slot) &&
                                dynamicSlots.contains(event.second.body.slot);
            minDynamicPairs = std::min(minDynamicPairs, dynamicPairs);
            dynamicContactTicks += dynamicPairs >= active / 4;
        }
        if(recording) profiler.publish_frame(i+1);
    }
    const auto actualWorkers = session.Runtime()->status().workers;
    const auto observed = session.Runtime()->latest_snapshot()->statistics.active_bodies;
    if (observed != active || (contacts && contactTicks != 240) || (stacks && dynamicContactTicks != 240))
    {
        std::cerr << "workload validation: active=" << observed << " contactTicks=" << contactTicks
                  << " minContacts=" << minContacts << " dynamicContactTicks=" << dynamicContactTicks
                  << " minDynamicPairs=" << minDynamicPairs << " minMeanHeight=" << minMeanHeight << '\n';
        return 7;
    }
    if (!session.Stop()) return 7;
    const auto mean=std::accumulate(samples.begin(),samples.end(),0.0)/samples.size();
    std::ranges::sort(samples);
    std::string costs = "{}";
    if(recording)
    {
        profiler.publish_frame(241);
        profiler.pause();
        profiler.wait_until_idle();
        const auto capture=profiler.capture();
        if(!capture || !capture->complete() || capture->unacked_streams()!=0 || capture->dropped_counters()!=0 ||
           std::ranges::any_of(capture->frames(), [](const auto& frame) { return frame.dropped_events != 0; }) ||
           !ce::save_capture(*capture,argv[4])) return 8;
        costs = profile_costs(*capture);
    }
    profiler.unregister_thread();
    profiler.shutdown();
    std::cout << "{\"backend\":\"" << (gpu?"gpu":"cpu") << "\",\"active\":" << active
              << ",\"workersRequested\":" << requestedWorkers << ",\"workersActual\":" << actualWorkers
              << ",\"hardwareThreads\":" << std::thread::hardware_concurrency()
              << ",\"workload\":\"" << workload << "\",\"contactTicks\":" << contactTicks
              << ",\"minContacts\":" << (contacts ? minContacts : 0)
              << ",\"dynamicContactTicks\":" << dynamicContactTicks
              << ",\"minDynamicPairs\":" << (stacks ? minDynamicPairs : 0)
              << ",\"minMeanHeight\":" << (stacks ? minMeanHeight : 0)
              << ",\"bodies\":" << bodies << ",\"profile\":" << (recording?"true":"false")
              << ",\"samples\":240,\"meanUs\":" << mean << ",\"p99Us\":" << samples[237] << ",\"profileCosts\":" << costs << "}\n";
}
