// Stage implementation included by MaterialGraphSceneHost.cpp inside material_graph.
    SceneFinalCullOutputs SceneHost::FinalCullStage::DeclareFinalCull(const own::shared_owner<const Frame>& prepared, EnhancedRenderGraph& graph, RGHandle geometry, RGHandle occluderDepth) const
    {
        ce::profile_scope profile{ce::marker<"MaterialDeclareFinalCull">()};
        const auto frame = prepared;
        if (!frame || frame->draws.empty())
        {
            RequireStage(occluderDepth, {}, "METEOR.Scene.Cull.Final received depth for an empty frame.");
            return {};
        }
        frame->CheckCurrent(&graph);
        RequireStage(geometry, frame->geometryStage.geometryTable->GraphTable(graph), "METEOR.Scene.Cull.Final requires the METEOR.Scene.Geometry table.");
        RequireStage(occluderDepth, (frame->occluderStage.occluderVisibility ? frame->occluderStage.occluderVisibility->ResolvedDepth(graph) : RGHandle{}), "METEOR.Scene.Cull.Final requires the METEOR.Scene.Occluder depth.");
        if (frame->occluderStage.occluderVisibility)
        {
            // Recovery culling reads the pyramid built from the occluder depth,
            // never from the recovery output.
            const auto& pyramid = frame->finalCullStage.currentPyramid;
            if (!pyramid) throw std::runtime_error("METEOR.Scene.Cull.Final has no prepared current pyramid.");
            pyramid->Declare(graph, occluderDepth);
            frame->finalCullStage.cameraMeshlets->DeclareWithOcclusion(graph, pyramid);
        }
        // Frustum-only blended meshlets, compacted in source order.
        if (frame->finalCullStage.blendedMeshlets) frame->finalCullStage.blendedMeshlets->Declare(graph);
        for (const auto& batch : {frame->finalCullStage.materialCommands, frame->finalCullStage.indexedCommands})
            if (batch) batch->Declare(graph, batch);
        for (const auto& range : frame->effectStage.blendedRanges)
            for (const auto& batch : range.commands) if (batch) batch->Declare(graph, batch);
        SceneFinalCullOutputs result;
        result.list = frame->finalCullStage.cameraMeshlets ? frame->finalCullStage.cameraMeshlets->GraphVisibleIds(graph) : RGHandle{};
        result.commands.surface = frame->finalCullStage.materialCommands ? frame->finalCullStage.materialCommands->GraphOutput(graph) : RGHandle{};
        result.commands.layer = frame->finalCullStage.indexedCommands ? frame->finalCullStage.indexedCommands->GraphOutput(graph) : RGHandle{};
        for (const auto& range : frame->effectStage.blendedRanges)
            for (const auto& batch : range.commands)
                if (batch) result.commands.blended.push_back(batch->GraphOutput(graph));
        return result;
    }

