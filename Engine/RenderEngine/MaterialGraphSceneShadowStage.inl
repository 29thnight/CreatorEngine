// Included inside material_graph by MaterialGraphSceneHost.cpp.
namespace
{
template<class Map>
void BoundShadowViews(Map& views, const typename Map::key_type& current)
{
    while (views.size() > 8)
    {
        auto victim = views.begin();
        if (victim->first == current)
        {
            ++victim;
        }
        // Submitted frames and history readers retain their immutable owners.
        views.erase(victim);
    }
}

void RequireShadowResources(const SceneShadowResources& actual, const SceneShadowResources& expected,
                            const char* message)
{
    for (unsigned cascade = 0; cascade < actual.size(); ++cascade)
    {
        RequireStage(actual[cascade], expected[cascade], message);
    }
}

RGHandle CreateShadowDepth(EnhancedRenderGraph& graph, unsigned extent, const std::string& name)
{
    RGTextureDesc desc;
    desc.width = desc.height = extent;
    desc.format = RHIFormat::D32Float;
    desc.allowDepthStencil = true;
    desc.name = name;
    return graph.CreateTexture(desc);
}
} // namespace

RGHandle SceneHost::ShadowRasterStage::Draw(const own::shared_owner<const FrameInput>& frame,
                                            EnhancedRenderGraph& graph,
                                            const std::shared_ptr<const IndirectMaterialBatch>& commands,
                                            RGHandle depth, unsigned cascade, unsigned slice, bool staticCasters,
                                            bool count, const std::string& name)
{
    if (!commands)
    {
        return depth;
    }
    commands->Declare(graph, commands);
    depth = graph.Modify(depth);
    std::vector<EnhancedRenderGraph::RGPassUsage> uses{{depth, RHIResourceState::DepthWrite, RGAccessMode::ReadWrite}};
    commands->visibility->AddVisibleIdsRead(graph, uses);
    commands->AddRead(graph, uses, commands->GraphOutput(graph));
    frame->sceneData->AddRead(graph, uses);
    uses.push_back({frame->raster->geometry->GraphTable(graph), RHIResourceState::ShaderResource, RGAccessMode::Read});
    for (const auto& draw : frame->shadowDraws)
    {
        if (!draw.shadowEnabled || draw.staticShadow != staticCasters)
        {
            continue;
        }
        uses.push_back({draw.geometry->GraphOutput(graph), RHIResourceState::ShaderResource, RGAccessMode::Read});
        uses.push_back({draw.geometry->GraphMeshlets(graph), RHIResourceState::ShaderResource, RGAccessMode::Read});
        for (const auto& texture : draw.bindings->resources.textures)
        {
            uses.push_back({graph.FindImportedTexture(texture.resource), RHIResourceState::PixelShaderResource,
                            RGAccessMode::Read});
        }
    }
    NormalizeSceneReads(graph, uses);
    const auto statisticsSlot = cascade * 2 + (staticCasters ? 0 : 1);
    const auto bucketCount = static_cast<uint32_t>(commands->buckets.size());
    const auto pass = graph.AddSplitPass(name, std::move(uses),
        [frame, commands, depth, cascade, slice, count, statisticsSlot](const auto& execution,
                                                            uint32_t recordSlice, uint32_t sliceCount) {
        frame->CheckCurrent(execution.graph);
        auto& encoder = *execution.encoder;
        const auto texture = execution.ResolveHandle(depth);
        const auto desc = frame->device->DescribeTexture(texture);
        if (desc.width != frame->shadowExtent || desc.height != frame->shadowExtent || desc.depthOrArraySize <= slice ||
            desc.format != RHIFormat::D32Float)
        {
            throw std::runtime_error("Shadow raster target differs from its sealed light extent/slice.");
        }
        const auto depthTarget = desc.depthOrArraySize == 1
                                     ? RHIDepthTargetDesc::Depth(texture, RHIFormat::D32Float)
                                     : RHIDepthTargetDesc::DepthSlice(texture, RHIFormat::D32Float, slice);
        const auto targets = frame->device->CreateRenderTargets(std::span<const RHITextureHandle>{}, &depthTarget);
        if (!targets.IsValid())
        {
            throw std::runtime_error("Shadow raster depth binding failed.");
        }
        encoder.BindRenderTargets(targets);
        encoder.SetViewportAndScissor(desc.width, desc.height);
        encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
        uint32_t recordedBuckets{};
        uint32_t recordedCommands{};
        const auto first = commands->buckets.size() * recordSlice / sliceCount;
        const auto last = commands->buckets.size() * (recordSlice + 1) / sliceCount;
        for (auto bucket = first; bucket < last; ++bucket)
        {
            const auto& range = commands->buckets[bucket];
            const auto& draw = frame->shadowDraws[range.representative];
            encoder.SetPipeline(RHIBindPoint::Graphics, frame->RasterPipeline(draw, draw.program->shadow.GetHandle()));
            std::string error;
            if (!RenderBindingCache::BindPass(*frame->device, encoder, RHIBindPoint::Graphics, *draw.bindings,
                                              draw.program->shadowLayout, error))
            {
                throw std::runtime_error(error);
            }
            frame->BindPacketResources(encoder, draw, frame->packetBindings.at(commands.get()),
                                         draw.program->shadowPacketRoots, cascade);
            commands->Draw(encoder, bucket, draw.program->shadowPacketRoots.selector);
            if (count)
            {
                ++recordedBuckets;
                recordedCommands += range.count;
            }
        }
        if (count)
        {
            auto& statistics = frame->shadowRecordingStats[statisticsSlot][recordSlice];
            statistics.buckets += recordedBuckets;
            statistics.commands += recordedCommands;
        }
    }, FrameInput::RasterRecordingSlices(bucketCount), false, bucketCount);
    // 명령/기하 입력은 밀봉되어 있다. 통계는 cascade/static/slice 전용 슬롯에
    // 기록하며 occluder는 통계를 쓰지 않는다. 합산은 전체 recording join 뒤다.
    graph.DeclareRecordingIndependent(pass);
    return depth;
}

