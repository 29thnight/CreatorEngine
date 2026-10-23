#define CE_CONTRACT_STANDALONE
#include "../../Editor/EngineGUIWindow/CompiledGraphViewerModel.h"
#include <cassert>
#include <cstdio>

namespace
{
    struct Usage
    {
        uint32_t resource{};
        uint16_t version{};
        uint8_t access{};
    };
    struct Phase
    {
        std::vector<Usage> usages;
    };
    struct Pass
    {
        bool culled{};
        std::vector<Usage> usages;
        std::vector<Phase> phases;
    };
    struct Edge
    {
        uint32_t producer{}, consumer{}, resource{};
        uint16_t version{};
        int reason{};
    };
    struct Snapshot
    {
        std::vector<Pass> passes;
        std::vector<int> resources;
        std::vector<uint16_t> executeOrder;
        std::vector<Edge> versionEdges, reachabilityEdges;
    };
} // namespace

int main()
{
    using editor::compiled_graph::ViewModel;
    ViewModel model;
    const auto access = [](const Usage& usage) { return usage.access; };
    Snapshot snapshot;
    snapshot.resources.resize(3);
    snapshot.passes.resize(4);
    snapshot.executeOrder = {2, 0, 1, 2, 99};
    snapshot.passes[3].culled = true;
    snapshot.passes[2].usages = {{0, 1, 2}};
    snapshot.passes[0].usages = {{0, 1, 1}, {1, 2, 2}, {99, 0, 2}};
    snapshot.passes[0].phases = {{{{1, 3, 1}, {2, 0, 2}}}, {{{1, 4, 2}}}};
    snapshot.passes[1].usages = {{1, 4, 1}};
    snapshot.versionEdges = {{2, 0, 0, 1, 0}, {0, 1, 1, 4, 0}, {3, 1, 2, 0, 0}, {99, 0, 0, 0, 0}};
    snapshot.reachabilityEdges = {{2, 0, 0}, {0, 1, 2}, {1, 3, 0}, {99, 0, 0}};
    model.Build(snapshot, access);
    assert((model.columns == std::vector<uint32_t>{2, 0, 1, 3}));
    const auto* merged = model.Find(1, 0);
    assert(merged && merged->access == 3 && merged->firstVersion == 2 && merged->lastVersion == 4);
    assert(model.Find(2, 0) && model.Find(2, 0)->access == 2);
    assert(!model.Find(3, 0) && !model.Find(UINT32_MAX, 0) && !model.Find(0, 99));
    assert(model.edges.size() == 3);
    model.Focus(0);
    assert((model.parents == std::vector<uint32_t>{2}));
    assert((model.children == std::vector<uint32_t>{1}));
    model.Focus(1);
    assert((model.parents == std::vector<uint32_t>{0}) && model.children.empty());
    // Replace a large snapshot with an empty/different view: no stale indices.
    model.Build(Snapshot{}, access);
    model.Focus(99);
    assert(model.cells.empty() && model.columns.empty() && model.edges.empty());
    assert(model.parents.empty() && model.children.empty() && !model.Find(0, 0));
    // Legacy snapshots expose reachability even without version edges.
    snapshot.versionEdges.clear();
    snapshot.reachabilityEdges = {{2, 0, 0}, {0, 1, 1}};
    model.Build(snapshot, access);
    assert(model.edges.size() == 2 && model.edges.front().reason == -1);
    model.Focus(0);
    assert(model.parents.size() == 1 && model.children.size() == 1);
    std::puts("COMPILED_GRAPH_VIEWER_MODEL_OK reordered/culled/phase-merge/invalid/legacy/snapshot-replacement");
}
