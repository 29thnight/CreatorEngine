// Pixel-level alpha composition across the real Code and cooked Graph consumers.
void RunForwardBlend(const std::filesystem::path& root, RecordingChangeDevice& device, ProbeRoots& roots,
                     ProbePipelines& pipelines, ProbeTextures& textures, ProbePool& pool,
                     std::array<std::shared_ptr<const Instance>, 2> instances, std::shared_ptr<Texture> cube)
{
    std::string error;
    for (unsigned tier = 0; tier < 2; ++tier)
    {
        VerifiedProduct cooked;
        const bool compiled = CompileSceneProduct(instances[tier]->generation->cooked.product.program,
                  root / "Dynamic_CPP/Assets/Shaders/DefaultPassShader",
                  root / "Build/Obj/MaterialProductProbe" / (tier ? "alpha-layered.slang" : "alpha-core.slang"),
                  {}, cooked, error, instances[tier]->description.graphId.value);
        Check(compiled, "Compile complete alpha product " + error);
        auto generation = std::make_shared<Generation>();
        generation->assetId = instances[tier]->description.graphId;
        generation->generation = tier + 1;
        generation->cooked = {std::move(cooked), {}, {}};
        Check(BuildInstance(generation, instances[tier]->description,
                  [&](const experiment::AssetId&, LXColorSpace, std::string&) { return instances[tier]->textures[0].owner; },
                  instances[tier], error), "Cooked alpha instance " + error);
    }
    ProbeMeshes meshes;
    Check(meshes.Initialize(&device, error), "Forward mesh cache");
    EnhancedGBufferPass gbuffer;
    EnhancedForwardPass forward;
    SceneHost host(ce::get_job_scheduler());
    EnhancedFrameContext context;
    context.resources = &device;
    context.rootSignatures = &roots;
    context.psoManager = &pipelines;
    context.meshCache = &meshes;
    context.textureCache = &textures;
    context.width = context.height = 16;
    context.sceneEpoch = 500;
    FrameCameraSnapshot camera;
    camera.view = camera.projection = camera.inverseProjection = math::matrix4x4::identity();
    camera.eyePosition = math::vector3(0, 0, 2);
    camera.forward = math::vector3(0, 0, -1);
    context.camera = &camera;
    std::vector<EnhancedLight> lights;
    context.lights = &lights;
    const bool initialized = gbuffer.Initialize(context, error) && forward.Initialize(context, error);
    Check(initialized, "Forward native initialization " + error);
    ShaderMeta codeMeta;
    const ShaderMetaHandle codeHandle{889, 1};
    Check(ShaderMetaLoader::LoadFile(root / "Dynamic_CPP/Assets/Shaders/DefaultPassShader/Forward.shadermeta",
              FileGuid{"88888888-8888-4888-8888-888888888888"}, codeMeta, error) &&
              forward.ApplyShaderMeta(context, codeHandle, codeMeta, {}, error), "Code Forward LX contract " + error);
    // Scene consumers must load the sealed bytecode, with no editor compilation.
    InternalPath::GetInstance()->AssetAuthoringEnabled = false;
    for (const auto& instance : instances) WaitSceneProgram(host, context, instance->generation);
    Check(host.ProgramStats().compileSubmissions == 0, "Graph Forward is source-free at consumption");
    const auto light = [] {
        EnhancedLight value;
        value.position = math::vector4(0, 0, 0, 0);
        value.direction = math::vector4(0, 0, -1, 0);
        value.color = math::color(.015f, .012f, .01f, 1);
        return value;
    }();
    auto geometry = MakeGeometry(false, true);
    geometry.draw.modelMeshView.indexData = geometry.indices.data();
    geometry.draw.modelMeshView.indexCount = 3;
    const std::array<std::array<float, 3>, 3> triangle{{{-.75f, -.75f, 0}, {.75f, -.75f, 0}, {0, .75f, 0}}};
    for (unsigned i = 0; i < 3; ++i)
    {
        const auto offset = i * geometry.draw.modelMeshView.vertexStride;
        std::memcpy(geometry.vertices.data() + offset, triangle[i].data(), 12);
        const std::array<float, 2> uv{.1f, .2f};
        std::memcpy(geometry.vertices.data() + offset + assets::OffsetOf(geometry.draw.modelMeshView.vertexAttributeMask,
                      assets::VertexAttribute::Uv0), uv.data(), 8);
    }
    std::array<std::vector<float>, 4> solo;
    std::vector<float> mixedReference;
    const std::array<float, 4> depths{.2f, .35f, .45f, .6f};
    const std::array<float, 4> alpha{.25f, .4f, .6f, .5f};
    const std::array<float, 4> background{.04f, .06f, .09f, 1};
    unsigned pixels{}, frames{};
    // Four isolated colors, mixed sequential and parallel, reverse collection,
    // reverse camera sorting, opaque occlusion, and >64-light overflow/reference.
    for (unsigned fixture = 0; fixture < 12; ++fixture)
    {
        std::cerr << "LX_FORWARD_FIXTURE " << fixture << "\n";
        Drain drain{device};
        context.frameId = 12000 + fixture;
        lights.assign(fixture >= 10 ? 65 : 1, light);
        camera.forward = math::vector3(0, 0, fixture == 7 ? 1.f : -1.f);
        std::vector<EnhancedDrawItem> code, graphDraws;
        for (unsigned i = 0; i < 4; ++i)
        {
            if (fixture < 4 && fixture != i) continue;
            auto draw = geometry.draw;
            draw.geometryKey = 8500 + i;
            draw.worldMatrix[3, 2] = depths[i];
            const float a = fixture < 4 ? 1 : fixture == 9 ? 0 : alpha[i];
            draw.coverage.flags = EnhancedMaterialCoverage::Enabled | EnhancedMaterialCoverage::Blended;
            draw.baseColorFactor = i == 0 ? math::color(.9f, .1f, .2f, a) : math::color(.1f, .2f, .9f, a);
            if (i == 1 || i == 2)
            {
                auto description = instances[i - 1]->description;
                description.parameters.push_back({902, double(a)});
                Check(BuildInstance(instances[i - 1]->generation, description,
                          [&](const experiment::AssetId&, LXColorSpace, std::string&) {
                              return instances[i - 1]->textures[0].owner;
                          }, draw.materialGraphInstance, error), "Alpha override " + error);
                draw.materialGraphSlot = 700 + i;
                graphDraws.push_back(std::move(draw));
            }
            else
            {
                auto snapshot = std::make_shared<EnhancedForwardMaterialDrawSnapshot>();
                snapshot->shaderMetaHandle = codeHandle;
                snapshot->keywordSelections.assign(codeMeta.keywords.size(), 0);
                std::shared_ptr<const ShaderMetaBindingLayout> layout;
                Check(forward.EnsureShaderMetaVariant(context, codeHandle, codeMeta, snapshot->keywordSelections,
                          snapshot->permutationKey, layout, error), "Code Forward variant " + error);
                snapshot->bindingLayout = *layout;
                ExperimentMaterialSealing::SealSource source;
                source.material.blendMode = experiment::MaterialBlendMode::Transparent;
                source.material.properties = {{"baseColor", draw.baseColorFactor.rgba()}};
                Check(ExperimentMaterialSealing::SealCore(source, codeMeta, *layout, snapshot->propertyBytes,
                          snapshot->textureBindings, error, &snapshot->runtimeInstance, codeHandle) &&
                          ExperimentMaterialSealing::SealCoverage(source, *layout, snapshot->propertyBytes,
                          snapshot->coverage, error) && forward.CaptureShaderVariant(*snapshot) && snapshot->IsValid(),
                      "Code immutable LX draw sealing " + error);
                EnhancedMaterialSeal::Stamp(*snapshot, i + 1, 1, 1, context.sceneEpoch, context.frameId);
                draw.forwardMaterialSnapshot = std::move(snapshot);
                code.push_back(std::move(draw));
            }
        }
        if (fixture == 6) { std::reverse(code.begin(), code.end()); std::reverse(graphDraws.begin(), graphDraws.end()); }
        context.forwardDraws = &code;
        const std::vector<EnhancedDrawItem> empty;
        context.draws = &empty;
        SceneInputView view{context.frameId, context.sceneEpoch, 99, 1, 16, 16, camera};
        std::shared_ptr<const SceneViewInput> input;
        Check(SceneViewInput::Seal(view, graphDraws, {}, input, error) &&
                  host.SelectReadyInput(context, input, input, error), "Mixed sealed input " + error);
        Check(input->Draws().size() == graphDraws.size(), "No ready alpha graph omitted");
        Check(device.BeginFrame(error), "Mixed begin " + error);
        textures.BeginFrame(context.frameId);
        meshes.BeginFrame(static_cast<std::uint32_t>(context.frameId));
        const auto environment = textures.GetOrUpload(cube.get(), error);
        Check(environment.IsValid(), "Alpha environment upload");
        Check(gbuffer.PrepareFrame(context, error) && forward.PrepareFrame(context, error) &&
                  host.PrepareResidency(context, input, error), "Mixed preparation " + error);
        auto graph = std::make_shared<EnhancedRenderGraph>(device);
        const unsigned workers = fixture == 5 || fixture == 6 || fixture == 11 ? 4 : 0;
        if (workers)
        {
            pool.BeginFrame(static_cast<std::uint32_t>(context.frameId));
            Check(graph->PrepareParallel(pool, error), "Mixed parallel prefix " + error);
        }
        Check(host.Prepare(context, input, environment.handle, {}, {}, {}, {}, error, 1), "Alpha host " + error);
        gbuffer.Declare(*graph, context);
        auto outputs = gbuffer.GetOutputs();
        RHITextureHandle opaqueDepth;
        if (fixture == 8)
        {
            RHITextureDesc depthDesc;
            depthDesc.width = depthDesc.height = 16;
            depthDesc.format = RHIFormat::D32Float;
            depthDesc.allowDepthStencil = true;
            depthDesc.clearDepth = .1f;
            Check(device.CreateTexture(depthDesc, opaqueDepth, error), "Opaque test depth");
            outputs.depth = graph->ImportTexture(opaqueDepth, RHIResourceState::Common, "Probe.OpaqueDepth");
        }
        if (fixture == 8)
            graph->AddPass("Probe.OpaqueDepth", {{outputs.depth, RHIResourceState::DepthWrite}},
                [depth = outputs.depth, &device](const auto& execution) {
                    const auto d = RHIDepthTargetDesc::Depth(execution.ResolveHandle(depth), RHIFormat::D32Float);
                    const auto target = device.CreateRenderTargets(std::span<const RHITextureHandle>{}, &d);
                    execution.encoder->BindRenderTargets(target);
                    execution.encoder->ClearDepthTarget(target, .1f);
                });
        host.DeclareGBuffer(*graph, outputs);
        RGTextureDesc colorDesc;
        colorDesc.width = colorDesc.height = 16;
        colorDesc.format = RHIFormat::RGBA16Float;
        colorDesc.allowRenderTarget = true;
        std::copy(background.begin(), background.end(), colorDesc.clearColor);
        const auto color = graph->CreateTexture(colorDesc);
        RGTextureDesc aoDesc = colorDesc;
        aoDesc.format = RHIFormat::RG16Float;
        std::fill(std::begin(aoDesc.clearColor), std::end(aoDesc.clearColor), 1.f);
        const auto ao = graph->CreateTexture(aoDesc);
        graph->AddPass("Probe.Background", {{color, RHIResourceState::RenderTarget}, {ao, RHIResourceState::RenderTarget}},
            [color, ao, background, &device](const auto& execution) {
                for (const auto h : {color, ao})
                {
                    const auto handle = execution.ResolveHandle(h);
                    const auto target = device.CreateRenderTargets({&handle, 1}, nullptr);
                    execution.encoder->BindRenderTargets(target);
                    const float white[]{1, 1, 1, 1};
                    execution.encoder->ClearRenderTargets(target, h.index == color.index ? background.data() : white);
                }
            });
        if (!graphDraws.empty()) host.DeclareColor(*graph, outputs, color, ao, {});
        forward.SetInputs({outputs.depth, color});
        forward.SetGraphMaterials(&host);
        forward.SetUseReferencePath(fixture == 11);
        forward.Declare(*graph, context);
        std::array<RHIReadback, 3> readbacks;
        const std::array handles{color, outputs.depth, outputs.bitmask};
        const std::array formats{RHIFormat::RGBA16Float, RHIFormat::D32Float, RHIFormat::R32Uint};
        for (unsigned i = 0; i < 3; ++i)
        {
            Check(device.CreateReadback(16, 16, formats[i], 1, readbacks[i], error), "Mixed readback");
            graph->AddPass("Probe.MixedReadback", {{handles[i], RHIResourceState::CopySource}},
                [h = handles[i], r = readbacks[i]](const auto& execution) {
                    execution.encoder->CopyToReadback(r, execution.ResolveHandle(h));
                }, true);
        }
        Check(graph->Compile(error), "Mixed graph compile " + error);
        RHISubmissionTicket ticket;
        RHICompletionPoint completion;
        if (workers)
        {
            graph->SetParallelRecordCostThreshold(0);
            RHIRecordedBatchDesc desc;
            desc.frameId = context.frameId;
            desc.backendGeneration = GetRHISubmissionThread().GetOwnerGeneration(&device);
            desc.lifetimeToken = graph;
            RHIRecordedBatch batch;
            Check(graph->RecordParallel(pool, workers, desc, batch, error) &&
                      GetRHISubmissionThread().EnqueueRecordedBatch(&device, device, std::move(batch), ticket, error),
                  "Mixed parallel submit " + error);
            completion = ticket.GetRecordedBatch()->GetCompletionPoint();
        }
        else Check(graph->Execute(error), "Mixed execute " + error);
        Check(device.EndFrame(error), "Mixed end " + error);
        if (!workers) completion = {device.GetLastSignaledFenceValue()};
        Check(GetRHISubmissionThread().DrainSubmissions(&device, error), "Mixed submission drain");
        if (!graphDraws.empty()) Check(host.PublishSubmittedCache(context.frameId, completion, error, ticket), "Alpha publication " + error);
        device.WaitForGpu();
        std::array<RHIReadbackImage, 3> mapped;
        for (unsigned i = 0; i < 3; ++i) Check(device.MapReadback(readbacks[i], mapped[i], error), "Mixed map");
        std::vector<float> actual;
        for (unsigned y = 0; y < 16; ++y)
            for (unsigned x = 0; x < 16; ++x)
            {
                const auto owner = reinterpret_cast<const std::uint32_t*>(mapped[2].data.data() + y * mapped[2].rowPitch)[x];
                Check(owner == 0, "Blend never writes opaque owner mask");
                Check(mapped[1].At(x, y, 0) == (fixture == 8 ? .1f : 1.f), "Blend preserves opaque depth");
                for (unsigned c = 0; c < 3; ++c)
                {
                    const auto value = mapped[0].At(x, y, c);
                    Check(std::isfinite(value), "Finite mixed HDR");
                    actual.push_back(value);
                    if (fixture >= 4 && fixture < 10)
                    {
                        double expected = background[c];
                        if (fixture != 8 && fixture != 9)
                        {
                            const bool covered = std::abs(solo[0][(y * 16 + x) * 3 + c] - background[c]) > .0001;
                            if (covered)
                                for (unsigned j = 0; j < 4; ++j)
                                {
                                    const auto i = fixture == 7 ? 3 - j : j;
                                    expected = solo[i][(y * 16 + x) * 3 + c] * alpha[i] + expected * (1 - alpha[i]);
                                }
                        }
                        Check(std::abs(value - expected) <= .003 + .003 * std::abs(expected), "Independent alpha recurrence");
                        ++pixels;
                    }
                    if (fixture == 11)
                        Check(std::abs(value - mixedReference[actual.size() - 1]) <= .001 + .002 * std::abs(value),
                              "65-light tiled overflow equals whole-light reference for Code and Graph");
                }
            }
        if (fixture < 4) solo[fixture] = actual;
        if (fixture == 10) mixedReference = actual;
        for (auto& r : readbacks) device.ReleaseReadback(r);
        if (opaqueDepth.IsValid()) device.ReleaseTexture(opaqueDepth);
        std::string validation;
        const auto validationCount = device.DrainDebugMessages(validation);
        Check(validationCount == 0, "Mixed GPU validation " + validation);
        ++frames;
    }
    Check(host.ProgramStats().compileSubmissions == 0 && host.ProgramStats().publications == 18,
          "Cooked Graph Blend retains submitted generations without source compilation");
    host.ShutdownAfterIdle();
    forward.Shutdown();
    gbuffer.Shutdown();
    meshes.Shutdown();
    InternalPath::GetInstance()->AssetAuthoringEnabled = true;
    std::cout << "LX_MATERIAL_FORWARD_BLEND_OK frames=" << frames << " pixels=" << pixels
              << " checks=" << checks << " graphCompiles=0 mixed=true parallel=true overflowLights=65 validation=0\n";
}
