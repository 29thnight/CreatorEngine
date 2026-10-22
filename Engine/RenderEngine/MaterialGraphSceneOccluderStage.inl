#include "../Utility_Framework/Ownership.h"
// Stage implementation included by MaterialGraphSceneHost.cpp inside material_graph.
RGHandle SceneHost::OccluderStage::DeclareOccluder(const own::shared_owner<const Frame>& prepared,
                                                   EnhancedRenderGraph& graph, RGHandle geometry,
                                                   const SceneFirstCullOutputs& first) const
{
    ce::profile_scope profile{ce::marker<"MaterialDeclareOccluder">()};
    const auto& frame = prepared;
    if (!frame || frame->draws.empty())
    {
        RequireStage(first.list, {}, "METEOR.Scene.Occluder received a list for an empty frame.");
        return {};
    }
    frame->CheckCurrent(&graph);
    RequireStage(geometry, frame->raster->geometry->GraphTable(graph),
                 "METEOR.Scene.Occluder requires the METEOR.Scene.Geometry table.");
    if (!this->occluderVisibility)
    {
        RequireStage(first.list, {}, "METEOR occluder has no first list.");
        return {};
    }
    RequireStage(first.list, frame->raster->first->GraphVisibleIds(graph),
                 "METEOR.Scene.Occluder requires the METEOR.Scene.Cull.First list.");
    depthCommands->Declare(graph, depthCommands);
    const auto command = depthCommands->GraphOutput(graph);
    const auto& occluder = this->occluderVisibility;
    occluder->Declare(graph, occluder);
    DeclareHardwareVisibility(graph, frame, occluder, frame->raster->first, depthCommands, true, command);
    occluder->DeclareSoftware(graph, occluder);
    RGTextureDesc depthDescription;
    depthDescription.width = frame->input->View().width;
    depthDescription.height = frame->input->View().height;
    depthDescription.format = RHIFormat::D32Float;
    depthDescription.allowDepthStencil = true;
    depthDescription.name = "METEOR.Scene.Occluder.Depth";
    return occluder->DeclareDepthResolve(graph, graph.CreateTexture(depthDescription), occluder);
}

bool SceneHost::OccluderStage::Prepare(const FrameInput& frame, const EnhancedFrameContext& context,
                                       const own::shared_owner<const SceneFrameGeometry>& geometry,
                                       const own::shared_owner<const GpuGeometryVisibility::Frame>& list,
                                       HybridVisibilityPipelines& pipelines, std::string& error)
{
    if (!list)
        return true;
    return VisibilityStage::PrepareVisibility(frame, context, geometry, pipelines, "METEOR.Scene.Occluder", true, list,
                                              occluderVisibility, error);
}
