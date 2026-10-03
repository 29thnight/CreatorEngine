VerifiedProduct RefractionProduct(const std::filesystem::path& root, bool hybrid, bool film = false)
{
    LXMaterialAsset asset;
    const auto surface = asset.CreateNode("ShaderNodeBsdfPrincipled", 0, 0);
    const auto output = asset.CreateNode("ShaderNodeOutputMaterial", 400, 0);
    const auto image = asset.CreateNode("ShaderNodeTexImage", -400, 0);
    asset.activeOutput = output;
    asset.blackboard = {{914, "ior", "IOR", PinType::Float, 1.5},
                        {915, "roughness", "Roughness", PinType::Float, 0.0},
                        {916, "transmission", "Transmission", PinType::Float, 1.0}};
    const auto connect = [&](Id from, const char* fromName, Id to, const char* toName) {
        Check(asset.graph
                  .Connect(Pin(asset.graph, from, fromName, Direction::Output),
                           Pin(asset.graph, to, toName, Direction::Input))
                  .has_value(),
              "Refraction link");
    };
    for (unsigned i = 0; i < 3; ++i)
    {
        const auto parameter = asset.CreateNode("LXParameterFloat", -200, float(i * 100));
        Check(asset.graph.SetProperty(parameter, "parameter", std::to_string(914 + i)), "Refraction parameter");
        const char* names[]{"IOR", "Roughness", "Transmission Weight"};
        connect(parameter, "Value", surface, names[i]);
    }
    const auto set = [&](const char* name, LXSocketValue value) {
        const auto pin = Pin(asset.graph, surface, name, Direction::Input);
        asset.graph.SetSocketValue(pin, value);
        Check(asset.graph.FindPin(pin)->value == value, name);
    };
    set("Base Color", std::array<double, 4>{1, 1, 1, 1});
    set("Metallic", hybrid ? .3 : 0.0);
    set("Subsurface Weight", hybrid ? .4 : 0.0);
    set("Subsurface Scale", 0.0);
    if (film)
    {
        set("Specular IOR Level", 0.0);
        set("Specular Tint", std::array<double, 4>{.5, .7, .9, 1});
        set("Thin Film Thickness", 420.0);
        set("Thin Film IOR", 1.33);
    }
    Check(asset.graph.SetProperty(image, "image", "22222222-2222-4222-8222-222222222222") &&
              asset.graph.SetProperty(image, "interpolation", "Closest"),
          "Refraction alpha texture");
    connect(image, "Alpha", surface, "Alpha");
    connect(surface, "BSDF", output, "Surface");
    std::vector<LXMaterialDiagnostic> diagnostics;
    const auto program = GenerateMaterialSlang(asset, &diagnostics);
    Check(program && (program->features & 0x0800u), "Refraction feature extraction");
    const auto file = root / "Build/Obj/MaterialProductProbe" /
                      (hybrid ? "refraction-hybrid-product.slang"
                       : film ? "refraction-film-product.slang"
                              : "refraction-product.slang");
    std::ofstream(file, std::ios::binary | std::ios::trunc)
        << BuildBoundSource(*program) << "\n#include \"MaterialGraphSceneHost.slang\"\n";
    const CompileTarget targets[]{{RHIShaderBinary::Dxil, "LXSceneVS", "vs_6_0"},
                                  {RHIShaderBinary::Dxil, "LXSceneColorPS", "ps_6_0"},
                                  {RHIShaderBinary::SpirV, "LXSceneVS", "vs_6_0"},
                                  {RHIShaderBinary::SpirV, "LXSceneColorPS", "ps_6_0"}};
    RHIShaderCompileOptions options;
    options.strictMath = options.fineDerivatives = true;
    options.includeDirectories.push_back(root / "Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes");
    Capabilities capabilities;
    capabilities.coreForward = capabilities.layeredLookup = capabilities.subsurface = capabilities.refraction = true;
    RHIShaderPermutation permutation;
    std::string error;
    Check(permutation.Set("LX_MATERIAL_ROUTE", "2", error) && permutation.Enable("LX_MATERIAL_PIXEL_FOOTPRINT", error),
          "Refraction permutation");
    VerifiedProduct product;
    const bool verified =
        VerifyProduct(*program, file, targets, permutation, options, capabilities, {}, product, diagnostics);
    std::string messages;
    for (const auto& diagnostic : diagnostics)
    {
        messages += diagnostic.message + "\n";
    }
    Check(verified, "Refraction verification: " + messages);
    Check(product.selection.tier == Tier::Special && product.selection.route == Route::Forward, "Refraction route");
    return product;
}

