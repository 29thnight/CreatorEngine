// Stage implementation included by MaterialGraphSceneHost.cpp inside material_graph.
RGHandle SceneHost::EffectStage::DeclareColor(const own::shared_owner<const FrameInput>& prepared,
                                              EnhancedRenderGraph& graph, const SceneSurfaceTargets::Outputs& expected,
                                              const SceneSurfaceTargets::Outputs& inputs,
                                              const SceneColorInputs& inputWork, RGHandle lighting,
                                              RGHandle ambientOcclusion, RGHandle shadowMap) const
{

    const SceneShadingInputs commands{{inputWork.layer, {}}, inputWork.material};
    const auto frame = prepared;
    if (!frame || frame->draws.empty())
    {
        return lighting;
    }
    frame->CheckCurrent(&graph);
    const auto expectedColors = Colors(expected), actualColors = Colors(inputs);
    const bool sameTargets = std::equal(expectedColors.begin(), expectedColors.end(), actualColors.begin(),
                                        [](RGHandle a, RGHandle b) { return a.index == b.index; });
    if (!expected.depth.IsValid() || expected.depth.index != inputs.depth.index || !sameTargets)
    {
        throw std::runtime_error("METEOR Color requires this recording surface targets.");
    }
    if (frame->raster->material)
    {
        frame->raster->material->ValidateInputs(graph, commands.material);
    }
    if (frame->raster->layerCommands)
    {
        frame->raster->layerCommands->ValidateInput(graph, commands.layers.layer);
    }
    RequireStage(inputs.visibility, expected.visibility, "METEOR Color requires its published surface visibility.");
    if (this->colorOutput.IsValid() || !inputs.depth.IsValid())
    {
        throw std::runtime_error("METEOR Color requires its surface producer and one declaration.");
    }
    const auto count = std::ranges::count_if(inputs.decalBaseline, [](auto handle) { return handle.IsValid(); });
    if (count != 0 && count != 3)
    {
        throw std::runtime_error("METEOR material packets require a complete Decal baseline.");
    }
    for (const auto baseline : inputs.decalBaseline)
    {
        if (baseline.IsValid())
        {
            if (!graph.HasDeclaredWriter(baseline, "Decal.Snapshot") || baseline.index == inputs.diffuse.index ||
                baseline.index == inputs.metalRough.index || baseline.index == inputs.normal.index)
            {
                throw std::runtime_error("METEOR material packets require distinct pre-Decal snapshots on this graph.");
            }
        }
    }
    this->lookup.opaque->DeclareInputs(graph);
    if (this->runtime.frame)
    {
        this->runtime.frame->DeclareInputs(graph);
    }
    if (this->volume.frame)
    {
        this->volume.frame->DeclareCoefficients(graph, this->volume.bindings);
    }
    if (this->subsurface)
    {
        this->subsurface->DeclareInputs(graph);
    }
    lighting = DeclareShading(prepared, graph, inputs, commands, lighting, ambientOcclusion, shadowMap, false);
    if (this->refraction)
    {
        this->refraction->DeclareInputs(graph);
    }
    this->colorOutput = lighting;
    this->lookup.opaque->DeclareReady(graph);
    return lighting;
}

std::vector<SceneHost::ForwardDraw> SceneHost::EffectStage::BlendedDraws(
    const own::shared_owner<const FrameInput>& prepared) const
{
    std::vector<ForwardDraw> result;
    if (!prepared)
    {
        return result;
    }
    for (std::size_t i = 0; i < prepared->input->Draws().size(); ++i)
    {
        const auto& draw = prepared->input->Draws()[i];
        const auto& p = draw.material->generation->cooked.product.program;
        if (p.surface && (draw.queue == SceneCoverage::Blended || (p.features & 0x0800u) != 0))
        {
            result.push_back({i, draw.geometryKey, draw.viewDepth});
        }
    }
    return result;
}

RGHandle SceneHost::EffectStage::DeclareBlended(const own::shared_owner<const FrameInput>& prepared,
                                                EnhancedRenderGraph& graph, const SceneSurfaceTargets::Outputs& opaque,
                                                const std::vector<std::size_t>& indices, const SceneLayerWork& commands,
                                                RGHandle lighting, RGHandle shadowMap,
                                                const EnhancedForwardLighting& forward) const
{

    const auto frame = prepared;
    if (indices.empty())
    {
        throw std::runtime_error("METEOR blended range is empty.");
    }
    const auto index = indices.front();
    if (!frame || !this->colorOutput.IsValid() || !this->lookup.layered || !lighting.IsValid() ||
        index >= frame->input->Draws().size() || !forward.lights.IsValid() || !forward.counts.IsValid() ||
        !forward.indices.IsValid() || !forward.graphCounts.IsValid() || !forward.graphIndices.IsValid())
    {
        throw std::runtime_error("Graph Blend needs its exact prepared frame and light-tile resources.");
    }
    frame->CheckCurrent(&graph);
    std::set<std::size_t> selected;
    for (const auto input : indices)
    {
        if (!selected.insert(input).second || input >= frame->input->Draws().size() ||
            this->layers.blendedOutputs.contains(input))
        {
            throw std::runtime_error("Graph Blend draw declared twice.");
        }
    }
    if (!this->lookup.layered->InputsDeclared(graph))
    {
        this->lookup.layered->DeclareInputs(graph);
    }
    const auto& p = frame->input->Draws()[index].material->generation->cooked.product.program;
    const bool needsSurfaceBuffer = (p.features & 0x1800u) != 0;
    if (needsSurfaceBuffer)
    {
        layers.DeclareForwardGBuffer(prepared, graph, opaque, {commands, {}}, index);
    }
    if ((p.features & 0x0800u) != 0)
    {
        if (this->runtime.frame)
        {
            this->runtime.frame->DeclareBackground(graph, lighting, opaque.depth);
        }
        else
        {
            this->refraction->DeclareBackground(graph, lighting, opaque.depth);
        }
    }
    lighting =
        DeclareShading(prepared, graph, needsSurfaceBuffer ? this->layers.forwardGbuffer : opaque, {commands, {}},
                       lighting, {}, shadowMap, (p.features & 0x0800u) != 0, index, forward, indices);
    this->lookup.layered->DeclareReady(graph);
    for (const auto input : indices)
    {
        this->layers.blendedOutputs.emplace(input, lighting);
    }
    return lighting;
}

