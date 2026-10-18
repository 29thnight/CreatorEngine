#pragma once
#include "../../Utility_Framework/StandardLibrary.h"

struct RHIRecordingPassDependencies
{
    bool independent{false};
    std::vector<uint32_t> producers;
};

struct RHIRecordingTaskNode
{
    uint32_t target{0};
    uint32_t wave{0};
    uint64_t work{0};
    std::vector<uint32_t> dependencies;
};

struct RHIRecordingTaskGraph
{
    std::vector<RHIRecordingTaskNode> nodes;

    bool ReplacesWaveJoins() const
    {
        size_t originalJobs = 0;
        bool singleRun = false;
        for (size_t begin = 0; begin < nodes.size();)
        {
            size_t end = begin + 1;
            while (end < nodes.size() && nodes[end].wave == nodes[begin].wave)
            {
                ++end;
            }
            const bool single = end - begin == 1;
            originalJobs += single ? (singleRun ? 0 : 1) : end - begin;
            singleRun = single;
            begin = end;
        }
        // 단일 target 연속 묶음을 풀어 작업 수를 늘리지는 않는다.
        return nodes.size() > 1 && nodes.size() <= originalJobs;
    }

    uint64_t CriticalWork() const
    {
        std::vector<uint64_t> completion(nodes.size(), 0);
        uint64_t longest = 0;
        for (size_t node = 0; node < nodes.size(); ++node)
        {
            uint64_t ready = 0;
            for (const auto dependency : nodes[node].dependencies)
            {
                ready = (std::max)(ready, completion[dependency]);
            }
            completion[node] = ready + nodes[node].work;
            longest = (std::max)(longest, completion[node]);
        }
        return longest;
    }
};

// GPU 제출 순서는 연속 target 그대로다. CPU 기록은 필요한 선행 작업만 연결한다.
inline bool RHIBuildRecordingTaskGraph(std::span<const uint32_t> unitTargets, std::span<const uint32_t> unitWaves,
                                       std::span<const uint32_t> unitPasses, std::span<const uint64_t> unitWork,
                                       std::span<const RHIRecordingPassDependencies> passes, uint32_t targetCount,
                                       RHIRecordingTaskGraph& result)
{
    if (unitTargets.size() != unitWaves.size() || unitTargets.size() != unitPasses.size() ||
        (!unitWork.empty() && unitWork.size() != unitTargets.size()) || targetCount == 0)
    {
        return false;
    }
    RHIRecordingTaskGraph graph;
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> groups;
    for (size_t unit = 0; unit < unitTargets.size(); ++unit)
    {
        if (unitTargets[unit] >= targetCount || unitPasses[unit] >= passes.size())
        {
            return false;
        }
        groups.try_emplace({unitWaves[unit], unitTargets[unit]}, 0);
    }
    std::vector<uint32_t> previous(targetCount, UINT32_MAX);
    for (auto& [key, index] : groups)
    {
        index = static_cast<uint32_t>(graph.nodes.size());
        RHIRecordingTaskNode node;
        node.wave = key.first;
        node.target = key.second;
        if (previous[node.target] != UINT32_MAX)
        {
            node.dependencies.push_back(previous[node.target]);
        }
        previous[node.target] = index;
        graph.nodes.push_back(std::move(node));
    }
    std::vector<uint32_t> unitNodes(unitTargets.size());
    for (size_t unit = 0; unit < unitTargets.size(); ++unit)
    {
        const auto node = groups.at({unitWaves[unit], unitTargets[unit]});
        unitNodes[unit] = node;
        graph.nodes[node].work += unitWork.empty() ? 1 : unitWork[unit];
    }
    const auto unique = [](std::vector<uint32_t>& values)
    {
        std::ranges::sort(values);
        values.erase(std::unique(values.begin(), values.end()), values.end());
    };
    std::vector<std::vector<uint32_t>> frontiers(passes.size());
    for (size_t begin = 0; begin < unitTargets.size();)
    {
        const auto pass = unitPasses[begin];
        size_t end = begin + 1;
        while (end < unitTargets.size() && unitPasses[end] == pass)
        {
            ++end;
        }
        std::vector<uint32_t> required;
        for (const auto producer : passes[pass].producers)
        {
            if (producer >= passes.size())
            {
                return false;
            }
            required.insert(required.end(), frontiers[producer].begin(), frontiers[producer].end());
        }
        unique(required);
        auto& frontier = frontiers[pass];
        // 독립 콜백은 선행 CPU 작업 없이 실행해도 뒤의 비독립 소비자에 그 선행 조건을 전한다.
        frontier = passes[pass].independent ? required : std::vector<uint32_t>{};
        for (size_t unit = begin; unit < end; ++unit)
        {
            const auto node = unitNodes[unit];
            if (!passes[pass].independent)
            {
                for (const auto dependency : required)
                {
                    if (dependency == node)
                    {
                        continue;
                    }
                    if (dependency > node)
                    {
                        return false;
                    }
                    graph.nodes[node].dependencies.push_back(dependency);
                }
            }
            frontier.push_back(node);
        }
        unique(frontier);
        begin = end;
    }
    for (auto& node : graph.nodes)
    {
        unique(node.dependencies);
    }
    result = std::move(graph);
    return true;
}