SceneShadowResources SceneHost::ShadowCacheStage::Declare(const own::shared_owner<const FrameInput>& frame,
                                                          EnhancedRenderGraph& graph, RGHandle geometry) const
{
    if (!frame || !frame->shadow || !cache)
    {
        return {};
    }
    frame->CheckCurrent(&graph);
    RequireStage(geometry, frame->raster->geometry->GraphTable(graph), "Shadow.StaticCache requires Geometry.");
    if (outputs[0].IsValid())
    {
        throw std::runtime_error("Shadow static cache was already declared in this recording.");
    }
    if (!hit && meshlets[0])
    {
        GpuGeometryVisibility::Frame::DeclareMeshletBatch(graph, meshlets, "METEOR.Shared.StaticMiss");
    }
    for (unsigned cascade = 0; cascade < 3; ++cascade)
    {
        const auto name = "METEOR.Shadow.Static.C" + std::to_string(cascade);
        auto depth = graph.ImportTexture(cache->depths[cascade],
                                         hit ? RHIResourceState::CopySource : RHIResourceState::Common, name);
        graph.RequireImportedFinalState(depth, RHIResourceState::CopySource);
        if (!hit)
        {
            depth = graph.Write(depth);
            graph.AddPass(name + ".Clear", {{depth, RHIResourceState::DepthWrite, RGAccessMode::Write}},
                          [frame, depth](const auto& execution) {
                              const auto target =
                                  RHIDepthTargetDesc::Depth(execution.ResolveHandle(depth), RHIFormat::D32Float);
                              const auto binding =
                                  frame->device->CreateRenderTargets(std::span<const RHITextureHandle>{}, &target);
                              if (!binding.IsValid())
                              {
                                  throw std::runtime_error("Shadow static cache clear binding failed.");
                              }
                              execution.encoder->BindRenderTargets(binding);
                              execution.encoder->ClearDepthTarget(binding, 1.f);
                          });
            if (meshlets[cascade])
            {
                const std::shared_ptr<const GpuGeometryVisibility::Frame> lists[]{meshlets[cascade]};
                GpuGeometryVisibility::Frame::DeclareMeshletBatch(graph, lists, (name + ".Cull").c_str());
            }
            depth = ShadowRasterStage::Draw(frame, graph, commands[cascade], depth, cascade, 0, true, true,
                                            name + ".Raster");
            graph.AddPass(
                name + ".Ready", {{depth, RHIResourceState::CopySource, RGAccessMode::Read}},
                [owner = cache, cascade](const auto&) {
                    owner->recordedMask.fetch_or(1u << cascade, std::memory_order_release);
                },
                true);
        }
        outputs[cascade] = depth;
    }
    return outputs;
}