RGHandle SceneHost::EffectStage::DeclareBlendedStream(const own::shared_owner<const FrameInput>& prepared,
                                                      EnhancedRenderGraph& graph,
                                                      const SceneSurfaceTargets::Outputs& opaque,
                                                      const SceneLayerWork& commands, RGHandle lighting,
                                                      RGHandle shadowMap, const EnhancedForwardLighting& tiles) const
{
    if (prepared && !prepared->draws.empty())
    {
        if (prepared->raster->layerCommands)
        {
            prepared->raster->layerCommands->ValidateInput(graph, commands.layer);
        }
        size_t next = 0;
        for (const auto& range : this->layers.blendedRanges)
        {
            for (const auto& batch : range.commands)
            {
                if (batch)
                {
                    if (next >= commands.blended.size())
                    {
                        throw std::runtime_error("METEOR blended commands missing.");
                    }
                    batch->ValidateInput(graph, commands.blended[next++]);
                }
            }
        }
        if (next != commands.blended.size())
        {
            throw std::runtime_error("METEOR blended commands have extra inputs.");
        }
    }
    const auto draws = BlendedDraws(prepared);
    if (draws.empty())
    {
        return lighting;
    }
    ce::profile_scope profile{ce::marker<"MaterialDeclareBlended">()};
    // Media inside blended shading use the frame's volume coefficients. The
    // composite itself is METEOR.Scene.Volume, declared once before this stream.
    EnhancedForwardLighting shared = tiles;
    // Medium bindings belong to VolumeStage; only its graph read crosses into layer work.
    if (const auto medium = this->volume.frame)
    {
        shared.graphVolumeCoefficients = medium->GraphCoefficients(graph);
    }
    for (const auto& range : this->layers.blendedRanges)
    {
        lighting = DeclareBlended(prepared, graph, opaque, range.inputs, commands, lighting, shadowMap, shared);
    }
    return lighting;
}

void SceneHost::EffectStage::DeclareRefractionCapture(const own::shared_owner<const FrameInput>& prepared,
                                                      EnhancedRenderGraph& graph,
                                                      const SceneSurfaceTargets::Outputs& inputs,
                                                      const SceneShadingInputs& commands,
                                                      std::optional<std::size_t> index) const
{
    const auto frame = prepared;
    const auto targets = this->refraction->DeclareCaptureOutputs(graph);
    std::vector<EnhancedRenderGraph::RGPassUsage> uses{
        {inputs.depth, RHIResourceState::DepthRead, RGAccessMode::Read},
        {inputs.bitmask, RHIResourceState::ShaderResource, RGAccessMode::Read}};
    for (const auto input : targets)
    {
        uses.push_back({input, RHIResourceState::RenderTarget, RGAccessMode::Write});
    }
    for (const auto& draw : frame->draws)
    {
        if (!draw.program->hasTransmission || (index && draw.inputIndex != *index))
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
        graph, uses, [&](const auto& draw)
        { return draw.program->hasTransmission && (!index || draw.inputIndex == *index); }, commands.layers.layer);
    NormalizeSceneReads(graph, uses);
    graph.AddPass(
        "METEOR.Scene.RefractionCapture", uses,
        [this, frame, inputs, index](const auto& execution)
        {
            frame->CheckCurrent(execution.graph);
            const auto depth =
                RHIDepthTargetDesc::DepthReadOnly(execution.ResolveHandle(inputs.depth), RHIFormat::D32Float);
            const auto targets = frame->device->CreateRenderTargets(this->refraction->Inputs(), &depth);
            const RHIBindingDesc descriptions[]{
                RHIBindingDesc::SrvCube({}, RHIFormat::RGBA16Float, 1).OrNull(),
                RHIBindingDesc::Srv2D(execution.ResolveHandle(inputs.bitmask), RHIFormat::R32Uint),
                RHIBindingDesc::Srv2D({}, RHIFormat::RG16Float).OrNull(),
                RHIBindingDesc::SrvArray({}, RHIFormat::R32Float, 3).OrNull()};
            const auto table = frame->device->CreateBindings(descriptions);
            const auto decalTable = this->decal.Table(*frame, execution, inputs, true);
            if (!targets.IsValid() || !table.IsValid())
            {
                throw std::runtime_error("METEOR Scene refraction capture binding failed.");
            }
            auto& encoder = *execution.encoder;
            encoder.BindRenderTargets(targets);
            const float clear[]{0, 0, 0, 0};
            encoder.ClearRenderTargets(targets, clear);
            encoder.SetViewportAndScissor(frame->input->View().width, frame->input->View().height);
            encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
            for (const auto& draw : frame->RasterDrawsFor())
            {
                if (!draw.program->hasTransmission || (index && draw.inputIndex != *index))
                {
                    continue;
                }
                encoder.SetPipeline(
                    RHIBindPoint::Graphics,
                    frame->RasterPipeline(draw, draw.program->refraction[draw.doubleSided].GetHandle()));
                std::string error;
                if (!RenderBindingCache::Bind(*frame->device, encoder, RHIBindPoint::Graphics, *draw.bindings, error))
                {
                    throw std::runtime_error(error);
                }
                encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0, draw.constants);
                frame->BindLayerGeometry(encoder, draw);
                encoder.SetBindings(RHIBindPoint::Graphics, 2, table);
                this->decal.Bind(encoder, draw, decalTable, false);
                frame->DrawLayerGeometry(encoder, draw);
            }
        });
}

