// Stage implementation included by MaterialGraphSceneHost.cpp inside material_graph.
    SceneSurfaceTargets::Outputs SceneHost::SurfaceStage::DeclareGBuffer(const own::shared_owner<const Frame>& prepared, EnhancedRenderGraph& graph, const SceneFinalCullOutputs& cull,
                                                           const SceneSurfaceTargets::Outputs& incoming) const
    {
        ce::profile_scope profile{ce::marker<"MaterialDeclareGBuffer">()};
        const auto frame = prepared;
        if (!frame || frame->draws.empty())
        {
            RequireStage(cull.list, {}, "METEOR.Scene.Surface received a list for an empty frame.");
            return incoming;
        }
        frame->CheckCurrent(&graph);
        RequireStage(cull.list, frame->finalCullStage.cameraMeshlets ? frame->finalCullStage.cameraMeshlets->GraphVisibleIds(graph) : RGHandle{}, "METEOR.Scene.Surface requires the METEOR.Scene.Cull.Final list.");
        if (frame->finalCullStage.materialCommands) frame->finalCullStage.materialCommands->ValidateInput(graph, cull.commands.surface);
        else RequireStage(cull.commands.surface, {}, "METEOR surface has no commands.");
        auto inputs = incoming;
        if (!frame->surfaceStage.hybridVisibility) {
            frame->surfaceStage.gbuffer = inputs;
            return inputs;
        }
        if (!inputs.visibility.IsValid()) throw std::runtime_error("METEOR surface requires a visibility target.");
        frame->surfaceStage.hybridVisibility->Declare(graph, frame->surfaceStage.hybridVisibility);
        DeclareHardwareVisibility(graph, frame, false, cull.commands.surface);
        frame->surfaceStage.hybridVisibility->DeclareSoftware(graph, frame->surfaceStage.hybridVisibility);
        inputs = frame->surfaceStage.hybridVisibility->DeclareResolve(graph, inputs, frame->surfaceStage.hybridVisibility);
        if (frame->surfaceStage.materialWork) frame->surfaceStage.materialWork->Declare(graph, inputs.visibility, frame->surfaceStage.materialWork);
        std::vector<EnhancedRenderGraph::RGPassUsage> restoreUses;
        for (auto* target : {&inputs.diffuse, &inputs.metalRough, &inputs.normal, &inputs.emissive, &inputs.bitmask}) {
            *target = graph.Modify(*target);
            restoreUses.push_back({*target, RHIResourceState::RenderTarget, RGAccessMode::ReadWrite});
        }
        frame->AddReconstructionReads(graph, restoreUses, inputs.visibility, frame->surfaceStage.materialWork->GraphInputs(graph));
        for (const auto& draw : frame->draws) {
            if (!draw.program->hasSurface || draw.program->hasTransmission || draw.blended) continue;
            restoreUses.push_back({draw.geometry->GraphOutput(graph), RHIResourceState::PixelShaderResource, RGAccessMode::Read});
            restoreUses.push_back({draw.geometry->GraphMeshlets(graph), RHIResourceState::PixelShaderResource, RGAccessMode::Read});
            for (const auto& texture : draw.bindings->resources.textures)
                restoreUses.push_back({graph.FindImportedTexture(texture.resource), RHIResourceState::PixelShaderResource, RGAccessMode::Read});
        }
        NormalizeSceneReads(graph, restoreUses);
        graph.AddPass("METEOR.Scene.Material.Resolve", restoreUses, [frame, inputs](const auto& execution) {
            frame->CheckCurrent(execution.graph);
            auto& encoder = *execution.encoder;
            std::array<RHITextureHandle, 5> colors;
            const auto handles = Colors(inputs);
            for (unsigned i = 0; i < colors.size(); ++i) colors[i] = execution.ResolveHandle(handles[i]);
            const auto targets = frame->device->CreateRenderTargets(colors, nullptr);
            if (!targets.IsValid()) throw std::runtime_error("METEOR reconstructed surface target binding failed.");
            encoder.BindRenderTargets(targets);
            encoder.SetViewportAndScissor(frame->input->View().width, frame->input->View().height);
            for (const auto& draw : frame->RasterDrawsFor()) {
                if (!draw.program->hasSurface || draw.program->hasTransmission || draw.blended) continue;
                frame->BindReconstruction(execution, draw, inputs.visibility, draw.program->gbuffer[draw.doubleSided].GetHandle());
                std::string error;
                if (!RenderBindingCache::Bind(*frame->device, encoder, RHIBindPoint::Graphics, *draw.bindings, error)) throw std::runtime_error(error);
                frame->DrawReconstruction(encoder, draw);
            }
        });
        frame->surfaceStage.gbuffer = inputs;
        return inputs;
    }


    bool SceneHost::SurfaceStage::PrepareVisibility(const Frame& frame, const EnhancedFrameContext& context,
    HybridVisibilityPipelines& pipelines, const char* name, bool depthOnly,
    const std::shared_ptr<const GpuGeometryVisibility::Frame>& list,
    std::shared_ptr<const SceneHybridVisibility>& result, std::string& error)
{
    auto hybrid = std::make_shared<SceneHybridVisibility>();
    hybrid->name = name;
    hybrid->depthOnly = depthOnly;
    hybrid->geometry = frame.geometryStage.geometryTable;
    hybrid->visible = list;
    hybrid->records.resize(frame.geometryStage.geometryTable->records.size());
    for (const auto& draw : frame.draws) {
        if (!draw.program->hasSurface || draw.program->hasTransmission || draw.blended) continue;
        const auto coverage = frame.input->Draws()[draw.inputIndex].coverage.flags;
        hybrid->records[draw.geometryRecord] = {list->MeshletOutputOffset(draw.cameraMeshletBin),
            1u | ((coverage & 2u) ? 0u : 2u) | (draw.doubleSided ? 4u : 0u), 0u, 0u};
    }
    if (!hybrid->Prepare(context, frame.input->ViewProjection(), pipelines, error)) return false;
    result = std::move(hybrid);
    return true;
}

    bool SceneHost::SurfaceStage::Prepare(const Frame& frame, const EnhancedFrameContext& context,
    HybridVisibilityPipelines& pipelines, std::string& error)
{
    if (!std::ranges::any_of(frame.draws, [](const auto& draw) {
        return draw.program->hasSurface && !draw.program->hasTransmission && !draw.blended;
    })) return true;
    return PrepareVisibility(frame, context, pipelines, "METEOR.Scene.Visibility", false,
        frame.finalCullStage.cameraMeshlets, hybridVisibility, error);
}
