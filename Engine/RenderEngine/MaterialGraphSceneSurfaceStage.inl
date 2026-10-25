// Stage implementations included by MaterialGraphSceneHost.cpp inside material_graph.
SceneSurfaceTargets::Outputs SceneHost::VisibilityStage::Declare(const own::shared_owner<const Frame>& prepared,
                                                                 EnhancedRenderGraph& graph,
                                                                 const SceneFinalCullOutputs& cull,
                                                                 const SceneSurfaceTargets::Outputs& incoming) const
{
    ce::profile_scope profile{ce::marker<"MaterialSceneVisibilityDeclare">()};
    const auto& frame = prepared;
    if (!frame || frame->draws.empty())
    {
        RequireStage(cull.list, {}, "METEOR.Scene.Visibility received a list for an empty frame.");
        targets = incoming;
        return incoming;
    }
    frame->CheckCurrent(&graph);
    RequireStage(cull.list, frame->raster->opaque ? frame->raster->opaque->GraphVisibleIds(graph) : RGHandle{},
                 "METEOR.Scene.Visibility requires the METEOR.Scene.Cull.Final list.");
    if (materialCommands)
    {
        materialCommands->Declare(graph, materialCommands);
    }
    targets = incoming;
    if (hybridVisibility)
    {
        if (!incoming.visibility.IsValid() || !incoming.depth.IsValid() || !materialCommands)
        {
            throw std::runtime_error("METEOR visibility requires depth, visibility and prepared commands.");
        }
        hybridVisibility->Declare(graph, hybridVisibility);
        DeclareHardwareVisibility(graph, frame, hybridVisibility, frame->raster->opaque, materialCommands, false,
                                  materialCommands->GraphOutput(graph));
        hybridVisibility->DeclareSoftware(graph, hybridVisibility);
        targets = hybridVisibility->DeclareResolve(graph, incoming, hybridVisibility);
    }
    return targets;
}

std::array<RGHandle, 2> SceneHost::MaterialWorkStage::Declare(const own::shared_owner<const Frame>& prepared,
                                                              EnhancedRenderGraph& graph, RGHandle expectedVisibility,
                                                              RGHandle visibility) const
{
    ce::profile_scope profile{ce::marker<"MaterialSceneWorkDeclare">()};
    if (!prepared || prepared->draws.empty())
    {
        return {};
    }
    prepared->CheckCurrent(&graph);
    RequireStage(visibility, expectedVisibility, "MaterialWork requires the published Visibility output.");
    if (!visibility.IsValid())
    {
        throw std::runtime_error("MaterialWork requires an explicit Visibility producer.");
    }
    if (!materialWork)
    {
        return {};
    }
    materialWork->Declare(graph, visibility, materialWork);
    if (prepared->m_primitiveCache)
    {
        prepared->m_primitiveCache->Declare(graph, visibility, prepared);
    }
    return materialWork->GraphInputs(graph);
}