RGHandle SceneHost::EffectStage::DeclareShading(const own::shared_owner<const FrameInput>& prepared,
                                                EnhancedRenderGraph& graph, const SceneSurfaceTargets::Outputs& inputs,
                                                const SceneShadingInputs& commands, RGHandle lighting,
                                                RGHandle ambientOcclusion, RGHandle shadowMap, bool transmissionStage,
                                                std::optional<std::size_t> blended, EnhancedForwardLighting forward,
                                                std::vector<std::size_t> blendedGroup) const
{

    const auto frame = prepared;
    const bool reconstruct = !blended && !transmissionStage;
    const auto lookup = blended ? this->lookup.layered : this->lookup.opaque;
    const auto selected = [blended, blendedGroup](const Frame::Draw& draw)
    {
        return blended ? draw.blended && (blendedGroup.empty()
                                              ? draw.inputIndex == *blended
                                              : std::ranges::find(blendedGroup, draw.inputIndex) != blendedGroup.end())
                       : !draw.blended;
    };
    const bool ordinary =
        !this->runtime.frame || std::ranges::any_of(frame->draws,
                                                    [&](const auto& draw)
                                                    {
                                                        return selected(draw) && draw.program->hasSurface &&
                                                               !draw.program->hasSpecial &&
                                                               draw.program->hasTransmission == transmissionStage;
                                                    });
    std::shared_ptr<const IndirectMaterialBatch> blendedCommands;
    if (blended && ordinary)
    {
        const auto order = blendedGroup.empty() ? std::vector<size_t>{*blended} : blendedGroup;
        const auto found =
            std::ranges::find_if(this->layers.blendedRanges, [&](const auto& range) { return range.inputs == order; });
        if (found == this->layers.blendedRanges.end())
        {
            throw std::runtime_error("METEOR blended range was not prepared.");
        }
        blendedCommands = found->commands[forward.reference ? 1 : 0];
    }
    if (ordinary)
    {
        lighting = AdvanceSceneColor(graph, lighting);
    }
    std::vector<EnhancedRenderGraph::RGPassUsage> uses{
        {lighting, RHIResourceState::RenderTarget, RGAccessMode::ReadWrite},
        {inputs.depth, RHIResourceState::DepthRead, RGAccessMode::Read},
        {inputs.bitmask, RHIResourceState::ShaderResource, RGAccessMode::Read},
        {ambientOcclusion, RHIResourceState::ShaderResource, RGAccessMode::Read}};
    if (reconstruct)
    {
        std::erase_if(uses, [&](const auto& use) { return use.handle.index == inputs.depth.index; });
    }
    if (blended)
    {
        uses.pop_back();
    }
    this->decal.AddUses(inputs, uses, transmissionStage || blended.has_value());
    lookup->DeclareShadingInputs(graph, uses);
    if (blended)
    {
        uses.push_back({forward.graphCounts, RHIResourceState::ShaderResource, RGAccessMode::Read});
        uses.push_back({forward.graphIndices, RHIResourceState::ShaderResource, RGAccessMode::Read});
        if (this->volume.frame)
        {
            uses.push_back({this->volume.frame->GraphCoefficients(graph), RHIResourceState::PixelShaderResource,
                            RGAccessMode::Read});
        }
    }
    if (frame->environment.IsValid())
    {
        auto environment = graph.FindImportedTexture(frame->environment);
        if (!environment.IsValid())
        {
            environment =
                graph.ImportTexture(frame->environment, RHIResourceState::PixelShaderResource, "METEOR.Scene.HDR");
        }
        uses.push_back({environment, RHIResourceState::PixelShaderResource, RGAccessMode::Read});
    }
    if (frame->shadow)
    {
        uses.push_back({shadowMap, RHIResourceState::ShaderResource, RGAccessMode::Read});
    }
    for (const auto& draw : frame->draws)
    {
        if (!selected(draw))
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
    for (unsigned part = 0; part < (lookup->RequiresCapture() ? 2u : 0u); ++part)
    {
        std::vector<EnhancedRenderGraph::RGPassUsage> captureUses{
            {inputs.depth, RHIResourceState::DepthRead, RGAccessMode::Read},
            {inputs.bitmask, RHIResourceState::ShaderResource, RGAccessMode::Read}};
        if (reconstruct)
        {
            std::erase_if(captureUses, [&](const auto& use) { return use.handle.index == inputs.depth.index; });
        }
        this->decal.AddUses(inputs, captureUses, transmissionStage || blended.has_value());
        const unsigned first = part ? 8 : 0, count = part ? 3 : 8;
        const auto lookupInputs = lookup->DeclareCaptureOutputs(graph, first, count);
        for (unsigned i = first; i < first + count; ++i)
        {
            captureUses.push_back({lookupInputs[i], RHIResourceState::RenderTarget, RGAccessMode::Write});
        }
        for (const auto& draw : frame->draws)
        {
            if (!selected(draw))
            {
                continue;
            }
            captureUses.push_back(
                {draw.geometry->GraphOutput(graph), RHIResourceState::ShaderResource, RGAccessMode::Read});
            if (frame->UsesMeshlets(draw))
            {
                captureUses.push_back(
                    {draw.geometry->GraphMeshlets(graph), RHIResourceState::ShaderResource, RGAccessMode::Read});
            }
            for (const auto& texture : draw.bindings->resources.textures)
            {
                captureUses.push_back({graph.FindImportedTexture(texture.resource),
                                       RHIResourceState::PixelShaderResource, RGAccessMode::Read});
            }
        }
        if (reconstruct)
        {
            frame->AddReconstructionReads(graph, captureUses, inputs.visibility, commands.material);
        }
        else
        {
            frame->AddLayerReads(
                graph, captureUses, [&](const auto& draw) { return draw.program->hasSurface && selected(draw); },
                commands.layers.layer);
        }
        NormalizeSceneReads(graph, captureUses);
        graph.AddPass(
            reconstruct ? "METEOR.Scene.Lookup.Resolve" : "METEOR.Scene.LookupCapture", captureUses,
            [this, frame, inputs, first, count, part, transmissionStage, blended, lookup, selected, forward,
             reconstruct](const auto& execution)
            {
                frame->CheckCurrent(execution.graph);
                auto& encoder = *execution.encoder;
                const auto handles = lookup->Inputs().subspan(first, count);
                const auto depth =
                    RHIDepthTargetDesc::DepthReadOnly(execution.ResolveHandle(inputs.depth), RHIFormat::D32Float);
                const auto targets = frame->device->CreateRenderTargets(handles, reconstruct ? nullptr : &depth);
                const RHIBindingDesc descriptions[]{
                    RHIBindingDesc::SrvCube({}, RHIFormat::RGBA16Float, 1).OrNull(),
                    RHIBindingDesc::Srv2D(execution.ResolveHandle(inputs.bitmask), RHIFormat::R32Uint),
                    RHIBindingDesc::Srv2D({}, RHIFormat::RG16Float).OrNull(),
                    RHIBindingDesc::SrvArray({}, RHIFormat::R32Float, 3).OrNull()};
                const auto table = frame->device->CreateBindings(descriptions);
                const auto decalTable =
                    this->decal.Table(*frame, execution, inputs, transmissionStage || blended.has_value());
                frame->CheckCurrent();
                if (!targets.IsValid() || !table.IsValid())
                {
                    throw std::runtime_error("METEOR Scene lookup capture binding failed.");
                }
                encoder.BindRenderTargets(targets);
                const float clear[]{0, 0, 0, 0};
                encoder.ClearRenderTargets(targets, clear);
                encoder.SetViewportAndScissor(frame->input->View().width, frame->input->View().height);
                encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
                RHIBindingTable reconstructionVisibility;
                for (const auto& draw : frame->RasterDrawsFor(blended))
                {
                    if (!draw.program->hasSurface || !selected(draw))
                    {
                        continue;
                    }
                    encoder.SetPipeline(
                        RHIBindPoint::Graphics,
                        frame->RasterPipeline(
                            draw, (blended ? draw.program->blendedLookup : draw.program->lookup)[part][draw.doubleSided]
                                      .GetHandle()));
                    if (reconstruct)
                    {
                        if (!reconstructionVisibility.IsValid())
                        {
                            reconstructionVisibility =
                                frame->CreateReconstructionVisibility(execution, inputs.visibility);
                        }
                        frame->BindReconstruction(encoder, draw, reconstructionVisibility,
                                                  draw.program->lookup[part][draw.doubleSided].GetHandle());
                    }
                    std::string error;
                    if (!RenderBindingCache::Bind(*frame->device, encoder, RHIBindPoint::Graphics, *draw.bindings,
                                                  error))
                    {
                        throw std::runtime_error(error);
                    }
                    encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0,
                                              blended && forward.reference ? draw.referenceConstants : draw.constants);
                    if (!reconstruct)
                    {
                        frame->BindLayerGeometry(encoder, draw);
                    }
                    encoder.SetBindings(RHIBindPoint::Graphics, 2, table);
                    this->decal.Bind(encoder, draw, decalTable,
                                     inputs.decalBaseline[0].IsValid() && !(transmissionStage || blended.has_value()));
                    this->BindForward(encoder, draw, forward);
                    if (reconstruct)
                    {
                        frame->DrawReconstruction(encoder, draw);
                    }
                    else
                    {
                        frame->DrawLayerGeometry(encoder, draw);
                    }
                }
            });
    }
    lookup->DeclareBake(graph, inputs.bitmask);
    uses.push_back({lookup->GraphSamples(graph), RHIResourceState::PixelShaderResource, RGAccessMode::Read});
    if (transmissionStage && !this->runtime.frame)
    {
        DeclareRefractionCapture(prepared, graph, inputs, commands, blended);
        this->refraction->DeclareBake(graph, *lookup, inputs.bitmask, shadowMap);
        uses.push_back(
            {this->refraction->GraphSamples(graph), RHIResourceState::PixelShaderResource, RGAccessMode::Read});
    }
    const bool special = std::ranges::any_of(
        frame->draws, [&](const auto& draw)
        { return selected(draw) && draw.program->hasSpecial && draw.program->hasTransmission == transmissionStage; });
    if (this->subsurface && special)
    {
        const auto targets = this->subsurface->DeclareCaptureOutputs(graph);
        this->subsurface->DeclareReflection(graph, *lookup, inputs.bitmask);
        std::vector<EnhancedRenderGraph::RGPassUsage> captureUses{
            {inputs.depth, RHIResourceState::DepthRead, RGAccessMode::Read},
            {inputs.bitmask, RHIResourceState::ShaderResource, RGAccessMode::Read},
            {lookup->GraphSamples(graph), RHIResourceState::PixelShaderResource, RGAccessMode::Read},
            {this->subsurface->GraphReflection(graph), RHIResourceState::PixelShaderResource, RGAccessMode::Read}};
        if (reconstruct)
        {
            std::erase_if(captureUses, [&](const auto& use) { return use.handle.index == inputs.depth.index; });
        }
        this->decal.AddUses(inputs, captureUses, transmissionStage || blended.has_value());
        lookup->DeclareShadingInputs(graph, captureUses);
        if (blended)
        {
            captureUses.push_back({forward.graphCounts, RHIResourceState::ShaderResource, RGAccessMode::Read});
            captureUses.push_back({forward.graphIndices, RHIResourceState::ShaderResource, RGAccessMode::Read});
        }
        for (const auto target : targets)
        {
            captureUses.push_back({target, RHIResourceState::RenderTarget, RGAccessMode::Write});
        }
        if (frame->shadow)
        {
            captureUses.push_back({shadowMap, RHIResourceState::ShaderResource, RGAccessMode::Read});
        }
        for (const auto& draw : frame->draws)
        {
            if (!draw.program->hasSubsurface)
            {
                continue;
            }
            captureUses.push_back(
                {draw.geometry->GraphOutput(graph), RHIResourceState::ShaderResource, RGAccessMode::Read});
            if (frame->UsesMeshlets(draw))
            {
                captureUses.push_back(
                    {draw.geometry->GraphMeshlets(graph), RHIResourceState::ShaderResource, RGAccessMode::Read});
            }
            for (const auto& texture : draw.bindings->resources.textures)
            {
                captureUses.push_back({graph.FindImportedTexture(texture.resource),
                                       RHIResourceState::PixelShaderResource, RGAccessMode::Read});
            }
        }
        if (transmissionStage)
        {
            captureUses.push_back(
                {this->refraction->GraphSamples(graph), RHIResourceState::PixelShaderResource, RGAccessMode::Read});
        }
        if (reconstruct)
        {
            frame->AddReconstructionReads(graph, captureUses, inputs.visibility, commands.material);
        }
        else
        {
            frame->AddLayerReads(
                graph, captureUses, [&](const auto& draw) { return draw.program->hasSubsurface && selected(draw); },
                commands.layers.layer);
        }
        NormalizeSceneReads(graph, captureUses);
        graph.AddPass(
            "METEOR.Scene.SubsurfaceCapture", captureUses,
            [this, frame, inputs, shadowMap, transmissionStage, blended, lookup, selected, forward,
             reconstruct](const auto& execution)
            {
                frame->CheckCurrent(execution.graph);
                auto& encoder = *execution.encoder;
                const auto depth =
                    RHIDepthTargetDesc::DepthReadOnly(execution.ResolveHandle(inputs.depth), RHIFormat::D32Float);
                const auto targets =
                    frame->device->CreateRenderTargets(this->subsurface->Inputs(), reconstruct ? nullptr : &depth);
                const RHIBindingDesc descriptions[]{
                    RHIBindingDesc::SrvCube(lookup->UsesRuntimeEvaluation() ? frame->environment : RHITextureHandle{},
                                            frame->environment.IsValid()
                                                ? frame->device->DescribeTexture(frame->environment).format
                                                : RHIFormat::RGBA16Float,
                                            1)
                        .OrNull(),
                    RHIBindingDesc::Srv2D(execution.ResolveHandle(inputs.bitmask), RHIFormat::R32Uint),
                    RHIBindingDesc::Srv2D({}, RHIFormat::RG16Float).OrNull(),
                    RHIBindingDesc::SrvArray(frame->shadow ? execution.ResolveHandle(shadowMap) : RHITextureHandle{},
                                             RHIFormat::R32Float, 3)
                        .OrNull()};
                const auto table = frame->device->CreateBindings(descriptions);
                const auto decalTable =
                    this->decal.Table(*frame, execution, inputs, transmissionStage || blended.has_value());
                if (!targets.IsValid() || !table.IsValid())
                {
                    throw std::runtime_error("METEOR Scene SSS capture target binding failed.");
                }
                encoder.BindRenderTargets(targets);
                const float clear[]{0, 0, 0, 0};
                encoder.ClearRenderTargets(targets, clear);
                encoder.SetViewportAndScissor(frame->input->View().width, frame->input->View().height);
                encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
                RHIBindingTable reconstructionVisibility;
                for (const auto& draw : frame->RasterDrawsFor(blended))
                {
                    if (!selected(draw) || !draw.program->hasSubsurface ||
                        (draw.program->hasTransmission && !transmissionStage))
                    {
                        continue;
                    }
                    encoder.SetPipeline(
                        RHIBindPoint::Graphics,
                        frame->RasterPipeline(draw, draw.program->subsurface[draw.doubleSided].GetHandle()));
                    if (reconstruct)
                    {
                        if (!reconstructionVisibility.IsValid())
                        {
                            reconstructionVisibility =
                                frame->CreateReconstructionVisibility(execution, inputs.visibility);
                        }
                        frame->BindReconstruction(encoder, draw, reconstructionVisibility,
                                                  draw.program->subsurface[draw.doubleSided].GetHandle());
                    }
                    std::string error;
                    if (!RenderBindingCache::Bind(*frame->device, encoder, RHIBindPoint::Graphics, *draw.bindings,
                                                  error))
                    {
                        throw std::runtime_error(error);
                    }
                    encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0,
                                              blended && forward.reference ? draw.referenceConstants : draw.constants);
                    if (!reconstruct)
                    {
                        frame->BindLayerGeometry(encoder, draw);
                    }
                    encoder.SetBindings(RHIBindPoint::Graphics, 2, table);
                    this->decal.Bind(encoder, draw, decalTable,
                                     inputs.decalBaseline[0].IsValid() && !(transmissionStage || blended.has_value()));
                    this->BindForward(encoder, draw, forward);
                    encoder.SetRootBuffer(RHIBindPoint::Graphics, 3, RHIBufferSlice::Whole(lookup->Samples()));
                    encoder.SetRootBuffer(RHIBindPoint::Graphics, 4,
                                          RHIBufferSlice::Whole(this->subsurface->Reflection()));
                    if (draw.program->hasTransmission)
                    {
                        encoder.SetRootBuffer(RHIBindPoint::Graphics, 6,
                                              RHIBufferSlice::Whole(this->refraction->Samples()));
                    }
                    if (reconstruct)
                    {
                        frame->DrawReconstruction(encoder, draw);
                    }
                    else
                    {
                        frame->DrawLayerGeometry(encoder, draw);
                    }
                }
            });
        this->subsurface->DeclareFilter(graph, inputs.bitmask);
        uses.push_back(
            {this->subsurface->GraphReflection(graph), RHIResourceState::PixelShaderResource, RGAccessMode::Read});
        uses.push_back(
            {this->subsurface->GraphIrradiance(graph), RHIResourceState::PixelShaderResource, RGAccessMode::Read});
    }
    if (ordinary)
    {
        if (blendedCommands)
        {
            frame->raster->layers->AddVisibleIdsRead(graph, uses);
        }
        else if (reconstruct)
        {
            frame->AddReconstructionReads(graph, uses, inputs.visibility, commands.material);
        }
        else
        {
            frame->AddLayerReads(
                graph, uses, [&](const auto& draw)
                { return draw.program->hasSurface && !draw.program->hasSpecial && selected(draw); },
                commands.layers.layer);
        }
        if (blendedCommands)
        {
            blendedCommands->AddRead(graph, uses, BlendedCommandInput(graph, *blendedCommands, commands.layers));
        }
        NormalizeSceneReads(graph, uses);
        const auto colorPass = graph.AddPass(
            blended             ? "METEOR.Blended.Color"
            : transmissionStage ? "METEOR.Scene.TransmissionColor"
                                : "METEOR.Scene.Color",
            uses,
            [this, frame, inputs, lighting, ambientOcclusion, shadowMap, transmissionStage, blended, lookup, selected,
             forward, blendedCommands, reconstruct](const auto& execution) mutable
            {
                frame->CheckCurrent(execution.graph);
                auto& encoder = *execution.encoder;
                const auto color = execution.ResolveHandle(lighting);
                const auto depth =
                    RHIDepthTargetDesc::DepthReadOnly(execution.ResolveHandle(inputs.depth), RHIFormat::D32Float);
                const auto target = frame->device->CreateRenderTargets({&color, 1}, reconstruct ? nullptr : &depth);
                std::array<RHIBindingDesc, 4> descriptions{
                    RHIBindingDesc::SrvCube(frame->environment,
                                            frame->environment.IsValid()
                                                ? frame->device->DescribeTexture(frame->environment).format
                                                : RHIFormat::RGBA16Float,
                                            1)
                        .OrNull(),
                    RHIBindingDesc::Srv2D(execution.ResolveHandle(inputs.bitmask), RHIFormat::R32Uint),
                    RHIBindingDesc::Srv2D(blended ? RHITextureHandle{} : execution.ResolveHandle(ambientOcclusion),
                                          RHIFormat::RG16Float)
                        .OrNull(),
                    RHIBindingDesc::SrvArray(frame->shadow ? execution.ResolveHandle(shadowMap) : RHITextureHandle{},
                                             RHIFormat::R32Float, 3)
                        .OrNull()};
                const auto table = frame->device->CreateBindings(descriptions);
                const auto decalTable =
                    this->decal.Table(*frame, execution, inputs, transmissionStage || blended.has_value());
                if (this->volume.frame && blended)
                {
                    forward.volumeTable = frame->device->CreateBindings(this->volume.frame->LightingBindings(
                        frame->shadow ? execution.ResolveHandle(shadowMap) : RHITextureHandle{}));
                    if (!forward.volumeTable.IsValid())
                    {
                        throw std::runtime_error("Graph medium lighting binding failed.");
                    }
                }
                frame->CheckCurrent();
                if (!target.IsValid() || !table.IsValid())
                {
                    throw std::runtime_error("METEOR Scene color/depth/AO/environment binding failed.");
                }
                encoder.BindRenderTargets(target);
                encoder.SetViewportAndScissor(frame->input->View().width, frame->input->View().height);
                encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
                RHIBindingTable reconstructionVisibility;
                for (const auto& draw : frame->RasterDrawsFor({}, blended.has_value()))
                {
                    if (!selected(draw) || !draw.program->hasSurface ||
                        draw.program->hasTransmission != transmissionStage ||
                        (this->runtime.frame && draw.program->hasSpecial))
                    {
                        continue;
                    }
                    if (blendedCommands && (blendedCommands->buckets.empty() ||
                                            &draw != &frame->draws[blendedCommands->buckets.front().representative]))
                    {
                        continue;
                    }
                    encoder.SetPipeline(RHIBindPoint::Graphics,
                                        frame->RasterPipeline(draw, (blended ? draw.program->blendedColor
                                                                             : draw.program->color)[draw.doubleSided]
                                                                        .GetHandle()));
                    if (reconstruct)
                    {
                        if (!reconstructionVisibility.IsValid())
                        {
                            reconstructionVisibility =
                                frame->CreateReconstructionVisibility(execution, inputs.visibility);
                        }
                        frame->BindReconstruction(encoder, draw, reconstructionVisibility,
                                                  draw.program->color[draw.doubleSided].GetHandle());
                    }
                    std::string error;
                    if (!RenderBindingCache::Bind(*frame->device, encoder, RHIBindPoint::Graphics, *draw.bindings,
                                                  error))
                    {
                        throw std::runtime_error(error);
                    }
                    encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0,
                                              blended && forward.reference ? draw.referenceConstants : draw.constants);
                    if (!reconstruct)
                    {
                        frame->BindLayerGeometry(encoder, draw);
                    }
                    encoder.SetBindings(RHIBindPoint::Graphics, 2, table);
                    this->decal.Bind(encoder, draw, decalTable,
                                     inputs.decalBaseline[0].IsValid() && !(transmissionStage || blended.has_value()));
                    this->BindForward(encoder, draw, forward);
                    encoder.SetRootBuffer(RHIBindPoint::Graphics, 3, RHIBufferSlice::Whole(lookup->Samples()));
                    if (draw.program->hasSpecial)
                    {
                        encoder.SetRootBuffer(RHIBindPoint::Graphics, 4,
                                              RHIBufferSlice::Whole(this->subsurface->Reflection()));
                        encoder.SetRootBuffer(RHIBindPoint::Graphics, 5,
                                              RHIBufferSlice::Whole(this->subsurface->Irradiance()));
                    }
                    if (draw.program->hasTransmission)
                    {
                        encoder.SetRootBuffer(RHIBindPoint::Graphics, 6,
                                              RHIBufferSlice::Whole(this->refraction->Samples()));
                    }
                    if (blendedCommands)
                    {
                        blendedCommands->Draw(encoder, 0);
                        frame->blendedBucketSubmissions.fetch_add(1, std::memory_order_relaxed);
                    }
                    else if (reconstruct)
                    {
                        frame->DrawReconstruction(encoder, draw);
                    }
                    else
                    {
                        frame->DrawLayerGeometry(encoder, draw);
                    }
                }
            });
        if (!blended && !transmissionStage)
        {
            // Prepared opaque shading does not consume another callback CPU output.
            graph.DeclareRecordingIndependent(colorPass);
        }
    }
    if (this->runtime.frame && special)
    {
        lighting = DeclareRuntimeEffects(prepared, graph, inputs, commands, lighting, ambientOcclusion, shadowMap,
                                         transmissionStage, blended, forward);
    }
    return lighting;
}