SceneShadowResources SceneHost::ShadowFirstCullStage::Declare(const own::shared_owner<const FrameInput>& frame,
                                                              EnhancedRenderGraph& graph, RGHandle geometry) const
{
    if (!frame || !frame->shadow || !meshlets[0])
    {
        return {};
    }
    frame->CheckCurrent(&graph);
    RequireStage(geometry, frame->raster->geometry->GraphTable(graph), "Shadow.Cull.First requires Geometry.");
    for (unsigned cascade = 0; cascade < 3; ++cascade)
    {
        const auto prefix = "METEOR.Shadow.Cull.First.C" + std::to_string(cascade);
        meshlets[cascade]->DeclareFirstStage(graph, prefix.c_str());
        outputs[cascade] = meshlets[cascade]->GraphVisibleIds(graph);
    }
    return outputs;
}

SceneShadowResources SceneHost::ShadowOccluderStage::Declare(const own::shared_owner<const FrameInput>& frame,
                                                             EnhancedRenderGraph& graph,
                                                             const SceneShadowResources& cache,
                                                             const SceneShadowResources& first) const
{
    if (!frame || !frame->shadow || !cache[0].IsValid())
    {
        return {};
    }
    frame->CheckCurrent(&graph);
    if (outputs[0].IsValid())
    {
        throw std::runtime_error("Shadow occluder was already declared.");
    }
    for (unsigned cascade = 0; cascade < 3; ++cascade)
    {
        if (commands[cascade])
        {
            RequireStage(first[cascade], commands[cascade]->visibility->GraphVisibleIds(graph),
                         "Shadow.Occluder requires its first-list version.");
        }
        const auto name = "METEOR.Shadow.Occluder.C" + std::to_string(cascade);
        const auto depth = graph.Write(CreateShadowDepth(graph, frame->shadowExtent, name));
        graph.AddPass(name + ".StaticCopy",
                      {{cache[cascade], RHIResourceState::CopySource, RGAccessMode::Read},
                       {depth, RHIResourceState::CopyDest, RGAccessMode::Write}},
                      [source = cache[cascade], depth](const auto& execution) {
                          execution.encoder->CopyTexture(execution.ResolveHandle(depth),
                                                         execution.ResolveHandle(source));
                      });
        outputs[cascade] =
            ShadowRasterStage::Draw(frame, graph, commands[cascade], depth, cascade, 0, false, false, name + ".Raster");
    }
    return outputs;
}

SceneShadowResources SceneHost::ShadowFinalCullStage::Declare(const own::shared_owner<const FrameInput>& frame,
                                                              EnhancedRenderGraph& graph,
                                                              const SceneShadowResources& depths) const
{
    if (!frame || !frame->shadow || !meshlets[0])
    {
        return {};
    }
    frame->CheckCurrent(&graph);
    {
        ce::profile_scope profile{ce::marker<"ShadowFinalCull.HzbDeclare">()};
        for (unsigned cascade = 0; cascade < 3; ++cascade)
        {
            pyramids[cascade]->Declare(graph, depths[cascade]);
        }
    }
    {
        ce::profile_scope profile{ce::marker<"ShadowFinalCull.SharedDeclare">()};
        GpuGeometryVisibility::Frame::DeclareOcclusionBatch(graph, meshlets, pyramids, "METEOR.Shared.ShadowRecovery");
    }
    for (unsigned cascade = 0; cascade < 3; ++cascade)
    {
        outputs[cascade] = meshlets[cascade]->GraphVisibleIds(graph);
    }
    return outputs;
}

