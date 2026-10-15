// Stage implementation included by MaterialGraphSceneHost.cpp inside material_graph.
    RGHandle SceneHost::ShadowStage::DeclareShadow(const own::shared_owner<const FrameInput>& prepared, EnhancedRenderGraph& graph, RGHandle geometry, RGHandle shadowMap) const
    {
        ce::profile_scope profile{ce::marker<"MaterialDeclareShadow">()};
        const auto frame = prepared;
        if (!frame || !frame->shadow)
        {
            return shadowMap;
        }
        frame->CheckCurrent();
        if (!frame->draws.empty()) frame->CheckCurrent(&graph);
        RequireStage(geometry, frame->raster->geometry->GraphTable(graph), "METEOR.Scene.Shadow requires the METEOR.Scene.Geometry table.");
        static constexpr const char* kCascadePassNames[]{"METEOR.Scene.Shadow.C0", "METEOR.Scene.Shadow.C1",
                                                         "METEOR.Scene.Shadow.C2"};
        if (graph.HasDeclaredWriter(shadowMap, kCascadePassNames[0]))
            throw std::runtime_error("METEOR shadow output already has this frame's cascade producer.");
        // Shadow lists depend only on posed geometry and the light projections,
        // so the shadow node owns its culling and command producers.
        for (unsigned cascade = 0; cascade < 3; ++cascade)
        {
            std::vector<std::shared_ptr<const GpuGeometryVisibility::Frame>> meshlets;
            for (const auto& draw : frame->shadowDraws)
            {
                if (draw.shadowMeshlets[cascade]) meshlets.push_back(draw.shadowMeshlets[cascade]);
            }
            GpuGeometryVisibility::Frame::DeclareMeshletBatch(graph, meshlets);
            if (this->shadowCommands[cascade]) this->shadowCommands[cascade]->Declare(graph, this->shadowCommands[cascade]);
        }
        // One pass per cascade: identical commands, but each cascade's GPU time
        // is attributed separately by the graph profiler.
        for (unsigned cascade = 0; cascade < 3; ++cascade)
        {
            shadowMap = graph.Modify(shadowMap);
            std::vector<EnhancedRenderGraph::RGPassUsage> uses{{shadowMap, RHIResourceState::DepthWrite, RGAccessMode::ReadWrite}};

            for (const auto& draw : frame->shadowDraws)
            {
                if (!draw.shadowEnabled)
                {
                    continue;
                }
                if (draw.shadowMeshlets[cascade]) draw.shadowMeshlets[cascade]->AddVisibleIdsRead(graph, uses);
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
            if (this->shadowCommands[cascade])
                this->shadowCommands[cascade]->AddRead(graph, uses, this->shadowCommands[cascade]->GraphOutput(graph));
            NormalizeSceneReads(graph, uses);
            graph.AddPass(kCascadePassNames[cascade], uses, [this, frame, shadowMap, cascade](const auto& execution) {
                frame->CheckCurrent(execution.graph);
                auto& encoder = *execution.encoder;
                const auto map = execution.ResolveHandle(shadowMap);
                const auto description = frame->device->DescribeTexture(map);
                if (description.depthOrArraySize != 3 || description.format != RHIFormat::D32Float)
                {
                    throw std::runtime_error("METEOR Scene shadow needs its three D32 cascades.");
                }
                encoder.SetViewportAndScissor(description.width, description.height);
                encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
                const auto depth = RHIDepthTargetDesc::DepthSlice(map, RHIFormat::D32Float, cascade);
                const auto targets = frame->device->CreateRenderTargets(std::span<const RHITextureHandle>{}, &depth);
                if (!targets.IsValid())
                {
                    throw std::runtime_error("METEOR Scene cascade target binding failed.");
                }
                encoder.BindRenderTargets(targets);
                const auto& batch = this->shadowCommands[cascade];
                if (!batch) return; // No admitted shadow casters in this sealed frame.
                for (std::size_t bucket = 0; bucket < batch->buckets.size(); ++bucket)
                {
                    const auto& range = batch->buckets[bucket];
                    const auto& draw = frame->shadowDraws[range.representative];
                    encoder.SetPipeline(RHIBindPoint::Graphics,
                        frame->RasterPipeline(draw, draw.program->shadow.GetHandle()));
                    std::string error;
                    if (!RenderBindingCache::BindPass(*frame->device, encoder, RHIBindPoint::Graphics,
                        *draw.bindings, draw.program->shadowLayout, error))
                        throw std::runtime_error(error);
                    batch->Draw(encoder, bucket);
                    frame->shadowBucketSubmissions[cascade].fetch_add(1, std::memory_order_relaxed);
                    // Public counters describe logical caster commands, not CPU submissions.
                    frame->shadowMeshletDrawCounts[cascade].fetch_add(range.count, std::memory_order_relaxed);
                    frame->shadowDrawCount.fetch_add(range.count, std::memory_order_relaxed);
                }
            });
        }
        return shadowMap;
    }

