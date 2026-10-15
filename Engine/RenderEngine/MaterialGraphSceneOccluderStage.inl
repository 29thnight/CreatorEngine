// Stage implementation included by MaterialGraphSceneHost.cpp inside material_graph.
    RGHandle SceneHost::OccluderStage::DeclareOccluder(const own::shared_owner<const Frame>& prepared, EnhancedRenderGraph& graph, RGHandle geometry, const SceneFirstCullOutputs& first) const
    {
        ce::profile_scope profile{ce::marker<"MaterialDeclareOccluder">()};
        const auto frame = prepared;
        if (!frame || frame->draws.empty())
        {
            RequireStage(first.list, {}, "METEOR.Scene.Occluder received a list for an empty frame.");
            return {};
        }
        frame->CheckCurrent(&graph);
        RequireStage(geometry, frame->geometryStage.geometryTable->GraphTable(graph), "METEOR.Scene.Occluder requires the METEOR.Scene.Geometry table.");
        if (!frame->occluderStage.occluderVisibility) { RequireStage(first.list, {}, "METEOR occluder has no first list."); RequireStage(first.depthCommands, {}, "METEOR occluder has no commands."); return {}; }
        RequireStage(first.list, frame->firstCullStage.cameraDepthMeshlets->GraphVisibleIds(graph), "METEOR.Scene.Occluder requires the METEOR.Scene.Cull.First list.");
        frame->firstCullStage.depthCommands->ValidateInput(graph, first.depthCommands);
        const auto& occluder = frame->occluderStage.occluderVisibility;
        occluder->Declare(graph, occluder);
        DeclareHardwareVisibility(graph, frame, true, first.depthCommands);
        occluder->DeclareSoftware(graph, occluder);
        RGTextureDesc depthDescription;
        depthDescription.width = frame->input->View().width;
        depthDescription.height = frame->input->View().height;
        depthDescription.format = RHIFormat::D32Float;
        depthDescription.allowDepthStencil = true;
        depthDescription.name = "METEOR.Scene.Occluder.Depth";
        return occluder->DeclareDepthResolve(graph, graph.CreateTexture(depthDescription), occluder);
    }


    bool SceneHost::OccluderStage::Prepare(const Frame& frame, const EnhancedFrameContext& context,
    HybridVisibilityPipelines& pipelines, std::string& error)
{
    if (!frame.firstCullStage.cameraDepthMeshlets) return true;
    return SurfaceStage::PrepareVisibility(frame, context, pipelines, "METEOR.Scene.Occluder", true,
        frame.firstCullStage.cameraDepthMeshlets, occluderVisibility, error);
}
