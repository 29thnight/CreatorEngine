VerifiedProduct VolumeProduct(const std::filesystem::path& root, bool surface)
{
    LXMaterialAsset asset;
    const auto volume = asset.CreateNode("LXPrincipledVolume", 0, 0);
    const auto output = asset.CreateNode("ShaderNodeOutputMaterial", 400, 0);
    asset.activeOutput = output;
    asset.blackboard = {{1011, "density", "Density", PinType::Float, 1.0},
                        {1012, "emission", "Emission", PinType::Float, 0.0},
                        {1013, "color", "Color", PinType::Color, std::array<double, 4>{0, 0, 0, 1}}};
    const auto connect = [&](Id from, const char* fromName, Id to, const char* toName) {
        Check(asset.graph
                  .Connect(Pin(asset.graph, from, fromName, Direction::Output),
                           Pin(asset.graph, to, toName, Direction::Input))
                  .has_value(),
              "Volume link");
    };
    for (unsigned i = 0; i < 3; ++i)
    {
        const auto parameter = asset.CreateNode(i == 2 ? "LXParameterColor" : "LXParameterFloat", -200, float(i * 100));
        Check(asset.graph.SetProperty(parameter, "parameter", std::to_string(1011 + i)), "Volume parameter");
        const char* names[]{"Density", "Emission Strength", "Color"};
        connect(parameter, "Value", volume, names[i]);
    }
    asset.graph.SetSocketValue(Pin(asset.graph, volume, "Emission Color", Direction::Input),
                               std::array<double, 4>{.7, .4, .2, 1});
    asset.graph.SetSocketValue(Pin(asset.graph, volume, "Anisotropy", Direction::Input), .4);
    connect(volume, "Volume", output, "Volume");
    if (surface)
    {
        const auto bsdf = asset.CreateNode("ShaderNodeBsdfPrincipled", 0, -200);
        for (const auto& [name, value] :
             std::array<std::pair<const char*, double>, 3>{{{"IOR", 1}, {"Roughness", 0}, {"Transmission Weight", 1}}})
        {
            asset.graph.SetSocketValue(Pin(asset.graph, bsdf, name, Direction::Input), value);
        }
        asset.graph.SetSocketValue(Pin(asset.graph, bsdf, "Base Color", Direction::Input),
                                   std::array<double, 4>{1, 1, 1, 1});
        connect(bsdf, "BSDF", output, "Surface");
    }
    std::vector<LXMaterialDiagnostic> diagnostics;
    const auto program = GenerateMaterialSlang(asset, &diagnostics);
    Check(program && program->volume && program->slang.find("LX_MATERIAL_VOLUME_HOMOGENEOUS 1") != std::string::npos,
          "Uniform Volume classification");
    const auto file =
        root / "Build/Obj/MaterialProductProbe" / (surface ? "volume-surface-product.slang" : "volume-product.slang");
    std::ofstream(file, std::ios::binary | std::ios::trunc)
        << BuildBoundSource(*program) << "\n#include \"MaterialGraphSceneHost.slang\"\n"
        << "#include \"MaterialGraphSceneVolumeCoefficients.slang\"\n";
    std::vector<CompileTarget> targets;
    for (auto backend : {RHIShaderBinary::Dxil, RHIShaderBinary::SpirV})
    {
        if (surface)
        {
            targets.push_back({backend, "LXSceneVS", "vs_6_0"});
            targets.push_back({backend, "LXSceneColorPS", "ps_6_0"});
        }
        targets.push_back({backend, "LXSceneVolumeCoefficientCS", "cs_6_0"});
    }
    RHIShaderCompileOptions options;
    options.strictMath = options.fineDerivatives = true;
    options.includeDirectories.push_back(root / "Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes");
    Capabilities capabilities;
    capabilities.coreForward = capabilities.layeredLookup = capabilities.refraction = capabilities.volume = true;
    RHIShaderPermutation permutation;
    std::string error;
    Check(permutation.Set("LX_MATERIAL_ROUTE", "2", error) && permutation.Enable("LX_MATERIAL_PIXEL_FOOTPRINT", error),
          "Volume permutation");
    VerifiedProduct product;
    for (const auto& target : targets)
    {
        RHIShaderCompiler::VerifiedShader shader;
        const bool compiled = RHIShaderCompiler::VerifyFile(file.string(), target.entry, target.profile, target.binary,
                                                            permutation, shader, error, options);
        Check(compiled, "Volume shader " + target.entry + ": " + error);
    }
    const bool verified =
        VerifyProduct(*program, file, targets, permutation, options, capabilities, {}, product, diagnostics);
    for (const auto& diagnostic : diagnostics)
    {
        error += diagnostic.message + "\n";
    }
    Check(verified, "Volume product verification " + error);
    return product;
}

