// Stage implementation included by MaterialGraphSceneHost.cpp inside material_graph.
    void SceneHost::HistoryStage::DeclareHistory(const own::shared_owner<const Frame>& prepared, EnhancedRenderGraph& graph, RGHandle depth) const
    {
        if (!prepared || !prepared->historyStage.nextCameraHistory) return;
        prepared->CheckCurrent(&graph);
        if (!prepared->surfaceStage.gbuffer.depth.IsValid() || depth.index != prepared->surfaceStage.gbuffer.depth.index)
            throw std::runtime_error("METEOR history requires its final surface depth.");
        prepared->historyStage.nextCameraHistory->Declare(graph, depth);
    }