RGHandle SceneHost::EffectStage::DeclareRuntimeEffects(const own::shared_owner<const FrameInput>& prepared,
                                                       EnhancedRenderGraph& graph,
                                                       const SceneSurfaceTargets::Outputs& inputs,
                                                       const SceneShadingInputs& commands, RGHandle lighting,
                                                       RGHandle ambientOcclusion, RGHandle shadowMap,
                                                       bool transmissionStage, std::optional<std::size_t> blended,
                                                       EnhancedForwardLighting forward) const
{
    const auto frame = prepared;
    const bool reconstruct = !blended && !transmissionStage;
    const auto effects = this->runtime.frame;
    const auto lookup = blended ? this->lookup.layered : this->lookup.opaque;
    const auto selected = [blended, transmissionStage](const Frame::Draw& draw)
    {
        return draw.program->hasSurface && draw.program->hasSpecial &&
               draw.program->hasTransmission == transmissionStage &&
               (blended ? draw.blended && draw.inputIndex == *blended : !draw.blended);
    };
    const auto commonUses = [&]
    {
        std::vector<EnhancedRenderGraph::RGPassUsage> uses{
            {inputs.bitmask, RHIResourceState::ShaderResource, RGAccessMode::Read},
            {lookup->GraphSamples(graph), RHIResourceState::PixelShaderResource, RGAccessMode::Read}};
        if (!blended)
        {
            uses.push_back({ambientOcclusion, RHIResourceState::ShaderResource, RGAccessMode::Read});
        }
        this->decal.AddUses(inputs, uses, transmissionStage || blended.has_value());
        lookup->DeclareShadingInputs(graph, uses);
        if (blended)
        {
            uses.push_back({forward.graphCounts, RHIResourceState::ShaderResource, RGAccessMode::Read});
            uses.push_back({forward.graphIndices, RHIResourceState::ShaderResource, RGAccessMode::Read});
            if (this->volume.frame)
            {
                uses.push_back({this->volume.frame->GraphCoefficients(graph), RHIResourceState::PixelShaderResource,
                                RGAccessMode::Read});
            }
        }
        if (frame->shadow)
        {
            uses.push_back({shadowMap, RHIResourceState::ShaderResource, RGAccessMode::Read});
        }
        for (const auto& draw : frame->DrawsFor(blended))
        {
            if (!selected(draw))
            {
                continue;
            }
            uses.push_back({draw.geometry->GraphOutput(graph), RHIResourceState::ShaderResource, RGAccessMode::Read});
            if (frame->UsesMeshlets(draw))
            {
                uses.push_back(
                    {draw.geometry->GraphMeshlets(graph), RHIResourceState::ShaderResource, RGAccessMode::Read});
            }
            for (const auto& texture : draw.bindings->resources.textures)
            {
                uses.push_back({graph.FindImportedTexture(texture.resource), RHIResourceState::PixelShaderResource,
                                RGAccessMode::Read});
            }
        }
        if (reconstruct)
        {
            frame->AddReconstructionReads(graph, uses, inputs.visibility, commands.material);
        }
        else
        {
            frame->AddLayerReads(graph, uses, selected, commands.layers.layer);
        }
        return uses;
    };
    const auto raster = [this, frame, effects, lookup, inputs, ambientOcclusion, shadowMap, transmissionStage, blended,
                         selected, reconstruct](const EnhancedRenderGraph::ExecuteContext& execution,
                                                std::size_t tileIndex, int part, RGHandle lighting,
                                                EnhancedForwardLighting forward)
    {
        frame->CheckCurrent(execution.graph);
        effects->CheckCurrent(*execution.graph);
        const auto& tile = effects->Tiles()[tileIndex];
        const bool capture = part >= 0;
        auto& encoder = *execution.encoder;
        const auto bindings = this->runtime.Tables(*frame, execution, inputs, lighting, ambientOcclusion, shadowMap,
                                                   blended.has_value(), transmissionStage, volume, decal);
        const auto targets = capture ? effects->CaptureTargets(static_cast<unsigned>(part)) : bindings.color;
        forward.volumeTable = bindings.volume;
        encoder.BindRenderTargets(targets);
        if (capture)
        {
            const float clear[]{0, 0, 0, 0};
            encoder.ClearRenderTargets(targets, clear);
            if (!encoder.SetViewport(-static_cast<float>(tile.sourceX), -static_cast<float>(tile.sourceY),
                                     frame->input->View().width, frame->input->View().height) ||
                !encoder.SetScissor(0, 0, tile.sourceWidth, tile.sourceHeight))
            {
                throw std::runtime_error("Runtime special MRT tile needs a supported offset viewport.");
            }
        }
        else
        {
            encoder.SetViewportAndScissor(frame->input->View().width, frame->input->View().height);
            if (!encoder.SetScissor(tile.x, tile.y, tile.width, tile.height))
            {
                throw std::runtime_error("Runtime special tile needs a supported bounded scissor.");
            }
        }
        encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
        RHIBindingTable reconstructionVisibility;
        for (const auto& draw : frame->RasterDrawsFor(blended))
        {
            if (!selected(draw))
            {
                continue;
            }
            const auto pipeline =
                capture ? draw.program->runtimeEffects[part][draw.doubleSided].GetHandle()
                        : (blended ? draw.program->blendedColor : draw.program->color)[draw.doubleSided].GetHandle();
            encoder.SetPipeline(RHIBindPoint::Graphics, frame->RasterPipeline(draw, pipeline));
            if (reconstruct)
            {
                if (!reconstructionVisibility.IsValid())
                {
                    reconstructionVisibility = frame->CreateReconstructionVisibility(execution, inputs.visibility);
                }
                frame->BindReconstruction(encoder, draw, reconstructionVisibility, pipeline);
            }
            std::string error;
            if (!RenderBindingCache::Bind(*frame->device, encoder, RHIBindPoint::Graphics, *draw.bindings, error))
            {
                throw std::runtime_error(error);
            }
            encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0,
                                      blended && forward.reference ? draw.referenceConstants : draw.constants);
            if (!reconstruct)
            {
                frame->BindLayerGeometry(encoder, draw);
            }
            encoder.SetBindings(RHIBindPoint::Graphics, 2, bindings.common);
            encoder.SetRootBuffer(RHIBindPoint::Graphics, 3, RHIBufferSlice::Whole(lookup->Samples()));
            this->decal.Bind(encoder, draw, bindings.decal,
                             inputs.decalBaseline[0].IsValid() && !(transmissionStage || blended.has_value()));
            this->BindForward(encoder, draw, forward);
            effects->Bind(encoder, draw.program->specialEffectsRoot, tileIndex);
            encoder.SetRootBuffer(RHIBindPoint::Graphics, 4, RHIBufferSlice::Whole(lookup->Samples()));
            encoder.SetRootBuffer(RHIBindPoint::Graphics, 5, RHIBufferSlice::Whole(lookup->Samples()));
            if (draw.program->hasTransmission)
            {
                encoder.SetRootBuffer(RHIBindPoint::Graphics, 6, RHIBufferSlice::Whole(lookup->Samples()));
            }
            if (reconstruct)
            {
                frame->DrawReconstruction(encoder, draw);
            }
            else
            {
                frame->DrawLayerGeometry(encoder, draw);
            }
        }
        encoder.SetViewportAndScissor(frame->input->View().width, frame->input->View().height);
    };
    const auto tileDepth = effects->DeclareDepthOutput(graph);
    const auto firstTargets = effects->DeclareCaptureOutputs(graph, 0);
    const auto targets = effects->DeclareCaptureOutputs(graph, 1);
    lighting = AdvanceSceneColor(graph, lighting);
    auto external = commonUses();
    external.push_back({inputs.depth, RHIResourceState::PixelShaderResource, RGAccessMode::Read});
    external.push_back({tileDepth, RHIResourceState::DepthWrite, RGAccessMode::Write});
    for (unsigned i = 0; i < targets.size(); ++i)
    {
        external.push_back({i < 8 ? firstTargets[i] : targets[i], RHIResourceState::RenderTarget, RGAccessMode::Write});
    }
    external.push_back({lighting, RHIResourceState::RenderTarget, RGAccessMode::ReadWrite});
    effects->DeclareShadingInputs(graph, external, false, transmissionStage);
    // Tile depth is already the external Write. Its local Read belongs to the
    // capture phases, not a duplicate access on the outer version.
    std::erase_if(external, [&](const auto& use)
                  { return use.handle.index == tileDepth.index && use.access == RGAccessMode::Read; });
    NormalizeSceneReads(graph, external);
    std::vector<EnhancedRenderGraph::RepeatedPhase> phases;
    phases.push_back({"METEOR.Scene.RuntimeTileDepth",
                      {{inputs.depth, RHIResourceState::PixelShaderResource, RGAccessMode::Read},
                       {tileDepth, RHIResourceState::DepthWrite, RGAccessMode::Write}}});
    for (unsigned part = 0; part < 2; ++part)
    {
        auto captureUses = commonUses();
        effects->DeclareShadingInputs(graph, captureUses, false, transmissionStage);
        const unsigned first = part ? 8 : 0, count = part ? 6 : 8;
        for (unsigned i = first; i < first + count; ++i)
        {
            captureUses.push_back(
                {i < 8 ? firstTargets[i] : targets[i], RHIResourceState::RenderTarget, RGAccessMode::Write});
        }
        NormalizeSceneReads(graph, captureUses);
        phases.push_back({part ? "METEOR.Scene.RuntimeEffectCapture1" : "METEOR.Scene.RuntimeEffectCapture0",
                          std::move(captureUses)});
    }
    auto colorUses = commonUses();
    colorUses.push_back({inputs.depth, RHIResourceState::DepthRead, RGAccessMode::Read});
    colorUses.push_back({lighting, RHIResourceState::RenderTarget, RGAccessMode::ReadWrite});
    effects->DeclareShadingInputs(graph, colorUses);
    NormalizeSceneReads(graph, colorUses);
    phases.push_back({"METEOR.Scene.RuntimeEffectColor", std::move(colorUses)});
    graph.AddRepeatedPass(
        "METEOR.Scene.RuntimeEffects", external, phases, static_cast<std::uint32_t>(effects->Tiles().size()),
        [effects, raster, inputs, lighting, forward](const auto& execution, std::uint32_t iteration,
                                                     std::uint32_t phase)
        {
            if (phase == 0)
            {
                effects->RecordDepth(execution, inputs.depth, iteration);
            }
            else
            {
                raster(execution, iteration, phase == 3 ? -1 : static_cast<int>(phase - 1), lighting, forward);
            }
        });
    return lighting;
}

