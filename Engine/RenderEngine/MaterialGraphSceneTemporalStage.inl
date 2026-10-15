// Stage implementation included by MaterialGraphSceneHost.cpp inside material_graph.
std::array<RGHandle, 5> SceneHost::TemporalStage::DeclareTemporal(const own::shared_owner<const Frame>& prepared, EnhancedRenderGraph& graph, RGHandle geometry,
        std::array<RGHandle, 5> targets) const
    {
        const auto frame = prepared;
        if (!frame || !frame->temporalStage.temporalCommands) return targets;
        frame->CheckCurrent(&graph);
        RequireStage(geometry, frame->geometryStage.geometryTable->GraphTable(graph), "Temporal.ObjectMotion requires the METEOR.Scene.Geometry table.");
        // Replay commands read no culling output; their consumer owns the producer.
        frame->temporalStage.temporalCommands->Declare(graph, frame->temporalStage.temporalCommands);
        std::vector<EnhancedRenderGraph::RGPassUsage> uses;
        for (unsigned i = 0; i < targets.size(); ++i)
        {
            targets[i] = graph.Modify(targets[i]);
            uses.push_back({targets[i], i == 4 ? RHIResourceState::DepthWrite : RHIResourceState::RenderTarget, RGAccessMode::ReadWrite});
        }
        for (const auto& draw : frame->draws)
        {
            uses.push_back({draw.geometry->GraphOutput(graph), RHIResourceState::ShaderResource, RGAccessMode::Read});
            if (draw.previousGeometry != draw.geometry)
                uses.push_back({draw.previousGeometry->GraphOutput(graph), RHIResourceState::ShaderResource, RGAccessMode::Read});
            for (const auto& texture : draw.bindings->resources.textures)
                uses.push_back({graph.FindImportedTexture(texture.resource), RHIResourceState::PixelShaderResource, RGAccessMode::Read});
        }
        frame->temporalStage.temporalCommands->AddRead(graph, uses, frame->temporalStage.temporalCommands->GraphOutput(graph));
        NormalizeSceneReads(graph, uses);
        const auto pass = graph.AddPass("Temporal.ObjectMotion", uses, [frame, targets](const auto& execution) {
            frame->CheckCurrent(execution.graph);
            auto& encoder = *execution.encoder;
            std::array<RHITextureHandle, 4> colors;
            for (unsigned i = 0; i < colors.size(); ++i) colors[i] = execution.ResolveHandle(targets[i]);
            const auto depth = RHIDepthTargetDesc::Depth(execution.ResolveHandle(targets[4]), RHIFormat::D32Float);
            const auto bindings = frame->device->CreateRenderTargets(colors, &depth);
            if (!bindings.IsValid()) throw std::runtime_error("Temporal motion render targets unavailable.");
            encoder.BindRenderTargets(bindings);
            encoder.SetViewportAndScissor(frame->input->View().width, frame->input->View().height);
            encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
            const auto& replay = frame->temporalStage.temporalCommands;
            for (std::size_t bucket = 0; bucket < replay->buckets.size(); ++bucket) {
                const auto& draw = frame->draws[replay->buckets[bucket].representative];
                encoder.SetPipeline(RHIBindPoint::Graphics, draw.program->temporal[draw.doubleSided].GetHandle());
                std::string error;
                if (!RenderBindingCache::Bind(*frame->device, encoder, RHIBindPoint::Graphics, *draw.bindings, error))
                    throw std::runtime_error(error);
                replay->Draw(encoder, bucket);
                frame->temporalBucketSubmissions.fetch_add(1, std::memory_order_relaxed);
            }

        }, true);
        // Sealed frame inputs only; route observations are read after the whole graph joins.
        graph.DeclareRecordingIndependent(pass);
        return targets;
    }