Vector SceneRefract(Vector incident, Vector normal, double eta)
{
    const double cosine = Dot(incident, normal);
    const double discriminant = 1 - eta * eta * (1 - cosine * cosine);
    return discriminant < 0 ? Vector{} : incident * eta - normal * (eta * cosine + std::sqrt(discriminant));
}

Vector SceneTransmissionIntegral(const MaterialProbe::Reference::Material& glass, Vector view)
{
    if (glass.ior == 1 || glass.roughness == 0)
    {
        return Vector{1, 1, 1} - Fresnel(glass, Clamp(Dot(glass.normal, view)));
    }
    const auto frame = MakeFrame(glass, false);
    Vector result{};
    for (unsigned i = 0; i < 1024; ++i)
    {
        const double reversed = Reverse(i), angle = 2 * kPi * i / 1024;
        const Vector local = Unit({frame.ax * std::sqrt(reversed) * std::cos(angle),
                                   frame.ay * std::sqrt(reversed) * std::sin(angle), std::sqrt(1 - reversed)});
        const Vector half = frame.tangent * local[0] + frame.bitangent * local[1] + frame.normal * local[2];
        const double vh = Dot(view, half);
        if (vh <= 0)
        {
            continue;
        }
        const Vector light = SceneRefract(view * -1, half, 1 / glass.ior);
        const double nl = -Dot(glass.normal, light);
        if (nl > 0)
        {
            const Vector front = light - glass.normal * (2 * Dot(glass.normal, light));
            const double weight =
                4 * nl * Visibility(frame, view, front) * vh / std::max(Dot(frame.normal, half), 1e-6) / 1024;
            result = result + (Vector{1, 1, 1} - Fresnel(glass, vh)) * weight;
        }
    }
    return Nonnegative(result);
}

