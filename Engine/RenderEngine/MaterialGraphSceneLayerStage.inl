// Ordered layer resources, command production and capture raster.
void SceneHost::LayerStage::DeclareForwardGBuffer(const own::shared_owner<const FrameInput>& prepared,
                                                  EnhancedRenderGraph& graph,
                                                  const SceneSurfaceTargets::Outputs& opaque,
                                                  const SceneShadingInputs& commands, std::size_t index) const
{

    const auto frame = prepared;
    if (!this->forwardGbuffer.depth.IsValid())
    {
        RGTextureDesc desc;
        desc.width = frame->input->View().width;
        desc.height = frame->input->View().height;
        desc.allowRenderTarget = true;
        auto targets =
            std::array{&this->forwardGbuffer.diffuse, &this->forwardGbuffer.metalRough, &this->forwardGbuffer.normal,
                       &this->forwardGbuffer.emissive, &this->forwardGbuffer.bitmask};
        for (unsigned i = 0; i < targets.size(); ++i)
        {
            desc.format = SceneSurfaceTargets::GetRenderTargetFormat(i);
            desc.name = "METEOR.Blended.Surface";
            *targets[i] = graph.CreateTexture(desc);
        }
        desc.format = RHIFormat::D32Float;
        desc.allowRenderTarget = false;
        desc.allowDepthStencil = true;
        desc.name = "METEOR.Blended.Depth";
        this->forwardGbuffer.depth = graph.CreateTexture(desc);
    }
    this->forwardGbuffer.depth = graph.Write(this->forwardGbuffer.depth);
    const auto copiedDepth = this->forwardGbuffer.depth;
    graph.AddPass("METEOR.Blended.DepthCopy",
                  {{opaque.depth, RHIResourceState::CopySource, RGAccessMode::Read},
                   {copiedDepth, RHIResourceState::CopyDest, RGAccessMode::Write}},
                  [this, frame, opaque, copiedDepth](const auto& execution) {
                      frame->CheckCurrent(execution.graph);
                      execution.encoder->CopyResource(execution.ResolveHandle(copiedDepth),
                                                      execution.ResolveHandle(opaque.depth));
                  });
    this->forwardGbuffer = AdvanceSceneSurface(graph, this->forwardGbuffer, RGAccessMode::Write);
    const auto inputs = this->forwardGbuffer;
    std::vector<EnhancedRenderGraph::RGPassUsage> uses{
        {inputs.depth, RHIResourceState::DepthWrite, RGAccessMode::ReadWrite}};
    for (const auto target : Colors(inputs))
    {
        uses.push_back({target, RHIResourceState::RenderTarget, RGAccessMode::Write});
    }
    for (const auto& draw : frame->draws)
    {
        if (draw.inputIndex != index || !draw.program->hasSurface)
        {
            continue;
        }
        uses.push_back({draw.geometry->GraphOutput(graph), RHIResourceState::ShaderResource, RGAccessMode::Read});
        if (frame->UsesMeshlets(draw))
        {
            uses.push_back({draw.geometry->GraphMeshlets(graph), RHIResourceState::ShaderResource, RGAccessMode::Read});
        }
        for (const auto& texture : draw.bindings->resources.textures)
        {
            uses.push_back({graph.FindImportedTexture(texture.resource), RHIResourceState::PixelShaderResource,
                            RGAccessMode::Read});
        }
    }
    frame->AddLayerReads(
        graph, uses, [&](const auto& draw) { return draw.inputIndex == index && draw.program->hasSurface; },
        commands.layers.layer);
    NormalizeSceneReads(graph, uses);
    graph.AddPass("METEOR.Blended.Surface", uses, [this, frame, inputs, index](const auto& execution) {
        frame->CheckCurrent(execution.graph);
        std::array<RHITextureHandle, 5> colors;
        const auto handles = Colors(inputs);
        for (unsigned i = 0; i < colors.size(); ++i)
        {
            colors[i] = execution.ResolveHandle(handles[i]);
        }
        RHIRenderTargetBinding targets;
        {
            std::lock_guard lock(this->forwardTargetMutex);
            if (!this->forwardTargets.IsValid())
            {
                const auto depth =
                    RHIDepthTargetDesc::Depth(execution.ResolveHandle(inputs.depth), RHIFormat::D32Float);
                this->forwardTargets = frame->device->CreateRenderTargets(colors, &depth);
            }
            targets = this->forwardTargets;
        }
        if (!targets.IsValid())
        {
            throw std::runtime_error("METEOR Scene transmission GBuffer binding failed.");
        }
        auto& encoder = *execution.encoder;
        encoder.BindRenderTargets(targets);
        const float clear[]{0, 0, 0, 0};
        encoder.ClearRenderTargets(targets, clear);
        encoder.SetViewportAndScissor(frame->input->View().width, frame->input->View().height);
        encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
        for (const auto& draw : frame->RasterDrawsFor())
        {
            if (draw.inputIndex != index || !draw.program->hasSurface)
            {
                continue;
            }
            encoder.SetPipeline(RHIBindPoint::Graphics,
                                frame->RasterPipeline(draw, draw.program->gbuffer[draw.doubleSided].GetHandle()));
            std::string error;
            if (!RenderBindingCache::Bind(*frame->device, encoder, RHIBindPoint::Graphics, *draw.bindings, error))
            {
                throw std::runtime_error(error);
            }
            encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0, draw.constants);
            frame->BindLayerGeometry(encoder, draw);
            frame->DrawLayerGeometry(encoder, draw);
        }
    });
}