RGHandle SceneHost::ShadowRasterStage::Declare(const own::shared_owner<const FrameInput>& frame,
                                               EnhancedRenderGraph& graph, const SceneShadowResources& cache,
                                               const SceneShadowResources& lists, RGHandle shadowMap) const
{
    if (!frame || !frame->shadow)
    {
        return shadowMap;
    }
    frame->CheckCurrent(&graph);
    if (output.IsValid())
    {
        throw std::runtime_error("Shadow final raster was already declared.");
    }
    if (!cache[0].IsValid())
    {
        output = shadowMap;
        return shadowMap;
    }
    for (unsigned cascade = 0; cascade < 3; ++cascade)
    {
        if (shadowCommands[cascade])
        {
            RequireStage(lists[cascade], shadowCommands[cascade]->visibility->GraphVisibleIds(graph),
                         "Shadow.Raster requires its final-list version.");
        }
        const auto name = "METEOR.Scene.Shadow.C" + std::to_string(cascade);
        shadowMap = graph.Modify(shadowMap);
        graph.AddPass(name + ".StaticMerge",
                      {{cache[cascade], RHIResourceState::PixelShaderResource, RGAccessMode::Read},
                       {shadowMap, RHIResourceState::DepthWrite, RGAccessMode::ReadWrite}},
                      [frame, pipeline = merge, source = cache[cascade], shadowMap, cascade](const auto& execution) {
                          const auto texture = execution.ResolveHandle(shadowMap);
                          const auto desc = frame->device->DescribeTexture(texture);
                          if (desc.width != frame->shadowExtent || desc.height != frame->shadowExtent ||
                              desc.depthOrArraySize != 3 || desc.format != RHIFormat::D32Float)
                          {
                              throw std::runtime_error("Shadow merge requires the sealed extent and three D32 slices.");
                          }
                          const auto depth = RHIDepthTargetDesc::DepthSlice(texture, RHIFormat::D32Float, cascade);
                          const auto targets =
                              frame->device->CreateRenderTargets(std::span<const RHITextureHandle>{}, &depth);
                          const RHIBindingDesc input[]{RHIBindingDesc::SrvDepth(execution.ResolveHandle(source))};
                          const auto bindings = frame->device->CreateBindings(input);
                          if (!pipeline || !targets.IsValid() || !bindings.IsValid())
                          {
                              throw std::runtime_error("Shadow cache merge pipeline or bindings are unavailable.");
                          }
                          auto& encoder = *execution.encoder;
                          encoder.BindRenderTargets(targets);
                          encoder.SetViewportAndScissor(desc.width, desc.height);
                          encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
                          encoder.SetPipeline(RHIBindPoint::Graphics, pipeline->pipeline.GetHandle());
                          encoder.SetBindings(RHIBindPoint::Graphics, 0, bindings);
                          encoder.Draw(3, 1);
                      });
        shadowMap = Draw(frame, graph, shadowCommands[cascade], shadowMap, cascade, cascade, false, true, name);
    }
    output = shadowMap;
    return output;
}

void SceneHost::ShadowHistoryStage::Declare(const own::shared_owner<const FrameInput>& frame,
                                            EnhancedRenderGraph& graph, RGHandle expected, RGHandle shadowMap) const
{
    if (!frame || !frame->shadow || !next[0])
    {
        return;
    }
    frame->CheckCurrent(&graph);
    RequireStage(shadowMap, expected, "Shadow.History requires the final raster version.");
    if (declaredInput.IsValid())
    {
        throw std::runtime_error("Shadow history was already declared.");
    }
    declaredInput = shadowMap;
    for (unsigned cascade = 0; cascade < 3; ++cascade)
    {
        const auto name = "METEOR.Shadow.History.C" + std::to_string(cascade);
        const auto depth = graph.Write(CreateShadowDepth(graph, frame->shadowExtent, name + ".Depth"));
        graph.AddPass(name + ".Copy",
                      {{shadowMap, RHIResourceState::CopySource, RGAccessMode::Read},
                       {depth, RHIResourceState::CopyDest, RGAccessMode::Write}},
                      [shadowMap, depth, cascade](const auto& execution) {
                          execution.encoder->CopyTexture(execution.ResolveHandle(depth),
                                                         execution.ResolveHandle(shadowMap), 0, cascade);
                      });
        next[cascade]->Declare(graph, depth);
    }
}
