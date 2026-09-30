// Native SceneHost coverage and Decal regression. Reference material instances
// use the observed GBuffer values, so derived lobes must be reevaluated as well.
VerifiedProduct ShadowDecalProduct(const std::filesystem::path& root, bool layered)
{
    LXMaterialAsset asset;
    const auto surface = asset.CreateNode("ShaderNodeBsdfPrincipled", 0, 0);
    const auto image = asset.CreateNode("ShaderNodeTexImage", -400, 0);
    const auto multiply = asset.CreateNode("LXMultiplyFloat", -200, 0);
    const auto output = asset.CreateNode("ShaderNodeOutputMaterial", 400, 0);
    asset.activeOutput = output;
    asset.blackboard = {{1200, "alpha", "Alpha", PinType::Float, 1.0},
                        {1201, "color", "Color", PinType::Color, std::array<double, 4>{.21, .37, .53, 1}},
                        {1202, "rough", "Roughness", PinType::Float, .42},
                        {1203, "metal", "Metallic", PinType::Float, .65},
                        {1205, "normal", "Normal", PinType::Vector, std::array<double, 3>{0, 0, 1}}};
    const auto connect = [&](Id from, const char* fromName, Id to, const char* toName) {
        Check(asset.graph
                  .Connect(Pin(asset.graph, from, fromName, Direction::Output),
                           Pin(asset.graph, to, toName, Direction::Input))
                  .has_value(),
              "Shadow/Decal link");
    };
    Check(asset.graph.SetProperty(image, "image", "22222222-2222-4222-8222-222222222222") &&
              asset.graph.SetProperty(image, "interpolation", "Closest"),
          "Shadow alpha image");
    connect(image, "Alpha", multiply, "A");
    const char* sockets[]{"B", "Base Color", "Roughness", "Metallic", "Normal"};
    for (unsigned i = 0; i < asset.blackboard.size(); ++i)
    {
        const auto node = asset.CreateNode(i == 1   ? "LXParameterColor"
                                           : i == 4 ? "LXParameterVector"
                                                    : "LXParameterFloat",
                                           -600, float(i * 100));
        Check(asset.graph.SetProperty(node, "parameter", std::to_string(i == 4 ? 1205 : 1200 + i)),
              "Shadow/Decal parameter");
        connect(node, "Value", i == 0 ? multiply : surface, sockets[i]);
    }
    connect(multiply, "Result", surface, "Alpha");
    connect(surface, "BSDF", output, "Surface");
    for (const auto& [name, value] :
         std::array<std::pair<const char*, double>, 5>{{{"Coat Weight", layered ? .35 : 0},
                                                        {"Sheen Weight", layered ? .2 : 0},
                                                        {"Anisotropic", layered ? .4 : 0},
                                                        {"Thin Film Thickness", layered ? 420 : 0},
                                                        {"IOR", 1.45}}})
    {
        const auto pin = Pin(asset.graph, surface, name, Direction::Input);
        asset.graph.SetSocketValue(pin, value);
        Check(asset.graph.FindPin(pin)->value == LXSocketValue{value}, name);
    }
    std::vector<LXMaterialDiagnostic> diagnostics;
    const auto program = GenerateMaterialSlang(asset, &diagnostics);
    Check(bool(program), "Shadow/Decal generation");
    const auto file =
        root / "Build/Obj/MaterialProductProbe" / (layered ? "shadow-decal-layered.slang" : "shadow-decal-core.slang");
    std::ofstream(file, std::ios::binary | std::ios::trunc)
        << BuildBoundSource(*program) << "\n#include \"MaterialGraphSceneHost.slang\"\n";
    std::vector<CompileTarget> targets;
    for (auto backend : {RHIShaderBinary::Dxil, RHIShaderBinary::SpirV})
    {
        for (const char* entry : {"LXSceneVS", "LXSceneColorPS"})
        {
            targets.push_back({backend, entry, std::string_view(entry).ends_with("VS") ? "vs_6_0" : "ps_6_0"});
        }
    }
    RHIShaderCompileOptions options;
    options.strictMath = options.fineDerivatives = true;
    options.includeDirectories.push_back(root / "Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes");
    RHIShaderPermutation permutation;
    std::string error;
    Check(permutation.Set("LX_MATERIAL_ROUTE", "2", error) && permutation.Enable("LX_MATERIAL_PIXEL_FOOTPRINT", error),
          "Shadow/Decal permutation");
    Capabilities capabilities;
    capabilities.coreForward = capabilities.layeredLookup = true;
    VerifiedProduct product;
    for (auto backend : {RHIShaderBinary::Dxil, RHIShaderBinary::SpirV})
    {
        for (const char* entry : {"LXSceneShadowVS", "LXSceneShadowPS"})
        {
            RHIShaderCompiler::VerifiedShader shader;
            Check(RHIShaderCompiler::VerifyFile(file.string(), entry,
                                                std::string_view(entry).ends_with("VS") ? "vs_6_0" : "ps_6_0", backend,
                                                permutation, shader, error, options),
                  "Shadow shader " + error);
        }
    }
    const bool verified =
        VerifyProduct(*program, file, targets, permutation, options, capabilities, {}, product, diagnostics);
    for (const auto& diagnostic : diagnostics)
    {
        error += diagnostic.message + "\n";
    }
    Check(verified, "Shadow/Decal verification " + error);
    return product;
}