struct VolumeGeometry
{
    std::vector<std::byte> vertices;
    std::vector<std::uint32_t> indices;
    EnhancedDrawItem draw;
};

LXMaterialProgram VolumeContextProgram(bool spatialVolume)
{
    LXMaterialAsset asset;
    const auto volume = asset.CreateNode("LXPrincipledVolume", 0, 0);
    const auto image = asset.CreateNode("ShaderNodeTexImage", -400, 0);
    const auto multiply = asset.CreateNode("LXMultiplyFloat", -200, 0);
    const auto surface = asset.CreateNode("ShaderNodeBsdfPrincipled", 0, 200);
    const auto output = asset.CreateNode("ShaderNodeOutputMaterial", 400, 0);
    asset.activeOutput = output;
    Check(asset.graph.SetProperty(image, "image", "22222222-2222-4222-8222-222222222222"),
          "Context classification image");
    const auto connect = [&](Id from, const char* fromName, Id to, const char* toName) {
        Check(asset.graph
                  .Connect(Pin(asset.graph, from, fromName, Direction::Output),
                           Pin(asset.graph, to, toName, Direction::Input))
                  .has_value(),
              "Context classification link");
    };
    connect(image, "Color", surface, "Base Color");
    connect(surface, "BSDF", output, "Surface");
    connect(volume, "Volume", output, "Volume");
    if (spatialVolume)
    {
        connect(image, "Alpha", multiply, "A");
        connect(multiply, "Result", volume, "Density");
    }
    const auto program = GenerateMaterialSlang(asset);
    Check(program && program->slang.find(spatialVolume ? "LX_MATERIAL_VOLUME_HOMOGENEOUS 0"
                                                       : "LX_MATERIAL_VOLUME_HOMOGENEOUS 1") != std::string::npos,
          "Context dependency propagates through numeric operations and excludes independent Surface");
    return *program;
}

void VolumeReflectionContracts()
{
    RHIShaderResourceReflection uniform;
    uniform.name = "LXMaterialProperties";
    uniform.registerIndex = 2;
    uniform.byteSize = 16;
    uniform.fields = {{"lx_bound_p1", {}, 0, 4}};
    RHIShaderResourceReflection texture;
    texture.name = "lx_texture_0";
    texture.kind = RHIShaderResourceKind::Texture;
    texture.registerIndex = 16;
    texture.registerSpace = 1;
    RHIShaderReflection surface{RHIShaderStage::Pixel, {uniform, texture}};
    RHIShaderReflection volume{RHIShaderStage::Compute, {uniform}};
    const RHIShaderReflection* stages[]{&surface, &volume};
    RHIShaderReflection merged;
    std::vector<LXMaterialDiagnostic> diagnostics;
    Check(MergeMaterialReflections(stages, merged, diagnostics) && merged.resources.size() == 2,
          "Stage union retains Surface-only resources");
    const auto accepted = merged;
    volume.resources[0].fields[0].byteOffset = 4;
    Check(!MergeMaterialReflections(stages, merged, diagnostics) && merged == accepted &&
              diagnostics.back().code == "product.stageLayout",
          "Conflicting stage field offsets preserve accepted reflection");
}