SceneHost::EffectStage::RuntimeBindings SceneHost::EffectStage::RuntimeState::Tables(
    const FrameInput& frame, const EnhancedRenderGraph::ExecuteContext& execution,
    const SceneSurfaceTargets::Outputs& inputs, RGHandle lighting, RGHandle ambientOcclusion, RGHandle shadowMap,
    bool blended, bool transmissionStage, const VolumeStage& volume, const DecalState& decal) const
{
    const auto color = execution.ResolveHandle(lighting);
    const auto depth = execution.ResolveHandle(inputs.depth);
    const auto owners = execution.ResolveHandle(inputs.bitmask);
    const auto ao = blended ? RHITextureHandle{} : execution.ResolveHandle(ambientOcclusion);
    const auto shadowTexture = frame.shadow ? execution.ResolveHandle(shadowMap) : RHITextureHandle{};
    const bool decalEnabled = inputs.decalBaseline[0].IsValid() && !(transmissionStage || blended);
    const std::array<std::uint64_t, 13> key{color.id,
                                            depth.id,
                                            owners.id,
                                            ao.id,
                                            shadowTexture.id,
                                            transmissionStage || blended,
                                            blended && bool(volume.frame),
                                            decalEnabled ? execution.ResolveHandle(inputs.diffuse).id : 0,
                                            decalEnabled ? execution.ResolveHandle(inputs.metalRough).id : 0,
                                            decalEnabled ? execution.ResolveHandle(inputs.normal).id : 0,
                                            decalEnabled ? execution.ResolveHandle(inputs.decalBaseline[0]).id : 0,
                                            decalEnabled ? execution.ResolveHandle(inputs.decalBaseline[1]).id : 0,
                                            decalEnabled ? execution.ResolveHandle(inputs.decalBaseline[2]).id : 0};
    std::lock_guard lock(mutex);
    auto& result = bindings[key];
    if (!result.color.IsValid())
    {
        const auto depthView = RHIDepthTargetDesc::DepthReadOnly(depth, RHIFormat::D32Float);
        result.color = frame.device->CreateRenderTargets({&color, 1}, &depthView);
        const RHIBindingDesc descriptions[]{
            RHIBindingDesc::SrvCube(frame.environment,
                                    frame.environment.IsValid()
                                        ? frame.device->DescribeTexture(frame.environment).format
                                        : RHIFormat::RGBA16Float,
                                    1)
                .OrNull(),
            RHIBindingDesc::Srv2D(owners, RHIFormat::R32Uint), RHIBindingDesc::Srv2D(ao, RHIFormat::RG16Float).OrNull(),
            RHIBindingDesc::SrvArray(shadowTexture, RHIFormat::R32Float, 3).OrNull()};
        result.common = frame.device->CreateBindings(descriptions);
        result.decal = decal.Table(frame, execution, inputs, transmissionStage || blended);
        if (volume.frame && blended)
        {
            result.volume = frame.device->CreateBindings(volume.frame->LightingBindings(shadowTexture));
        }
    }
    if (!result.color.IsValid() || !result.common.IsValid() || !result.decal.IsValid() ||
        (volume.frame && blended && !result.volume.IsValid()))
    {
        throw std::runtime_error("Runtime special immutable stream bindings could not be allocated.");
    }
    return result;
}

