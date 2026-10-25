#pragma once
#include "../RenderContainerReusePolicy.h"
#include "../../Utility_Framework/Foundation/StandardLibrary.h"

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
    struct Scratch
    {
        std::vector<std::pair<uint32_t, uint32_t>> groups;
        std::vector<uint32_t> previous, unitNodes, required;
        std::vector<std::vector<uint32_t>> frontiers;
        std::vector<RHIRecordingTaskNode> stagedNodes;
        RHIRecordingTaskTopology details;
        std::vector<std::pair<std::pair<uint32_t, uint32_t>, uint8_t>> dependencyReasons;
    };
    Scratch scratch;
    mutable std::vector<uint64_t> completion;
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
        std::optional<std::vector<uint64_t>> freshCompletion;
        if (!RenderContainerReuseEnabled())
        {
            freshCompletion.emplace();
        }
        auto& completedWork = freshCompletion ? *freshCompletion : completion;
        completedWork.assign(nodes.size(), 0);
        uint64_t longest = 0;
        for (size_t node = 0; node < nodes.size(); ++node)
        {
            uint64_t ready = 0;
            for (const auto dependency : nodes[node].dependencies)
            {
                ready = (std::max)(ready, completedWork[dependency]);
            }
            completedWork[node] = ready + nodes[node].work;
            longest = (std::max)(longest, completedWork[node]);
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
    auto& graph = result;
    const bool reuse = RenderContainerReuseEnabled();
    if (!reuse)
    {
        graph.scratch = RHIRecordingTaskGraph::Scratch{};
    }
    auto& nodes = graph.scratch.stagedNodes;
    auto& details = graph.scratch.details;
    details.available = false;
    details.truncated = false;
    details.unitNodes.clear();
    for (auto& node : details.nodes)
    {
        node.dependencies.clear();
        node.units.clear();
    }
    bool captureTopology = topology && unitTargets.size() <= RHIRecordingTaskTopology::kMaximumEntries;
    details.truncated = topology && !captureTopology;
    auto& dependencyReasons = graph.scratch.dependencyReasons;
    dependencyReasons.clear();
    const auto recordDependency = [&](uint32_t node, uint32_t dependency, RHIRecordingDependencyReason reason)
    {
        if (!captureTopology)
        {
            return;
        }
        const auto key = std::pair{node, dependency};
        auto& recorded = dependencyReasons;
        const auto found = std::ranges::lower_bound(recorded, key, {}, [](const auto& value) { return value.first; });
        if (found == recorded.end() || found->first != key)
        {
            if (recorded.size() >= RHIRecordingTaskTopology::kMaximumDependencies)
            {
                captureTopology = false;
                details.truncated = true;
                dependencyReasons.clear();
                return;
            }
            recorded.insert(found, {key, static_cast<uint8_t>(reason)});
        }
        else
        {
            found->second |= static_cast<uint8_t>(reason);
        }
    };
    auto& groups = graph.scratch.groups;
    groups.clear();
    for (size_t unit = 0; unit < unitTargets.size(); ++unit)
    {
        if (unitTargets[unit] >= targetCount || unitPasses[unit] >= passes.size())
        {
            return false;
        }
        groups.emplace_back(unitWaves[unit], unitTargets[unit]);
    }
    std::ranges::sort(groups);
    groups.erase(std::unique(groups.begin(), groups.end()), groups.end());
    auto& previous = graph.scratch.previous;
    previous.assign(targetCount, UINT32_MAX);
    nodes.resize(groups.size());
    for (uint32_t index = 0; index < groups.size(); ++index)
    {
        const auto& key = groups[index];
        auto& node = nodes[index];
        node.dependencies.clear();
        node.work = 0;
        node.wave = key.first;
        node.target = key.second;
        if (previous[node.target] != UINT32_MAX)
        {
            node.dependencies.push_back(previous[node.target]);
            recordDependency(index, previous[node.target], RHIRecordingDependencyReason::SameTarget);
        }
        previous[node.target] = index;
    }
    auto& unitNodes = graph.scratch.unitNodes;
    unitNodes.resize(unitTargets.size());
    for (size_t unit = 0; unit < unitTargets.size(); ++unit)
    {
        const auto node = static_cast<uint32_t>(
            std::ranges::lower_bound(groups, std::pair{unitWaves[unit], unitTargets[unit]}) - groups.begin());
        unitNodes[unit] = node;
        nodes[node].work += unitWork.empty() ? 1 : unitWork[unit];
    }
    const auto unique = [](std::vector<uint32_t>& values)
    {
        std::ranges::sort(values);
        values.erase(std::unique(values.begin(), values.end()), values.end());
    };
    auto& frontiers = graph.scratch.frontiers;
    if (frontiers.size() < passes.size())
    {
        frontiers.resize(passes.size());
    }
    for (auto& frontier : frontiers)
    {
        frontier.clear();
    }
    auto& required = graph.scratch.required;
    for (size_t begin = 0; begin < unitTargets.size();)
    {
        const auto pass = unitPasses[begin];
        size_t end = begin + 1;
        while (end < unitTargets.size() && unitPasses[end] == pass)
        {
            ++end;
        }
        required.clear();
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
        if (passes[pass].independent)
        {
            frontier.assign(required.begin(), required.end());
        }
        else
        {
            frontier.clear();
        }
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
                    nodes[node].dependencies.push_back(dependency);
                    recordDependency(node, dependency, RHIRecordingDependencyReason::CpuProducer);
                }
            }
            frontier.push_back(node);
        }
        unique(frontier);
        begin = end;
    }
    for (auto& node : nodes)
    {
        unique(node.dependencies);
    }
    if (captureTopology)
    {
        details.available = true;
        details.nodes.resize(nodes.size());
        for (const auto& [key, reasons] : dependencyReasons)
        {
            details.nodes[key.first].dependencies.push_back({key.second, reasons});
        }
        for (size_t unit = 0; unit < unitNodes.size(); ++unit)
        {
            details.nodes[unitNodes[unit]].units.push_back(static_cast<uint32_t>(unit));
        }
        details.unitNodes.assign(unitNodes.begin(), unitNodes.end());
    }
    if (topology)
    {
        if (captureTopology)
        {
            *topology = details;
        }
        else
        {
            topology->truncated = details.truncated;
        }
    }
    if (!reuse)
    {
        // Commit only after validation. Failed inputs preserve the accepted DAG.
        result.nodes = std::vector<RHIRecordingTaskNode>{};
    }
    result.nodes.resize(nodes.size());
    for (size_t index = 0; index < nodes.size(); ++index)
    {
        auto& destination = result.nodes[index];
        const auto& source = nodes[index];
        destination.target = source.target;
        destination.wave = source.wave;
        destination.work = source.work;
        destination.dependencies.assign(source.dependencies.begin(), source.dependencies.end());
    }
    return true;
}
