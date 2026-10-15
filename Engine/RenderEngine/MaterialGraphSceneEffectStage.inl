// Stage implementation included by MaterialGraphSceneHost.cpp inside material_graph.
    void SceneHost::EffectStage::DeclareDecalInputs(const own::shared_owner<const Frame>& prepared, EnhancedRenderGraph& graph, const SceneSurfaceTargets::Outputs& inputs,
                                       const std::array<RGHandle, 3>& baseline) const
    {
        const auto frame = prepared;
        if (!frame || frame->draws.empty())
        {
            return;
        }
        frame->CheckCurrent(&graph);
        const auto expected = Colors(frame->surfaceStage.gbuffer), actual = Colors(inputs);
        if (!frame->surfaceStage.gbuffer.depth.IsValid() || frame->effectStage.colorOutput.IsValid() || frame->effectStage.decalBaseline[0].IsValid() ||
            !std::equal(expected.begin(), expected.end(), actual.begin(),
                        [](RGHandle a, RGHandle b) { return a.index == b.index; }) ||
            frame->surfaceStage.gbuffer.depth.index != inputs.depth.index ||
            std::ranges::any_of(baseline, [](RGHandle handle) { return !handle.IsValid(); }))
        {
            throw std::runtime_error(
                "METEOR Scene Decal requires its GBuffer, a valid snapshot and one declaration before Color.");
        }
        frame->effectStage.decalBaseline = baseline;
        frame->effectStage.decalInputs = inputs;
    }

    RGHandle SceneHost::EffectStage::DeclareColor(const own::shared_owner<const Frame>& prepared, EnhancedRenderGraph& graph, const SceneSurfaceTargets::Outputs& inputs, const SceneCommandInputs& commands,
                                     RGHandle lighting, RGHandle ambientOcclusion, RGHandle shadowMap) const
    {

        const auto frame = prepared;
        if (!frame || frame->draws.empty())
        {
            return lighting;
        }
        frame->CheckCurrent();
        if (frame->surfaceStage.materialWork) frame->surfaceStage.materialWork->ValidateInputs(graph, commands.material);
        if (frame->finalCullStage.indexedCommands) frame->finalCullStage.indexedCommands->ValidateInput(graph, commands.layer);
        const auto expected = Colors(frame->effectStage.SurfaceInputs(*frame)), actual = Colors(inputs);
        const bool sameTargets = std::equal(expected.begin(), expected.end(), actual.begin(),
                                            [](RGHandle a, RGHandle b) { return a.index == b.index; });
        if (!frame->effectStage.SurfaceInputs(*frame).depth.IsValid() || frame->graph != &graph || frame->graphEpoch != graph.ResourceEpoch() ||
            frame->effectStage.colorOutput.IsValid() || !sameTargets || frame->effectStage.SurfaceInputs(*frame).depth.index != inputs.depth.index)
        {
            throw std::runtime_error("METEOR Scene color requires its own GBuffer producer and one declaration.");
        }
        frame->effectStage.lookup->DeclareInputs(graph);
        if (frame->effectStage.runtimeEffects)
        {
            frame->effectStage.runtimeEffects->DeclareInputs(graph);
        }
        if (frame->effectStage.volume)
        {
            frame->effectStage.volume->DeclareCoefficients(graph, frame->effectStage.volumeBindings);
        }
        if (frame->effectStage.subsurface)
        {
            frame->effectStage.subsurface->DeclareInputs(graph);
        }
        lighting = DeclareShading(prepared, graph, inputs, commands, lighting, ambientOcclusion, shadowMap, false);
        if (frame->effectStage.refraction)
        {
            frame->effectStage.refraction->DeclareInputs(graph);
        }
        frame->effectStage.colorOutput = lighting;
        frame->effectStage.lookup->DeclareReady(graph);
        return lighting;
    }

