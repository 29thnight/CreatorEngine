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

enum class RHIRecordingDependencyReason : uint8_t
{
    SameTarget = 1,
    CpuProducer = 2
};

struct RHIRecordingTaskDependency
{
    uint32_t node{0};
    uint8_t reasons{0};
};

struct RHIRecordingTaskDetails
{
    std::vector<RHIRecordingTaskDependency> dependencies;
    std::vector<uint32_t> units;
};

// 진단을 요청한 경우에만 연결 이유와 역매핑을 보관한다. 상한을 넘으면 부분 그래프를 공개하지 않는다.
struct RHIRecordingTaskTopology
{
    static constexpr size_t kMaximumEntries = 4096;
    static constexpr size_t kMaximumDependencies = 16384;
    bool available{false}, truncated{false};
    std::vector<RHIRecordingTaskDetails> nodes;
    std::vector<uint32_t> unitNodes;
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
                                       RHIRecordingTaskGraph& result, RHIRecordingTaskTopology* topology = nullptr)
{
    if (topology)
    {
        *topology = {};
    }
    if (unitTargets.size() != unitWaves.size() || unitTargets.size() != unitPasses.size() ||
        (!unitWork.empty() && unitWork.size() != unitTargets.size()) || targetCount == 0)
    {
        return false;
    }
    RHIRecordingTaskTopology details;
    bool captureTopology = topology && unitTargets.size() <= RHIRecordingTaskTopology::kMaximumEntries;
    details.truncated = topology && !captureTopology;
    // 일부 STL은 빈 map도 sentinel을 할당하므로 trace 없는 빌더에서는 생성하지 않는다.
    std::optional<std::map<std::pair<uint32_t, uint32_t>, uint8_t>> dependencyReasons;
    if (captureTopology)
    {
        dependencyReasons.emplace();
    }
    const auto recordDependency = [&](uint32_t node, uint32_t dependency, RHIRecordingDependencyReason reason)
    {
        if (!captureTopology)
        {
            return;
        }
        const auto key = std::pair{node, dependency};
        auto& recorded = *dependencyReasons;
        const auto found = recorded.find(key);
        if (found == recorded.end())
        {
            if (recorded.size() >= RHIRecordingTaskTopology::kMaximumDependencies)
            {
                captureTopology = false;
                details.truncated = true;
                dependencyReasons.reset();
                return;
            }
            recorded.emplace(key, static_cast<uint8_t>(reason));
        }
        else
        {
            found->second |= static_cast<uint8_t>(reason);
        }
    };
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
            recordDependency(index, previous[node.target], RHIRecordingDependencyReason::SameTarget);
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
                    recordDependency(node, dependency, RHIRecordingDependencyReason::CpuProducer);
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
    if (captureTopology)
    {
        details.available = true;
        details.nodes.resize(graph.nodes.size());
        for (const auto& [key, reasons] : *dependencyReasons)
        {
            details.nodes[key.first].dependencies.push_back({key.second, reasons});
        }
        for (size_t unit = 0; unit < unitNodes.size(); ++unit)
        {
            details.nodes[unitNodes[unit]].units.push_back(static_cast<uint32_t>(unit));
        }
        details.unitNodes = std::move(unitNodes);
    }
    if (topology)
    {
        *topology = std::move(details);
    }
    result = std::move(graph);
    return true;
}