void SceneHost::EffectStage::DecalState::AddUses(const SceneSurfaceTargets::Outputs& inputs,
                                                 std::vector<EnhancedRenderGraph::RGPassUsage>& uses,
                                                 bool transmissionStage) const
{
    if (!inputs.decalBaseline[0].IsValid() || transmissionStage)
    {
        return;
    }
    for (const auto handle : {inputs.diffuse, inputs.metalRough, inputs.normal})
    {
        uses.push_back({handle, RHIResourceState::PixelShaderResource, RGAccessMode::Read});
    }
    for (const auto handle : inputs.decalBaseline)
    {
        uses.push_back({handle, RHIResourceState::PixelShaderResource, RGAccessMode::Read});
    }
}

RHIBindingTable SceneHost::EffectStage::DecalState::Table(const FrameInput& frame,
                                                          const EnhancedRenderGraph::ExecuteContext& execution,
                                                          const SceneSurfaceTargets::Outputs& inputs,
                                                          bool transmissionStage) const
{
    const bool enabled = inputs.decalBaseline[0].IsValid() && !transmissionStage;
    const std::array<RGHandle, 6> handles{inputs.diffuse,          inputs.metalRough,       inputs.normal,
                                          inputs.decalBaseline[0], inputs.decalBaseline[1], inputs.decalBaseline[2]};
    std::array<RHIBindingDesc, 6> descriptions;
    for (unsigned i = 0; i < handles.size(); ++i)
    {
        descriptions[i] = RHIBindingDesc::Srv2D(enabled ? execution.ResolveHandle(handles[i]) : RHITextureHandle{},
                                                RHIFormat::RGBA16Float)
                              .OrNull();
    }
    const auto table = frame.device->CreateBindings(descriptions);
    if (!table.IsValid())
    {
        throw std::runtime_error("METEOR Scene Decal binding failed.");
    }
    return table;
}