VolumeGeometry VolumeBoxes(std::span<const std::array<float, 2>> depths, std::uint64_t key, bool seams)
{
    VolumeGeometry geometry;
    auto& source = geometry.draw.modelMeshView;
    Check(Uuid::TryParse("99999999-9999-8999-8999-999999999999", source.handle.modelId) &&
              Uuid::TryParse("AAAAAAAA-AAAA-8AAA-8AAA-AAAAAAAAAAAA", source.handle.meshId),
          "Volume mesh identity");
    source.handle.generation = key;
    source.vertexAttributeMask = assets::kModelVertexMasks.front();
    source.vertexStride = assets::StrideOf(source.vertexAttributeMask);
    source.vertexLayoutHash = assets::VertexLayoutHash(source.vertexAttributeMask);
    geometry.draw.worldMatrix = math::matrix4x4::identity();
    geometry.draw.geometryKey = key;
    geometry.draw.coverage.flags = EnhancedMaterialCoverage::Enabled | EnhancedMaterialCoverage::DoubleSided;
    for (const auto depth : depths)
    {
        const std::array<std::array<float, 3>, 8> positions{{{-.75f, -.75f, depth[0]},
                                                             {.75f, -.75f, depth[0]},
                                                             {.75f, .75f, depth[0]},
                                                             {-.75f, .75f, depth[0]},
                                                             {-.75f, -.75f, depth[1]},
                                                             {.75f, -.75f, depth[1]},
                                                             {.75f, .75f, depth[1]},
                                                             {-.75f, .75f, depth[1]}}};
        const std::array<unsigned, 36> triangles{0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4,
                                                 3, 7, 6, 3, 6, 2, 0, 4, 7, 0, 7, 3, 1, 2, 6, 1, 6, 5};
        const auto base = static_cast<unsigned>(geometry.vertices.size() / source.vertexStride);
        const unsigned count = seams ? 36 : 8;
        geometry.vertices.resize(geometry.vertices.size() + count * source.vertexStride);
        for (unsigned i = 0; i < count; ++i)
        {
            const auto index = seams ? triangles[i] : i;
            auto* vertex = geometry.vertices.data() + (base + i) * source.vertexStride;
            const std::array<float, 3> normal{0, 0, index < 4 ? -1.f : 1.f};
            const std::array<float, 4> tangent{1, 0, 0, 1};
            std::memcpy(vertex, positions[index].data(), 12);
            std::memcpy(vertex + assets::OffsetOf(source.vertexAttributeMask, assets::VertexAttribute::Normal),
                        normal.data(), 12);
            std::memcpy(vertex + assets::OffsetOf(source.vertexAttributeMask, assets::VertexAttribute::Tangent),
                        tangent.data(), 16);
        }
        for (unsigned i = 0; i < 36; ++i)
        {
            geometry.indices.push_back(base + (seams ? i : triangles[i]));
        }
    }
    source.vertexData = geometry.vertices.data();
    source.vertexBytes = geometry.vertices.size();
    source.indexData = geometry.indices.data();
    source.indexCount = static_cast<unsigned>(geometry.indices.size());
    return geometry;
}

