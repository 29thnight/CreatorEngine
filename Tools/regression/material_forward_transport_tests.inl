// Independent alpha recurrence, identity refraction, and Beer-Lambert transport
// across the real shared Code/Graph Forward+ stream.
own::shared_owner<const Generation> ForwardTransportProduct(const std::filesystem::path& root, unsigned kind)
{
    LXMaterialAsset asset;
    const auto output = asset.CreateNode("ShaderNodeOutputMaterial", 400, 0);
    asset.activeOutput = output;
    asset.blackboard = {{2001, "alpha", "Alpha", PinType::Float, 1.0},
                        {2002, "color", "Color", PinType::Color, std::array<double, 4>{.7, .2, .1, 1}}};
    const auto connect = [&](Id from, const char* source, Id to, const char* target) {
        Check(asset.graph
                  .Connect(Pin(asset.graph, from, source, Direction::Output),
                           Pin(asset.graph, to, target, Direction::Input))
                  .has_value(),
              "Transport graph link");
    };
    if (kind != 3)
    {
        const auto surface = asset.CreateNode("ShaderNodeBsdfPrincipled", 0, 0);
        const auto alpha = asset.CreateNode("LXParameterFloat", -200, 0);
        const auto color = asset.CreateNode("LXParameterColor", -200, 100);
        Check(asset.graph.SetProperty(alpha, "parameter", "2001") &&
                  asset.graph.SetProperty(color, "parameter", "2002"),
              "Transport parameters");
        connect(alpha, "Value", surface, "Alpha");
        connect(color, "Value", surface, kind == 0 ? "Emission Color" : "Base Color");
        const auto set = [&](const char* name, LXSocketValue value) {
            const auto pin = Pin(asset.graph, surface, name, Direction::Input);
            asset.graph.SetSocketValue(pin, value);
            Check(asset.graph.FindPin(pin)->value == value, name);
        };
        set("Roughness", kind == 2 ? 0.0 : .5);
        set("IOR", kind == 2 ? 1.0 : 1.5);
        set("Transmission Weight", kind == 2 ? 1.0 : 0.0);
        if (kind == 0)
        {
            set("Base Color", std::array<double, 4>{0, 0, 0, 1});
            set("Specular IOR Level", 0.0);
            set("Emission Strength", 1.0);
        }
        if (kind == 1)
        {
            set("Subsurface Weight", .8);
            set("Subsurface Radius", std::array<double, 3>{.2, .12, .08});
            set("Subsurface Scale", .2);
        }
        connect(surface, "BSDF", output, "Surface");
    }
    else
    {
        const auto volume = asset.CreateNode("LXPrincipledVolume", 0, 0);
        const auto set = [&](const char* name, LXSocketValue value) {
            const auto pin = Pin(asset.graph, volume, name, Direction::Input);
            asset.graph.SetSocketValue(pin, value);
            Check(asset.graph.FindPin(pin)->value == value, name);
        };
        set("Density", 2.0);
        set("Color", std::array<double, 4>{0, 0, 0, 1});
        set("Absorption Color", std::array<double, 4>{0, 0, 0, 1});
        connect(volume, "Volume", output, "Volume");
    }
    std::vector<LXMaterialDiagnostic> diagnostics;
    const auto program = GenerateMaterialSlang(asset, &diagnostics);
    Check(bool(program), "Transport material generation");
    VerifiedProduct product;
    std::string error;
    const auto source =
        root / "Build/Obj/MaterialProductProbe" / ("forward-transport-" + std::to_string(kind) + ".slang");
    const FileGuid guid{"CCCCCCCC-CCCC-4CCC-8CCC-CCCCCCCCCCCC"};
    const bool compiled = CompileSceneProduct(*program, root / "Dynamic_CPP/Assets/Shaders/DefaultPassShader", source,
                                              {}, product, error, guid);
    Check(compiled, "Transport sealed product " + error);
    std::vector<std::uint8_t> bytes;
    CookedProgram restored;
    const bool restoredOk =
        WriteCookedProgram(product, {}, bytes, error) && ReadCookedProgram(bytes, {}, restored, error);
    Check(restoredOk, "Transport binary product round trip " + error);
    Generation generationValue;
    Check(Uuid::TryParse("CCCCCCCC-CCCC-4CCC-8CCC-CCCCCCCCCCCC", generationValue.assetId.value),
          "Transport graph identity");
    generationValue.generation = kind + 1;
    generationValue.cooked = std::move(restored);
    return own::make_shared<const Generation>(std::move(generationValue));
}

