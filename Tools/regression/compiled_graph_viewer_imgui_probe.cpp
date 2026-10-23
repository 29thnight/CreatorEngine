#define CE_CONTRACT_STANDALONE
#include "../../Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h"
#include "../../Editor/ImGuiHelper/ImGui.h"
#include "../../Editor/EngineGUIWindow/CompiledGraphViewerModel.h"
#include <cassert>
#include <cstdio>

namespace EnhancedRenderDebugUi
{
    constexpr ImVec4 kWarnColor{1.f, .7f, .2f, 1.f};
    const char* GraphAccessName(RGAccessMode mode)
    {
        return mode == RGAccessMode::Read ? "Read" : "Write/Modify";
    }
    const char* GraphStateName(RHIResourceState)
    {
        return "state";
    }
    const char* GraphEdgeReason(EnhancedRenderGraph::DiagnosticSnapshot::VersionEdge::Reason)
    {
        return "dependency";
    }
} // namespace EnhancedRenderDebugUi
#include "../../Editor/EngineGUIWindow/CompiledGraphViewer.inl"

int main()
{
#ifdef _WIN32
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    using namespace EnhancedRenderDebugUi;
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1280, 800);
    io.DeltaTime = 1.f / 60.f;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    GraphSnapshot snapshot;
    snapshot.passes.resize(80);
    snapshot.resources.resize(120);
    for (uint32_t i = 0; i < snapshot.passes.size(); ++i)
    {
        auto& pass = snapshot.passes[i];
        pass.name = "Pass " + std::to_string(i);
        pass.authoredIndex = i;
        pass.compiledIndex = int(i);
        pass.usages.push_back({i, RHIResourceState::Common, true, RGAccessMode::Write, 1});
        snapshot.executeOrder.push_back(uint16_t(i));
        if (i)
        {
            pass.usages.push_back({i - 1, RHIResourceState::Common, false, RGAccessMode::Read, 1});
            snapshot.versionEdges.push_back({i - 1, i, i - 1, 1, GraphSnapshot::VersionEdge::Reason::RAW});
        }
    }
    for (uint32_t i = 0; i < snapshot.resources.size(); ++i)
    {
        auto& resource = snapshot.resources[i];
        resource.name = "Resource " + std::to_string(i);
        resource.used = true;
        resource.firstUse = i;
        resource.lastUse = i + 1;
    }
    snapshot.queueExecution.batches = {{RHIQueueKind::Graphics, {}, 1, true},
                                       {RHIQueueKind::Graphics, {0}, 1, true},
                                       {RHIQueueKind::Compute, {1, 2}, 2, false},
                                       {RHIQueueKind::Graphics, {3}, 1, false}};
    snapshot.queueExecution.waits = {{1, 2, true}, {2, 3, false}, {99, 0, false}};
    GraphViewModel model;
    model.Build(snapshot, GraphAccessBits);
    std::vector<uint32_t> resources;
    for (uint32_t i = 0; i < snapshot.resources.size(); ++i)
    {
        resources.push_back(i);
    }
    uint32_t selectedPass = 1, selectedResource = 0;
    size_t vertices = 0;
    for (int frame = 0; frame < 6; ++frame)
    {
        io.DisplaySize = frame < 3 ? ImVec2(1280, 800) : ImVec2(640, 480);
        ImGui::NewFrame();
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::Begin("Graph probe");
        switch (frame % 3)
        {
        case 0:
            DrawGraphMatrix(snapshot, model, model.columns, resources, selectedPass, selectedResource, 280.f);
            DrawGraphInspector(snapshot, model, selectedPass, selectedResource);
            break;
        case 1:
            DrawGraphQueues(snapshot, selectedPass);
            break;
        default:
            DrawGraphDag(snapshot, model, selectedPass, selectedResource);
            break;
        }
        ImGui::End();
        ImGui::Render();
        assert(ImGui::GetDrawData()->TotalVtxCount > 0);
        vertices += ImGui::GetDrawData()->TotalVtxCount;
    }
    // Exercise the real range and split-barrier renderer with an immutable snapshot.
    snapshot.resources[0].buffer = true;
    snapshot.passes[0].usages[0].kind = RGResourceKind::Buffer;
    snapshot.passes[0].usages[0].bufferRange = {16, 64};
    snapshot.passes[1].usages[0].textureRange = {2, 1, 3, 2, RHITextureAspect::Depth};
    snapshot.passes[1].barriers.push_back({1, RHIResourceState::DepthWrite, RHIResourceState::CopySource,
        false, false, false, {2, 1, 3, 2, RHITextureAspect::Depth}, {}, RHITransitionPhase::End});
    // A new auto-sized ImGui window can suppress its first frame while measuring.
    // Give the range view an explicit size and verify the settled frame.
    for (int frame = 0; frame < 2; ++frame)
    {
        ImGui::NewFrame();
        ImGui::SetNextWindowSize(ImVec2(1000, 700));
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::Begin("Range probe");
        DrawGraphUsage(snapshot, snapshot.passes[0].usages, selectedResource);
        DrawGraphUsage(snapshot, snapshot.passes[1].usages, selectedResource);
        DrawGraphBarriers(snapshot, snapshot.passes[1].barriers);
        ImGui::End();
        ImGui::Render();
        if (frame == 1)
        {
            assert(ImGui::GetDrawData()->TotalVtxCount > 0);
        }
    }
    // Drive the real header hit test over consecutive ImGui input frames.
    ImVec2 headerClick;
    selectedPass = 1;
    for (int frame = 0; frame < 3; ++frame)
    {
        if (frame == 1)
        {
            io.AddMousePosEvent(headerClick.x, headerClick.y);
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        }
        if (frame == 2)
        {
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        }
        ImGui::NewFrame();
        ImGui::SetNextWindowSize(ImVec2(1000, 700));
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::Begin("Interaction probe");
        DrawGraphMatrix(snapshot, model, model.columns, resources, selectedPass, selectedResource, 280.f);
        const auto* table = ImGui::GetCurrentContext()->Tables.GetByKey(ImGui::GetID("PassResourceMatrix"));
        assert(table);
        headerClick = ImVec2(table->Columns[1].WorkMinX + 10.f, table->OuterRect.Min.y + 12.f);
        ImGui::End();
        ImGui::Render();
    }
    assert(selectedPass == 0);
    // No snapshot entries / invalid selection must still produce a usable UI.
    snapshot = {};
    model.Build(snapshot, GraphAccessBits);
    selectedPass = selectedResource = UINT32_MAX;
    ImGui::NewFrame();
    ImGui::Begin("Empty");
    DrawGraphMatrix(snapshot, model, model.columns, {}, selectedPass, selectedResource, 120.f);
    DrawGraphInspector(snapshot, model, selectedPass, selectedResource);
    DrawGraphDag(snapshot, model, selectedPass, selectedResource);
    DrawGraphQueues(snapshot, selectedPass);
    ImGui::End();
    ImGui::Render();
    ImGui::DestroyContext();
    std::printf(
        "COMPILED_GRAPH_VIEWER_IMGUI_OK 80-passes/120-resources/all-views/2-sizes/header-click/empty/invalid-wait vertices=%zu\n",
        vertices);
}