void ShadowDecalPlane(Geometry& geometry, float u = .875f)
{
    auto& source = geometry.draw.modelMeshView;
    source.indexData = geometry.indices.data();
    geometry.draw.geometryKey = 12500;
    geometry.draw.coverage.flags = EnhancedMaterialCoverage::Enabled;
    const std::array<std::array<float, 3>, 6> positions{{{-.75f, -.75f, .4f},
                                                         {.75f, -.75f, .4f},
                                                         {-.75f, .75f, .4f},
                                                         {-.75f, .75f, .4f},
                                                         {.75f, -.75f, .4f},
                                                         {.75f, .75f, .4f}}};
    for (unsigned i = 0; i < positions.size(); ++i)
    {
        auto* vertex = geometry.vertices.data() + i * source.vertexStride;
        const std::array<float, 2> uv{u, .25f};
        std::memcpy(vertex, positions[i].data(), 12);
        std::memcpy(vertex + assets::OffsetOf(source.vertexAttributeMask, assets::VertexAttribute::Uv0), uv.data(), 8);
    }
}

void SubmitShadowDecal(RecordingChangeDevice& device, ProbePool& pool,
                       const std::shared_ptr<EnhancedRenderGraph>& graph, SceneHost& host, std::uint64_t frameId,
                       unsigned workers, bool publish)
{
    std::string error;
    Check(graph->Compile(error), "Shadow/Decal graph compile " + error);
    RHISubmissionTicket ticket;
    RHICompletionPoint completion;
    if (workers)
    {
        graph->SetParallelRecordCostThreshold(0);
        RHIRecordedBatchDesc desc;
        desc.frameId = frameId;
        desc.backendGeneration = GetRHISubmissionThread().GetOwnerGeneration(&device);
        desc.lifetimeToken = graph;
        RHIRecordedBatch batch;
        Check(graph->RecordParallel(pool, workers, desc, batch, error) &&
                  GetRHISubmissionThread().EnqueueRecordedBatch(&device, device, std::move(batch), ticket, error),
              "Shadow/Decal parallel " + error);
        completion = ticket.GetRecordedBatch()->GetCompletionPoint();
    }
    else
    {
        Check(graph->Execute(error), "Shadow/Decal sequential " + error);
    }
    Check(device.EndFrame(error) && GetRHISubmissionThread().DrainSubmissions(&device, error), "Shadow/Decal submit");
    if (!workers)
    {
        completion = {device.GetLastSignaledFenceValue()};
    }
    if (publish)
    {
        Check(host.PublishSubmittedCache(frameId, completion, error, ticket), "Shadow/Decal publication " + error);
    }
    device.WaitForGpu();
    Check(GetRHISubmissionThread().Drain(&device, error), "Shadow/Decal retirement");
    std::string messages;
    const auto errors = device.DrainDebugMessages(messages);
    Check(errors == 0, "Shadow/Decal GPU validation " + messages);
}

