#include "ProfileAggregate.h"
#include "ProfileAggregateBaseline.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace profile_aggregate_benchmark
{
    template<class Aggregate>
    std::uint64_t digest(const Aggregate& value)
    {
        std::uint64_t hash = 14695981039346656037ull;
        const auto add = [&](auto field)
        {
            hash ^= static_cast<std::uint64_t>(field);
            hash *= 1099511628211ull;
        };
        add(value.frame_begin()); add(value.frame_end()); add(value.event_count());
        add(value.dropped_events()); add(value.truncated_events());
        add(value.tick_begin()); add(value.tick_end());
        add(value.timeline_total_ticks()); add(value.hierarchy_total_ticks());
        for (const auto range : {value.spans(), value.instants()})
        {
            add(range.size());
            for (const auto& event : range)
            {
                add(event.tick_begin); add(event.tick_end); add(event.marker); add(event.frame);
                add(event.thread_slot); add(event.depth); add(event.flags); add(event.queue);
                add(event.submission); add(event.view); add(event.reserved);
                add(event.cpu.session); add(event.cpu.tick); add(event.cpu.task);
            }
        }
        for (const auto& lane : value.threads())
        {
            add(lane.thread_slot); add(lane.event_count); add(lane.max_depth);
            add(lane.root_ticks); add(lane.span_begin); add(lane.span_end);
        }
        for (const auto& boundary : value.boundaries())
        {
            add(boundary.engine_frame); add(boundary.tick_begin); add(boundary.tick_end);
        }
        for (const auto range : {value.hierarchy(), value.flat()})
        {
            add(range.size());
            for (const auto& row : range)
            {
                add(row.marker); add(row.thread_slot); add(row.depth); add(row.call_count);
                add(row.total_ticks); add(row.self_ticks); add(row.max_ticks); add(row.min_ticks);
                add(row.p95_ticks); add(row.frame_appearances); add(row.truncated);
                add(row.child_begin); add(row.child_end);
            }
        }
        return hash;
    }

    ce::capture_session make_capture(unsigned lanes)
    {
        std::vector<ce::frame_record> frames;
        std::vector<ce::thread_info> threads;
        for (unsigned lane = 0; lane < lanes; ++lane)
        {
            ce::thread_info info;
            info.slot = lane * 251; // Sparse slots; the final lane is absent from the registry.
            info.kind = lane % 2 ? ce::track_kind::command_thread : ce::track_kind::game_thread;
            info.track_order = lanes - lane;
            if (lane + 1 < lanes)
            {
                threads.push_back(info);
            }
        }
        for (unsigned frame = 0; frame < 120; ++frame)
        {
            ce::frame_record record;
            record.engine_frame = frame;
            record.tick_begin = frame * 1000;
            record.tick_end = record.tick_begin + 1000;
            record.dropped_events = frame % 3;
            for (unsigned lane = lanes; lane > 0; --lane)
            {
                for (unsigned call = 0; call < 8; ++call)
                {
                    ce::profile_event event;
                    event.thread_slot = static_cast<std::uint16_t>((lane - 1) * 251);
                    event.frame = frame;
                    event.marker = call % 3 + 1;
                    event.tick_begin = record.tick_begin + call * 100;
                    event.tick_end = event.tick_begin + 50 + frame % 17;
                    event.depth = 1;
                    record.events.push_back(event); // Child finishes before parent.
                    event.marker = 4;
                    event.depth = 0;
                    event.tick_end += 10;
                    if (call == 0)
                    {
                        event.flags = ce::event_flags::truncated_begin;
                    }
                    record.events.push_back(event);
                }
                ce::profile_event instant;
                instant.thread_slot = static_cast<std::uint16_t>((lane - 1) * 251);
                instant.frame = frame;
                instant.tick_begin = record.tick_begin + 900;
                instant.tick_end = instant.tick_begin;
                instant.flags = ce::event_flags::instant;
                record.events.push_back(instant);
            }
            frames.push_back(std::move(record));
        }
        return {std::move(frames), std::move(threads), {}, {1000000}, true, 0};
    }
}

int main()
{
    using namespace profile_aggregate_benchmark;
    for (const unsigned lanes : {1u, 8u, 64u, 256u})
    {
        const auto capture = make_capture(lanes);
        for (const auto scope : {ce::aggregate_scope::spans_only, ce::aggregate_scope::full})
        {
            std::uint64_t expected = digest(ce::aggregate_frames_baseline(capture, 0, 119, scope));
            const auto measure = [&](auto aggregate, int repetitions)
            {
                double elapsedMs = 0;
                for (int repetition = 0; repetition < repetitions; ++repetition)
                {
                    const auto begin = std::chrono::steady_clock::now();
                    const auto result = aggregate(capture, 0, 119, scope);
                    const auto end = std::chrono::steady_clock::now();
                    elapsedMs += std::chrono::duration<double, std::milli>(end - begin).count();
                    if (digest(result) != expected)
                    {
                        std::exit(2);
                    }
                }
                return elapsedMs / repetitions;
            };
            double warmupMs = 0;
            for (int warmup = 0; warmup < 2; ++warmup)
            {
                warmupMs = (std::max)(measure(ce::aggregate_frames_baseline, 1),
                                     measure(ce::aggregate_frames, 1));
            }
            const int repetitions = (std::clamp)(static_cast<int>(20.0 / warmupMs), 1, 128);
            std::vector<double> baseline, candidate;
            for (int trial = 0; trial < 14; ++trial)
            {
                if (trial % 2 == 0)
                {
                    baseline.push_back(measure(ce::aggregate_frames_baseline, repetitions));
                    candidate.push_back(measure(ce::aggregate_frames, repetitions));
                }
                else
                {
                    candidate.push_back(measure(ce::aggregate_frames, repetitions));
                    baseline.push_back(measure(ce::aggregate_frames_baseline, repetitions));
                }
            }
            for (int side = 0; side < 2; ++side)
            {
                const auto& samples = side == 0 ? baseline : candidate;
                std::cout << "{\"side\":\"" << (side == 0 ? "baseline" : "candidate")
                          << "\",\"lanes\":" << lanes << ",\"scope\":\""
                          << (scope == ce::aggregate_scope::full ? "full" : "spans")
                          << "\",\"digest\":\"" << expected << "\",\"samples_ms\":[";
                for (std::size_t trial = 0; trial < samples.size(); ++trial)
                {
                    if (trial > 0)
                    {
                        std::cout << ',';
                    }
                    std::cout << samples[trial];
                }
                std::cout << "]}\n";
            }
        }
    }
}