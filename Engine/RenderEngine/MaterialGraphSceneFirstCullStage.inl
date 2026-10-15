// Stage implementation included by MaterialGraphSceneHost.cpp inside material_graph.
    SceneFirstCullOutputs SceneHost::FirstCullStage::DeclareFirstCull(const own::shared_owner<const Frame>& prepared, EnhancedRenderGraph& graph, RGHandle geometry) const
    {
        ce::profile_scope profile{ce::marker<"MaterialDeclareFirstCull">()};
        const auto frame = prepared;
        if (!frame || frame->draws.empty())
        {
            RequireStage(geometry, {}, "METEOR.Scene.Cull.First received geometry for an empty frame.");
            return {};
        }
        frame->CheckCurrent(&graph);
        RequireStage(geometry, frame->geometryStage.geometryTable->GraphTable(graph), "METEOR.Scene.Cull.First requires the METEOR.Scene.Geometry table.");
        if (!frame->firstCullStage.cameraDepthMeshlets) return {};
        frame->firstCullStage.cameraDepthMeshlets->DeclareFirstStage(graph);
        frame->firstCullStage.depthCommands->Declare(graph, frame->firstCullStage.depthCommands);
        return {frame->firstCullStage.cameraDepthMeshlets->GraphVisibleIds(graph), frame->firstCullStage.depthCommands->GraphOutput(graph)};
    }

