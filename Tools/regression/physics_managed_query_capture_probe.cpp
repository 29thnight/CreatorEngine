#include "../../Engine/EngineDiagnostics/ProfileCaptureFile.h"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <map>
#include <ranges>

int main(int argc, char** argv)
{
    if (argc < 2 || argc > 4) return 2;
    const bool boundedDense = argc == 4 && std::string_view(argv[3]) == "--dense-bounded";
    if (argc == 4 && !boundedDense) return 2;
    auto loaded = ce::load_capture(argv[1]);
    if (!loaded) return 3;
    const auto& capture = **loaded;
    std::cerr << "capture complete=" << capture.complete() << " unacked=" << capture.unacked_streams() << " droppedCounters=" << capture.dropped_counters() << " frames=" << capture.frame_count() << " bytes=" << capture.memory_bytes() << "\n";
    if (!capture.complete() || capture.unacked_streams()) return 4;
    std::map<std::uint16_t, std::vector<ce::profile_event>> threads;
    for (const auto& frame : capture.frames())
    {
        if (frame.dropped_events) return 5;
        for (const auto& event : frame.events) threads[event.thread_slot].push_back(event);
    }
    std::uint64_t scripts = 0, batches = 0, updates = 0, queries = 0, violations = 0;
    struct bridge_cost
    {
        double total = 0, validate = 0, prepare = 0, sdk = 0, translate = 0, commit = 0;
        double update = 0, ray = 0, overlap = 0;
        double initialize = 0, lookup = 0, encode = 0;
        std::uint64_t initialized = 0, lookups = 0, encodes = 0;
        std::uint64_t updateCount = 0, rays = 0, overlaps = 0, tick = 0, session = 0;
        std::uint64_t requests = 0, stages = 0;
    };
    std::vector<bridge_cost> bridgeCosts;
    std::map<std::uint64_t, std::uint64_t> sceneTicks;
    for (auto& [thread, events] : threads)
    {
        std::ranges::sort(events, [](const auto& a, const auto& b) {
            if (a.tick_begin != b.tick_begin) return a.tick_begin < b.tick_begin;
            if (a.depth != b.depth) return a.depth < b.depth;
            return a.tick_end > b.tick_end;
        });
        std::vector<ce::profile_event> stack;
        std::vector<std::size_t> bridgeStack;
        for (const auto& event : events)
        {
            if (ce::has_flag(event.flags, ce::event_flags::instant)) continue;
            while (!stack.empty() && (stack.back().depth >= event.depth || stack.back().tick_end < event.tick_end)) { stack.pop_back(); bridgeStack.pop_back(); }
            const auto name = capture.marker(event.marker).name;
            auto parent = stack.empty() ? std::string_view{} : capture.marker(stack.back().marker).name;
            // A zero-length child can share the boundary of adjacent sibling scopes.
            // Timestamp sorting puts the next sibling first; require one containing
            // translate scope at the exact parent depth instead of dropping the event.
            if (event.tick_begin == event.tick_end && name.starts_with("Physics.ScriptHit") &&
                parent != "Physics.ScriptQueryTranslate")
            {
                const ce::profile_event* candidate = nullptr;
                std::size_t matches = 0;
                for (const auto& possible : events)
                {
                    if (possible.depth + 1 == event.depth && possible.tick_begin <= event.tick_begin &&
                        possible.tick_end >= event.tick_end && capture.marker(possible.marker).name == "Physics.ScriptQueryTranslate")
                    {
                        candidate = &possible;
                        ++matches;
                    }
                }
                if (matches == 1) parent = capture.marker(candidate->marker).name;
            }
            auto bridge = bridgeStack.empty() ? std::size_t(-1) : bridgeStack.back();
            if (name == "Physics.ScriptQueryBatch")
            {
                ++scripts;
                bridge = bridgeCosts.size();
                bridgeCosts.push_back({capture.milliseconds(event.tick_end - event.tick_begin) * 1000.0});
            }
            if (bridge != std::size_t(-1) && name != "Physics.ScriptQueryBatch")
            {
                auto& cost = bridgeCosts[bridge];
                const auto us = capture.milliseconds(event.tick_end - event.tick_begin) * 1000.0;
                if (name == "Physics.Raycast" || name == "Physics.Overlap") ++cost.requests;
                if (name == "Physics.ScriptHitInitialize" || name == "Physics.ScriptHitLookup" || name == "Physics.ScriptHitEncode")
                {
                    if (parent != "Physics.ScriptQueryTranslate" || stack.back().depth + 1 != event.depth) { ++violations; std::cerr << "hierarchy name=" << name << " parent=" << parent << " depth=" << event.depth
                        << " begin=" << event.tick_begin << " end=" << event.tick_end << "\n"; }
                    if (name == "Physics.ScriptHitInitialize") { cost.initialize += us; ++cost.initialized; }
                    if (name == "Physics.ScriptHitLookup") { cost.lookup += us; ++cost.lookups; }
                    if (name == "Physics.ScriptHitEncode") { cost.encode += us; ++cost.encodes; }
                }
                if (parent == "Physics.QueryBatch")
                {
                    if (name == "Physics.QueryStructureUpdate") { cost.update += us; ++cost.updateCount; }
                    if (name == "Physics.Raycast") { cost.ray += us; ++cost.rays; }
                    if (name == "Physics.Overlap") { cost.overlap += us; ++cost.overlaps; }
                }
                if (parent == "Physics.ScriptQueryBatch")
                {
                    if (name == "Physics.ScriptQueryValidate") { cost.validate += us; cost.stages |= 1; }
                    if (name == "Physics.ScriptQueryPrepare") { cost.prepare += us; cost.stages |= 2; }
                    if (name == "Physics.QueryBatch") { cost.sdk += us; cost.stages |= 4; cost.tick = event.cpu.tick; cost.session = event.cpu.session; }
                    if (name == "Physics.ScriptQueryTranslate") { cost.translate += us; cost.stages |= 8; }
                    if (name == "Physics.ScriptQueryCommit") { cost.commit += us; cost.stages |= 16; }
                }
                if (name.starts_with("Physics.ScriptQuery") && parent != "Physics.ScriptQueryBatch") { ++violations; std::cerr << "hierarchy name=" << name << " parent=" << parent << " depth=" << event.depth
                        << " begin=" << event.tick_begin << " end=" << event.tick_end << "\n"; }
            }
            if (name == "Physics.QueryBatch")
            {
                ++batches;
                if (!event.cpu.session || !event.cpu.tick || event.cpu.task) { ++violations; std::cerr << "hierarchy name=" << name << " parent=" << parent << " depth=" << event.depth
                        << " begin=" << event.tick_begin << " end=" << event.tick_end << "\n"; }
                sceneTicks[event.cpu.session] = std::max(sceneTicks[event.cpu.session], event.cpu.tick);
                // Scalar APIs also use owner batches; only mixed batch has this parent.
                if (parent == "Physics.ScriptQueryBatch" && stack.back().depth + 1 != event.depth) { ++violations; std::cerr << "hierarchy name=" << name << " parent=" << parent << " depth=" << event.depth
                        << " begin=" << event.tick_begin << " end=" << event.tick_end << "\n"; }
            }
            if (name == "Physics.QueryStructureUpdate" || name == "Physics.Raycast" || name == "Physics.Overlap")
            {
                if (name == "Physics.QueryStructureUpdate") ++updates; else ++queries;
                if (parent != "Physics.QueryBatch" || stack.back().depth + 1 != event.depth ||
                    event.cpu.session != stack.back().cpu.session || event.cpu.tick != stack.back().cpu.tick || event.cpu.task ||
                    stack.back().tick_begin > event.tick_begin) { ++violations; std::cerr << "hierarchy name=" << name << " parent=" << parent << " depth=" << event.depth
                        << " begin=" << event.tick_begin << " end=" << event.tick_end << "\n"; }
            }
            stack.push_back(event);
            bridgeStack.push_back(bridge);
        }
    }
    if (argc >= 3)
    {
        std::ofstream raw(argv[2]);
        if (!raw) return 7;
        raw << "requests,session,tick,total,validate,prepare,sdk,translate,commit,residual,update,ray,overlap,sdkResidual,initialize,lookup,encode,translateResidual,initialized,lookups,encodes\n";
        raw << std::setprecision(17);
        for (const auto& cost : bridgeCosts)
        {
            if ((cost.requests != 16 && cost.requests != 64) || cost.stages != 31) continue;
            if (cost.updateCount != 1) return 8;
            if (cost.rays != cost.requests / 2 || cost.overlaps != cost.requests / 2) continue;
            const auto residual = cost.total - cost.validate - cost.prepare - cost.sdk - cost.translate - cost.commit;
            const auto sdkResidual = cost.sdk - cost.update - cost.ray - cost.overlap;
            if (residual < -0.000001 || sdkResidual < -0.000001) return 9;
            raw << cost.requests << ',' << cost.session << ',' << cost.tick << ',' << cost.total << ','
                << cost.validate << ',' << cost.prepare << ',' << cost.sdk << ',' << cost.translate << ','
                << cost.commit << ',' << residual << ',' << cost.update << ',' << cost.ray << ','
                << cost.overlap << ',' << sdkResidual << ',' << cost.initialize << ',' << cost.lookup << ','
                << cost.encode << ',' << cost.translate - cost.initialize - cost.lookup - cost.encode << ','
                << cost.initialized << ',' << cost.lookups << ',' << cost.encodes << '\n';
        }
        raw.flush();
        if (!raw) return 10;
    }

    bool denseCoverage = true;
    if (boundedDense)
    {
        for (const auto count : {16u, 64u})
        {
            std::uint64_t covered = 0;

            for (const auto& cost : bridgeCosts)
            {
                // The capture also retains startup correctness requests. Select
                // the benchmark's half-ray/half-overlap workload explicitly.
                if (cost.requests != count || cost.rays != count / 2 || cost.overlaps != count / 2) continue;

                ++covered;
                denseCoverage &= cost.stages == 31 && cost.updateCount == 1 &&
                    cost.rays == count / 2 && cost.overlaps == count / 2 &&
                    cost.initialized == 1 && cost.lookups == count / 2 * 12 && cost.encodes == cost.lookups;
            }

            denseCoverage &= covered == 96;
        }
    }

    const bool valid = !capture.dropped_counters() && !violations && denseCoverage &&
        scripts >= (boundedDense ? 192u : 5280u) && queries >= (boundedDense ? 14736u : 422400u) &&
        updates == batches && !sceneTicks.empty();
    std::cout << "{\"bridgeCosts\":[";
    bool first = true;
    for (const auto count : {16u, 64u})
    {
        bridge_cost sum;
        std::uint64_t samples = 0;
        for (const auto& cost : bridgeCosts)
        {
            if (cost.requests != count || cost.stages != 31) continue;
            if (boundedDense && (cost.rays != count / 2 || cost.overlaps != count / 2)) continue;
            ++samples;
            sum.total += cost.total; sum.validate += cost.validate; sum.prepare += cost.prepare;
            sum.sdk += cost.sdk; sum.translate += cost.translate; sum.commit += cost.commit;
        }
        if (!samples) continue;
        if (!first) std::cout << ',';
        first = false;
        std::cout << "{\"requests\":" << count << ",\"samples\":" << samples
                  << ",\"totalMeanUs\":" << sum.total / samples
                  << ",\"validateMeanUs\":" << sum.validate / samples
                  << ",\"prepareMeanUs\":" << sum.prepare / samples
                  << ",\"sdkMeanUs\":" << sum.sdk / samples
                  << ",\"translateMeanUs\":" << sum.translate / samples
                  << ",\"commitMeanUs\":" << sum.commit / samples
                  << ",\"residualMeanUs\":" << (sum.total - sum.validate - sum.prepare - sum.sdk - sum.translate - sum.commit) / samples << '}';
    }
    std::cout << "],\"boundedDense\":" << (boundedDense ? "true" : "false")
              << ",\"denseCoverage\":" << (denseCoverage ? "true" : "false")
              << ",\"complete\":true,\"unacked\":0,\"dropped\":0,\"scripts\":" << scripts
              << ",\"batches\":" << batches << ",\"updates\":" << updates << ",\"queries\":" << queries
              << ",\"scenes\":" << sceneTicks.size() << ",\"hierarchyViolations\":" << violations
              << ",\"droppedCounters\":" << capture.dropped_counters() << ",\"frames\":" << capture.frame_count()
              << ",\"memoryBytes\":" << capture.memory_bytes() << "}\n";
    return valid ? 0 : 6;
}