void RunSceneShadow(RecordingChangeDevice& device, ProbeRoots& roots, ProbePipelines& pipelines,
                    ProbeTextures& textures, ProbePool& pool, const std::filesystem::path& root,
                    const std::shared_ptr<Texture>& image)
{
    const auto product = ShadowDecalProduct(root, false);
    GenerationStore store;
    experiment::AssetId id;
    Check(Uuid::TryParse("DDDDDDDD-DDDD-4DDD-8DDD-DDDDDDDDDDDD", id.value), "Shadow graph identity");
    std::string error;
    const auto generation = store.Load(
        id,
        [&](CookedProgram& cooked, std::string&) {
            cooked = {product, WriteMaterialProgramMetadata(product.program), BuildBoundSource(product.program)};
            return true;
        },
        true, error);
    Check(bool(generation), "Shadow generation " + error);
    const auto volumeProduct = VolumeProduct(root, false);
    const auto volumeGeneration = store.Load(
        id,
        [&](CookedProgram& cooked, std::string&) {
            cooked = {volumeProduct, WriteMaterialProgramMetadata(volumeProduct.program),
                      BuildBoundSource(volumeProduct.program)};
            return true;
        },
        true, error);
    Check(bool(volumeGeneration), "Pure Volume shadow generation " + error);
    SceneHost host;
    EnhancedFrameContext context;
    context.resources = &device;
    context.rootSignatures = &roots;
    context.psoManager = &pipelines;
    context.textureCache = &textures;
    context.width = context.height = 16;
    FrameCameraSnapshot camera;
    camera.view = camera.projection = math::matrix4x4::identity();
    camera.eyePosition = math::vector3(0, 0, -2);
    context.camera = &camera;
    WaitSceneProgram(host, context, generation);
    WaitSceneProgram(host, context, volumeGeneration);
    unsigned frames{}, coveredPixels{};
    for (unsigned fixture = 0; fixture < 14; ++fixture)
    {
        for (unsigned workers : {0u, 1u, 4u})
        {
            std::cout << "LX_SHADOW_FIXTURE_BEGIN fixture=" << fixture << " workers=" << workers << std::endl;
            Drain drain{device};
            context.frameId = 13000 + frames;
            context.sceneEpoch = 94;
            auto geometry = MakeGeometry(false, fixture != 6 && fixture != 7);
            ShadowDecalPlane(geometry, fixture < 2 ? .125f : fixture == 3 || fixture == 4 ? .625f : .875f);
            geometry.draw.geometryKey += fixture;
            if (fixture >= 1 && fixture <= 5)
            {
                geometry.draw.coverage.flags |= EnhancedMaterialCoverage::Masked;
            }
            if (fixture == 7)
            {
                geometry.draw.coverage.flags |= EnhancedMaterialCoverage::DoubleSided;
            }
            geometry.draw.coverage.cutoff = fixture == 3 ? .6f : fixture == 4 ? .4f : .5f;
            if (fixture == 9)
            {
                geometry.draw.worldMatrix.m[3][0] = .25f;
            }
            std::array<math::matrix4x4, 1> bones{math::matrix4x4::identity()};
            if (fixture == 13)
            {
                const auto oldMask = geometry.draw.modelMeshView.vertexAttributeMask;
                const auto oldStride = geometry.draw.modelMeshView.vertexStride;
                const auto skinMask = oldMask | assets::kSkinVertexAttributes;
                const auto skinStride = assets::StrideOf(skinMask);
                std::vector<std::byte> skinned(6 * skinStride);
                bones[0].m[3][0] = .25f;
                for (unsigned i = 0; i < 6; ++i)
                {
                    for (const auto& attribute : assets::kVertexAttributeTable)
                    {
                        if (assets::Has(oldMask, attribute.attribute))
                        {
                            std::memcpy(skinned.data() + i * skinStride +
                                            assets::OffsetOf(skinMask, attribute.attribute),
                                        geometry.vertices.data() + i * oldStride +
                                            assets::OffsetOf(oldMask, attribute.attribute),
                                        assets::SizeOf(attribute.format));
                        }
                    }
                    const std::array<float, 4> weights{1, 0, 0, 0};
                    std::memcpy(skinned.data() + i * skinStride +
                                    assets::OffsetOf(skinMask, assets::VertexAttribute::BoneWeights),
                                weights.data(), sizeof(weights));
                }
                geometry.vertices = std::move(skinned);
                auto& source = geometry.draw.modelMeshView;
                source.vertexAttributeMask = skinMask;
                source.vertexStride = skinStride;
                source.vertexLayoutHash = assets::VertexLayoutHash(skinMask);
                source.vertexBytes = geometry.vertices.size();
                source.vertexData = geometry.vertices.data();
                geometry.draw.boneCount = 1;
                geometry.draw.bonePalette = bones.data();
            }
            Check(BuildInstance(
                      generation, {id, {{1200, fixture == 5 ? .25 : 1.0}}, {}},
                      [&](const experiment::AssetId&, LXColorSpace, std::string&) { return image; },
                      geometry.draw.materialGraphInstance, error),
                  "Shadow instance " + error);
            SceneInputBudget sealBudget;
            if (fixture == 10)
            {
                sealBudget.mesh.maxChunkPoints = 3;
            }
            std::shared_ptr<const SceneViewInput> input;
            const std::array<std::array<float, 2>, 1> volumeDepth{{{.2f, .6f}}};
            auto volume = VolumeBoxes(volumeDepth, 12600, false);
            if (fixture == 12)
            {
                Check(BuildInstance(volumeGeneration, {id, {}, {}}, {}, volume.draw.materialGraphInstance, error),
                      "Pure Volume shadow instance");
            }
            Check(SceneViewInput::Seal({context.frameId, context.sceneEpoch, 95, 1, 16, 16, camera},
                                       {fixture == 12 ? &volume.draw : &geometry.draw, 1}, sealBudget, input, error),
                  "Shadow seal " + error);
            bones[0].m[3][0] = 999;
            Check(device.BeginFrame(error), "Shadow begin");
            textures.BeginFrame(context.frameId);
            Check(host.PrepareResidency(context, input, error), "Shadow residency");
            auto graph = std::make_shared<EnhancedRenderGraph>(device);
            if (workers)
            {
                pool.BeginFrame(static_cast<unsigned>(context.frameId));
                Check(graph->PrepareParallel(pool, error), "Shadow prefix " + error);
            }
            EnhancedShadowData shadow;
            shadow.enabled = fixture != 11;
            for (unsigned cascade = 0; cascade < 3; ++cascade)
            {
                shadow.lightViewProjection[cascade] = math::matrix4x4::identity();
                shadow.lightViewProjection[cascade].m[3][0] = float(cascade) * .125f;
            }
             Check(host.Prepare(context, input, {}, {}, {}, shadow, {}, error, 0), "Shadow prepare " + error);
            RHITextureDesc desc;
            desc.width = desc.height = 16;
            desc.depthOrArraySize = 3;
            desc.format = RHIFormat::D32Float;
            desc.allowDepthStencil = true;
            desc.clearDepth = fixture == 8 ? .2f : 1.f;
            RHITextureHandle texture;
            Check(device.CreateTexture(desc, texture, error), "Shadow depth array");
            const auto map = graph->ImportTexture(texture, RHIResourceState::Common, "Probe.LXShadow");
            const float clear = fixture == 8 ? .2f : 1.f;
            graph->AddPass("Probe.LXShadow.Clear", {{map, RHIResourceState::DepthWrite}},
                           [map, clear, &device](const auto& execution) {
                               for (unsigned cascade = 0; cascade < 3; ++cascade)
                               {
                                   const auto depth = RHIDepthTargetDesc::DepthSlice(execution.ResolveHandle(map),
                                                                                     RHIFormat::D32Float, cascade);
                                   const auto target =
                                       device.CreateRenderTargets(std::span<const RHITextureHandle>{}, &depth);
                                   execution.encoder->BindRenderTargets(target);
                                   execution.encoder->ClearDepthTarget(target, clear);
                               }
                           });
            host.DeclareShadow(*graph, map);
            if (shadow.enabled)
            {
                bool rejected = false;
                try
                {
                    host.DeclareShadow(*graph, map);
                }
                catch (const std::runtime_error&)
                {
                    rejected = true;
                }
                Check(rejected, "Duplicate shadow declaration rejected");
                EnhancedRenderGraph other(device);
                rejected = false;
                try
                {
                    host.DeclareShadow(other, map);
                }
                catch (const std::runtime_error&)
                {
                    rejected = true;
                }
                Check(rejected, "Shadow graph ownership rejected");
            }
            RHIReadback readback;
            Check(device.CreateReadback(16, 16, RHIFormat::D32Float, 3, readback, error), "Shadow readback");
            graph->AddPass(
                "Probe.LXShadow.Read", {{map, RHIResourceState::CopySource}},
                [map, readback](const auto& execution) {
                    for (unsigned cascade = 0; cascade < 3; ++cascade)
                    {
                        execution.encoder->CopyToReadback(readback, execution.ResolveHandle(map), cascade, cascade);
                    }
                },
                true);
            SubmitShadowDecal(device, pool, graph, host, context.frameId, workers, false);
            RHIReadbackImage mapped;
            Check(device.MapReadback(readback, mapped, error), "Shadow map");
            const bool casts = fixture == 0 || fixture == 2 || fixture == 4 || fixture == 7 || fixture == 9 ||
                               fixture == 10 || fixture == 13;
            for (unsigned cascade = 0; cascade < 3; ++cascade)
            {
                const unsigned left = 2 + cascade + (fixture == 9 || fixture == 13 ? 2 : 0);
                for (unsigned y = 0; y < 16; ++y)
                {
                    for (unsigned x = 0; x < 16; ++x)
                    {
                        const bool covered = casts && x >= left && x < left + 12 && y >= 2 && y < 14;
                        Near(mapped.At(x, y, 0, cascade), covered ? .4 : clear, "Graph alpha/cascade shadow");
                        coveredPixels += covered;
                    }
                }
            }
            device.ReleaseReadback(readback);
            graph.reset();
            device.ReleaseTexture(texture);
            ++frames;
        }
    }
    host.ShutdownAfterIdle();
    std::cout << "LX_MATERIAL_SCENE_SHADOW_OK frames=" << frames << " covered=" << coveredPixels << " checks=" << checks
              << " gpuComponents=" << gpuComponents << " validation=0\n";
}