void RunForwardTransport(const std::filesystem::path& root, RecordingChangeDevice& device, ProbeRoots& roots,
                         ProbePipelines& pipelines, ProbeTextures& textures, ProbePool& pool,
                         own::shared_owner<const Texture> image, own::shared_owner<const Texture> cube,
                         bool versionedAcceptance = false)
{
    std::string error;
    std::array<own::shared_owner<const Generation>, 4> generations;
    for (unsigned i = 0; i < generations.size(); ++i)
    {
        generations[i] = ForwardTransportProduct(root, i);
    }
    ProbeMeshes meshes;
    Check(meshes.Initialize(&device, error), "Transport mesh cache");
    EnhancedGBufferPass gbuffer;
    EnhancedForwardPass forward;
    SceneHost host;
    EnhancedFrameContext context;
    context.resources = &device;
    context.rootSignatures = &roots;
    context.psoManager = &pipelines;
    context.meshCache = &meshes;
    context.textureCache = &textures;
    context.width = context.height = 16;
    context.sceneEpoch = 900;
    FrameCameraSnapshot camera;
    camera.view = camera.projection = camera.inverseProjection = math::matrix4x4::identity();
    camera.eyePosition = math::vector3(0, 0, -2);
    camera.forward = math::vector3(0, 0, 1);
    context.camera = &camera;
    EnhancedLight light;
    light.position = math::vector4(0, 0, 0, 0);
    light.direction = math::vector4(0, 0, 1, 0);
    light.color = math::color(.1f, .09f, .08f, 1);
    const std::vector lights{light};
    context.lights = &lights;
    auto divided = light;
    divided.color = math::color(.1f / 65, .09f / 65, .08f / 65, 1);
    const std::vector<EnhancedLight> manyLights(65, divided);
    const bool initialized = gbuffer.Initialize(context, error) && forward.Initialize(context, error);
    Check(initialized, "Transport native passes " + error);
    ShaderMeta meta;
    const ShaderMetaHandle codeHandle{990, 1};
    const bool codeReady =
        ShaderMetaLoader::LoadFile(root / "Dynamic_CPP/Assets/Shaders/DefaultPassShader/Forward.shadermeta",
                                   FileGuid{"DDDDDDDD-DDDD-4DDD-8DDD-DDDDDDDDDDDD"}, meta, error) &&
        forward.ApplyShaderMeta(context, codeHandle, meta, {}, error);
    Check(codeReady, "Transport Code contract " + error);
    InternalPath::GetInstance()->AssetAuthoringEnabled = false;
    for (const auto& generation : generations)
    {
        WaitSceneProgram(host, context, generation);
    }
    auto geometry = MakeGeometry(false, true);
    const std::array<std::array<float, 3>, 6> quad{
        {{-.75f, -.75f, 0}, {.75f, -.75f, 0}, {.75f, .75f, 0}, {-.75f, -.75f, 0}, {.75f, .75f, 0}, {-.75f, .75f, 0}}};
    for (unsigned i = 0; i < 6; ++i)
    {
        auto* vertex = geometry.vertices.data() + i * geometry.draw.modelMeshView.vertexStride;
        std::memcpy(vertex, quad[i].data(), 12);
        const std::array<float, 3> normal{0, 0, -1};
        std::memcpy(
            vertex + assets::OffsetOf(geometry.draw.modelMeshView.vertexAttributeMask, assets::VertexAttribute::Normal),
            normal.data(), 12);
    }
    const std::array<std::array<float, 2>, 1> ranges{{{.2f, .6f}}};
    auto boundary = VolumeBoxes(ranges, 9999, false);
    std::array<std::vector<float>, 3> solo;
    std::vector<float> mixed, refracted, fullTransport;
    const std::array<float, 3> alphas{.3f, .45f, .6f};
    const std::array<float, 3> depths{.25f, .5f, .7f};
    const std::array<float, 4> background{.12f, .15f, .18f, 1};
    unsigned frames{}, components{};
    std::array<std::vector<float>, 24> referencePixels;
    float policyMaxError{};
    const unsigned policyCount = versionedAcceptance ? 3 : 1;
    for (unsigned policy = 0; policy < policyCount; ++policy)
    {
        for (unsigned fixture = 0; fixture < 24; ++fixture)
        {
            std::cout << "LX_FORWARD_TRANSPORT_BEGIN fixture=" << fixture << std::endl;
            Drain drain{device};
            context.frameId = 9000 + policy * 24 + fixture;
            std::vector<EnhancedDrawItem> code, graphDraws;
            const bool medium = fixture >= 15 && fixture < 23;
            context.lights = fixture == 23 ? &manyLights : &lights;
            const auto instance = [&](unsigned kind, double alpha, std::array<double, 4> color) {
                own::shared_owner<const Instance> value;
                InstanceDescription description{generations[kind]->assetId, {}, {}};
                if (kind != 3)
                {
                    description.parameters = {{2001, alpha}, {2002, color}};
                }
                const bool built = BuildInstance(
                    generations[kind], description,
                    [&](const experiment::AssetId&, LXColorSpace, std::string&) { return image; }, value, error);
                Check(built, "Transport instance " + error);
                return value;
            };
            const auto add = [&](unsigned kind, float depth, float alpha, std::array<double, 4> color) {
                auto draw = geometry.draw;
                draw.geometryKey = 9100 + kind;
                draw.worldMatrix[3, 2] = depth;
                draw.coverage.flags = EnhancedMaterialCoverage::Enabled | EnhancedMaterialCoverage::DoubleSided |
                                      EnhancedMaterialCoverage::Blended;
                if (kind == 4)
                {
                    auto snapshot = std::make_shared<EnhancedForwardMaterialDrawSnapshot>();
                    snapshot->shaderMetaHandle = codeHandle;
                    snapshot->keywordSelections.assign(meta.keywords.size(), 0);
                    std::shared_ptr<const ShaderMetaBindingLayout> layout;
                    Check(forward.EnsureShaderMetaVariant(context, codeHandle, meta, snapshot->keywordSelections,
                                                          snapshot->permutationKey, layout, error),
                          "Transport Code variant");
                    snapshot->bindingLayout = *layout;
                    ExperimentMaterialSealing::SealSource source;
                    source.material.blendMode = experiment::MaterialBlendMode::Transparent;
                    source.material.properties = {
                        {"baseColor", math::color(0, 0, 0, alpha).rgba()},
                        {"metallic", 1.f},
                        {"emissive", math::vector3(float(color[0]), float(color[1]), float(color[2]))},
                        {"emissiveStrength", 1.f}};
                    Check(ExperimentMaterialSealing::SealCore(source, meta, *layout, snapshot->propertyBytes,
                                                              snapshot->textureBindings, error,
                                                              &snapshot->runtimeInstance, codeHandle) &&
                              ExperimentMaterialSealing::SealCoverage(source, *layout, snapshot->propertyBytes,
                                                                      snapshot->coverage, error) &&
                              forward.CaptureShaderVariant(*snapshot) && snapshot->IsValid(),
                          "Transport Code sealing");
                    EnhancedMaterialSeal::Stamp(*snapshot, kind + 1, 1, 1, context.sceneEpoch, context.frameId);
                    draw.forwardMaterialSnapshot = std::move(snapshot);
                    code.push_back(std::move(draw));
                }
                else
                {
                    draw.materialGraphInstance = instance(kind, alpha, color);
                    draw.materialGraphSlot = 9200 + kind;
                    if (fixture == 12 && kind == 2)
                    {
                        draw.coverage.flags &= ~EnhancedMaterialCoverage::Blended;
                    }
                    graphDraws.push_back(std::move(draw));
                }
            };
            if (fixture < 3)
            {
                add(fixture == 0   ? 1
                    : fixture == 1 ? 0
                                   : 4,
                    depths[fixture], 1,
                    fixture == 0   ? std::array<double, 4>{.7, .2, .1, 1}
                    : fixture == 1 ? std::array<double, 4>{.1, .6, .2, 1}
                                   : std::array<double, 4>{.2, .1, .7, 1});
            }
            else if (fixture == 23)
            {
                add(1, depths[0], 1, {.7, .2, .1, 1});
            }
            else if (!medium)
            {
                const bool reverse = fixture == 5;
                add(1, depths[0], fixture == 7 ? 0 : fixture == 8 ? 1 : alphas[0], {.7, .2, .1, 1});
                add(0, depths[1], fixture == 7 ? 0 : fixture == 8 ? 1 : alphas[1], {.1, .6, .2, 1});
                add(4, depths[2], fixture == 7 ? 0 : fixture == 8 ? 1 : alphas[2], {.2, .1, .7, 1});
                camera.forward = math::vector3(0, 0, reverse ? -1.f : 1.f);
                if (fixture >= 9)
                {
                    add(2, .4f, fixture == 11 ? 0 : fixture == 12 ? 1 : .4f, {1, 1, 1, 1});
                }
                if (fixture == 14)
                {
                    auto second = graphDraws.back();
                    second.geometryKey++;
                    second.materialGraphSlot++;
                    second.worldMatrix[3, 2] = .6f;
                    graphDraws.push_back(second);
                }
            }
            else
            {
                auto draw = boundary.draw;
                draw.geometryKey = 9300;
                draw.materialGraphSlot = 9300;
                draw.materialGraphInstance = instance(3, 1, {0, 0, 0, 1});
                if (fixture == 18)
                {
                    draw.coverage.flags |= EnhancedMaterialCoverage::Blended;
                }
                graphDraws.push_back(draw);
                const float depth = fixture == 15 ? .1f : fixture == 16 ? .4f : .8f;
                add(0, depth, fixture == 18 ? 0 : .5f, {.1, .6, .2, 1});
                if (fixture >= 19)
                {
                    add(4, .1f, .6f, {.2, .1, .7, 1});
                }
                if (fixture >= 21)
                {
                    add(1, .25f, .3f, {.7, .2, .1, 1});
                    add(2, .4f, .4f, {1, 1, 1, 1});
                }
            }
            if (fixture == 4 || fixture == 10 || fixture == 20 || fixture == 22)
            {
                std::reverse(graphDraws.begin(), graphDraws.end());
                std::reverse(code.begin(), code.end());
            }
            context.forwardDraws = &code;
            const std::vector<EnhancedDrawItem> empty;
            context.draws = &empty;
            std::shared_ptr<const SceneViewInput> input;
            Check(SceneViewInput::Seal({context.frameId, context.sceneEpoch, 901, 1, 16, 16, camera}, graphDraws, {},
                                       input, error) &&
                      host.SelectReadyInput(context, input, input, error) && input->Draws().size() == graphDraws.size(),
                  "All transport draws accepted");
            Check(device.BeginFrame(error), "Transport begin");
            textures.BeginFrame(context.frameId);
            meshes.BeginFrame(context.frameId);
            const auto environment = textures.GetOrUpload((cube ? &*cube.borrow() : nullptr), cube ? cube->NonRehydratableImage() : own::shared_owner<const Texture::CodecImage>{}, error);
            Check(gbuffer.PrepareFrame(context, error) && forward.PrepareFrame(context, error) &&
                      host.PrepareResidency(context, input, error),
                  "Transport preparation");
            const bool versioned = policy != 0;
            const auto accessRead = versioned ? RGAccessMode::Read : RGAccessMode::LegacyState;
            const auto accessWrite = versioned ? RGAccessMode::Write : RGAccessMode::LegacyState;
            auto graph = std::make_shared<EnhancedRenderGraph>(
                device, versioned ? RGSchedulingMode::ExplicitVersioned : RGSchedulingMode::DeclarationOrder,
                policy == 1 ? RGOrderPolicy::PreserveDeclarationOrder : RGOrderPolicy::DependencyOrder);
            const bool parallel = fixture == 4 || fixture == 10 || fixture == 20 || fixture == 22;
            if (parallel)
            {
                pool.BeginFrame(static_cast<std::uint32_t>(context.frameId));
                Check(graph->PrepareParallel(pool, error), "Transport parallel prefix");
            }
            const bool prepared = host.Prepare(context, input, environment.handle, {}, {}, {}, {}, error, 1);
            Check(prepared, "Transport host " + error);
            gbuffer.Declare(*graph, context);
            auto outputs = gbuffer.GetOutputs();
            RHITextureDesc opaqueDepthDesc;
            opaqueDepthDesc.width = opaqueDepthDesc.height = 16;
            opaqueDepthDesc.format = RHIFormat::D32Float;
            opaqueDepthDesc.allowDepthStencil = true;
            opaqueDepthDesc.clearDepth = fixture == 6 ? .1f : .9f;
            RHITextureHandle opaqueDepth;
            Check(device.CreateTexture(opaqueDepthDesc, opaqueDepth, error), "Transport opaque depth target");
            outputs.depth = graph->ImportTexture(opaqueDepth, RHIResourceState::Common, "Probe.OpaqueDepth");
            if (versioned)
            {
                outputs.depth = graph->Write(outputs.depth);
            }
            graph->AddPass("Probe.Transport.OpaqueDepth", {{outputs.depth, RHIResourceState::DepthWrite, accessWrite}},
                           [&, outputs, fixture](const auto& execution) {
                               const auto depth = RHIDepthTargetDesc::Depth(execution.ResolveHandle(outputs.depth),
                                                                            RHIFormat::D32Float);
                               const auto target =
                                   device.CreateRenderTargets(std::span<const RHITextureHandle>{}, &depth);
                               execution.encoder->BindRenderTargets(target);
                               execution.encoder->ClearDepthTarget(target, fixture == 6 ? .1f : .9f);
                           });
            outputs = host.DeclareGBuffer(*graph, outputs);
            RGTextureDesc desc;
            desc.width = desc.height = 16;
            desc.format = RHIFormat::RGBA16Float;
            desc.allowRenderTarget = true;
            std::copy(background.begin(), background.end(), desc.clearColor);
            auto color = graph->CreateTexture(desc);
            desc.format = RHIFormat::RG16Float;
            std::fill(std::begin(desc.clearColor), std::end(desc.clearColor), 1.f);
            auto ao = graph->CreateTexture(desc);
            if (versioned)
            {
                color = graph->Write(color);
                ao = graph->Write(ao);
            }
            graph->AddPass("Probe.Transport.Background",
                           {{color, RHIResourceState::RenderTarget, accessWrite},
                            {ao, RHIResourceState::RenderTarget, accessWrite}},
                           [&, color, ao](const auto& execution) {
                               for (auto h : {color, ao})
                               {
                                   const auto texture = execution.ResolveHandle(h);
                                   const auto target = device.CreateRenderTargets({&texture, 1}, nullptr);
                                   const float white[]{1, 1, 1, 1};
                                   execution.encoder->BindRenderTargets(target);
                                   execution.encoder->ClearRenderTargets(
                                       target, h.index == color.index ? background.data() : white);
                               }
                           });
            color = host.DeclareColor(*graph, outputs, color, ao, {});
            forward.SetInputs({outputs.depth, color});
            forward.SetGraphMaterials(&host);
            forward.Declare(*graph, context);
            const auto final = forward.GetOutput();
            Check(final.IsValid(), "Transport shared output");
            std::array<RHIReadback, 3> readbacks;
            const std::array handles{final, outputs.depth, outputs.bitmask};
            const std::array formats{RHIFormat::RGBA16Float, RHIFormat::D32Float, RHIFormat::R32Uint};
            for (unsigned i = 0; i < 3; ++i)
            {
                Check(device.CreateReadback(16, 16, formats[i], 1, readbacks[i], error), "Transport readback");
                graph->AddPass(
                    "Probe.Transport.Readback", {{handles[i], RHIResourceState::CopySource, accessRead}},
                    [h = handles[i], r = readbacks[i]](const auto& execution) {
                        execution.encoder->CopyToReadback(r, execution.ResolveHandle(h));
                    },
                    true);
            }
            const bool compiled = graph->Compile(error);
            Check(compiled, "Transport graph compile " + error);
            if (versioned)
            {
                EnhancedRenderGraph::DiagnosticSnapshot snapshot;
                Check(graph->CaptureDiagnosticSnapshot(snapshot), "Versioned transport diagnostics");
                for (const auto& pass : snapshot.passes)
                {
                    for (const auto& usage : pass.usages)
                    {
                        Check(usage.access != RGAccessMode::LegacyState, "Versioned transport has no inferred access");
                    }
                }
                Check((final.index == color.index && final.version > color.version) ||
                          (medium && final.index != color.index &&
                           snapshot.resources[final.index].name == "LX.Scene.VolumeColor"),
                      "Mixed stream returns its updated color or Volume output");
                bool outputWritten = false;
                for (const auto& pass : snapshot.passes)
                {
                    for (const auto& usage : pass.usages)
                    {
                        if (usage.kind == final.kind && usage.resource == final.index &&
                            (usage.access == RGAccessMode::Write || usage.access == RGAccessMode::ReadWrite))
                        {
                            Check(usage.version <= final.version, "Mixed stream output is not a stale version");
                            outputWritten |= usage.version == final.version;
                        }
                    }
                }
                Check(outputWritten, "Mixed stream output has its current writer");
            }
            RHISubmissionTicket ticket;
            RHICompletionPoint completion;
            if (parallel)
            {
                graph->SetParallelRecordCostThreshold(0);
                RHIRecordedBatchDesc batchDesc;
                batchDesc.frameId = context.frameId;
                batchDesc.backendGeneration = GetRHISubmissionThread().GetOwnerGeneration(&device);
                batchDesc.lifetimeToken = graph;
                RHIRecordedBatch batch;
                Check(
                    graph->RecordParallel(pool, 4, batchDesc, batch, error) &&
                        GetRHISubmissionThread().EnqueueRecordedBatch(&device, device, std::move(batch), ticket, error),
                    "Transport parallel submit");
                completion = ticket.GetRecordedBatch()->GetCompletionPoint();
            }
            else
            {
                Check(graph->Execute(error), "Transport execute " + error);
            }
            Check(device.EndFrame(error) && GetRHISubmissionThread().DrainSubmissions(&device, error),
                  "Transport submission");
            if (!parallel)
            {
                completion = {device.GetLastSignaledFenceValue()};
            }
            Check(host.PublishSubmittedCache(context.frameId, completion, error, ticket),
                  "Transport publication " + error);
            device.WaitForGpu();
            std::array<RHIReadbackImage, 3> mapped;
            for (unsigned i = 0; i < 3; ++i)
            {
                Check(device.MapReadback(readbacks[i], mapped[i], error), "Transport map");
            }
            std::vector<float> actual;
            for (unsigned y = 0; y < 16; ++y)
            {
                for (unsigned x = 0; x < 16; ++x)
                {
                    const auto owner =
                        reinterpret_cast<const std::uint32_t*>(mapped[2].data.data() + y * mapped[2].rowPitch)[x];
                    Check(owner == 0 && mapped[1].At(x, y, 0) == (fixture == 6 ? .1f : .9f),
                          "Transport preserves opaque depth and ownership");
                    for (unsigned c = 0; c < 3; ++c)
                    {
                        const float value = mapped[0].At(x, y, c);
                        Check(std::isfinite(value) && value >= 0, "Finite transport HDR");
                        actual.push_back(value);
                        if (fixture >= 3 && x >= 5 && x < 11 && y >= 5 && y < 11)
                        {
                            const auto index = (y * 16 + x) * 3 + c;
                            double expected = background[c];
                            if (fixture == 23)
                            {
                                expected = solo[0][index];
                            }
                            else if (!medium && fixture != 6 && fixture != 7)
                            {
                                for (unsigned j = 0; j < 3; ++j)
                                {
                                    const unsigned i = fixture == 5 ? j : 2 - j;
                                    const float a = fixture == 8 ? 1 : alphas[i];
                                    expected = solo[i][index] * a + expected * (1 - a);
                                }
                            }
                            if (medium)
                            {
                                const double t = std::exp(-2 * .4);
                                expected *= t;
                                if (fixture != 18)
                                {
                                    const double path = fixture == 15 ? 0 : fixture == 16 ? .2 : .4;
                                    expected = solo[1][index] * std::exp(-2 * path) * .5 + expected * .5;
                                }
                                if (fixture >= 21)
                                {
                                    expected = solo[0][index] * std::exp(-2 * .05) * .3 + expected * .7;
                                }
                                if (fixture >= 19)
                                {
                                    expected = solo[2][index] * .6 + expected * .4;
                                }
                            }
                            Near(value, expected,
                                 "Independent ordered alpha/identity-refraction/medium fixture=" +
                                     std::to_string(fixture),
                                 .004);
                            ++components;
                        }
                        if ((fixture == 1 || fixture == 2) && x >= 5 && x < 11 && y >= 5 && y < 11)
                        {
                            const std::array<float, 3> emission = fixture == 1 ? std::array<float, 3>{.1f, .6f, .2f}
                                                                               : std::array<float, 3>{.2f, .1f, .7f};
                            Near(value, emission[c], "Graph and Code emission fixture renders", .001);
                        }
                    }
                }
            }
            if (fixture < 3)
            {
                solo[fixture] = actual;
            }
            if (fixture == 3)
            {
                mixed = actual;
            }
            if (fixture == 9)
            {
                refracted = actual;
            }
            if (fixture == 21)
            {
                fullTransport = actual;
            }
            if (fixture == 4 || fixture == 10 || fixture == 22)
            {
                const auto& expected = fixture == 4 ? mixed : fixture == 10 ? refracted : fullTransport;
                for (unsigned i = 0; i < actual.size(); ++i)
                {
                    Near(actual[i], expected[i], "Collection and parallel transport identity", .004);
                }
            }
            if (policy == 0)
            {
                referencePixels[fixture] = actual;
            }
            else
            {
                Check(actual.size() == referencePixels[fixture].size(), "Versioned transport pixel count");
                for (unsigned i = 0; i < actual.size(); ++i)
                {
                    policyMaxError = std::max(policyMaxError, std::abs(actual[i] - referencePixels[fixture][i]));
                    Near(actual[i], referencePixels[fixture][i], "Versioned transport policy parity", .004);
                }
            }
            for (auto r : readbacks)
            {
                device.ReleaseReadback(r);
            }
            device.ReleaseTexture(opaqueDepth);
            std::string validation;
            const auto validationCount = device.DrainDebugMessages(validation);
            Check(validationCount == 0, "Transport validation " + validation);
            ++frames;
        }
    }
    Check(host.ProgramStats().compileSubmissions == 0, "Sealed transport consumes no Graph compiler");
    if (versionedAcceptance)
    {
        const auto cache = host.GeometryStats();
        Check(cache.uploads > 0 && cache.hits > 0 && cache.transforms > 0 && cache.transformHits > 0,
              "Versioned transport exercises cold uploads and completed geometry reuse");
        std::cout << "RG5_MIXED_CACHE uploads=" << cache.uploads << " hits=" << cache.hits
                  << " transforms=" << cache.transforms << " transformHits=" << cache.transformHits << '\n';
    }
    host.ShutdownAfterIdle();
    forward.Shutdown();
    gbuffer.Shutdown();
    meshes.Shutdown();
    InternalPath::GetInstance()->AssetAuthoringEnabled = true;
    if (versionedAcceptance)
    {
        std::cout << "RG5_MIXED_GPU_OK policies=3 frames=" << frames << " components=" << components
                  << " maxError=" << policyMaxError << " graphCompiles=0 validation=0\n";
    }
    else
    {
        std::cout << "LX_MATERIAL_FORWARD_TRANSPORT_OK frames=" << frames << " components=" << components
                  << " checks=" << checks << " graphCompiles=0 validation=0\n";
    }
}