SceneSurfaceTargets::Outputs SceneHost::SurfaceResolveStage::Declare(const own::shared_owner<const Frame>& prepared,
                                                                     EnhancedRenderGraph& graph,
                                                                     RGHandle expectedVisibility,
                                                                     const SceneSurfaceTargets::Outputs& incoming,
                                                                     const std::array<RGHandle, 2>& material) const
{
    ce::profile_scope profile{ce::marker<"MaterialSceneResolveDeclare">()};
    const auto& frame = prepared;
    gbuffer = incoming;
    if (!frame || frame->draws.empty())
    {
        return incoming;
    }
    frame->CheckCurrent(&graph);
    RequireStage(incoming.visibility, expectedVisibility, "SurfaceResolve requires the published Visibility output.");
    if (!incoming.visibility.IsValid() || !incoming.depth.IsValid())
    {
        throw std::runtime_error("SurfaceResolve requires an explicit Visibility producer.");
    }
    if (!frame->raster->material)
    {
        if (material[0].IsValid() || material[1].IsValid())
        {
            throw std::runtime_error("SurfaceResolve received unexpected material work.");
        }
        return incoming;
    }
    frame->raster->material->ValidateInputs(graph, material);
    auto inputs = incoming;
    for (const auto target : Colors(inputs))
    {
        if (!target.IsValid())
        {
            throw std::runtime_error("SurfaceResolve requires all five initialized surface targets.");
        }
    }
    auto restoreUses = graph.CreateDeclarationUsages();
    for (auto* target : {&inputs.diffuse, &inputs.metalRough, &inputs.normal, &inputs.emissive, &inputs.bitmask})
    {
        *target = graph.Modify(*target);
        restoreUses.push_back({*target, RHIResourceState::RenderTarget, RGAccessMode::ReadWrite});
    }
    frame->AddReconstructionReads(graph, restoreUses, inputs.visibility, material, true);
    frame->AddPassReadPlan(graph, restoreUses, PassReadPlan::Pass::Surface, RHIResourceState::PixelShaderResource);
    NormalizeSceneReads(graph, restoreUses);
    graph.AddPass(
        "METEOR.Scene.Material.Resolve", restoreUses,
        [this, frame = frame, inputs](const auto& execution)
        {
            frame->CheckCurrent(execution.graph);
            auto& encoder = *execution.encoder;
            std::array<RHITextureHandle, 5> colors;
            const auto handles = Colors(inputs);
            for (unsigned i = 0; i < colors.size(); ++i)
            {
                colors[i] = execution.ResolveHandle(handles[i]);
            }
            const auto targets = frame->device->CreateRenderTargets(colors, nullptr);
            if (!targets.IsValid())
            {
                throw std::runtime_error("METEOR reconstructed surface target binding failed.");
            }
            encoder.BindRenderTargets(targets);
            encoder.SetViewportAndScissor(frame->input->View().width, frame->input->View().height);
            // 같은 callback의 visibility SRV만 공유한다. 적격 draw가 없으면 할당하지 않고
            // descriptor를 다음 recording이나 다른 callback의 수명으로 넘기지 않는다.
            RHIBindingTable visibility{};
            for (const auto& draw : frame->RasterDrawsFor())
            {
                if (!draw.program->hasSurface || draw.program->hasTransmission || draw.blended)
                {
                    continue;
                }
                if (!visibility.IsValid())
                {
                    visibility = frame->CreateReconstructionVisibility(execution, inputs.visibility);
                }
                frame->BindReconstruction(encoder, draw, visibility,
                                          draw.program->gbuffer[draw.doubleSided].GetHandle());
                std::string error;
                if (!RenderBindingCache::Bind(*frame->device, encoder, RHIBindPoint::Graphics, *draw.bindings, error))
                {
                    throw std::runtime_error(error);
                }
                frame->DrawReconstruction(encoder, draw);
            }
        });
    this->gbuffer = inputs;
    return inputs;
}

bool SceneHost::VisibilityStage::PrepareVisibility(const FrameInput& frame, const EnhancedFrameContext& context,
                                                   const own::shared_owner<const SceneFrameGeometry>& geometry,
                                                   HybridVisibilityPipelines& pipelines, const char* name,
                                                   bool depthOnly,
                                                   const own::shared_owner<const GpuGeometryVisibility::Frame>& list,
                                                   own::shared_owner<const SceneHybridVisibility>& result,
                                                   std::string& error,
                                                   const SceneHybridVisibility* sharedPreparation)
{
    auto hybrid = own::make_shared<SceneHybridVisibility>();
    hybrid->name = name;
    hybrid->depthOnly = depthOnly;
    hybrid->geometry = geometry;
    hybrid->pool = geometry->pool;
    hybrid->visible = list;
    hybrid->records.resize(geometry->records.size());
    for (const auto& draw : frame.draws)
    {
        if (!draw.program->hasSurface || draw.program->hasTransmission || draw.blended)
        {
            continue;
        }
        const auto coverage = frame.input->Draws()[draw.inputIndex].coverage.flags;
        hybrid->records[draw.geometryRecord] = {list->MeshletOutputOffset(draw.cameraMeshletBin),
                                                1u | ((coverage & 2u) ? 0u : 2u) | (draw.doubleSided ? 4u : 0u), 0u,
                                                0u};
    }
    if (!hybrid->Prepare(context, frame.input->ViewProjection(), pipelines, error, sharedPreparation))
    {
        return false;
    }
    result = std::move(hybrid);
    return true;
}

bool SceneHost::VisibilityStage::Prepare(const FrameInput& frame, const EnhancedFrameContext& context,
                                         const own::shared_owner<const SceneFrameGeometry>& geometry,
                                         const own::shared_owner<const GpuGeometryVisibility::Frame>& list,
                                         HybridVisibilityPipelines& pipelines, std::string& error,
                                         const SceneHybridVisibility* sharedPreparation)
{
    if (!std::ranges::any_of(frame.draws, [](const auto& draw) {
            return draw.program->hasSurface && !draw.program->hasTransmission && !draw.blended;
        }))
    {
        return true;
    }
    return PrepareVisibility(frame, context, geometry, pipelines, "METEOR.Scene.Visibility", false, list,
                             hybridVisibility, error, sharedPreparation);
}
