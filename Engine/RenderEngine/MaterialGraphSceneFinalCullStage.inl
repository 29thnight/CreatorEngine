// Stage implementation included by MaterialGraphSceneHost.cpp inside material_graph.
SceneFinalCullOutputs SceneHost::FinalCullStage::DeclareFinalCull(
    const own::shared_owner<const Frame>& prepared, EnhancedRenderGraph& graph, RGHandle geometry,
    RGHandle occluderDepth, const own::shared_owner<const SceneHybridVisibility>& occluder) const
{
    ce::profile_scope profile{ce::marker<"MaterialDeclareFinalCull">()};
    const auto& frame = prepared;
    if (!frame || frame->draws.empty())
    {
        RequireStage(occluderDepth, {}, "METEOR.Scene.Cull.Final received depth for an empty frame.");
        return {};
    }
    frame->CheckCurrent(&graph);
    RequireStage(geometry, frame->raster->geometry->GraphTable(graph),
                 "METEOR.Scene.Cull.Final requires the METEOR.Scene.Geometry table.");
    RequireStage(occluderDepth, (occluder ? occluder->ResolvedDepth(graph) : RGHandle{}),
                 "METEOR.Scene.Cull.Final requires the METEOR.Scene.Occluder depth.");
    if (occluder)
    {
        // Recovery culling reads the pyramid built from the occluder depth,
        // never from the recovery output.
        const auto& pyramid = this->currentPyramid;
        if (!pyramid)
            throw std::runtime_error("METEOR.Scene.Cull.Final has no prepared current pyramid.");
        pyramid->Declare(graph, occluderDepth);
        this->cameraMeshlets->DeclareWithOcclusion(graph, pyramid);
    }
    // Frustum-only blended meshlets, compacted in source order.
    if (this->blendedMeshlets)
        this->blendedMeshlets->Declare(graph);
    SceneFinalCullOutputs result;
    result.list = this->cameraMeshlets ? this->cameraMeshlets->GraphVisibleIds(graph) : RGHandle{};
    result.blendedList = this->blendedMeshlets ? this->blendedMeshlets->GraphVisibleIds(graph) : RGHandle{};
    return result;
}