std::vector<SceneHost::ForwardDraw> SceneHost::EffectStage::BlendedDraws(const own::shared_owner<const Frame>& prepared) const
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

    RGHandle SceneHost::EffectStage::DeclareBlended(const own::shared_owner<const Frame>& prepared, EnhancedRenderGraph& graph, const std::vector<std::size_t>& indices, const SceneCommandInputs& commands, RGHandle lighting,
                                       RGHandle shadowMap, const EnhancedForwardLighting& forward) const
    {

        const auto frame = prepared;
        if (indices.empty()) throw std::runtime_error("METEOR blended range is empty.");
        const auto index = indices.front();
        if (!frame || !frame->effectStage.colorOutput.IsValid() || !frame->effectStage.alphaLookup || !lighting.IsValid() ||
            index >= frame->input->Draws().size() || !forward.lights.IsValid() || !forward.counts.IsValid() ||
            !forward.indices.IsValid() || !forward.graphCounts.IsValid() || !forward.graphIndices.IsValid())
        {
            throw std::runtime_error("Graph Blend needs its exact prepared frame and light-tile resources.");
        }
        frame->CheckCurrent(&graph);
        std::set<std::size_t> selected;
        for (const auto input : indices) if (!selected.insert(input).second || input >= frame->input->Draws().size() || frame->effectStage.blendedOutputs.contains(input))
        {
            throw std::runtime_error("Graph Blend draw declared twice.");
        }
        if (!frame->effectStage.alphaLookup->InputsDeclared(graph))
        {
            frame->effectStage.alphaLookup->DeclareInputs(graph);
        }
        const auto& p = frame->input->Draws()[index].material->generation->cooked.product.program;
        const bool needsSurfaceBuffer = (p.features & 0x1800u) != 0;
        if (needsSurfaceBuffer)
        {
            DeclareForwardGBuffer(prepared, graph, frame->effectStage.SurfaceInputs(*frame), commands, index);
        }
        if ((p.features & 0x0800u) != 0)
        {
            if (frame->effectStage.runtimeEffects)
            {
                frame->effectStage.runtimeEffects->DeclareBackground(graph, lighting, frame->effectStage.SurfaceInputs(*frame).depth);
            }
            else
            {
                frame->effectStage.refraction->DeclareBackground(graph, lighting, frame->effectStage.SurfaceInputs(*frame).depth);
            }
        }
        lighting = DeclareShading(prepared, graph, needsSurfaceBuffer ? frame->effectStage.forwardGbuffer : frame->effectStage.SurfaceInputs(*frame), commands, lighting, {},
                                  shadowMap, (p.features & 0x0800u) != 0, index, forward, indices);
        frame->effectStage.alphaLookup->DeclareReady(graph);
        for (const auto input : indices) frame->effectStage.blendedOutputs.emplace(input, lighting);
        return lighting;
    }

    RGHandle SceneHost::EffectStage::DeclareBlendedStream(const own::shared_owner<const Frame>& prepared, EnhancedRenderGraph& graph, const SceneCommandInputs& commands, RGHandle lighting, RGHandle shadowMap,
                                             const EnhancedForwardLighting& tiles) const
    {
        if (prepared && !prepared->draws.empty()) {
            if (prepared->finalCullStage.indexedCommands) prepared->finalCullStage.indexedCommands->ValidateInput(graph, commands.layer);
            size_t next = 0;
            for (const auto& range : prepared->effectStage.blendedRanges)
                for (const auto& batch : range.commands) if (batch) {
                    if (next >= commands.blended.size()) throw std::runtime_error("METEOR blended commands missing.");
                    batch->ValidateInput(graph, commands.blended[next++]);
                }
            if (next != commands.blended.size()) throw std::runtime_error("METEOR blended commands have extra inputs.");
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
        if (const auto volume = prepared->effectStage.volume)
        {
            shared.volumeConstants = volume->TransportConstants();
            shared.volumeTriangles = volume->Triangles();
            shared.volumeCoefficients = RHIBufferSlice::Whole(volume->Coefficients());
            shared.graphVolumeCoefficients = volume->GraphCoefficients(graph);
            shared.volumeEnvironment = volume->LightingBindings({})[1].resource;
        }
        else
        {
            shared.volumeConstants = prepared->effectStage.emptyMediumConstants;
            shared.volumeTriangles = prepared->effectStage.emptyMediumTriangles;
            shared.volumeCoefficients = shared.volumeTriangles;
        }
        for (const auto& range : prepared->effectStage.blendedRanges)
            lighting = DeclareBlended(prepared, graph, range.inputs, commands, lighting, shadowMap, shared);
        return lighting;
    }

    RGHandle SceneHost::EffectStage::DeclareVolume(const own::shared_owner<const Frame>& prepared, EnhancedRenderGraph& graph, RGHandle lighting, RGHandle depth,
                                      RGHandle shadowMap) const
    {
        const auto frame = prepared;
        if (!frame || !frame->effectStage.volume || frame->effectStage.volumeOutput.IsValid())
        {
            return lighting;
        }
        frame->CheckCurrent(&graph);
        if (!frame->effectStage.colorOutput.IsValid())
        {
            throw std::runtime_error("METEOR Scene Volume requires completed surface shading.");
        }
        const auto result = frame->effectStage.volume->DeclareComposite(graph, lighting, depth, shadowMap);
        frame->effectStage.volumeOutput = result;
        return result;
    }

    void SceneHost::EffectStage::DeclareForwardGBuffer(const own::shared_owner<const Frame>& prepared, EnhancedRenderGraph& graph, const SceneSurfaceTargets::Outputs& opaque, const SceneCommandInputs& commands,
                                          std::size_t index) const
    {

        const auto frame = prepared;
        if (!frame->effectStage.forwardGbuffer.depth.IsValid())
        {
            RGTextureDesc desc;
            desc.width = frame->input->View().width;
            desc.height = frame->input->View().height;
            desc.allowRenderTarget = true;
            auto targets = std::array{&frame->effectStage.forwardGbuffer.diffuse, &frame->effectStage.forwardGbuffer.metalRough,
                                      &frame->effectStage.forwardGbuffer.normal, &frame->effectStage.forwardGbuffer.emissive,
                                      &frame->effectStage.forwardGbuffer.bitmask};
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
            frame->effectStage.forwardGbuffer.depth = graph.CreateTexture(desc);
        }
        frame->effectStage.forwardGbuffer.depth = graph.Write(frame->effectStage.forwardGbuffer.depth);
        const auto copiedDepth = frame->effectStage.forwardGbuffer.depth;
        graph.AddPass("METEOR.Blended.DepthCopy",
                      {{opaque.depth, RHIResourceState::CopySource, RGAccessMode::Read},
                       {copiedDepth, RHIResourceState::CopyDest, RGAccessMode::Write}},
                      [frame, opaque, copiedDepth](const auto& execution) {
                          frame->CheckCurrent(execution.graph);
                          execution.encoder->CopyResource(execution.ResolveHandle(copiedDepth),
                                                          execution.ResolveHandle(opaque.depth));
                      });
        frame->effectStage.forwardGbuffer = AdvanceSceneSurface(graph, frame->effectStage.forwardGbuffer, RGAccessMode::Write);
        const auto inputs = frame->effectStage.forwardGbuffer;
        std::vector<EnhancedRenderGraph::RGPassUsage> uses{{inputs.depth, RHIResourceState::DepthWrite, RGAccessMode::ReadWrite}};
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
                uses.push_back(
                    {graph.FindImportedTexture(texture.resource), RHIResourceState::PixelShaderResource, RGAccessMode::Read});
            }
        }
        frame->AddGeometryReads(graph, uses, [&](const auto& draw) { return draw.inputIndex == index && draw.program->hasSurface; }, commands);
        NormalizeSceneReads(graph, uses);
        graph.AddPass("METEOR.Blended.Surface", uses, [frame, inputs, index](const auto& execution) {
            frame->CheckCurrent(execution.graph);
            std::array<RHITextureHandle, 5> colors;
            const auto handles = Colors(inputs);
            for (unsigned i = 0; i < colors.size(); ++i)
            {
                colors[i] = execution.ResolveHandle(handles[i]);
            }
            RHIRenderTargetBinding targets;
            {
                std::lock_guard lock(frame->effectStage.forwardTargetMutex);
                if (!frame->effectStage.forwardTargets.IsValid())
                {
                    const auto depth = RHIDepthTargetDesc::Depth(execution.ResolveHandle(inputs.depth), RHIFormat::D32Float);
                    frame->effectStage.forwardTargets = frame->device->CreateRenderTargets(colors, &depth);
                }
                targets = frame->effectStage.forwardTargets;
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
                encoder.SetPipeline(RHIBindPoint::Graphics, frame->RasterPipeline(draw, draw.program->gbuffer[draw.doubleSided].GetHandle()));
                std::string error;
                if (!RenderBindingCache::Bind(*frame->device, encoder, RHIBindPoint::Graphics, *draw.bindings, error))
                {
                    throw std::runtime_error(error);
                }
                encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0, draw.constants);
                frame->BindGeometry(encoder, draw);
                frame->DrawGeometry(encoder, draw);
            }
        });
    }

    void SceneHost::EffectStage::DeclareRefractionCapture(const own::shared_owner<const Frame>& prepared, EnhancedRenderGraph& graph, const SceneSurfaceTargets::Outputs& inputs, const SceneCommandInputs& commands,
                                             std::optional<std::size_t> index) const
    {
        const auto frame = prepared;
        const auto targets = frame->effectStage.refraction->DeclareCaptureOutputs(graph);
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
                uses.push_back(
                    {graph.FindImportedTexture(texture.resource), RHIResourceState::PixelShaderResource, RGAccessMode::Read});
            }
        }
        frame->AddGeometryReads(graph, uses, [&](const auto& draw) { return draw.program->hasTransmission && (!index || draw.inputIndex == *index); }, commands);
        NormalizeSceneReads(graph, uses);
        graph.AddPass("METEOR.Scene.RefractionCapture", uses, [frame, inputs, index](const auto& execution) {
            frame->CheckCurrent(execution.graph);
            const auto depth =
                RHIDepthTargetDesc::DepthReadOnly(execution.ResolveHandle(inputs.depth), RHIFormat::D32Float);
            const auto targets = frame->device->CreateRenderTargets(frame->effectStage.refraction->Inputs(), &depth);
            const RHIBindingDesc descriptions[]{
                RHIBindingDesc::SrvCube({}, RHIFormat::RGBA16Float, 1).OrNull(),
                RHIBindingDesc::Srv2D(execution.ResolveHandle(inputs.bitmask), RHIFormat::R32Uint),
                RHIBindingDesc::Srv2D({}, RHIFormat::RG16Float).OrNull(),
                RHIBindingDesc::SrvArray({}, RHIFormat::R32Float, 3).OrNull()};
            const auto table = frame->device->CreateBindings(descriptions);
            const auto decalTable = frame->effectStage.DecalTable(*frame, execution, true);
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
                encoder.SetPipeline(RHIBindPoint::Graphics, frame->RasterPipeline(draw, draw.program->refraction[draw.doubleSided].GetHandle()));
                std::string error;
                if (!RenderBindingCache::Bind(*frame->device, encoder, RHIBindPoint::Graphics, *draw.bindings, error))
                {
                    throw std::runtime_error(error);
                }
                encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0, draw.constants);
                frame->BindGeometry(encoder, draw);
                encoder.SetBindings(RHIBindPoint::Graphics, 2, table);
                frame->effectStage.BindDecal(encoder, draw, decalTable, true);
                frame->DrawGeometry(encoder, draw);
            }
        });
    }

    RGHandle SceneHost::EffectStage::DeclareShading(const own::shared_owner<const Frame>& prepared, EnhancedRenderGraph& graph, const SceneSurfaceTargets::Outputs& inputs, const SceneCommandInputs& commands,
                                       RGHandle lighting, RGHandle ambientOcclusion, RGHandle shadowMap,
                                       bool transmissionStage, std::optional<std::size_t> blended,
                                       EnhancedForwardLighting forward, std::vector<std::size_t> blendedGroup) const
    {

        const auto frame = prepared;
        const bool reconstruct = !blended && !transmissionStage;
        const auto lookup = blended ? frame->effectStage.alphaLookup : frame->effectStage.lookup;
        const auto selected = [blended, blendedGroup](const Frame::Draw& draw) {
            return blended ? draw.blended && (blendedGroup.empty() ? draw.inputIndex == *blended :
                std::ranges::find(blendedGroup, draw.inputIndex) != blendedGroup.end()) : !draw.blended;
        };
        const bool ordinary = !frame->effectStage.runtimeEffects || std::ranges::any_of(frame->draws, [&](const auto& draw) {
            return selected(draw) && draw.program->hasSurface && !draw.program->hasSpecial &&
                   draw.program->hasTransmission == transmissionStage;
        });
        std::shared_ptr<const IndirectMaterialBatch> blendedCommands;
        if (blended && ordinary) {
            const auto order = blendedGroup.empty() ? std::vector<size_t>{*blended} : blendedGroup;
            const auto found = std::ranges::find_if(frame->effectStage.blendedRanges,
                [&](const auto& range) { return range.inputs == order; });
            if (found == frame->effectStage.blendedRanges.end())
                throw std::runtime_error("METEOR blended range was not prepared.");
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
        if (reconstruct) std::erase_if(uses, [&](const auto& use) { return use.handle.index == inputs.depth.index; });
        if (blended)
        {
            uses.pop_back();
        }
        frame->effectStage.AddDecalUses(*frame, uses, transmissionStage || blended.has_value());
        lookup->DeclareShadingInputs(graph, uses);
        if (blended)
        {
            uses.push_back({forward.graphCounts, RHIResourceState::ShaderResource, RGAccessMode::Read});
            uses.push_back({forward.graphIndices, RHIResourceState::ShaderResource, RGAccessMode::Read});
            if (frame->effectStage.volume)
            {
                uses.push_back(
                    {frame->effectStage.volume->GraphCoefficients(graph), RHIResourceState::PixelShaderResource, RGAccessMode::Read});
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
                uses.push_back(
                    {graph.FindImportedTexture(texture.resource), RHIResourceState::PixelShaderResource, RGAccessMode::Read});
            }
        }
        for (unsigned part = 0; part < (lookup->RequiresCapture() ? 2u : 0u); ++part)
        {
            std::vector<EnhancedRenderGraph::RGPassUsage> captureUses{
                {inputs.depth, RHIResourceState::DepthRead, RGAccessMode::Read},
                {inputs.bitmask, RHIResourceState::ShaderResource, RGAccessMode::Read}};
            if (reconstruct) std::erase_if(captureUses, [&](const auto& use) { return use.handle.index == inputs.depth.index; });
            frame->effectStage.AddDecalUses(*frame, captureUses, transmissionStage || blended.has_value());
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
                    captureUses.push_back({draw.geometry->GraphMeshlets(graph), RHIResourceState::ShaderResource, RGAccessMode::Read});
                }
                for (const auto& texture : draw.bindings->resources.textures)
                {
                    captureUses.push_back({graph.FindImportedTexture(texture.resource),
                                           RHIResourceState::PixelShaderResource, RGAccessMode::Read});
                }
            }
            if (reconstruct) frame->AddReconstructionReads(graph, captureUses, inputs.visibility, commands.material);
            else frame->AddGeometryReads(graph, captureUses, [&](const auto& draw) { return draw.program->hasSurface && selected(draw); }, commands);
            NormalizeSceneReads(graph, captureUses);
            graph.AddPass(
                reconstruct ? "METEOR.Scene.Lookup.Resolve" : "METEOR.Scene.LookupCapture", captureUses,
                [frame, inputs, first, count, part, transmissionStage, blended, lookup, selected,
                 forward, reconstruct](const auto& execution) {
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
                    const auto decalTable = frame->effectStage.DecalTable(*frame, execution, transmissionStage || blended.has_value());
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
                    for (const auto& draw : frame->RasterDrawsFor(blended))
                    {
                        if (!draw.program->hasSurface || !selected(draw))
                        {
                            continue;
                        }
                        encoder.SetPipeline(RHIBindPoint::Graphics, frame->RasterPipeline(draw, (blended ? draw.program->blendedLookup : draw.program->lookup)[part][draw.doubleSided]
                                .GetHandle()));
                        if (reconstruct) frame->BindReconstruction(execution, draw, inputs.visibility, draw.program->lookup[part][draw.doubleSided].GetHandle());
                        std::string error;
                        if (!RenderBindingCache::Bind(*frame->device, encoder, RHIBindPoint::Graphics, *draw.bindings,
                                                      error))
                        {
                            throw std::runtime_error(error);
                        }
                        encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0,
                                                  blended && forward.reference ? draw.referenceConstants
                                                                               : draw.constants);
                        if (!reconstruct) frame->BindGeometry(encoder, draw);
                        encoder.SetBindings(RHIBindPoint::Graphics, 2, table);
                        frame->effectStage.BindDecal(encoder, draw, decalTable, transmissionStage || blended.has_value());
                        frame->effectStage.BindForward(encoder, draw, forward);
                        if (reconstruct) frame->DrawReconstruction(encoder, draw);
                        else frame->DrawGeometry(encoder, draw);
                    }
                });
        }
        lookup->DeclareBake(graph, inputs.bitmask);
        uses.push_back({lookup->GraphSamples(graph), RHIResourceState::PixelShaderResource, RGAccessMode::Read});
        if (transmissionStage && !frame->effectStage.runtimeEffects)
        {
            DeclareRefractionCapture(prepared, graph, inputs, commands, blended);
            frame->effectStage.refraction->DeclareBake(graph, *lookup, inputs.bitmask, shadowMap);
            uses.push_back({frame->effectStage.refraction->GraphSamples(graph), RHIResourceState::PixelShaderResource, RGAccessMode::Read});
        }
        const bool special = std::ranges::any_of(frame->draws, [&](const auto& draw) {
            return selected(draw) && draw.program->hasSpecial && draw.program->hasTransmission == transmissionStage;
        });
        if (frame->effectStage.subsurface && special)
        {
            const auto targets = frame->effectStage.subsurface->DeclareCaptureOutputs(graph);
            frame->effectStage.subsurface->DeclareReflection(graph, *lookup, inputs.bitmask);
            std::vector<EnhancedRenderGraph::RGPassUsage> captureUses{
                {inputs.depth, RHIResourceState::DepthRead, RGAccessMode::Read},
                {inputs.bitmask, RHIResourceState::ShaderResource, RGAccessMode::Read},
                {lookup->GraphSamples(graph), RHIResourceState::PixelShaderResource, RGAccessMode::Read},
                {frame->effectStage.subsurface->GraphReflection(graph), RHIResourceState::PixelShaderResource, RGAccessMode::Read}};
            if (reconstruct) std::erase_if(captureUses, [&](const auto& use) { return use.handle.index == inputs.depth.index; });
            frame->effectStage.AddDecalUses(*frame, captureUses, transmissionStage || blended.has_value());
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
                    captureUses.push_back({draw.geometry->GraphMeshlets(graph), RHIResourceState::ShaderResource, RGAccessMode::Read});
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
                    {frame->effectStage.refraction->GraphSamples(graph), RHIResourceState::PixelShaderResource, RGAccessMode::Read});
            }
            if (reconstruct) frame->AddReconstructionReads(graph, captureUses, inputs.visibility, commands.material);
            else frame->AddGeometryReads(graph, captureUses, [&](const auto& draw) { return draw.program->hasSubsurface && selected(draw); }, commands);
            NormalizeSceneReads(graph, captureUses);
            graph.AddPass(
                "METEOR.Scene.SubsurfaceCapture", captureUses,
                [frame, inputs, shadowMap, transmissionStage, blended, lookup, selected,
                 forward, reconstruct](const auto& execution) {
                    frame->CheckCurrent(execution.graph);
                    auto& encoder = *execution.encoder;
                    const auto depth =
                        RHIDepthTargetDesc::DepthReadOnly(execution.ResolveHandle(inputs.depth), RHIFormat::D32Float);
                    const auto targets = frame->device->CreateRenderTargets(frame->effectStage.subsurface->Inputs(), reconstruct ? nullptr : &depth);
                    const RHIBindingDesc descriptions[]{
                        RHIBindingDesc::SrvCube(lookup->UsesRuntimeEvaluation() ? frame->environment : RHITextureHandle{},
                                                frame->environment.IsValid()
                                                    ? frame->device->DescribeTexture(frame->environment).format
                                                    : RHIFormat::RGBA16Float, 1).OrNull(),
                        RHIBindingDesc::Srv2D(execution.ResolveHandle(inputs.bitmask), RHIFormat::R32Uint),
                        RHIBindingDesc::Srv2D({}, RHIFormat::RG16Float).OrNull(),
                        RHIBindingDesc::SrvArray(frame->shadow ? execution.ResolveHandle(shadowMap)
                                                               : RHITextureHandle{},
                                                 RHIFormat::R32Float, 3)
                            .OrNull()};
                    const auto table = frame->device->CreateBindings(descriptions);
                    const auto decalTable = frame->effectStage.DecalTable(*frame, execution, transmissionStage || blended.has_value());
                    if (!targets.IsValid() || !table.IsValid())
                    {
                        throw std::runtime_error("METEOR Scene SSS capture target binding failed.");
                    }
                    encoder.BindRenderTargets(targets);
                    const float clear[]{0, 0, 0, 0};
                    encoder.ClearRenderTargets(targets, clear);
                    encoder.SetViewportAndScissor(frame->input->View().width, frame->input->View().height);
                    encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
                    for (const auto& draw : frame->RasterDrawsFor(blended))
                    {
                        if (!selected(draw) || !draw.program->hasSubsurface ||
                            (draw.program->hasTransmission && !transmissionStage))
                        {
                            continue;
                        }
                        encoder.SetPipeline(RHIBindPoint::Graphics, frame->RasterPipeline(draw, draw.program->subsurface[draw.doubleSided].GetHandle()));
                        if (reconstruct) frame->BindReconstruction(execution, draw, inputs.visibility, draw.program->subsurface[draw.doubleSided].GetHandle());
                        std::string error;
                        if (!RenderBindingCache::Bind(*frame->device, encoder, RHIBindPoint::Graphics, *draw.bindings,
                                                      error))
                        {
                            throw std::runtime_error(error);
                        }
                        encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0,
                                                  blended && forward.reference ? draw.referenceConstants
                                                                               : draw.constants);
                        if (!reconstruct) frame->BindGeometry(encoder, draw);
                        encoder.SetBindings(RHIBindPoint::Graphics, 2, table);
                        frame->effectStage.BindDecal(encoder, draw, decalTable, transmissionStage || blended.has_value());
                        frame->effectStage.BindForward(encoder, draw, forward);
                        encoder.SetRootBuffer(RHIBindPoint::Graphics, 3, RHIBufferSlice::Whole(lookup->Samples()));
                        encoder.SetRootBuffer(RHIBindPoint::Graphics, 4,
                                              RHIBufferSlice::Whole(frame->effectStage.subsurface->Reflection()));
                        if (draw.program->hasTransmission)
                        {
                            encoder.SetRootBuffer(RHIBindPoint::Graphics, 6,
                                                  RHIBufferSlice::Whole(frame->effectStage.refraction->Samples()));
                        }
                        if (reconstruct) frame->DrawReconstruction(encoder, draw);
                        else frame->DrawGeometry(encoder, draw);
                    }
                });
            frame->effectStage.subsurface->DeclareFilter(graph, inputs.bitmask);
            uses.push_back(
                {frame->effectStage.subsurface->GraphReflection(graph), RHIResourceState::PixelShaderResource, RGAccessMode::Read});
            uses.push_back(
                {frame->effectStage.subsurface->GraphIrradiance(graph), RHIResourceState::PixelShaderResource, RGAccessMode::Read});
        }
        if (ordinary)
        {
            if (blendedCommands) frame->finalCullStage.blendedMeshlets->AddVisibleIdsRead(graph, uses);
            else if (reconstruct) frame->AddReconstructionReads(graph, uses, inputs.visibility, commands.material);
            else frame->AddGeometryReads(graph, uses, [&](const auto& draw) { return draw.program->hasSurface && !draw.program->hasSpecial && selected(draw); }, commands);
            if (blendedCommands) blendedCommands->AddRead(graph, uses, BlendedCommandInput(graph, *blendedCommands, commands));
            NormalizeSceneReads(graph, uses);
            const auto colorPass = graph.AddPass(
                blended             ? "METEOR.Blended.Color"
                : transmissionStage ? "METEOR.Scene.TransmissionColor"
                                    : "METEOR.Scene.Color",
                uses,
                [frame, inputs, lighting, ambientOcclusion, shadowMap, transmissionStage, blended, lookup, selected,
                 forward, blendedCommands, reconstruct](const auto& execution) mutable {
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
                    const auto decalTable = frame->effectStage.DecalTable(*frame, execution, transmissionStage || blended.has_value());
                    if (frame->effectStage.volume && blended)
                    {
                        forward.volumeTable = frame->device->CreateBindings(frame->effectStage.volume->LightingBindings(
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
                    for (const auto& draw : frame->RasterDrawsFor({}, blended.has_value()))
                    {
                        if (!selected(draw) || !draw.program->hasSurface ||
                            draw.program->hasTransmission != transmissionStage ||
                            (frame->effectStage.runtimeEffects && draw.program->hasSpecial))
                        {
                            continue;
                        }
                        if (blendedCommands && (blendedCommands->buckets.empty() ||
                            &draw != &frame->draws[blendedCommands->buckets.front().representative])) continue;
                        encoder.SetPipeline(RHIBindPoint::Graphics, frame->RasterPipeline(draw, (blended ? draw.program->blendedColor : draw.program->color)[draw.doubleSided].GetHandle()));
                        if (reconstruct) frame->BindReconstruction(execution, draw, inputs.visibility, draw.program->color[draw.doubleSided].GetHandle());
                        std::string error;
                        if (!RenderBindingCache::Bind(*frame->device, encoder, RHIBindPoint::Graphics, *draw.bindings,
                                                      error))
                        {
                            throw std::runtime_error(error);
                        }
                        encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0,
                                                  blended && forward.reference ? draw.referenceConstants : draw.constants);
                        if (!reconstruct) frame->BindGeometry(encoder, draw);
                        encoder.SetBindings(RHIBindPoint::Graphics, 2, table);
                        frame->effectStage.BindDecal(encoder, draw, decalTable, transmissionStage || blended.has_value());
                        frame->effectStage.BindForward(encoder, draw, forward);
                        encoder.SetRootBuffer(RHIBindPoint::Graphics, 3, RHIBufferSlice::Whole(lookup->Samples()));
                        if (draw.program->hasSpecial)
                        {
                            encoder.SetRootBuffer(RHIBindPoint::Graphics, 4,
                                                  RHIBufferSlice::Whole(frame->effectStage.subsurface->Reflection()));
                            encoder.SetRootBuffer(RHIBindPoint::Graphics, 5,
                                                  RHIBufferSlice::Whole(frame->effectStage.subsurface->Irradiance()));
                        }
                        if (draw.program->hasTransmission)
                        {
                            encoder.SetRootBuffer(RHIBindPoint::Graphics, 6,
                                                  RHIBufferSlice::Whole(frame->effectStage.refraction->Samples()));
                        }
                        if (blendedCommands) {
                            blendedCommands->Draw(encoder, 0);
                            frame->blendedBucketSubmissions.fetch_add(1, std::memory_order_relaxed);
                        } else if (reconstruct) frame->DrawReconstruction(encoder, draw);
                        else frame->DrawGeometry(encoder, draw);
                    }
                });
            if (!blended && !transmissionStage)
            {
                // Prepared opaque shading does not consume another callback CPU output.
                graph.DeclareRecordingIndependent(colorPass);
            }
        }
        if (frame->effectStage.runtimeEffects && special)
        {
            lighting = DeclareRuntimeEffects(prepared, graph, inputs, commands, lighting, ambientOcclusion, shadowMap,
                                             transmissionStage, blended, forward);
        }
        return lighting;
    }

    RGHandle SceneHost::EffectStage::DeclareRuntimeEffects(const own::shared_owner<const Frame>& prepared, EnhancedRenderGraph& graph, const SceneSurfaceTargets::Outputs& inputs, const SceneCommandInputs& commands,
                                              RGHandle lighting, RGHandle ambientOcclusion, RGHandle shadowMap,
                                              bool transmissionStage, std::optional<std::size_t> blended,
                                              EnhancedForwardLighting forward) const
    {
        const auto frame = prepared;
        const bool reconstruct = !blended && !transmissionStage;
        const auto effects = frame->effectStage.runtimeEffects;
        const auto lookup = blended ? frame->effectStage.alphaLookup : frame->effectStage.lookup;
        const auto selected = [blended, transmissionStage](const Frame::Draw& draw) {
            return draw.program->hasSurface && draw.program->hasSpecial &&
                   draw.program->hasTransmission == transmissionStage &&
                   (blended ? draw.blended && draw.inputIndex == *blended : !draw.blended);
        };
        const auto commonUses = [&] {
            std::vector<EnhancedRenderGraph::RGPassUsage> uses{
                {inputs.bitmask, RHIResourceState::ShaderResource, RGAccessMode::Read},
                {lookup->GraphSamples(graph), RHIResourceState::PixelShaderResource, RGAccessMode::Read}};
            if (!blended)
            {
                uses.push_back({ambientOcclusion, RHIResourceState::ShaderResource, RGAccessMode::Read});
            }
            frame->effectStage.AddDecalUses(*frame, uses, transmissionStage || blended.has_value());
            lookup->DeclareShadingInputs(graph, uses);
            if (blended)
            {
                uses.push_back({forward.graphCounts, RHIResourceState::ShaderResource, RGAccessMode::Read});
                uses.push_back({forward.graphIndices, RHIResourceState::ShaderResource, RGAccessMode::Read});
                if (frame->effectStage.volume)
                {
                    uses.push_back({frame->effectStage.volume->GraphCoefficients(graph), RHIResourceState::PixelShaderResource, RGAccessMode::Read});
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
                    uses.push_back({draw.geometry->GraphMeshlets(graph), RHIResourceState::ShaderResource, RGAccessMode::Read});
                }
                for (const auto& texture : draw.bindings->resources.textures)
                {
                    uses.push_back({graph.FindImportedTexture(texture.resource), RHIResourceState::PixelShaderResource, RGAccessMode::Read});
                }
            }
            if (reconstruct) frame->AddReconstructionReads(graph, uses, inputs.visibility, commands.material);
            else frame->AddGeometryReads(graph, uses, selected, commands);
            return uses;
        };
        const auto raster = [frame, effects, lookup, inputs, ambientOcclusion, shadowMap, transmissionStage,
                             blended, selected, reconstruct](const EnhancedRenderGraph::ExecuteContext& execution,
                                                std::size_t tileIndex, int part, RGHandle lighting,
                                                EnhancedForwardLighting forward) {
            frame->CheckCurrent(execution.graph);
            effects->CheckCurrent(*execution.graph);
            const auto& tile = effects->Tiles()[tileIndex];
            const bool capture = part >= 0;
            auto& encoder = *execution.encoder;
            const auto bindings = frame->effectStage.RuntimeTables(*frame, execution, inputs, lighting, ambientOcclusion, shadowMap,
                                                       blended.has_value(), transmissionStage);
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
            for (const auto& draw : frame->RasterDrawsFor(blended))
            {
                if (!selected(draw))
                {
                    continue;
                }
                const auto pipeline = capture ? draw.program->runtimeEffects[part][draw.doubleSided].GetHandle()
                    : (blended ? draw.program->blendedColor : draw.program->color)[draw.doubleSided].GetHandle();
                encoder.SetPipeline(RHIBindPoint::Graphics, frame->RasterPipeline(draw, pipeline));
                if (reconstruct) frame->BindReconstruction(execution, draw, inputs.visibility, pipeline);
                std::string error;
                if (!RenderBindingCache::Bind(*frame->device, encoder, RHIBindPoint::Graphics, *draw.bindings, error))
                {
                    throw std::runtime_error(error);
                }
                encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0,
                                          blended && forward.reference ? draw.referenceConstants : draw.constants);
                if (!reconstruct) frame->BindGeometry(encoder, draw);
                encoder.SetBindings(RHIBindPoint::Graphics, 2, bindings.common);
                encoder.SetRootBuffer(RHIBindPoint::Graphics, 3, RHIBufferSlice::Whole(lookup->Samples()));
                frame->effectStage.BindDecal(encoder, draw, bindings.decal, transmissionStage || blended.has_value());
                frame->effectStage.BindForward(encoder, draw, forward);
                effects->Bind(encoder, draw.program->specialEffectsRoot, tileIndex);
                encoder.SetRootBuffer(RHIBindPoint::Graphics, 4, RHIBufferSlice::Whole(lookup->Samples()));
                encoder.SetRootBuffer(RHIBindPoint::Graphics, 5, RHIBufferSlice::Whole(lookup->Samples()));
                if (draw.program->hasTransmission)
                {
                    encoder.SetRootBuffer(RHIBindPoint::Graphics, 6, RHIBufferSlice::Whole(lookup->Samples()));
                }
                if (reconstruct) frame->DrawReconstruction(encoder, draw);
                else frame->DrawGeometry(encoder, draw);
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
        std::erase_if(external, [&](const auto& use) {
            return use.handle.index == tileDepth.index && use.access == RGAccessMode::Read;
        });
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
                captureUses.push_back({i < 8 ? firstTargets[i] : targets[i], RHIResourceState::RenderTarget,
                                       RGAccessMode::Write});
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
        graph.AddRepeatedPass("METEOR.Scene.RuntimeEffects", external, phases,
            static_cast<std::uint32_t>(effects->Tiles().size()),
            [effects, raster, inputs, lighting, forward](const auto& execution, std::uint32_t iteration,
                                                         std::uint32_t phase) {
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


    bool SceneHost::EffectStage::PrepareBlended(Frame& frame, const std::shared_ptr<const LX::Runtime::ComputeGeneration>& generation, std::string& error)
{
            // Back to front. Equal depths keep a stable geometry/draw order.
            std::vector<ForwardDraw> order;
            for (size_t i = 0; i < frame.input->Draws().size(); ++i) {
                const auto& draw = frame.input->Draws()[i];
                const auto& p = draw.material->generation->cooked.product.program;
                if (p.surface && (draw.queue == SceneCoverage::Blended || (p.features & 0x0800u)))
                    order.push_back({i, draw.geometryKey, draw.viewDepth});
            }
            for (const auto& draw : order) if (!std::isfinite(draw.depth))
                throw std::runtime_error("METEOR blended sorting depth is non-finite.");
            std::stable_sort(order.begin(), order.end(), [](const ForwardDraw& a, const ForwardDraw& b) {
                if (a.depth != b.depth) return a.depth > b.depth;
                if (a.geometryKey != b.geometryKey) return a.geometryKey < b.geometryKey;
                return a.index < b.index;
            });
            const auto mergePipeline = [&](std::size_t input) {
                RHIPipelineHandle result{};
                if (alphaLookup->RequiresCapture()) return result;
                for (const auto& draw : frame.DrawsFor(input)) {
                    // Surface effects consume per-layer captures/backgrounds. Those
                    // are semantic ordering boundaries, not alternative alpha paths.
                    if (!draw.program->hasSurface || draw.program->hasSpecial || draw.program->hasTransmission) return RHIPipelineHandle{};
                    const auto pipeline = draw.program->blendedColor[draw.doubleSided].GetHandle();
                    if (result.IsValid() && result != pipeline) return RHIPipelineHandle{};
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

            if (!blendedRanges.empty() && !volume) {
                const std::array<std::byte, SceneVolumeConstantsBytes> empty{};
                emptyMediumConstants = frame.device->UploadConstants(empty.data(), empty.size());
                emptyMediumTriangles = frame.device->AllocateUpload({48, RHIUploadUsage::Raw, 16});
                if (!emptyMediumConstants.IsValid() || !emptyMediumTriangles.IsWritable())
                    return Fail(error, "METEOR blended empty medium allocation failed.");
                std::memset(emptyMediumTriangles.cpuAddress, 0, 48);
            }
            for (auto& range : blendedRanges) {
                const bool transmissionStage = (frame.input->Draws()[range.inputs.front()].material->generation->cooked.product.program.features & 0x0800u) != 0;
                for (unsigned reference = 0; reference != 2; ++reference) {
                auto blendedCommands = std::make_shared<IndirectMaterialBatch>();
                blendedCommands->name = "METEOR.Scene.Commands.Blended." + std::to_string(range.inputs.front()) + (reference ? ".Reference" : ".Color");
                blendedCommands->device = frame.device;
                blendedCommands->pipeline = generation;
                const auto& list = frame.finalCullStage.blendedMeshlets;
                blendedCommands->visibility = list;
                std::vector<uint32_t> words;
                std::vector<std::array<uint32_t, 4>> records;
                uint32_t outputBytes{};
                const auto& order = range.inputs;
                RHIPipelineHandle pipeline{};

                for (const auto input : order) for (const auto& draw : frame.DrawsFor(input))
                {
                    if (!draw.program->hasSurface || draw.program->hasTransmission != transmissionStage ||
                        (runtimeEffects && draw.program->hasSpecial)) continue;
                    const auto current = draw.program->blendedColor[draw.doubleSided].GetHandle();
                    if (blendedCommands->buckets.empty()) {
                        IndirectMaterialBatch::Bucket bucket;
                        bucket.representative = &draw - frame.draws.data();
                        blendedCommands->buckets.push_back(std::move(bucket));
                        pipeline = current;
                    }
                    if (pipeline != current) throw std::runtime_error("METEOR blended range crossed a PSO boundary.");
                    auto& bucket = blendedCommands->buckets.front();
                    const uint32_t first = static_cast<uint32_t>(words.size() * 4);
                    std::vector<uint32_t> roots;
                    const auto append = [&](uint32_t slot, RHIBufferSlice slice, bool cbv = false) {
                        uint64_t address{};
                        if (!frame.device->EncodeIndirectBuffer(slice, address) || (cbv && address % 256))
                            throw std::runtime_error("METEOR blended indirect root preparation failed.");
                        roots.push_back(slot | (cbv ? 0x80000000u : 0));
                        words.push_back(uint32_t(address)); words.push_back(uint32_t(address >> 32));
                    };
                    if (!list || !frame.UsesMeshlets(draw))
                        throw std::runtime_error("METEOR blended range requires its ordered meshlet list.");
                    append(0, reference ? draw.referenceConstants : draw.constants, true);
                    append(1, RHIBufferSlice::Whole(draw.geometry->Buffer()));
                    append(draw.program->visibleOwnerRoot, draw.visibleOwner);
                    append(draw.program->meshletRoot, draw.geometry->Meshlets());
                    append(draw.program->temporalPreviousRoot + 1,
                        list->VisibleIds(list->MeshletOutputOffset(draw.cameraMeshletBin), draw.geometry->MeshletCount() + 1));
                    const auto& layout = draw.program->layout;
                    if (layout.uniformSlot) append(*layout.uniformSlot, draw.bindings->uniforms, true);
                    if (layout.descriptorIndexSlot) append(*layout.descriptorIndexSlot, draw.bindings->descriptorIndices, true);
                    if (bucket.roots.empty()) bucket.roots = roots;
                    if (bucket.roots != roots) throw std::runtime_error("METEOR blended indirect root ABI mismatch.");
                    // Mesh dispatch arguments come from this draw's ordered list bin.
                    records.push_back({first, outputBytes, uint32_t(roots.size() * 2), static_cast<uint32_t>(list->ArgsOffset(draw.cameraMeshletBin))});
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

    const SceneSurfaceTargets::Outputs& SceneHost::EffectStage::SurfaceInputs(const Frame& frame) const
{
    return decalInputs.depth.IsValid() ? decalInputs : frame.surfaceStage.gbuffer;
}

    SceneHost::EffectStage::RuntimeBindings SceneHost::EffectStage::RuntimeTables(const Frame& frame, const EnhancedRenderGraph::ExecuteContext& execution,
                                      const SceneSurfaceTargets::Outputs& inputs, RGHandle lighting,
                                      RGHandle ambientOcclusion, RGHandle shadowMap, bool blended,
                                      bool transmissionStage) const
    {
            const auto color = execution.ResolveHandle(lighting);
            const auto depth = execution.ResolveHandle(inputs.depth);
            const auto owners = execution.ResolveHandle(inputs.bitmask);
            const auto ao = blended ? RHITextureHandle{} : execution.ResolveHandle(ambientOcclusion);
            const auto shadowTexture = frame.shadow ? execution.ResolveHandle(shadowMap) : RHITextureHandle{};
            const std::array<std::uint64_t, 7> key{color.id, depth.id, owners.id, ao.id, shadowTexture.id,
                                                   transmissionStage || blended, blended && bool(volume)};
            std::lock_guard lock(runtimeBindingMutex);
            auto& result = runtimeBindings[key];
            if (!result.color.IsValid())
            {
                const auto depthView = RHIDepthTargetDesc::DepthReadOnly(depth, RHIFormat::D32Float);
                result.color = frame.device->CreateRenderTargets({&color, 1}, &depthView);
                const RHIBindingDesc descriptions[]{
                    RHIBindingDesc::SrvCube(frame.environment, frame.environment.IsValid() ? frame.device->DescribeTexture(frame.environment).format
                                                                            : RHIFormat::RGBA16Float, 1).OrNull(),
                    RHIBindingDesc::Srv2D(owners, RHIFormat::R32Uint),
                    RHIBindingDesc::Srv2D(ao, RHIFormat::RG16Float).OrNull(),
                    RHIBindingDesc::SrvArray(shadowTexture, RHIFormat::R32Float, 3).OrNull()};
                result.common = frame.device->CreateBindings(descriptions);
                result.decal = DecalTable(frame, execution, transmissionStage || blended);
                if (volume && blended)
                {
                    result.volume = frame.device->CreateBindings(volume->LightingBindings(shadowTexture));
                }
            }
            if (!result.color.IsValid() || !result.common.IsValid() || !result.decal.IsValid() ||
                (volume && blended && !result.volume.IsValid()))
            {
                throw std::runtime_error("Runtime special immutable stream bindings could not be allocated.");
            }
            return result;
        }

    void SceneHost::EffectStage::AddDecalUses(const Frame& frame, std::vector<EnhancedRenderGraph::RGPassUsage>& uses, bool transmissionStage) const
    {
            if (!decalBaseline[0].IsValid() || transmissionStage)
            {
                return;
            }
            for (const auto handle : {SurfaceInputs(frame).diffuse, SurfaceInputs(frame).metalRough, SurfaceInputs(frame).normal})
            {
                uses.push_back({handle, RHIResourceState::PixelShaderResource, RGAccessMode::Read});
            }
            for (const auto handle : decalBaseline)
            {
                uses.push_back({handle, RHIResourceState::PixelShaderResource, RGAccessMode::Read});
            }
        }

    RHIBindingTable SceneHost::EffectStage::DecalTable(const Frame& frame, const EnhancedRenderGraph::ExecuteContext& execution, bool transmissionStage) const
    {
            const bool enabled = decalBaseline[0].IsValid() && !transmissionStage;
            const std::array<RGHandle, 6> handles{SurfaceInputs(frame).diffuse,  SurfaceInputs(frame).metalRough, SurfaceInputs(frame).normal,
                                                  decalBaseline[0], decalBaseline[1],   decalBaseline[2]};
            std::array<RHIBindingDesc, 6> descriptions;
            for (unsigned i = 0; i < handles.size(); ++i)
            {
                descriptions[i] =
                    RHIBindingDesc::Srv2D(enabled ? execution.ResolveHandle(handles[i]) : RHITextureHandle{},
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
    void SceneHost::EffectStage::BindDecal(RHIEncoder& encoder, const DrawType& draw, RHIBindingTable table, bool transmissionStage) const
    {
            encoder.SetBindings(RHIBindPoint::Graphics, draw.program->decalRoot, table);
            encoder.SetConstantBuffer(RHIBindPoint::Graphics, draw.program->decalRoot + 1,
                                      decalConstants[decalBaseline[0].IsValid() && !transmissionStage]);
        }

    template<class DrawType>
    void SceneHost::EffectStage::BindForward(RHIEncoder& encoder, const DrawType& draw, const EnhancedForwardLighting& forward) const
    {
            const unsigned index = draw.program->forwardLightsRoot;
            encoder.SetRootBuffer(RHIBindPoint::Graphics, index,
                                  forward.lights.IsValid() ? forward.lights : draw.constants);
            encoder.SetRootBuffer(RHIBindPoint::Graphics, index + 1,
                                  forward.counts.IsValid() ? RHIBufferSlice::Whole(forward.counts) : draw.constants);
            encoder.SetRootBuffer(RHIBindPoint::Graphics, index + 2,
                                  forward.indices.IsValid() ? RHIBufferSlice::Whole(forward.indices) : draw.constants);
            const bool medium = draw.blended && bool(volume);
            encoder.SetConstantBuffer(RHIBindPoint::Graphics, index + 3,
                                      medium ? volume->TransportConstants() : emptyVolumeConstants);
            encoder.SetRootBuffer(RHIBindPoint::Graphics, index + 4, medium ? volume->Triangles() : emptyVolumeBuffer);
            encoder.SetRootBuffer(RHIBindPoint::Graphics, index + 5,
                                  medium ? RHIBufferSlice::Whole(volume->Coefficients()) : emptyVolumeBuffer);
            const auto table =
                medium && forward.volumeTable.IsValid() ? forward.volumeTable : emptyVolumeBindings;
            if (!table.IsValid())
            {
                throw std::runtime_error("Forward medium binding failed.");
            }
            encoder.SetBindings(RHIBindPoint::Graphics, index + 6, table);
            const auto& sceneLookup = draw.blended ? alphaLookup : lookup;
            sceneLookup->BindRuntime(encoder, index + 7);
            if (draw.program->hasSpecial && !runtimeEffects)
            {
                // Offline 분기는 tile을 읽지 않지만 공유 PSO의 root는 모두
                // 유효하게 둔다. 전체 화면 bake 레코드 계약은 그대로 유지한다.
                const unsigned effectIndex = index + 10;
                encoder.SetConstantBuffer(RHIBindPoint::Graphics, effectIndex, emptyVolumeConstants);
                encoder.SetBindings(RHIBindPoint::Graphics, effectIndex + 1, emptyRuntimeInputs);
                encoder.SetBindings(RHIBindPoint::Graphics, effectIndex + 2, emptyRuntimeBackground);
            }
        }
