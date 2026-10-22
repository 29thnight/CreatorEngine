// Stage implementation included by MaterialGraphSceneHost.cpp inside material_graph.
void SceneHost::HistoryStage::DeclareHistory(const own::shared_owner<const Frame>& prepared, EnhancedRenderGraph& graph,
                                             RGHandle expectedDepth, RGHandle depth) const
{
    if (!prepared || !this->nextCameraHistory)
        return;
    prepared->CheckCurrent(&graph);
    if (!expectedDepth.IsValid() || depth.index != expectedDepth.index)
        throw std::runtime_error("METEOR history requires its final surface depth.");
    this->nextCameraHistory->Declare(graph, depth);
}
