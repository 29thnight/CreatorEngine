#pragma once
#include "../../Engine/Utility_Framework/StandardLibrary.h"

namespace editor::compiled_graph
{
    // Editor-only sparse projection. No GPU handles, graph mutations or locks.
    // Storage is reused; construction runs only when the immutable snapshot changes.
    struct ViewModel
    {
        static constexpr uint32_t invalid = UINT32_MAX;
        struct Cell
        {
            uint32_t resource{}, pass{};
            uint16_t firstVersion{}, lastVersion{};
            uint8_t access{}; // bit 0: read, bit 1: write
        };
        struct Edge
        {
            uint32_t producer{}, consumer{}, resource{};
            uint16_t version{};
            int reason{-1}; // -1: reachability-only edge
        };
        std::vector<uint32_t> columns, resourceOffsets;
        std::vector<Cell> cells;
        std::vector<Edge> edges;
        std::vector<uint32_t> parents, children;
        uint32_t focused{invalid};

        template<class Snapshot, class Access>
        void Build(const Snapshot& snapshot, Access access)
        {
            columns.clear();
            cells.clear();
            edges.clear();
            parents.clear();
            children.clear();
            focused = invalid;
            for (const auto pass : snapshot.executeOrder)
            {
                if (pass < snapshot.passes.size() && std::find(columns.begin(), columns.end(), pass) == columns.end())
                {
                    columns.push_back(pass);
                }
            }
            for (uint32_t pass = 0; pass < snapshot.passes.size(); ++pass)
            {
                if (std::find(columns.begin(), columns.end(), pass) == columns.end())
                {
                    columns.push_back(pass);
                }
                const auto append = [&](const auto& usages)
                {
                    for (const auto& usage : usages)
                    {
                        if (usage.resource < snapshot.resources.size())
                        {
                            cells.push_back({usage.resource, pass, usage.version, usage.version, access(usage)});
                        }
                    }
                };
                append(snapshot.passes[pass].usages);
                for (const auto& phase : snapshot.passes[pass].phases)
                {
                    append(phase.usages);
                }
            }
            std::sort(cells.begin(), cells.end(), [](const Cell& a, const Cell& b)
                      { return std::tie(a.resource, a.pass) < std::tie(b.resource, b.pass); });
            size_t count = 0;
            for (size_t index = 0; index < cells.size(); ++index)
            {
                const Cell cell = cells[index];
                if (count && cells[count - 1].resource == cell.resource && cells[count - 1].pass == cell.pass)
                {
                    auto& previous = cells[count - 1];
                    previous.access |= cell.access;
                    previous.firstVersion = (std::min)(previous.firstVersion, cell.firstVersion);
                    previous.lastVersion = (std::max)(previous.lastVersion, cell.lastVersion);
                }
                else
                {
                    cells[count++] = cell;
                }
            }
            cells.resize(count);
            resourceOffsets.assign(snapshot.resources.size() + 1, 0);
            for (const auto& cell : cells)
            {
                ++resourceOffsets[cell.resource + 1];
            }
            for (size_t i = 1; i < resourceOffsets.size(); ++i)
            {
                resourceOffsets[i] += resourceOffsets[i - 1];
            }
            for (const auto& edge : snapshot.versionEdges)
            {
                if (edge.producer < snapshot.passes.size() && edge.consumer < snapshot.passes.size() &&
                    !snapshot.passes[edge.producer].culled && !snapshot.passes[edge.consumer].culled)
                {
                    edges.push_back({edge.producer, edge.consumer, edge.resource, edge.version, int(edge.reason)});
                }
            }
            for (const auto& edge : snapshot.reachabilityEdges)
            {
                if (edge.producer >= snapshot.passes.size() || edge.consumer >= snapshot.passes.size() ||
                    snapshot.passes[edge.producer].culled || snapshot.passes[edge.consumer].culled)
                {
                    continue;
                }
                const auto duplicate = std::find_if(edges.begin(), edges.end(),
                                                    [&](const Edge& other)
                                                    {
                                                        return other.producer == edge.producer &&
                                                               other.consumer == edge.consumer &&
                                                               other.resource == edge.resource;
                                                    });
                if (duplicate == edges.end())
                {
                    edges.push_back({edge.producer, edge.consumer, edge.resource, 0, -1});
                }
            }
        }

        const Cell* Find(uint32_t resource, uint32_t pass) const
        {
            if (resourceOffsets.empty() || resource >= resourceOffsets.size() - 1)
            {
                return nullptr;
            }
            const auto begin = cells.begin() + resourceOffsets[resource];
            const auto end = cells.begin() + resourceOffsets[resource + 1];
            const auto found =
                std::lower_bound(begin, end, pass, [](const Cell& cell, uint32_t value) { return cell.pass < value; });
            return found != end && found->pass == pass ? &*found : nullptr;
        }

        void Focus(uint32_t selected)
        {
            if (focused == selected)
            {
                return;
            }
            focused = selected;
            parents.clear();
            children.clear();
            for (const auto& edge : edges)
            {
                if (edge.consumer == selected && edge.producer != selected)
                {
                    parents.push_back(edge.producer);
                }
                if (edge.producer == selected && edge.consumer != selected)
                {
                    children.push_back(edge.consumer);
                }
            }
            const auto unique = [](auto& values)
            {
                std::sort(values.begin(), values.end());
                values.erase(std::unique(values.begin(), values.end()), values.end());
            };
            unique(parents);
            unique(children);
        }
    };
} // namespace editor::compiled_graph