void RunSceneVolume(RecordingChangeDevice& device, ProbeRoots& roots, ProbePipelines& pipelines,
                    ProbeTextures& textures, ProbePool& pool, const std::filesystem::path& root,
                    const std::shared_ptr<Texture>& image, const std::shared_ptr<Texture>& cube,
                    const std::shared_ptr<const Instance>& background)
{
    const std::array products{VolumeProduct(root, false), VolumeProduct(root, true)};
    GenerationStore store;
    experiment::AssetId graphId;
    Check(Uuid::TryParse("BBBBBBBB-BBBB-4BBB-8BBB-BBBBBBBBBBBB", graphId.value), "Volume graph identity");
    std::string error;
    std::array<std::shared_ptr<const Generation>, 2> generations;
    for (unsigned i = 0; i < 2; ++i)
    {
        generations[i] = store.Load(
            graphId,
            [&](CookedProgram& cooked, std::string&) {
                cooked = {products[i], WriteMaterialProgramMetadata(products[i].program),
                          BuildBoundSource(products[i].program)};
                return true;
            },
            true, error);
        Check(bool(generations[i]), "Volume generation " + error);
    }
    SceneHost host;
    ProbeMeshes meshes;
    Check(meshes.Initialize(&device, error), "Volume mesh cache");
    EnhancedGBufferPass gbuffer;
    EnhancedDeferredPass deferred;
    EnhancedFrameContext context;
    context.resources = &device;
    context.rootSignatures = &roots;
    context.psoManager = &pipelines;
    context.textureCache = &textures;
    context.meshCache = &meshes;
    context.width = context.height = 16;
    FrameCameraSnapshot camera;
    camera.view = camera.projection = math::matrix4x4::identity();
    camera.eyePosition = math::vector3(0, 0, -2);
    context.camera = &camera;
    VolumeReflectionContracts();
    VolumeContextProgram(false);
    auto spatial = std::make_shared<Generation>(*generations[0]);
    spatial->cooked.product.program = VolumeContextProgram(true);
    SceneHost rejectedHost;
    Check(!rejectedHost.RequestProgram(context, spatial, error) && error.find("homogeneous") != std::string::npos,
          "Spatial Volume is rejected before native preparation");
    Check(gbuffer.Initialize(context, error) && deferred.Initialize(context, error), "Volume actual Scene passes");
    WaitSceneProgram(host, context, background->generation);
    for (const auto& generation : generations)
    {
        WaitSceneProgram(host, context, generation);
    }
    std::uint64_t frames{}, pixels{}, failures{}, interior{}, scattering{};
    for (unsigned fixture = 0; fixture < 11; ++fixture)
    {
        for (unsigned workers : {0u, 1u, 4u})
        {
            std::cout << "LX_VOLUME_FIXTURE_BEGIN fixture=" << fixture << " workers=" << workers << std::endl;
            Drain drain{device};
            context.frameId = 7000 + frames;
            context.sceneEpoch = 91;
            const bool hybrid = fixture == 8;
            camera.projection = math::matrix4x4::identity();
            if (hybrid)
            {
                camera.projection.m[2][2] = .8f;
                camera.projection.m[3][2] = .2f;
                camera.projection.m[2][3] = 1;
                camera.projection.m[3][3] = 2;
            }
            const double emission = fixture == 1 || fixture == 4 ? 3 : 0;
            const double density = fixture == 9 ? 0 : 2;
            const double color = fixture == 6 || fixture == 7 || fixture == 10 ? .6 : 0;
            const auto instance = [&](double scale) {
                std::shared_ptr<const Instance> value;
                Check(BuildInstance(
                          generations[hybrid],
                          {graphId,
                           {{1011, density * scale},
                            {1012, emission},
                            {1013, std::array<double, 4>{color, color, color, 1}}},
                           {}},
                          [&](const experiment::AssetId&, LXColorSpace, std::string&) { return image; }, value, error),
                      "Volume instance " + error);
                return value;
            };
            std::vector<std::array<float, 2>> ranges{fixture == 2 ? std::array<float, 2>{-.2f, .4f}
                                                                  : std::array<float, 2>{.2f, .6f}};
            if (fixture == 4)
            {
                ranges = {{.1f, .3f}, {.5f, .7f}};
            }
            auto volume = VolumeBoxes(ranges, 8100 + fixture, fixture == 5);
            const std::array<std::array<float, 2>, 1> overlapRange{{{.4f, .8f}}};
            auto overlap = VolumeBoxes(overlapRange, 8200 + fixture, false);
            if (fixture == 5)
            {
                volume.draw.worldMatrix.m[0][0] = -1;
                volume.draw.worldMatrix.m[2][2] = 1.25f;
                ranges = {{.25f, .75f}};
            }
            volume.draw.materialGraphInstance = instance(1);
            overlap.draw.materialGraphInstance = instance(2);
            volume.draw.materialGraphSlot = 71;
            overlap.draw.materialGraphSlot = 72;
            auto backdrop = MakeGeometry(false, true), occluder = MakeGeometry(false, true);
            const auto plane = [&](Geometry& geometry, float left, float right, float depth, std::uint64_t key) {
                auto& source = geometry.draw.modelMeshView;
                source.indexData = geometry.indices.data();
                source.handle.generation = key;
                geometry.draw.geometryKey = key;
                geometry.draw.coverage.flags = EnhancedMaterialCoverage::Enabled;
                const std::array<std::array<float, 2>, 6> xy{
                    {{left, -1}, {right, -1}, {left, 1}, {left, 1}, {right, -1}, {right, 1}}};
                for (unsigned i = 0; i < 6; ++i)
                {
                    auto* vertex = geometry.vertices.data() + i * source.vertexStride;
                    const float scale = hybrid ? depth + 2 : 1;
                    const std::array<float, 3> position{xy[i][0] * scale, xy[i][1] * scale, depth};
                    const std::array<float, 2> uv{(xy[i][0] + 1) * .5f, (xy[i][1] + 1) * .5f};
                    std::memcpy(vertex, position.data(), 12);
                    std::memcpy(vertex + assets::OffsetOf(source.vertexAttributeMask, assets::VertexAttribute::Uv0),
                                uv.data(), 8);
                }
            };
            plane(backdrop, -1, 1, .9f, 8300);
            plane(occluder, -1, 0, .4f, 8301);
            backdrop.draw.materialGraphInstance = background;
            backdrop.draw.materialGraphSlot = 73;
            Check(Uuid::TryParse("CCCCCCCC-CCCC-8CCC-8CCC-CCCCCCCCCCCC", occluder.draw.modelMeshView.handle.meshId),
                  "Volume legacy occluder");
            std::vector<EnhancedDrawItem> oldDraws;
            if (fixture == 0 || fixture == 3)
            {
                oldDraws.push_back(occluder.draw);
            }
            context.draws = &oldDraws;
            std::vector<EnhancedLight> lights;
            if (fixture == 6 || fixture == 10)
            {
                EnhancedLight light{};
                light.position = {0, 0, 0, 0};
                light.direction = {0, 0, 1, 0};
                light.color = {2, 1, .5f, 1};
                lights.push_back(light);
            }
            context.lights = &lights;
            std::vector draws{volume.draw, backdrop.draw};
            if (fixture == 3)
            {
                draws.push_back(overlap.draw);
            }
            SceneInputBudget sealBudget;
            if (fixture == 5)
            {
                sealBudget.mesh.maxChunkPoints = 3;
            }
            std::shared_ptr<const SceneViewInput> input;
            const bool sealed = SceneViewInput::Seal({context.frameId, context.sceneEpoch, 93, 1, 16, 16, camera},
                                                     draws, sealBudget, input, error);
            Check(sealed, "Volume seal " + error);
            Check(device.BeginFrame(error), "Volume frame begin");
            textures.BeginFrame(context.frameId);
            meshes.BeginFrame(context.frameId);
            const auto environment =
                fixture != 6 && fixture != 10 ? textures.GetOrUpload(cube.get(), error).handle : RHITextureHandle{};
            Check(host.PrepareResidency(context, input, error), "Volume residency " + error);
            Check(gbuffer.PrepareFrame(context, error) && deferred.PrepareFrame(context, error),
                  "Volume Scene prepare " + error);
            auto graph = std::make_shared<EnhancedRenderGraph>(device);
            if (workers)
            {
                pool.BeginFrame(static_cast<unsigned>(context.frameId));
                Check(graph->PrepareParallel(pool, error), "Volume parallel prefix " + error);
            }
            SceneHostBudget budget;
            EnhancedShadowData shadow;
            RGHandle shadowMap;
            RHITextureHandle shadowTexture;
            if (fixture == 10)
            {
                shadow.enabled = true;
                shadow.splitDepths = {10, 20, 30, 0};
                shadow.cameraForward = {0, 0, 1, 0};
                for (auto& matrix : shadow.lightViewProjection)
                {
                    matrix = math::matrix4x4::identity();
                }
                RHITextureDesc desc;
                desc.width = desc.height = 4;
                desc.depthOrArraySize = 3;
                desc.format = RHIFormat::D32Float;
                desc.allowDepthStencil = true;
                desc.clearDepth = 0;
                Check(device.CreateTexture(desc, shadowTexture, error), "Volume shadow texture");
                shadowMap = graph->ImportTexture(shadowTexture, RHIResourceState::Common, "Probe.Volume.Shadow");
                graph->AddPass("Probe.Volume.Shadow", {{shadowMap, RHIResourceState::DepthWrite}},
                               [shadowMap, &device](const auto& execution) {
                                   for (unsigned slice = 0; slice < 3; ++slice)
                                   {
                                       const auto description = RHIDepthTargetDesc::DepthSlice(
                                           execution.ResolveHandle(shadowMap), RHIFormat::D32Float, slice);
                                       const auto target = device.CreateRenderTargets(
                                           std::span<const RHITextureHandle>{}, &description);
                                       execution.encoder->BindRenderTargets(target);
                                       execution.encoder->ClearDepthTarget(target, 0);
                                   }
                               });
            }
             Check(host.Prepare(context, input, environment, {}, {}, shadow, budget, error, environment.IsValid() ? 1 : 0),
                  "Volume prepare " + error);
            const auto accepted = host.VolumeFrame();
            Check(accepted && accepted->ObjectCount() == (fixture == 3 ? 2u : 1u), "Volume frame exists");
            budget.volumeBytes = 1;
             Check(!host.Prepare(context, input, environment, {}, {}, {}, budget, error, environment.IsValid() ? 1 : 0) &&
                      host.VolumeFrame() == accepted,
                  "Volume budget failure preserves accepted frame");
            ++failures;
            if (fixture == 0)
            {
                SceneHostBudget validBudget;
                auto open = volume.draw;
                open.modelMeshView.indexCount -= 3;
                open.geometryKey += 100;
                std::shared_ptr<const SceneViewInput> invalid;
                Check(SceneViewInput::Seal({context.frameId, context.sceneEpoch, 93, 1, 16, 16, camera}, {&open, 1}, {},
                                           invalid, error),
                      "Open boundary input remains structurally valid");
                 const bool prepared = host.Prepare(context, invalid, environment, {}, {}, {}, validBudget, error, 1);
                Check(!prepared && error.find("closed") != std::string::npos && host.VolumeFrame() == accepted,
                      "Open Volume boundary rejected without replacing accepted frame");
                std::vector<std::array<float, 2>> many(11, {.2f, .6f});
                auto excessive = VolumeBoxes(many, 9900, false);
                excessive.draw.materialGraphInstance = volume.draw.materialGraphInstance;
                Check(SceneViewInput::Seal({context.frameId, context.sceneEpoch, 93, 1, 16, 16, camera},
                                           {&excessive.draw, 1}, {}, invalid, error),
                      "Excessive Volume input seal");
                 const bool admitted = host.Prepare(context, invalid, environment, {}, {}, {}, validBudget, error, 1);
                Check(!admitted && error.find("128-triangle") != std::string::npos && host.VolumeFrame() == accepted,
                      "Exact intersection limit rejects overflow without truncation");
            }
            gbuffer.Declare(*graph, context);
            const auto outputs = gbuffer.GetOutputs();
            host.DeclareGBuffer(*graph, outputs);
            RGTextureDesc aoDesc;
            aoDesc.width = aoDesc.height = 16;
            aoDesc.format = RHIFormat::RG16Float;
            aoDesc.allowRenderTarget = true;
            aoDesc.clearColor[0] = 1;
            const auto ao = graph->CreateTexture(aoDesc);
            graph->AddPass("Probe.Volume.AO", {{ao, RHIResourceState::RenderTarget}}, [&](const auto& execution) {
                const auto texture = execution.ResolveHandle(ao);
                const auto target = device.CreateRenderTargets({&texture, 1}, nullptr);
                const float clear[]{1, 0, 0, 0};
                execution.encoder->BindRenderTargets(target);
                execution.encoder->ClearRenderTargets(target, clear);
            });
            deferred.SetInputs(outputs);
            deferred.SetAmbientOcclusion(ao);
            deferred.Declare(*graph, context);
            host.DeclareColor(*graph, outputs, deferred.GetOutput(), ao, shadowMap);
            const auto final = host.DeclareVolume(*graph, deferred.GetOutput(), outputs.depth, shadowMap);
            std::array<RHIReadback, 4> readbacks;
            const auto copy = [&](unsigned i, RGHandle handle, RHIFormat format) {
                Check(device.CreateReadback(16, 16, format, 1, readbacks[i], error), "Volume readback");
                graph->AddPass(
                    "Probe.Volume.Texture", {{handle, RHIResourceState::CopySource}},
                    [readback = readbacks[i], handle](const auto& execution) {
                        execution.encoder->CopyToReadback(readback, execution.ResolveHandle(handle));
                    },
                    true);
            };
            copy(0, hybrid ? host.RefractionFrame()->GraphBackgroundColor(*graph) : deferred.GetOutput(),
                 RHIFormat::RGBA16Float);
            copy(1, final, RHIFormat::RGBA16Float);
            copy(2, outputs.depth, RHIFormat::D32Float);
            copy(3, outputs.bitmask, RHIFormat::R32Uint);
            Check(graph->Compile(error), "Volume graph compile " + error);
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
                    "Volume native parallel " + error);
                completion = ticket.GetRecordedBatch()->GetCompletionPoint();
            }
            else
            {
                Check(graph->Execute(error), "Volume sequential " + error);
            }
            Check(device.EndFrame(error) && GetRHISubmissionThread().DrainSubmissions(&device, error), "Volume submit");
            if (!workers)
            {
                completion = {device.GetLastSignaledFenceValue()};
            }
            Check(host.PublishSubmittedCache(context.frameId, completion, error, ticket),
                  "Volume publication " + error);
            device.WaitForGpu();
            Check(GetRHISubmissionThread().Drain(&device, error), "Volume retirement");
            std::array<RHIReadbackImage, 4> mapped;
            for (unsigned i = 0; i < 4; ++i)
            {
                Check(device.MapReadback(readbacks[i], mapped[i], error), "Volume map");
            }
            for (unsigned y = 0; y < 16; ++y)
            {
                for (unsigned x = 0; x < 16; ++x)
                {
                    const bool covered =
                        hybrid ? x >= 5 && x < 11 && y >= 5 && y < 11 : x >= 2 && x < 14 && y >= 2 && y < 14;
                    const bool occluded = !oldDraws.empty() && x < 8;
                    Near(mapped[2].At(x, y, 0),
                         hybrid     ? (covered ? .36 / 2.2 : .92 / 2.9)
                         : occluded ? .4
                                    : .9,
                         "Volume does not replace opaque depth");
                    const double limit = occluded ? .4 : .9;
                    std::vector<std::array<double, 4>> media;
                    if (covered)
                    {
                        for (auto range : ranges)
                        {
                            media.push_back({std::max(double(range[0]), 0.0), std::min(double(range[1]), limit),
                                             density, emission});
                        }
                        if (fixture == 3)
                        {
                            media.push_back({.4, std::min(.8, limit), density * 2, emission});
                        }
                    }
                    std::vector<double> events{0, limit};
                    for (auto medium : media)
                    {
                        if (medium[1] > medium[0])
                        {
                            events.push_back(medium[0]);
                            events.push_back(medium[1]);
                        }
                    }
                    std::sort(events.begin(), events.end());
                    double transmittance = 1;
                    Vector radiance{};
                    for (unsigned i = 1; i < events.size(); ++i)
                    {
                        const double length = events[i] - events[i - 1], middle = (events[i] + events[i - 1]) * .5;
                        double extinction = 0, source = 0;
                        for (auto medium : media)
                        {
                            if (middle >= medium[0] && middle < medium[1])
                            {
                                extinction += medium[2];
                                source += medium[3];
                            }
                        }
                        const double stepT = std::exp(-extinction * length);
                        const double integral =
                            extinction > 0 ? -std::expm1(-extinction * length) / extinction : length;
                        radiance = radiance + Vector{.7, .4, .2} * (transmittance * source * integral);
                        transmittance *= stepT;
                    }
                    if (hybrid && covered)
                    {
                        const double nx = (x + .5) / 8 - 1, ny = 1 - (y + .5) / 8;
                        const double exit = std::min(.6, .75 / std::max(std::abs(nx), std::abs(ny)) - 2);
                        const double distance = (exit - .2) * std::sqrt(1 + nx * nx + ny * ny);
                        transmittance = std::exp(-density * distance);
                    }
                    if (fixture == 6 && covered)
                    {
                        const double phase = (1 - .4 * .4) / (4 * kPi * std::pow(1 + .4 * .4 + .8, 1.5));
                        radiance = radiance + Vector{2, 1, .5} * (density * color * phase *
                                                                  -std::expm1(-2 * density * .4) / (2 * density));
                        ++scattering;
                    }
                    for (unsigned c = 0; c < 3; ++c)
                    {
                        const double expected = radiance[c] + transmittance * mapped[0].At(x, y, c);
                        const float actual = mapped[1].At(x, y, c);
                        if (fixture == 7 && covered)
                        {
                            Check(std::isfinite(actual) && actual > expected, "Environment in-scattering contributes");
                            ++scattering;
                        }
                        else
                        {
                            Near(actual, expected,
                                 "Independent Volume slab integral fixture=" + std::to_string(fixture),
                                 fixture == 6 ? 2e-3 : 1.5e-3);
                        }
                        ++gpuComponents;
                    }
                    interior += hybrid && covered;
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
                accepted->GraphCoefficients(*graph);
            }
            catch (const std::runtime_error&)
            {
                rejected = true;
            }
            Check(rejected, "Volume frame cannot cross graph reset");
            ++failures;
            graph.reset();
            if (shadowTexture.IsValid())
            {
                device.ReleaseTexture(shadowTexture);
            }
            ++frames;
            std::cout << "LX_VOLUME_FIXTURE_OK fixture=" << fixture << " workers=" << workers << '\n';
        }
    }
    host.ShutdownAfterIdle();
    gbuffer.Shutdown();
    deferred.Shutdown();
    meshes.Shutdown();
    std::string messages;
    const auto validation = device.DrainDebugMessages(messages);
    Check(validation == 0, "Volume native GPU validation: " + messages);
    Check(frames == 33 && interior && scattering && failures == 66, "Volume fixture coverage");
    std::cout << "LX_MATERIAL_SCENE_VOLUME_OK frames=" << frames << " pixels=" << pixels << " interior=" << interior
              << " scattering=" << scattering << " failures=" << failures << " checks=" << checks
              << " gpuComponents=" << gpuComponents << " validation=0\n";
}