bool SceneHost::LayerStage::PrepareBlended(const FrameInput& frame,
                                           const std::shared_ptr<const SceneLookupFrame>& alphaLookup,
                                           bool hasRuntimeEffects,
                                           const std::shared_ptr<const LX::Runtime::ComputeGeneration>& generation,
                                           std::string& error)
{
    // Prepared from the sealed input while the owner prepared native resources.
    // Capture/transmission boundaries below still split adjacent sorted runs.
    const auto& order = frame.blendedOrder;
    const auto mergePipeline = [&](std::size_t input) {
        RHIPipelineHandle result{};
        if (alphaLookup->RequiresCapture())
            return result;
        for (const auto& draw : frame.DrawsFor(input))
        {
            // Surface effects consume per-layer captures/backgrounds. Those
            // are semantic ordering boundaries, not alternative alpha paths.
            if (!draw.program->hasSurface || draw.program->hasSpecial || draw.program->hasTransmission)
                return RHIPipelineHandle{};
            const auto pipeline = draw.program->blendedColor[draw.doubleSided].GetHandle();
            if (result.IsValid() && result != pipeline)
                return RHIPipelineHandle{};
            result = pipeline;
        }
        return result;
    };
    for (std::size_t first = 0; first < order.size();)
    {
        const auto pipeline = mergePipeline(order[first].index);
        std::vector<std::size_t> group{order[first++].index};
        while (first < order.size() && pipeline.IsValid() && mergePipeline(order[first].index) == pipeline)
            group.push_back(order[first++].index);
        blendedRanges.push_back({std::move(group), {}});
    }

    for (auto& range : blendedRanges)
    {
        const bool transmissionStage =
            (frame.input->Draws()[range.inputs.front()].material->generation->cooked.product.program.features &
             0x0800u) != 0;
        for (unsigned reference = 0; reference != 2; ++reference)
        {
            auto blendedCommands = std::make_shared<IndirectMaterialBatch>();
            blendedCommands->name = "METEOR.Scene.Commands.Blended." + std::to_string(range.inputs.front()) +
                                    (reference ? ".Reference" : ".Color");
            blendedCommands->device = frame.device;
            blendedCommands->pipeline = generation;
            const auto& list = frame.raster->layers;
            blendedCommands->visibility = list;
            std::vector<uint32_t> words;
            std::vector<std::array<uint32_t, 4>> records;
            uint32_t outputBytes{};
            const auto& order = range.inputs;
            RHIPipelineHandle pipeline{};

            for (const auto input : order)
                for (const auto& draw : frame.DrawsFor(input))
                {
                    if (!draw.program->hasSurface || draw.program->hasTransmission != transmissionStage ||
                        (hasRuntimeEffects && draw.program->hasSpecial))
                        continue;
                    const auto current = draw.program->blendedColor[draw.doubleSided].GetHandle();
                    if (blendedCommands->buckets.empty())
                    {
                        IndirectMaterialBatch::Bucket bucket;
                        bucket.representative = &draw - frame.draws.data();
                        blendedCommands->buckets.push_back(std::move(bucket));
                        pipeline = current;
                    }
                    if (pipeline != current)
                        throw std::runtime_error("METEOR blended range crossed a PSO boundary.");
                    auto& bucket = blendedCommands->buckets.front();
                    const uint32_t first = static_cast<uint32_t>(words.size() * 4);
                    std::vector<uint32_t> roots;
                    const auto append = [&](uint32_t slot, RHIBufferSlice slice, bool cbv = false) {
                        uint64_t address{};
                        if (!frame.device->EncodeIndirectBuffer(slice, address) || (cbv && address % 256))
                            throw std::runtime_error("METEOR blended indirect root preparation failed.");
                        roots.push_back(slot | (cbv ? 0x80000000u : 0));
                        words.push_back(uint32_t(address));
                        words.push_back(uint32_t(address >> 32));
                    };
                    if (!list || !frame.UsesMeshlets(draw))
                        throw std::runtime_error("METEOR blended range requires its ordered meshlet list.");
                    append(0, reference ? draw.referenceConstants : draw.constants, true);
                    append(1, RHIBufferSlice::Whole(draw.geometry->Buffer()));
                    append(draw.program->visibleOwnerRoot, draw.visibleOwner);
                    append(draw.program->meshletRoot, draw.geometry->Meshlets());
                    append(draw.program->temporalPreviousRoot + 1,
                           list->VisibleIds(list->MeshletOutputOffset(draw.cameraMeshletBin),
                                            draw.geometry->MeshletCount() + 1));
                    const auto& layout = draw.program->layout;
                    if (layout.uniformSlot)
                        append(*layout.uniformSlot, draw.bindings->uniforms, true);
                    if (layout.descriptorIndexSlot)
                        append(*layout.descriptorIndexSlot, draw.bindings->descriptorIndices, true);
                    if (bucket.roots.empty())
                        bucket.roots = roots;
                    if (bucket.roots != roots)
                        throw std::runtime_error("METEOR blended indirect root ABI mismatch.");
                    // Mesh dispatch arguments come from this draw's ordered list bin.
                    records.push_back({first, outputBytes, uint32_t(roots.size() * 2),
                                       static_cast<uint32_t>(list->ArgsOffset(draw.cameraMeshletBin))});
                    outputBytes += uint32_t(roots.size() * 8 + 12);
                    ++bucket.count;
                }
            if (!blendedCommands->Upload(words, records, outputBytes, error))
                throw std::runtime_error(error);

            range.commands[reference] = std::move(blendedCommands);
        }
    }
    return true;
}

SceneLayerWork SceneHost::LayerStage::Declare(const own::shared_owner<const FrameInput>& frame,
                                              EnhancedRenderGraph& graph, RGHandle list) const
{
    if (!frame || frame->draws.empty())
    {
        RequireStage(list, {}, "METEOR layer work received a list for an empty frame.");
        return {};
    }
    frame->CheckCurrent(&graph);
    RequireStage(list, frame->raster->layers ? frame->raster->layers->GraphVisibleIds(graph) : RGHandle{},
                 "METEOR layer work requires the ordered layer list.");
    SceneLayerWork work;
    if (indexedCommands)
    {
        indexedCommands->Declare(graph, indexedCommands);
        work.layer = indexedCommands->GraphOutput(graph);
    }
    for (const auto& range : blendedRanges)
        for (const auto& batch : range.commands)
            if (batch)
            {
                batch->Declare(graph, batch);
                work.blended.push_back(batch->GraphOutput(graph));
            }
    return work;
}
