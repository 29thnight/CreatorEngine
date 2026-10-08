// Included after the shared Shadow/Decal product and native submission helpers.
std::shared_ptr<Texture> DecalImage(std::array<float, 4> value)
{
    auto image = TextureImage::Allocate(RHIFormat::RGBA32Float, 1, 1, 1, 1, false);
    std::memcpy(image.MutablePixelsAt(*image.Find(0, 0)), value.data(), sizeof(value));
    return Texture::CreateSharedFromImage("LX.Decal.Image", std::move(image));
}

void RunSceneDecal(RecordingChangeDevice& device, ProbeRoots& roots, ProbePipelines& pipelines, ProbeTextures& textures,
                   ProbePool& pool, const std::filesystem::path& root, const std::shared_ptr<Texture>& image,
                   const std::shared_ptr<Texture>& cube, bool versionedAcceptance = false)
{
    const std::array products{ShadowDecalProduct(root, false), ShadowDecalProduct(root, true)};
    GenerationStore store;
    experiment::AssetId id;
    Check(Uuid::TryParse("EEEEEEEE-EEEE-4EEE-8EEE-EEEEEEEEEEEE", id.value), "Decal graph identity");
    std::string error;
    std::array<own::shared_owner<const Generation>, 2> generations;
    for (unsigned i = 0; i < 2; ++i)
    {
        generations[i] = store.Load(
            id,
            [&](CookedProgram& cooked, std::string&) {
                cooked = {products[i], WriteMaterialProgramMetadata(products[i].program),
                          BuildBoundSource(products[i].program)};
                return true;
            },
            true, error);
        Check(bool(generations[i]), "Decal generation " + error);
    }
    SceneHost host;
    ProbeMeshes meshes;
    Check(meshes.Initialize(&device, error), "Decal mesh cache");
    EnhancedGBufferPass gbuffer;
    EnhancedDeferredPass deferred;
    EnhancedDecalPass decal;
    EnhancedFrameContext context;
    context.resources = &device;
    context.rootSignatures = &roots;
    context.psoManager = &pipelines;
    context.textureCache = &textures;
    context.meshCache = &meshes;
    context.width = context.height = 16;
    FrameCameraSnapshot camera;
    camera.view = camera.projection = camera.inverseView = camera.inverseProjection = math::matrix4x4::identity();
    camera.eyePosition = math::vector3(0, 0, 2);
    context.camera = &camera;
    std::vector<EnhancedDrawItem> legacy;
    context.draws = &legacy;
    EnhancedLight light{};
    light.direction = {0, 0, -1, 0};
    light.color = {2, 1, .5f, 1};
    const std::vector<EnhancedLight> lights{light};
    context.lights = &lights;
    Check(gbuffer.Initialize(context, error) && deferred.Initialize(context, error) && decal.Initialize(context, error),
          "Actual Decal Scene passes " + error);
    for (const auto& generation : generations)
    {
        WaitSceneProgram(host, context, generation);
    }
    const auto diffuse = DecalImage({.7f, .25f, .15f, .5f});
    const auto transparent = DecalImage({.7f, .25f, .15f, 0});
    // AO/R/M texture is stored directly for LX, swizzled only for legacy owners.
    const auto orm = DecalImage({.28f, .18f, .77f, 1});
    const auto normal = DecalImage({.8f, .5f, .9f, 1});
    using Images = std::array<RHIReadbackImage, 9>;
    unsigned frames{}, alteredPixels{}, preservedPixels{}, failures{};
    std::vector<Images> referenceFrames;
    unsigned frameOrdinal{};
    float policyMaxError{};
    const unsigned policyCount = versionedAcceptance ? 3 : 1;
    for (unsigned policy = 0; policy < policyCount; ++policy)
    {
        frameOrdinal = 0;
        for (unsigned tier = 0; tier < 2; ++tier)
        {
            for (unsigned workers : {0u, 1u, 4u})
            {
                Images baseline;
                const auto render = [&](unsigned fixture, const InstanceDescription& description, double ao) {
                    Drain drain{device};
                    context.frameId = 14000 + frames++;
                    context.sceneEpoch = 96;
                    auto geometry = MakeGeometry(false, true);
                    ShadowDecalPlane(geometry);
                    Check(BuildInstance(
                              generations[tier], description,
                              [&](const experiment::AssetId&, LXColorSpace, std::string&) { return image; },
                              geometry.draw.materialGraphInstance, error),
                          "Decal instance " + error);
                    std::shared_ptr<const SceneViewInput> input;
                    Check(SceneViewInput::Seal({context.frameId, context.sceneEpoch, 97 + tier, 1, 16, 16, camera},
                                               {&geometry.draw, 1}, {}, input, error),
                          "Decal seal " + error);
                    Check(device.BeginFrame(error), "Decal begin");
                    textures.BeginFrame(context.frameId);
                    meshes.BeginFrame(context.frameId);
                    const auto environment = textures.GetOrUpload(cube.get(), error).handle;
                    Check(environment.IsValid() && host.PrepareResidency(context, input, error),
                          "Decal residency " + error);
                    EnhancedDecalPass::Item item;
                    item.worldMatrix = math::matrix4x4::identity();
                    item.worldMatrix.m[3][2] = .5f;
                    item.diffuse = fixture == 1 || fixture == 3 ? diffuse.get()
                                   : fixture == 5               ? transparent.get()
                                                                : nullptr;
                    item.occRoughMetal = fixture == 2 || fixture == 3 || fixture == 6 ? orm.get() : nullptr;
                    item.normal = fixture == 4 ? normal.get() : nullptr;
                    decal.SetDecals(fixture >= 1 && fixture <= 6 ? std::vector{item}
                                                                 : std::vector<EnhancedDecalPass::Item>{});
                    Check(gbuffer.PrepareFrame(context, error) && deferred.PrepareFrame(context, error) &&
                              decal.PrepareFrame(context, error),
                          "Decal preparation " + error);
                    const bool versioned = policy != 0;
                    const auto readAccess = versioned ? RGAccessMode::Read : RGAccessMode::LegacyState;
                    const auto writeAccess = versioned ? RGAccessMode::Write : RGAccessMode::LegacyState;
                    auto graph = std::make_shared<EnhancedRenderGraph>(
                        device, versioned ? RGSchedulingMode::ExplicitVersioned : RGSchedulingMode::DeclarationOrder,
                        policy == 1 ? RGOrderPolicy::PreserveDeclarationOrder : RGOrderPolicy::DependencyOrder);
                    if (workers)
                    {
                        pool.BeginFrame(static_cast<unsigned>(context.frameId));
                        Check(graph->PrepareParallel(pool, error), "Decal prefix " + error);
                    }
                    Check(gbuffer.PrepareGpuVisibility(context, error) && decal.PrepareGpuVisibility(context, error),
                          "Decal GPU visibility preparation " + error);
                    Check(host.Prepare(context, input, environment, {}, {}, {}, {}, error, 1),
                          "Decal host prepare " + error);
                    gbuffer.Declare(*graph, context);
                    auto outputs = host.DeclareGBuffer(*graph, gbuffer.GetOutputs());
                    auto decalInputs = outputs;
                    if (fixture == 6)
                    {
                        RGTextureDesc ownerDesc;
                        ownerDesc.width = ownerDesc.height = 16;
                        ownerDesc.format = RHIFormat::R32Uint;
                        ownerDesc.allowRenderTarget = true;
                        auto legacyOwners = graph->CreateTexture(ownerDesc);
                        if (versioned)
                        {
                            legacyOwners = graph->Write(legacyOwners);
                        }
                        graph->AddPass("Probe.Decal.LegacyOwner",
                                       {{legacyOwners, RHIResourceState::RenderTarget, writeAccess}},
                                       [legacyOwners, &device](const auto& execution) {
                                           const auto texture = execution.ResolveHandle(legacyOwners);
                                           const auto targets = device.CreateRenderTargets({&texture, 1}, nullptr);
                                           const float clear[]{0, 0, 0, 0};
                                           execution.encoder->BindRenderTargets(targets);
                                           execution.encoder->ClearRenderTargets(targets, clear);
                                       });
                        decalInputs.bitmask = legacyOwners;
                    }
                    decal.SetInputs(decalInputs);
                    decal.Declare(*graph, context);
                    outputs.diffuse = decal.GetOutputs().diffuse;
                    outputs.normal = decal.GetOutputs().normal;
                    outputs.metalRough = decal.GetOutputs().metalRough;
                    if (decal.HasPreparedDecals())
                    {
                        bool rejected = false;
                        try
                        {
                            host.DeclareDecalInputs(*graph, outputs, {});
                        }
                        catch (const std::runtime_error&)
                        {
                            rejected = true;
                        }
                        Check(rejected, "Invalid Decal snapshot preserves frame");
                        ++failures;
                        host.DeclareDecalInputs(*graph, outputs, decal.GetBaseline());
                        rejected = false;
                        try
                        {
                            host.DeclareDecalInputs(*graph, outputs, decal.GetBaseline());
                        }
                        catch (const std::runtime_error&)
                        {
                            rejected = true;
                        }
                        Check(rejected, "Duplicate Decal declaration rejected");
                        ++failures;
                    }
                    RGTextureDesc aoDesc;
                    aoDesc.width = aoDesc.height = 16;
                    aoDesc.format = RHIFormat::RG16Float;
                    aoDesc.allowRenderTarget = true;
                    aoDesc.clearColor[0] = float(ao);
                    auto ambient = graph->CreateTexture(aoDesc);
                    if (versioned)
                    {
                        ambient = graph->Write(ambient);
                    }
                    graph->AddPass("Probe.Decal.AO", {{ambient, RHIResourceState::RenderTarget, writeAccess}},
                                   [ambient, ao, &device](const auto& execution) {
                                       const auto texture = execution.ResolveHandle(ambient);
                                       const auto targets = device.CreateRenderTargets({&texture, 1}, nullptr);
                                       const float clear[]{float(ao), 0, 0, 0};
                                       execution.encoder->BindRenderTargets(targets);
                                       execution.encoder->ClearRenderTargets(targets, clear);
                                   });
                    deferred.SetInputs(outputs);
                    deferred.SetAmbientOcclusion(ambient);
                    deferred.Declare(*graph, context);
                    const auto color = host.DeclareColor(*graph, outputs, deferred.GetOutput(), ambient, {});
                    std::array<RHIReadback, 9> readbacks;
                    const auto copyTexture = [&](unsigned index, RGHandle texture, RHIFormat format) {
                        Check(device.CreateReadback(16, 16, format, 1, readbacks[index], error),
                              "Decal texture readback");
                        graph->AddPass(
                            "Probe.Decal.Texture", {{texture, RHIResourceState::CopySource, readAccess}},
                            [texture, readback = readbacks[index]](const auto& execution) {
                                execution.encoder->CopyToReadback(readback, execution.ResolveHandle(texture));
                            },
                            true);
                    };
                    copyTexture(0, color, RHIFormat::RGBA16Float);
                    copyTexture(1, outputs.diffuse, RHIFormat::RGBA16Float);
                    copyTexture(2, outputs.metalRough, RHIFormat::RGBA16Float);
                    copyTexture(3, outputs.normal, RHIFormat::RGBA16Float);
                    for (unsigned i = 0; i < 3; ++i)
                    {
                        copyTexture(4 + i, host.LookupFrame()->GraphInputs(*graph)[i], RHIFormat::RGBA32Float);
                    }
                    Check(device.CreateBufferReadback(16 * 16 * sizeof(IblBakeSample), readbacks[7], error),
                          "Decal IBL readback");
                    graph->AddPass(
                        "Probe.Decal.IBL",
                        {{host.LookupFrame()->GraphSamples(*graph), RHIResourceState::CopySource, readAccess}},
                        [readback = readbacks[7], buffer = host.LookupFrame()->Samples()](const auto& execution) {
                            execution.encoder->CopyBufferToReadback(readback, buffer);
                        },
                        true);
                    Check(device.CreateBufferReadback(sizeof(SceneLookupStats), readbacks[8], error),
                          "Decal statistics readback");
                    graph->AddPass(
                        "Probe.Decal.Stats",
                        {{host.GraphLookupStatistics(*graph), RHIResourceState::CopySource, readAccess}},
                        [readback = readbacks[8], buffer = host.LookupStatistics()](const auto& execution) {
                            execution.encoder->CopyBufferToReadback(readback, buffer);
                        },
                        true);
                    SubmitShadowDecal(device, pool, graph, host, context.frameId, workers, true);
                    if (versioned)
                    {
                        EnhancedRenderGraph::DiagnosticSnapshot snapshot;
                        Check(graph->CaptureDiagnosticSnapshot(snapshot), "Versioned Decal diagnostic");
                        for (const auto& pass : snapshot.passes)
                        {
                            for (const auto& usage : pass.usages)
                            {
                                Check(usage.access != RGAccessMode::LegacyState, "Decal chain has no inferred access");
                            }
                        }
                    }

                    Images result;
                    for (unsigned i = 0; i < readbacks.size(); ++i)
                    {
                        Check(device.MapReadback(readbacks[i], result[i], error), "Decal map");
                        device.ReleaseReadback(readbacks[i]);
                    }
                    if (policy == 0)
                    {
                        referenceFrames.push_back(result);
                    }
                    else
                    {
                        Check(frameOrdinal < referenceFrames.size(), "Decal policy frame count");
                        for (unsigned attachment = 0; attachment < 7; ++attachment)
                        {
                            for (unsigned y = 0; y < 16; ++y)
                            {
                                for (unsigned x = 0; x < 16; ++x)
                                {
                                    for (unsigned channel = 0; channel < 4; ++channel)
                                    {
                                        const float difference =
                                            std::abs(result[attachment].At(x, y, channel) -
                                                     referenceFrames[frameOrdinal][attachment].At(x, y, channel));
                                        policyMaxError = std::max(policyMaxError, difference);
                                        Check(difference == 0, "Decal policy exact pixel parity");
                                    }
                                }
                            }
                        }
                    }
                    ++frameOrdinal;
                    return result;
                };
                baseline = render(0, {id, {}, {}}, 1);
                for (unsigned fixture = 1; fixture <= 6; ++fixture)
                {
                    std::cout << "LX_DECAL_FIXTURE_BEGIN tier=" << tier << " fixture=" << fixture
                              << " workers=" << workers << std::endl;
                    const auto actual = render(fixture, {id, {}, {}}, 1);
                    const bool changes = fixture <= 3 || fixture == 6;
                    std::array<double, 4> color{.21, .37, .53, 1};
                    double roughness = .42, metallic = .65, occlusion = 1;
                    for (unsigned c = 0; c < 3; ++c)
                    {
                        if (actual[1].At(8, 8, c) != baseline[1].At(8, 8, c))
                        {
                            color[c] = actual[1].At(8, 8, c);
                        }
                    }
                    if (actual[2].At(8, 8, 1) != baseline[2].At(8, 8, 1))
                    {
                        roughness = actual[2].At(8, 8, 1);
                    }
                    if (actual[2].At(8, 8, 2) != baseline[2].At(8, 8, 2))
                    {
                        metallic = actual[2].At(8, 8, 2);
                    }
                    if (actual[2].At(8, 8, 0) != baseline[2].At(8, 8, 0))
                    {
                        occlusion = actual[2].At(8, 8, 0);
                    }
                    const auto reference =
                        render(7, {id, {{1201, color}, {1202, roughness}, {1203, metallic}}, {}}, occlusion);
                    const auto samples = actual[7].Elements<IblBakeSample>();
                    const auto referenceSamples = reference[7].Elements<IblBakeSample>();
                    for (unsigned y = 2; y < 14; ++y)
                    {
                        for (unsigned x = 2; x < 14; ++x)
                        {
                            const bool inside = x >= 4 && x < 12 && y >= 4 && y < 12;
                            const auto& expected = inside ? reference : baseline;
                            for (unsigned c = 0; c < 3; ++c)
                            {
                                Near(actual[0].At(x, y, c), expected[0].At(x, y, c), "Decal derived lobe HDR", 2e-3);
                                Near(actual[4].At(x, y, c), expected[4].At(x, y, c), "Decal lookup base precision");
                            }
                            Near(actual[4].At(x, y, 3), 1, "Decal preserves authored coverage Alpha");
                            Near(actual[5].At(x, y, 3), expected[5].At(x, y, 3), "Decal lookup roughness");
                            Near(actual[6].At(x, y, 0), expected[6].At(x, y, 0), "Decal lookup metal");
                            Near(actual[6].At(x, y, 3), inside ? occlusion : 1, "Decal lookup occlusion");
                            const auto values = std::bit_cast<std::array<float, 36>>(samples[y * 16 + x]);
                            const auto expectedValues = std::bit_cast<std::array<float, 36>>(
                                inside ? referenceSamples[y * 16 + x]
                                       : baseline[7].Elements<IblBakeSample>()[y * 16 + x]);
                            for (unsigned c = 0; c < values.size(); ++c)
                            {
                                Near(values[c], expectedValues[c], "Decal derived IBL sample", 2e-4);
                            }
                            if (inside && changes)
                            {
                                ++alteredPixels;
                            }
                            else
                            {
                                ++preservedPixels;
                            }
                            if ((fixture == 1 || fixture == 3) && inside)
                            {
                                const double texels[]{.7, .25, .15};
                                for (unsigned c = 0; c < 3; ++c)
                                {
                                    const double base = baseline[1].At(x, y, c);
                                    Near(actual[1].At(x, y, c), base * .75 + std::pow(texels[c], 2.2) * .25,
                                         "Existing Decal alpha squared contract", 8e-4);
                                }
                            }
                            if ((fixture == 2 || fixture == 3) && inside)
                            {
                                Near(actual[2].At(x, y, 0), .28, "LX Decal occlusion", 8e-4);
                                Near(actual[2].At(x, y, 1), .18, "Existing Decal ORM channel 1", 8e-4);
                                Near(actual[2].At(x, y, 2), .77, "LX Decal metallic", 8e-4);
                            }
                            for (unsigned c = 0; c < 3; ++c)
                            {
                                Near(actual[3].At(x, y, c), baseline[3].At(x, y, c),
                                     "Legacy normal blend preserves normal");
                            }
                        }
                    }
                    if (fixture == 6)
                    {
                        Near(actual[2].At(8, 8, 0), .77, "Legacy Decal metallic channel", 8e-4);
                        Near(actual[2].At(8, 8, 1), .18, "Legacy Decal roughness channel", 8e-4);
                        Near(actual[2].At(8, 8, 2), .28, "Legacy Decal occlusion channel", 8e-4);
                    }
                    const auto unchanged = render(0, {id, {}, {}}, 1);
                    Check(unchanged[0].data == baseline[0].data, "Decal removal restores exact original HDR");
                    const auto stats = unchanged[8].Elements<SceneLookupStats>();
                    Check(stats && stats->visible == 144 && stats->rejected == 0,
                          "Decal cache remains valid after removal");
                }
            }
        }
    }
    host.ShutdownAfterIdle();
    decal.Shutdown();
    deferred.Shutdown();
    gbuffer.Shutdown();
    meshes.Shutdown();
    std::cout << "LX_MATERIAL_SCENE_DECAL_OK frames=" << frames << " altered=" << alteredPixels
              << " preserved=" << preservedPixels << " failures=" << failures << " checks=" << checks
              << " gpuComponents=" << gpuComponents << " validation=0\n";
    if (versionedAcceptance)
    {
        std::cout << "RG5_DECAL_GPU_OK policies=3 frames=" << frames << " maxError=" << policyMaxError
                  << " validation=0\n";
    }
}