template<class DrawType>
void SceneHost::EffectStage::DecalState::Bind(RHIEncoder& encoder, const DrawType& draw, RHIBindingTable table,
                                              bool enabled) const
{
    encoder.SetBindings(RHIBindPoint::Graphics, draw.program->decalRoot, table);
    encoder.SetConstantBuffer(RHIBindPoint::Graphics, draw.program->decalRoot + 1, constants[enabled]);
}

template<class DrawType>
void SceneHost::EffectStage::BindForward(RHIEncoder& encoder, const DrawType& draw,
                                         const EnhancedForwardLighting& forward) const
{
    volume.BindForward(encoder, draw, forward);
    const unsigned index = draw.program->forwardLightsRoot;
    const auto& sceneLookup = draw.blended ? lookup.layered : lookup.opaque;
    sceneLookup->BindRuntime(encoder, index + 7);
    if (draw.program->hasSpecial && !runtime.frame)
    {
        // Offline 분기는 tile을 읽지 않지만 공유 PSO의 root는 모두
        // 유효하게 둔다. 전체 화면 bake 레코드 계약은 그대로 유지한다.
        const unsigned effectIndex = index + 10;
        encoder.SetConstantBuffer(RHIBindPoint::Graphics, effectIndex, volume.constants);
        encoder.SetBindings(RHIBindPoint::Graphics, effectIndex + 1, runtime.emptyInputs);
        encoder.SetBindings(RHIBindPoint::Graphics, effectIndex + 2, runtime.emptyBackground);
    }
}
