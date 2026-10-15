// Stage implementation included by MaterialGraphSceneHost.cpp inside material_graph.
    SceneFirstCullOutputs SceneHost::FirstCullStage::DeclareFirstCull(const own::shared_owner<const FrameInput>& prepared, EnhancedRenderGraph& graph, RGHandle geometry) const
    {
        ce::profile_scope profile{ce::marker<"MaterialDeclareFirstCull">()};
        const auto frame = prepared;
        if (!frame || frame->draws.empty())
        {
            RequireStage(geometry, {}, "METEOR.Scene.Cull.First received geometry for an empty frame.");
            return {};
        }
        frame->CheckCurrent(&graph);
        RequireStage(geometry, frame->raster->geometry->GraphTable(graph), "METEOR.Scene.Cull.First requires the METEOR.Scene.Geometry table.");
        if (!this->cameraDepthMeshlets) return {};
        this->cameraDepthMeshlets->DeclareFirstStage(graph);
        return {this->cameraDepthMeshlets->GraphVisibleIds(graph)};
    }

