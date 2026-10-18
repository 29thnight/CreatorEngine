// Stage implementation included by MaterialGraphSceneHost.cpp inside material_graph.
std::array<RGHandle, 5> SceneHost::TemporalStage::DeclareTemporal(const own::shared_owner<const FrameInput>& prepared,
                                                                  EnhancedRenderGraph& graph,
                                                                  RGHandle expectedVisibility, RGHandle visibility,
                                                                  const std::array<RGHandle, 2>& material,
                                                                  RGHandle layerList,
                                                                  std::array<RGHandle, 5> targets) const
{
    const auto frame = prepared;
    if (!frame || frame->draws.empty())
        return targets;
    frame->CheckCurrent(&graph);
    RequireStage(visibility, expectedVisibility, "Temporal requires its final surface visibility version.");
    RequireStage(layerList, frame->raster->layers ? frame->raster->layers->GraphVisibleIds(graph) : RGHandle{},
                 "Temporal requires the ordered layer LOD list.");
    const auto work = frame->raster->material;
    if (work)
        work->ValidateInputs(graph, material);
    else if (material[0].IsValid() || material[1].IsValid())
        throw std::runtime_error("Temporal has no material tile producer.");
    if (work)
    {
        std::vector<EnhancedRenderGraph::RGPassUsage> uses;
        for (unsigned i = 0; i < targets.size(); ++i)
        {
            targets[i] = graph.Modify(targets[i]);
            uses.push_back({targets[i], i == 4 ? RHIResourceState::DepthWrite : RHIResourceState::RenderTarget,
                            RGAccessMode::ReadWrite});
        }
        frame->AddReconstructionReads(graph, uses, visibility, material);
        for (const auto& draw : frame->draws)
        {
            if (!draw.program->hasSurface || draw.program->hasTransmission || draw.blended)
                continue;
            for (const auto handle : {draw.geometry->GraphOutput(graph), draw.previousGeometry->GraphOutput(graph),
                                      draw.geometry->GraphMeshlets(graph)})
                uses.push_back({handle, RHIResourceState::PixelShaderResource, RGAccessMode::Read});
        }
        NormalizeSceneReads(graph, uses);
        const auto pass = graph.AddPass(
            "METEOR.Scene.Temporal.Resolve", uses,
            [this, frame, targets, visibility](const auto& execution) {
                frame->CheckCurrent(execution.graph);
                auto& encoder = *execution.encoder;
                std::array<RHITextureHandle, 4> colors;
                for (unsigned i = 0; i < 4; ++i)
                    colors[i] = execution.ResolveHandle(targets[i]);
                const auto depth = RHIDepthTargetDesc::Depth(execution.ResolveHandle(targets[4]), RHIFormat::D32Float);
                const auto binding = frame->device->CreateRenderTargets(colors, &depth);
                if (!binding.IsValid())
                    throw std::runtime_error("Temporal material packet targets unavailable.");
                encoder.BindRenderTargets(binding);
                encoder.SetViewportAndScissor(frame->input->View().width, frame->input->View().height);
                // 이 callback 안의 적격 bucket만 공유한다. 다른 패스나 recording의
                // descriptor를 재사용하지 않아 기존 GPU 수명 경계를 유지한다.
                RHIBindingTable reconstructionVisibility{};
                for (const auto& draw : frame->RasterDrawsFor())
                {
                    if (!draw.program->hasSurface || draw.program->hasTransmission || draw.blended)
                    {
                        continue;
                    }
                    if (!reconstructionVisibility.IsValid())
                    {
                        reconstructionVisibility = frame->CreateReconstructionVisibility(execution, visibility);
                    }
                    frame->BindReconstruction(encoder, draw, reconstructionVisibility,
                                              draw.program->temporal[draw.doubleSided].GetHandle());
                    std::string error;
                    if (!RenderBindingCache::Bind(*frame->device, encoder, RHIBindPoint::Graphics, *draw.bindings,
                                                  error))
                        throw std::runtime_error(error);
                    frame->DrawReconstruction(encoder, draw);
                    frame->temporalBucketSubmissions.fetch_add(1, std::memory_order_relaxed);
                }
            },
            true);
        graph.DeclareRecordingIndependent(pass);
    }
    // Ordered layers have no opaque visibility record. Preserve their depth,
    // alpha and source order; this is the layer path, never an opaque fallback.
    const auto replay = this->temporalCommands;
    if (!replay)
        return targets;
    replay->Declare(graph, replay);
    std::vector<EnhancedRenderGraph::RGPassUsage> uses;
    for (unsigned i = 0; i < targets.size(); ++i)
    {
        targets[i] = graph.Modify(targets[i]);
        uses.push_back({targets[i], i == 4 ? RHIResourceState::DepthWrite : RHIResourceState::RenderTarget,
                        RGAccessMode::ReadWrite});
    }
    for (const auto& draw : frame->draws)
    {
        if (!draw.program->hasSurface || !draw.blended)
            continue;
        uses.push_back({draw.geometry->GraphMeshlets(graph), RHIResourceState::ShaderResource, RGAccessMode::Read});
        uses.push_back({draw.geometry->GraphOutput(graph), RHIResourceState::ShaderResource, RGAccessMode::Read});
        if (draw.previousGeometry != draw.geometry)
            uses.push_back(
                {draw.previousGeometry->GraphOutput(graph), RHIResourceState::ShaderResource, RGAccessMode::Read});
        for (const auto& texture : draw.bindings->resources.textures)
            uses.push_back({graph.FindImportedTexture(texture.resource), RHIResourceState::PixelShaderResource,
                            RGAccessMode::Read});
    }
    replay->visibility->AddVisibleIdsRead(graph, uses);
    replay->AddRead(graph, uses, replay->GraphOutput(graph));
    NormalizeSceneReads(graph, uses);
    const auto pass = graph.AddPass(
        (replay->name + ".Raster").c_str(), uses,
        [this, frame, targets, replay](const auto& execution) {
            frame->CheckCurrent(execution.graph);
            auto& encoder = *execution.encoder;
            std::array<RHITextureHandle, 4> colors;
            for (unsigned i = 0; i < colors.size(); ++i)
                colors[i] = execution.ResolveHandle(targets[i]);
            const auto depth = RHIDepthTargetDesc::Depth(execution.ResolveHandle(targets[4]), RHIFormat::D32Float);
            const auto bindings = frame->device->CreateRenderTargets(colors, &depth);
            if (!bindings.IsValid())
                throw std::runtime_error("Temporal motion render targets unavailable.");
            encoder.BindRenderTargets(bindings);
            encoder.SetViewportAndScissor(frame->input->View().width, frame->input->View().height);
            encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
            for (std::size_t bucket = 0; bucket < replay->buckets.size(); ++bucket)
            {
                const auto& draw = frame->draws[replay->buckets[bucket].representative];
                encoder.SetPipeline(
                    RHIBindPoint::Graphics,
                    draw.program->meshPipelines.at(draw.program->temporal[draw.doubleSided].GetHandle().id));
                std::string error;
                if (!RenderBindingCache::Bind(*frame->device, encoder, RHIBindPoint::Graphics, *draw.bindings, error))
                    throw std::runtime_error(error);
                replay->Draw(encoder, bucket);
                frame->temporalBucketSubmissions.fetch_add(1, std::memory_order_relaxed);
            }
        },
        true);
    // Sealed frame inputs only; route observations are read after the whole graph joins.
    graph.DeclareRecordingIndependent(pass);
    return targets;
}