void RunSceneRefraction(RecordingChangeDevice& device, ProbeRoots& roots, ProbePipelines& pipelines,
                        ProbeTextures& textures, ProbePool& pool, const std::filesystem::path& root,
                        std::shared_ptr<Texture> image, std::shared_ptr<Texture> cube,
                        const std::shared_ptr<const Instance>& background)
{
    const std::array products{RefractionProduct(root, false), RefractionProduct(root, true),
                              RefractionProduct(root, false, true)};
    GenerationStore store;
    experiment::AssetId graphId;
    Check(Uuid::TryParse("88888888-8888-4888-8888-888888888888", graphId.value), "Refraction graph identity");
    std::string error;
    std::array<std::shared_ptr<const Generation>, 3> generations;
    for (unsigned i = 0; i < generations.size(); ++i)
    {
        generations[i] = store.Load(
            graphId,
            [&](CookedProgram& cooked, std::string&) {
                cooked = {products[i], WriteMaterialProgramMetadata(products[i].program),
                          BuildBoundSource(products[i].program)};
                return true;
            },
            true, error);
        Check(bool(generations[i]), "Refraction generation " + error);
    }
    SceneHost host;
    ProbeMeshes meshes;
    Check(meshes.Initialize(&device, error), "Refraction mesh cache");
    EnhancedGBufferPass gbuffer;
    EnhancedDeferredPass deferred;
    EnhancedForwardPass forward;
    EnhancedFrameContext context;
    context.resources = &device;
    context.rootSignatures = &roots;
    context.psoManager = &pipelines;
    context.textureCache = &textures;
    context.meshCache = &meshes;
    context.width = context.height = 24;
    FrameCameraSnapshot camera;
    camera.view = camera.projection = math::matrix4x4::identity();
    camera.projection.m[2][2] = .8f;
    camera.projection.m[3][2] = .2f;
    camera.projection.m[2][3] = 1;
    camera.projection.m[3][3] = 2;
    camera.eyePosition = math::vector3(0, 0, -2);
    context.camera = &camera;
    const std::vector<EnhancedLight> lights;
    context.lights = &lights;
    const std::vector<EnhancedDrawItem> forwardDraws;
    context.forwardDraws = &forwardDraws;
    Check(gbuffer.Initialize(context, error) && deferred.Initialize(context, error) && forward.Initialize(context, error), "Refraction actual Scene passes");
    WaitSceneProgram(host, context, background->generation);
    for (const auto& generation : generations)
    {
        WaitSceneProgram(host, context, generation);
    }
    std::uint64_t frames{}, pixels{}, hits{}, misses{}, shifted{}, tir{}, maskedHoles{}, hybridPixels{}, failures{},
        roughChecks{}, roughMixed{};
    for (unsigned fixture = 0; fixture < 8; ++fixture)
    {
        for (unsigned workers : {0u, 1u, 4u})
        {
            Drain drain{device};
            context.frameId = 6000 + frames;
            context.sceneEpoch = 81;
            const double ior = fixture == 0 ? 1 : 1.5;
            const double roughness = fixture == 2 ? .5 : fixture == 6 ? .2 : 0;
            const double transmission = fixture == 6 ? .5 : 1;
            std::shared_ptr<const Instance> instance;
            Check(BuildInstance(
                      generations[fixture == 6   ? 1
                                  : fixture == 7 ? 2
                                                 : 0],
                      {graphId, {{914, ior}, {915, roughness}, {916, transmission}}, {}},
                      [&](const experiment::AssetId&, LXColorSpace, std::string&) { return image; }, instance, error),
                  "Refraction instance " + error);
            auto glass = MakeGeometry(false, fixture != 3), backdrop = MakeGeometry(false, true),
                 legacy = MakeGeometry(false, true);
            Vector normal = fixture == 3   ? Unit({.7, 0, -.714142843})
                            : fixture == 0 ? Vector{0, 0, -1}
                                           : Vector{.6, 0, -.8};
            const auto plane = [&](Geometry& geometry, float left, float right, float bottom, float top, float z,
                                   std::uint64_t key, Vector vertexNormal) {
                const std::array<std::array<float, 2>, 6> xy{
                    {{left, bottom}, {right, bottom}, {left, top}, {left, top}, {right, bottom}, {right, top}}};
                auto& source = geometry.draw.modelMeshView;
                source.indexData = geometry.indices.data();
                source.handle.generation = key;
                geometry.draw.geometryKey = key;
                geometry.draw.coverage.flags = EnhancedMaterialCoverage::Enabled;
                const float w = z + 2;
                for (unsigned v = 0; v < 6; ++v)
                {
                    geometry.points[v].position = {xy[v][0] * w, xy[v][1] * w, z, 0};
                    geometry.points[v].uvLod = {(xy[v][0] + 1) * .5f, (xy[v][1] + 1) * .5f, 0, 0};
                    auto* vertex = geometry.vertices.data() + v * source.vertexStride;
                    const std::array<float, 3> n{float(vertexNormal[0]), float(vertexNormal[1]),
                                                 float(vertexNormal[2])};
                    std::memcpy(vertex, geometry.points[v].position.data(), 12);
                    std::memcpy(vertex + assets::OffsetOf(source.vertexAttributeMask, assets::VertexAttribute::Normal),
                                n.data(), 12);
                    std::memcpy(vertex + assets::OffsetOf(source.vertexAttributeMask, assets::VertexAttribute::Uv0),
                                geometry.points[v].uvLod.data(), 8);
                }
            };
            plane(glass, -.75f, .75f, -.75f, .75f, .25f, 7101, fixture == 3 ? normal * -1 : normal);
            plane(backdrop, fixture == 2 ? -2.f : -.9375f,
                  fixture == 4   ? -.125f
                  : fixture == 2 ? 2.f
                                 : .9375f,
                  fixture == 2 ? -2.f : -.9375f, fixture == 2 ? 2.f : .9375f, .75f, 7102, {0, 0, 1});
            plane(legacy, -.25f, .25f, -.25f, .25f, .1f, 7103, {0, 0, 1});
            glass.draw.materialGraphInstance = instance;
            backdrop.draw.materialGraphInstance = background;
            glass.draw.materialGraphSlot = 31;
            backdrop.draw.materialGraphSlot = 32;
            if (fixture == 3)
            {
                glass.draw.coverage.flags |= EnhancedMaterialCoverage::DoubleSided;
            }
            if (fixture == 5)
            {
                glass.draw.coverage.flags |= EnhancedMaterialCoverage::Masked;
            }
            Check(Uuid::TryParse("44444444-4444-8444-8444-444444444444", legacy.draw.modelMeshView.handle.meshId),
                  "Refraction legacy identity");
            std::vector<EnhancedDrawItem> oldDraws{legacy.draw};
            if (fixture == 2)
            {
                oldDraws.clear();
            }
            context.draws = &oldDraws;
            const std::array draws{glass.draw, backdrop.draw};
            std::shared_ptr<const SceneViewInput> input;
            Check(SceneViewInput::Seal({context.frameId, context.sceneEpoch, 92, 1, 24, 24, camera}, draws, {}, input,
                                       error),
                  "Refraction Scene seal " + error);
            Check(device.BeginFrame(error), "Refraction frame begin");
            textures.BeginFrame(context.frameId);
            meshes.BeginFrame(context.frameId);
            const auto environment = fixture == 4 ? RHITextureHandle{} : textures.GetOrUpload(cube.get(), error).handle;
            Check(host.PrepareResidency(context, input, error), "Refraction residency " + error);
            Check(gbuffer.PrepareFrame(context, error) && deferred.PrepareFrame(context, error) && forward.PrepareFrame(context, error),
                  "Refraction Scene prepare " + error);
            auto graph = std::make_shared<EnhancedRenderGraph>(device);
            if (workers)
            {
                pool.BeginFrame(static_cast<std::uint32_t>(context.frameId));
                Check(graph->PrepareParallel(pool, error), "Refraction parallel prefix " + error);
            }
            SceneHostBudget budget;
             Check(host.Prepare(context, input, environment, {}, {}, {}, budget, error, environment.IsValid() ? 1 : 0),
                  "Refraction prepare " + error);
            const auto accepted = host.RefractionFrame();
            Check(bool(accepted), "Refraction frame exists");
            budget.refractionBytes = 1;
             Check(!host.Prepare(context, input, environment, {}, {}, {}, budget, error, environment.IsValid() ? 1 : 0) &&
                      host.RefractionFrame() == accepted,
                  "Refraction budget failure preserves prior frame");
            ++failures;
            gbuffer.Declare(*graph, context);
            const auto outputs = gbuffer.GetOutputs();
            host.DeclareGBuffer(*graph, outputs);
            RGTextureDesc aoDesc;
            aoDesc.width = aoDesc.height = 24;
            aoDesc.format = RHIFormat::RG16Float;
            aoDesc.allowRenderTarget = true;
            aoDesc.clearColor[0] = .25f;
            const auto ao = graph->CreateTexture(aoDesc);
            graph->AddPass("Probe.Refraction.AO", {{ao, RHIResourceState::RenderTarget}}, [&](const auto& execution) {
                const auto texture = execution.ResolveHandle(ao);
                const auto target = device.CreateRenderTargets({&texture, 1}, nullptr);
                const float clear[]{.25f, 0, 0, 0};
                execution.encoder->BindRenderTargets(target);
                execution.encoder->ClearRenderTargets(target, clear);
            });
            deferred.SetInputs(outputs);
            deferred.SetAmbientOcclusion(ao);
            deferred.Declare(*graph, context);
            host.DeclareColor(*graph, outputs, deferred.GetOutput(), ao, {});
            forward.SetInputs({outputs.depth, deferred.GetOutput()});
            forward.SetGraphMaterials(&host);
            forward.Declare(*graph, context);
            const auto surfaceOutputs = host.ForwardSurfaceOutputs();
            std::array<RHIReadback, 11> readbacks;
            const auto copy = [&](unsigned i, RGHandle handle, RHIFormat format) {
                Check(device.CreateReadback(24, 24, format, 1, readbacks[i], error), "Refraction readback");
                graph->AddPass(
                    "Probe.Refraction.Texture", {{handle, RHIResourceState::CopySource}},
                    [readback = readbacks[i], handle](const auto& execution) {
                        execution.encoder->CopyToReadback(readback, execution.ResolveHandle(handle));
                    },
                    true);
            };
            copy(0, accepted->GraphBackgroundColor(*graph), RHIFormat::RGBA16Float);
            copy(1, accepted->GraphBackgroundDepth(*graph), RHIFormat::D32Float);
            copy(2, forward.GetOutput(), RHIFormat::RGBA16Float);
            copy(3, surfaceOutputs.depth, RHIFormat::D32Float);
            copy(4, surfaceOutputs.bitmask, RHIFormat::R32Uint);
            copy(10, outputs.depth, RHIFormat::D32Float);
            copy(5, graph->FindImportedTexture(accepted->Inputs()[0]), RHIFormat::RGBA32Float);
            copy(6, graph->FindImportedTexture(accepted->Inputs()[1]), RHIFormat::RGBA32Float);
            copy(8, graph->FindImportedTexture(host.ForwardLookupFrame()->Inputs()[1]), RHIFormat::RGBA32Float);
            copy(9, graph->FindImportedTexture(host.ForwardLookupFrame()->Inputs()[10]), RHIFormat::RGBA32Float);
            Check(device.CreateBufferReadback(24 * 24 * sizeof(SceneRefractionSample), readbacks[7], error),
                  "Refraction samples readback");
            graph->AddPass(
                "Probe.Refraction.Samples", {{accepted->GraphSamples(*graph), RHIResourceState::CopySource}},
                [readback = readbacks[7], buffer = accepted->Samples()](const auto& execution) {
                    execution.encoder->CopyBufferToReadback(readback, buffer);
                },
                true);
            Check(graph->Compile(error), "Refraction graph compile " + error);
            RHICompletionPoint completion;
            RHISubmissionTicket ticket;
            if (workers)
            {
                graph->SetParallelRecordCostThreshold(0);
                RHIRecordedBatchDesc desc;
                desc.frameId = context.frameId;
                desc.backendGeneration = GetRHISubmissionThread().GetOwnerGeneration(&device);
                desc.lifetimeToken = graph;
                RHIRecordedBatch batch;
                Check(
                    graph->RecordParallel(pool, workers, desc, batch, error) &&
                        GetRHISubmissionThread().EnqueueRecordedBatch(&device, device, std::move(batch), ticket, error),
                    "Refraction native parallel " + error);
                completion = ticket.GetRecordedBatch()->GetCompletionPoint();
            }
            else
            {
                Check(graph->Execute(error), "Refraction sequential " + error);
            }
            Check(device.EndFrame(error) && GetRHISubmissionThread().DrainSubmissions(&device, error),
                  "Refraction submit");
            if (!workers)
            {
                completion = {device.GetLastSignaledFenceValue()};
            }
            Check(host.PublishSubmittedCache(context.frameId, completion, error, ticket),
                  "Refraction publication " + error);
            device.WaitForGpu();
            Check(GetRHISubmissionThread().Drain(&device, error), "Refraction retirement");
            std::array<RHIReadbackImage, 11> mapped;
            for (unsigned i = 0; i < mapped.size(); ++i)
            {
                Check(device.MapReadback(readbacks[i], mapped[i], error), "Refraction map");
            }
            for (unsigned y = 0; y < 24; ++y)
            {
                for (unsigned x = 0; x < 24; ++x)
                {
                    Near(mapped[10].At(x,y,0), mapped[1].At(x,y,0), "Refraction preserves shared opaque depth");
                    std::uint32_t owner;
                    std::memcpy(&owner, mapped[4].data.data() + y * mapped[4].rowPitch + x * 4, 4);
                    if (fixture == 5 && x >= 3 && x < 21 && y >= 3 && y < 21 && !(x >= 9 && x < 15 && y >= 9 && y < 15))
                    {
                        const unsigned tx = unsigned((x + .5) / 6), ty = unsigned(2 - (y + .5) / 12);
                        const bool visible = kTexels[ty * 4 + tx][3] >= 128;
                        Check((owner == 0x80000001u) == visible, "Refraction masked coverage matches texture alpha");
                    }
                    if (owner != 0x80000001u)
                    {
                        Check(std::memcmp(mapped[0].data.data() + y * mapped[0].rowPitch + x * 8,
                                          mapped[2].data.data() + y * mapped[2].rowPitch + x * 8, 8) == 0,
                              "Refraction preserves opaque HDR exactly");
                        Near(mapped[3].At(x, y, 0), mapped[1].At(x, y, 0), "Refraction preserves opaque depth");
                        if (fixture == 5 && x >= 3 && x < 21 && y >= 3 && y < 21 &&
                            !(x >= 9 && x < 15 && y >= 9 && y < 15))
                        {
                            ++maskedHoles;
                        }
                        continue;
                    }
                    Near(mapped[3].At(x, y, 0), .4 / 2.25, "Refraction scratch depth is transmitting surface");
                    Near(mapped[5].At(x, y, 3), fixture == 3 ? 1 / ior : ior, "Refraction face-dependent medium ratio");
                    Vector position{};
                    for (unsigned c = 0; c < 3; ++c)
                    {
                        position[c] = mapped[5].At(x, y, c);
                        Near(mapped[6].At(x, y, c), normal[c], "Refraction oriented normal");
                    }
                    const Vector view = Unit(Vector{0, 0, -2} - position);
                    for (unsigned c = 0; c < 3; ++c)
                    {
                        Near(mapped[8].At(x, y, c), normal[c], "Refraction lookup normal");
                        Near(mapped[9].At(x, y, c), view[c], "Refraction lookup view");
                    }
                    ProbeInput source{};
                    source.baseAlpha = {1, 1, 1, 1};
                    source.normalRoughness = {float(normal[0]), float(normal[1]), float(normal[2]), float(roughness)};
                    source.metalIorLevelAo = {fixture == 6 ? .3f : 0, float(ior), fixture == 7 ? 0.f : .5f, 1};
                    source.tintAnisotropy = {1, 1, 1, 0};
                    if (fixture == 7)
                    {
                        source.tintAnisotropy = {.5f, .7f, .9f, 0};
                        source.coatWeightRoughIorFilmThickness.w = 420;
                        source.coatTintFilmIor.w = 1.33f;
                    }
                    const Vector tangent = Unit(Vector{1, 0, 0} - normal * normal[0]);
                    source.tangentRotation = {float(tangent[0]), float(tangent[1]), float(tangent[2]), 0};
                    auto material = Evaluate(source, 0x7ff), glassMaterial = material;
                    glassMaterial.metal = 0;
                    glassMaterial.ior = fixture == 3 ? 1 / ior : ior;
                    glassMaterial.level = .5;
                    const auto integral = Integrate(glassMaterial, view, false, false);
                    const auto raw = SceneTransmissionIntegral(glassMaterial, view);
                    SceneRefractionSample sample;
                    std::memcpy(&sample, mapped[7].data.data() + (y * 24 + x) * sizeof(sample), sizeof(sample));
                    Vector refracted{};
                    for (unsigned c = 0; c < 3; ++c)
                    {
                        Near(sample.glassSingleAlbedo[c], integral.single[c], "Independent glass integral", 2e-4);
                        Near(sample.rawTransmission[c], raw[c], "Independent BTDF normalization", 2e-4);
                        refracted[c] = sample.radiance[c];
                        Check(std::isfinite(refracted[c]) && refracted[c] >= 0, "Finite refracted radiance");
                    }
                    Near(sample.glassAverage.w, 1,
                         "Refraction valid sample fixture=" + std::to_string(fixture) + " pixel=" + std::to_string(x) +
                             "," + std::to_string(y));
                    if (sample.radiance.w > 0)
                    {
                        ++hits;
                    }
                    else
                    {
                        ++misses;
                    }
                    const Vector direction = SceneRefract(view * -1, normal, 1 / glassMaterial.ior);
                    if (fixture == 3 && Dot(direction, direction) == 0)
                    {
                        for (unsigned c = 0; c < 3; ++c)
                        {
                            Near(sample.radiance[c], 0, "Total internal reflection has no transmitted light");
                        }
                        ++tir;
                    }
                    if (fixture == 0)
                    {
                        for (unsigned c = 0; c < 3; ++c)
                        {
                            Near(sample.radiance[c], mapped[0].At(x, y, c), "IOR one samples exact opaque pixel", 1e-3);
                        }
                    }
                    // The independent smooth-ray intersection uses the fixture
                    // plane at z=.75; screen quantization boundaries are excluded.
                    if ((fixture == 1 || fixture == 5) && direction[2] > 0)
                    {
                        const Vector hit = position + direction * ((.75 - position[2]) / direction[2]);
                        const double sx = (hit[0] / 2.75 * .5 + .5) * 24;
                        const double sy = (.5 - hit[1] / 2.75 * .5) * 24;
                        if (sx >= 1 && sx < 23 && sy >= 1 && sy < 23 && std::abs(sx - std::round(sx)) > .02 &&
                            std::abs(sy - std::round(sy)) > .02 && !(sx >= 9 && sx < 15 && sy >= 9 && sy < 15))
                        {
                            for (unsigned c = 0; c < 3; ++c)
                            {
                                Near(sample.radiance[c], mapped[0].At(unsigned(sx), unsigned(sy), c),
                                     "Independent refracted backdrop intersection", 1e-3);
                            }
                            if (unsigned(sx) != x || unsigned(sy) != y)
                            {
                                ++shifted;
                            }
                        }
                    }
                    if (fixture == 4 && sample.radiance.w == 0)
                    {
                        for (unsigned c = 0; c < 3; ++c)
                        {
                            Near(sample.radiance[c], 0, "No environment screen miss is black");
                        }
                    }
                    if ((fixture == 1 || fixture == 5) && sample.radiance.w == 0 && Dot(direction, direction) > 0)
                    {
                        const Vector environmentColor{2, 1, .5};
                        for (unsigned c = 0; c < 3; ++c)
                        {
                            Near(sample.radiance[c], environmentColor[c], "Screen miss samples current environment");
                        }
                    }
                    if (fixture == 2)
                    {
                        const auto frame = MakeFrame(glassMaterial, false);
                        Vector numerator{}, denominator{};
                        double expectedHits = 0;
                        bool ambiguous = false;
                        for (unsigned ray = 0; ray < 32; ++ray)
                        {
                            const double reversed = Reverse(ray), angle = 2 * kPi * ray / 32;
                            const Vector local =
                                Unit({frame.ax * std::sqrt(reversed) * std::cos(angle),
                                      frame.ay * std::sqrt(reversed) * std::sin(angle), std::sqrt(1 - reversed)});
                            const Vector half =
                                frame.tangent * local[0] + frame.bitangent * local[1] + frame.normal * local[2];
                            const double vh = Dot(view, half);
                            const Vector rayDirection = SceneRefract(view * -1, half, 1 / glassMaterial.ior);
                            const double nl = -Dot(normal, rayDirection);
                            if (vh <= 0 || nl <= 0)
                            {
                                continue;
                            }
                            const Vector front = rayDirection - normal * (2 * Dot(normal, rayDirection));
                            const double weight =
                                4 * nl * Visibility(frame, view, front) * vh / std::max(Dot(normal, half), 1e-6);
                            const Vector response = (Vector{1, 1, 1} - Fresnel(glassMaterial, vh)) * weight;
                            Vector radiance{2, 1, .5};
                            if (rayDirection[2] > 0)
                            {
                                const Vector point = position + rayDirection * ((.75 - position[2]) / rayDirection[2]);
                                const double sx = (point[0] / 2.75 * .5 + .5) * 24;
                                const double sy = (.5 - point[1] / 2.75 * .5) * 24;
                                // Occluder crossings and native texel boundaries
                                // are excluded from this analytic plane fixture.
                                ambiguous |= std::abs(sx - std::round(sx)) < .02 || std::abs(sy - std::round(sy)) < .02;
                                if (sx >= 0 && sx < 24 && sy >= 0 && sy < 24)
                                {
                                    for (unsigned c = 0; c < 3; ++c)
                                    {
                                        radiance[c] = mapped[0].At(unsigned(sx), unsigned(sy), c);
                                    }
                                    expectedHits += 1.0 / 32;
                                }
                            }
                            numerator = numerator + response * radiance;
                            denominator = denominator + response;
                        }
                        if (!ambiguous)
                        {
                            for (unsigned c = 0; c < 3; ++c)
                            {
                                Near(sample.radiance[c], denominator[c] > 0 ? numerator[c] / denominator[c] : 0,
                                     "Independent rough glass radiance convolution", 1e-3);
                            }
                            Near(sample.radiance.w, expectedHits, "Independent rough glass hit fraction");
                            ++roughChecks;
                        }
                        roughMixed += sample.radiance.w > 0 && sample.radiance.w < 1;
                    }
                    auto metal = material, dielectric = material;
                    metal.metal = 1;
                    dielectric.metal = 0;
                    const auto mi = Integrate(metal, view, false), di = Integrate(dielectric, view, false);
                    const Vector gm = Multiple(integral);
                    const double m = fixture == 6 ? .3 : 0, sss = fixture == 6 ? .4 : 0;
                    const double dw = (1 - m) * (1 - transmission), gw = (1 - m) * transmission;
                    // Base reflection uses the common multiplicative GGX budget;
                    // only glass keeps the single-interface multiple-scatter term.
                    const double remaining = std::max(1 - Maximum(Energy(material, view).dielectricAlbedo), 0.0);
                    const Vector normalization = ReflectionBudget(
                        material.base * ((1 - m) * remaining), mi.single * m + di.single * (1 - m));
                    const Vector environmentColor = fixture == 4 ? Vector{} : Vector{2, 1, .5};
                    const double specularAo = 1;
                    Vector result =
                        (normalization * (mi.single * m + di.single * dw) + integral.single * gw) *
                        environmentColor * specularAo;
                    result = result + (gm * gw + Vector{1, 1, 1} * (dw * remaining * (1 - sss))) *
                                          environmentColor;
                    result = result + Vector{1, 1, 1} * (dw * remaining * sss) * environmentColor;
                    for (unsigned c = 0; c < 3; ++c)
                    {
                        result[c] += gw * std::max(1 - integral.single[c] - gm[c], 0.0) * refracted[c];
                        Near(mapped[2].At(x, y, c), result[c], "Independent Special Scene composition", 1.5e-3);
                    }
                    hybridPixels += fixture == 6;
                    ++pixels;
                }
            }
            for (auto& readback : readbacks)
            {
                device.ReleaseReadback(readback);
            }
            graph->Reset();
            bool rejected = false;
            try
            {
                accepted->GraphSamples(*graph);
            }
            catch (const std::runtime_error&)
            {
                rejected = true;
            }
            Check(rejected, "Refraction frame cannot cross a graph reset");
            ++failures;
            graph.reset();
            ++frames;
            std::cout << "LX_REFRACTION_FIXTURE_OK fixture=" << fixture << " workers=" << workers << '\n';
        }
    }
    Check(pixels && hits && misses && shifted && tir && maskedHoles && hybridPixels && failures && roughChecks &&
              roughMixed,
          "Refraction fixture coverage");
    host.ShutdownAfterIdle();
    gbuffer.Shutdown();
    deferred.Shutdown();
    forward.Shutdown();
    meshes.Shutdown();
    std::string messages;
    const auto validation = device.DrainDebugMessages(messages);
    Check(validation == 0, "Refraction native GPU validation: " + messages);
    std::cout << "LX_MATERIAL_SCENE_REFRACTION_OK frames=" << frames << " pixels=" << pixels << " hits=" << hits
              << " misses=" << misses << " shifted=" << shifted << " tir=" << tir << " maskedHoles=" << maskedHoles
              << " hybridPixels=" << hybridPixels << " failures=" << failures << " roughChecks=" << roughChecks
              << " roughMixed=" << roughMixed << " checks=" << checks << " gpuComponents=" << gpuComponents
              << " validation=0\n";
}
