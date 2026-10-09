// Included by the raster probe after its native device, geometry and reference
// helpers. Exercises the product SceneHost, rather than a standalone blur shader.
VerifiedProduct SubsurfaceProduct(const std::filesystem::path& root)
{
    LXMaterialAsset asset;
    const auto surface = asset.CreateNode("ShaderNodeBsdfPrincipled", 0, 0);
    const auto image = asset.CreateNode("ShaderNodeTexImage", -200, 0);
    const auto scale = asset.CreateNode("LXParameterFloat", -200, 100);
    const auto metal = asset.CreateNode("LXParameterFloat", -200, 200);
    const auto output = asset.CreateNode("ShaderNodeOutputMaterial", 400, 0);
    asset.activeOutput = output;
    asset.blackboard = {{904, "scale", "SSS Scale", PinType::Float, 1.0},
                        {905, "metal", "Metallic", PinType::Float, 0.0}};
    Check(asset.graph.SetProperty(scale, "parameter", "904") && asset.graph.SetProperty(metal, "parameter", "905") &&
              asset.graph.SetProperty(image, "image", "22222222-2222-4222-8222-222222222222") &&
              asset.graph.SetProperty(image, "interpolation", "Closest"),
          "SSS properties");
    const auto set = [&](const char* name, LXSocketValue value) {
        const auto pin = Pin(asset.graph, surface, name, Direction::Input);
        asset.graph.SetSocketValue(pin, value);
        Check(asset.graph.FindPin(pin)->value == value, name);
    };
    set("Subsurface Weight", .8);
    set("Subsurface Radius", std::array<double, 3>{.25, .12, 0});
    set("Subsurface IOR", 1.4);
    set("Subsurface Anisotropy", .2);
    set("Roughness", .5);
    const auto connect = [&](Id from, const char* fromName, Id to, const char* toName) {
        Check(asset.graph
                  .Connect(Pin(asset.graph, from, fromName, Direction::Output),
                           Pin(asset.graph, to, toName, Direction::Input))
                  .has_value(),
              "SSS connection");
    };
    connect(image, "Color", surface, "Base Color");
    connect(image, "Alpha", surface, "Alpha");
    connect(scale, "Value", surface, "Subsurface Scale");
    connect(metal, "Value", surface, "Metallic");
    connect(surface, "BSDF", output, "Surface");
    std::vector<LXMaterialDiagnostic> diagnostics;
    const auto program = GenerateMaterialSlang(asset, &diagnostics);
    Check(program && (program->features & 0x1000u), "SSS graph feature extraction");
    const auto file = root / "Build/Obj/MaterialProductProbe/subsurface-product.slang";
    std::ofstream(file, std::ios::binary | std::ios::trunc)
        << BuildBoundSource(*program) << "\n#include \"MaterialGraphSceneHost.slang\"\n";
    const CompileTarget targets[]{{RHIShaderBinary::Dxil, "LXSceneVS", "vs_6_0"},
                                  {RHIShaderBinary::Dxil, "LXSceneColorPS", "ps_6_0"},
                                  {RHIShaderBinary::SpirV, "LXSceneVS", "vs_6_0"},
                                  {RHIShaderBinary::SpirV, "LXSceneColorPS", "ps_6_0"}};
    RHIShaderCompileOptions options;
    options.strictMath = true;
    options.fineDerivatives = true;
    options.includeDirectories.push_back(root / "Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes");
    Capabilities capabilities;
    capabilities.coreForward = capabilities.layeredLookup = capabilities.subsurface = true;
    VerifiedProduct product;
    RHIShaderPermutation permutation;
    std::string error;
    Check(permutation.Set("LX_MATERIAL_ROUTE", "2", error) && permutation.Enable("LX_MATERIAL_PIXEL_FOOTPRINT", error),
          "SSS product permutation");
    const bool verified =
        VerifyProduct(*program, file, targets, permutation, options, capabilities, {}, product, diagnostics);
    std::string messages;
    for (const auto& diagnostic : diagnostics)
    {
        messages += diagnostic.message + "\n";
    }
    Check(verified, "Verify SSS product: " + messages);
    Check(product.selection.tier == Tier::Special && product.selection.route == Route::Forward,
          "SSS automatically selects Special Forward");
    return product;
}

struct SceneSssProfile
{
    Vector real{}, virtualDepth{}, attenuation{}, normalization{};
};

SceneSssProfile SssProfile(Vector color, double scale)
{
    SceneSssProfile result;
    const double eta = 1.4;
    const double f = std::clamp(-1.4399 / (eta * eta) + .7099 / eta + .6681 + .0636 * eta, 0.0, .99);
    const double boundary = (1 + f) / (1 - f);
    const Vector radius{.25, .12, 0};
    for (unsigned c = 0; c < 3; ++c)
    {
        const double meanFreePath = radius[c] * scale / (4 * kPi);
        const double extinction = 1 / std::max(meanFreePath, 1e-6);
        const double absorption = extinction * (1 - Clamp(color[c]));
        const double reduced = std::max(absorption + extinction * Clamp(color[c]) * .8, 1e-8);
        const double diffusion = 1 / (3 * reduced);
        result.real[c] = 1 / reduced;
        result.virtualDepth[c] = result.real[c] + 4 * boundary * diffusion;
        result.attenuation[c] = std::sqrt(absorption / diffusion);
        result.normalization[c] = std::exp(-result.attenuation[c] * result.real[c]) +
                                  std::exp(-result.attenuation[c] * result.virtualDepth[c]);
    }
    return result;
}

double SssDensity(const SceneSssProfile& profile, double distance, unsigned channel)
{
    double density = 0;
    for (const double depth : {profile.real[channel], profile.virtualDepth[channel]})
    {
        const double length = std::hypot(depth, distance);
        density += depth * (profile.attenuation[channel] + 1 / length) *
                   std::exp(-profile.attenuation[channel] * length) / (length * length);
    }
    return density / (2 * kPi * profile.normalization[channel]);
}

void RunSceneSubsurface(RecordingChangeDevice& device, ProbeRoots& roots, ProbePipelines& pipelines,
                        ProbeTextures& textures, ProbePool& pool, const std::filesystem::path& root,
                        own::shared_owner<const Texture> image, own::shared_owner<const Texture> cube, const SheenTable& table)
{
    const auto product = SubsurfaceProduct(root);
    std::string error;
    experiment::AssetId graphId;
    Check(Uuid::TryParse("77777777-7777-4777-8777-777777777777", graphId.value), "SSS graph identity");
    GenerationStore store;
    const auto generation = store.Load(
        graphId,
        [&](CookedProgram& cooked, std::string&) {
            cooked = {product, WriteMaterialProgramMetadata(product.program), BuildBoundSource(product.program)};
            return true;
        },
        true, error);
    Check(bool(generation), "SSS immutable generation " + error);
    std::array<own::shared_owner<const Instance>, 3> instances;
    for (unsigned i = 0; i < instances.size(); ++i)
    {
        InstanceDescription description{graphId, {{904, i == 1 ? 0.0 : 1.0}, {905, i == 2 ? .45 : 0.0}}, {}};
        Check(BuildInstance(
                  generation, description,
                  [&](const experiment::AssetId&, LXColorSpace, std::string&) { return image; }, instances[i], error),
              "SSS instance " + error);
    }
    SceneHost host;
    ProbeMeshes meshes;
    Check(meshes.Initialize(&device, error), "SSS mesh cache");
    EnhancedGBufferPass gbuffer;
    EnhancedDeferredPass deferred;
    EnhancedFrameContext context;
    context.resources = &device;
    context.rootSignatures = &roots;
    context.psoManager = &pipelines;
    context.textureCache = &textures;
    context.meshCache = &meshes;
    context.width = context.height = 24;
    FrameCameraSnapshot camera;
    camera.view = camera.projection = math::matrix4x4::identity();
    camera.eyePosition = math::vector3(0, 0, 2);
    context.camera = &camera;
    Check(gbuffer.Initialize(context, error) && deferred.Initialize(context, error), "SSS actual Scene passes");
    WaitSceneProgram(host, context, generation);
    for (unsigned features : {0x3000u})
    {
        Generation unsupportedValue(*generation);
        unsupportedValue.cooked.product.program.features = features;
        unsupportedValue.cooked.product.program.volume = (features & 0x2000u) != 0;
        const auto unsupported = own::make_shared<const Generation>(std::move(unsupportedValue));
        Check(!host.RequestProgram(context, unsupported, error), "Uninstalled Volume remains rejected");
    }
    std::uint64_t pixels{}, spread{}, boundaries{}, maskedHoles{}, frames{};
    for (unsigned fixture = 0; fixture < 4; ++fixture)
    {
        for (unsigned workers : {0u, 1u, 4u})
        {
            Drain drain{device};
            context.frameId = 5000 + frames;
            context.sceneEpoch = 80;
            auto a = MakeGeometry(false, true), b = MakeGeometry(false, true), legacy = MakeGeometry(false, true);
            const auto plane = [&](Geometry& geometry, float left, float right, float bottom, float top, float z,
                                   std::uint64_t key) {
                const std::array<std::array<float, 2>, 6> xy{
                    {{left, bottom}, {right, bottom}, {left, top}, {left, top}, {right, bottom}, {right, top}}};
                geometry.draw.modelMeshView.indexData = geometry.indices.data();
                geometry.draw.modelMeshView.handle.generation = key;
                geometry.draw.geometryKey = key;
                geometry.draw.coverage.flags = EnhancedMaterialCoverage::Enabled;
                for (unsigned i = 0; i < 6; ++i)
                {
                    geometry.points[i].position = {xy[i][0], xy[i][1], z, 0};
                    geometry.points[i].uvLod = {(xy[i][0] + 1) * .5f, (xy[i][1] + 1) * .5f, 0, 0};
                    auto* vertex = geometry.vertices.data() + i * geometry.draw.modelMeshView.vertexStride;
                    std::memcpy(vertex, geometry.points[i].position.data(), 12);
                    std::memcpy(vertex + assets::OffsetOf(geometry.draw.modelMeshView.vertexAttributeMask,
                                                          assets::VertexAttribute::Uv0),
                                geometry.points[i].uvLod.data(), 8);
                }
            };
            // Binary coordinates also land exactly on the native subpixel grid.
            // This keeps raster quantization separate from the transport reference.
            plane(a, -.9375f, .9375f, -.9375f, .9375f, .4f, 6101);
            plane(b, .125f, .9375f, -.9375f, .9375f, .3f, 6102);
            plane(legacy, -.25f, .25f, -.375f, .375f, .1f, 6103);
            a.draw.materialGraphInstance = instances[fixture == 1 ? 1 : 0];
            b.draw.materialGraphInstance = instances[2];
            a.draw.materialGraphSlot = 21;
            b.draw.materialGraphSlot = 22;
            if (fixture == 3)
            {
                a.draw.coverage.flags |= EnhancedMaterialCoverage::Masked;
                b.draw.coverage.flags |= EnhancedMaterialCoverage::Masked;
            }
            Check(Uuid::TryParse("33333333-3333-8333-8333-333333333333", legacy.draw.modelMeshView.handle.meshId),
                  "Legacy SSS occluder identity");
            std::vector<EnhancedDrawItem> oldDraws{legacy.draw};
            context.draws = &oldDraws;
            EnhancedLight light;
            light.position = math::vector4(.2f, .1f, 1.5f, 1);
            light.color = math::color(3, 2, 1, 1);
            light.attenuation = math::vector4(1, 0, 1, 10);
            std::vector<EnhancedLight> lights{light};
            context.lights = &lights;
            const std::array draws{a.draw, b.draw};
            SceneInputView view{context.frameId, context.sceneEpoch, 91, 1, 24, 24, camera};
            own::shared_owner<const SceneViewInput> input;
            Check(SceneViewInput::Seal(view, draws, {}, input, error), "SSS Scene seal " + error);
            Check(device.BeginFrame(error), "SSS begin frame");
            textures.BeginFrame(context.frameId);
            meshes.BeginFrame(context.frameId);
            const auto environment = fixture == 2 ? RHITextureHandle{} : textures.GetOrUpload((cube ? &*cube.borrow() : nullptr), cube ? cube->NonRehydratableImage() : own::shared_owner<const Texture::CodecImage>{}, error).handle;
            Check(host.PrepareResidency(context, input, error), "SSS residency " + error);
            Check(gbuffer.PrepareFrame(context, error) && deferred.PrepareFrame(context, error),
                  "SSS actual Scene pass preparation " + error);
            auto graph = std::make_shared<EnhancedRenderGraph>(device);
            if (workers)
            {
                RHIRecordedBatchDesc desc;
                desc.frameId = context.frameId;
                desc.backendGeneration = GetRHISubmissionThread().GetOwnerGeneration(&device);
                pool.BeginFrame(static_cast<std::uint32_t>(context.frameId));
                Check(graph->PrepareParallel(pool, error), "SSS native prefix " + error);
            }
            SceneHostBudget budget;
            const bool prepared =
                 host.Prepare(context, input, environment, {}, {}, {}, budget, error, environment.IsValid() ? 1 : 0);
            Check(prepared, "SSS Scene preparation " + error);
            const auto accepted = host.SubsurfaceFrame();
            Check(bool(accepted), "SSS frame resources exist");
            auto insufficient = budget;
            insufficient.subsurfaceBytes = 1;
             Check(!host.Prepare(context, input, environment, {}, {}, {}, insufficient, error, environment.IsValid() ? 1 : 0) &&
                      host.SubsurfaceFrame() == accepted,
                  "SSS budget failure preserves accepted frame");
            gbuffer.Declare(*graph, context);
            const auto outputs = gbuffer.GetOutputs();
            host.DeclareGBuffer(*graph, outputs);
            RGTextureDesc aoDesc;
            aoDesc.width = aoDesc.height = 24;
            aoDesc.format = RHIFormat::RG16Float;
            aoDesc.allowRenderTarget = true;
            aoDesc.clearColor[0] = .25f;
            const auto ao = graph->CreateTexture(aoDesc);
            graph->AddPass("Probe.SSS.AOClear", {{ao, RHIResourceState::RenderTarget}}, [&](const auto& execution) {
                const auto handle = execution.ResolveHandle(ao);
                const auto target = device.CreateRenderTargets({&handle, 1}, nullptr);
                const float clear[]{.25f, 0, 0, 0};
                execution.encoder->BindRenderTargets(target);
                execution.encoder->ClearRenderTargets(target, clear);
            });
            deferred.SetInputs(outputs);
            deferred.SetAmbientOcclusion(ao);
            deferred.Declare(*graph, context);
            std::array<RHIReadback, 14> readbacks;
            const auto copyTexture = [&](unsigned index, RGHandle handle, RHIFormat format) {
                Check(device.CreateReadback(24, 24, format, 1, readbacks[index], error), "SSS texture readback");
                graph->AddPass(
                    "Probe.SSS.TextureReadback", {{handle, RHIResourceState::CopySource}},
                    [readback = readbacks[index], handle](const auto& execution) {
                        execution.encoder->CopyToReadback(readback, execution.ResolveHandle(handle));
                    },
                    true);
            };
            copyTexture(0, deferred.GetOutput(), RHIFormat::RGBA16Float);
            copyTexture(1, outputs.depth, RHIFormat::D32Float);
            host.DeclareColor(*graph, outputs, deferred.GetOutput(), ao, {});
            copyTexture(2, deferred.GetOutput(), RHIFormat::RGBA16Float);
            copyTexture(3, outputs.depth, RHIFormat::D32Float);
            copyTexture(4, outputs.bitmask, RHIFormat::R32Uint);
            for (unsigned field = 0; field < 7; ++field)
            {
                copyTexture(5 + field, graph->FindImportedTexture(accepted->Inputs()[field]), RHIFormat::RGBA32Float);
            }
            copyTexture(12, graph->FindImportedTexture(host.LookupFrame()->Inputs()[0]), RHIFormat::RGBA32Float);
            Check(device.CreateBufferReadback(24 * 24 * 16, readbacks[13], error), "SSS irradiance readback");
            graph->AddPass(
                "Probe.SSS.IrradianceReadback", {{accepted->GraphIrradiance(*graph), RHIResourceState::CopySource}},
                [readback = readbacks[13], buffer = accepted->Irradiance()](const auto& execution) {
                    execution.encoder->CopyBufferToReadback(readback, buffer);
                },
                true);
            Check(graph->Compile(error), "SSS graph compile " + error);
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
                    "SSS parallel graph " + error);
                completion = ticket.GetRecordedBatch()->GetCompletionPoint();
            }
            else
            {
                Check(graph->Execute(error), "SSS sequential graph " + error);
            }
            Check(device.EndFrame(error) && GetRHISubmissionThread().DrainSubmissions(&device, error), "SSS submit");
            if (!workers)
            {
                completion = {device.GetLastSignaledFenceValue()};
            }
            Check(host.PublishSubmittedCache(context.frameId, completion, error, ticket), "SSS publication " + error);
            device.WaitForGpu();
            Check(GetRHISubmissionThread().Drain(&device, error), "SSS retirement");
            std::array<RHIReadbackImage, 14> mapped;
            for (unsigned i = 0; i < mapped.size(); ++i)
            {
                Check(device.MapReadback(readbacks[i], mapped[i], error), "SSS map readback");
            }
            const auto field = [&](unsigned imageIndex, unsigned x, unsigned y, unsigned channel) {
                float value;
                std::memcpy(&value,
                            mapped[imageIndex].data.data() + y * mapped[imageIndex].rowPitch + x * 16 + channel * 4, 4);
                return double(value);
            };
            const auto owner = [&](unsigned x, unsigned y) {
                std::uint32_t value;
                std::memcpy(&value, mapped[4].data.data() + y * mapped[4].rowPitch + x * 4, 4);
                return value;
            };
            for (unsigned y = 0; y < 24; ++y)
            {
                for (unsigned x = 0; x < 24; ++x)
                {
                    if (fixture == 3 && x >= 1 && x <= 22 && y >= 1 && y <= 22 &&
                        !(x >= 9 && x <= 14 && y >= 7 && y <= 16))
                    {
                        const unsigned tx = unsigned((x + .5) / 6);
                        const unsigned ty = unsigned(2 - (y + .5) / 12);
                        const bool visible = kTexels[ty * 4 + tx][3] >= 128;
                        Check(((owner(x, y) & 0x80000000u) != 0) == visible,
                              "SSS masked coverage matches independent texture alpha");
                        if (!visible)
                        {
                            ++maskedHoles;
                        }
                    }
                    Check(std::memcmp(mapped[1].data.data() + y * mapped[1].rowPitch + x * 4,
                                      mapped[3].data.data() + y * mapped[3].rowPitch + x * 4, 4) == 0,
                          "SSS preserves shared depth exactly");
                    if ((owner(x, y) & 0x80000000u) == 0)
                    {
                        Check(std::memcmp(mapped[0].data.data() + y * mapped[0].rowPitch + x * 8,
                                          mapped[2].data.data() + y * mapped[2].rowPitch + x * 8, 8) == 0,
                              "SSS preserves legacy/background HDR");
                        continue;
                    }
                    const double scale = fixture == 1 && owner(x, y) == 0x80000001u ? 0 : 1;
                    Vector color{}, position{};
                    for (unsigned c = 0; c < 3; ++c)
                    {
                        color[c] = field(12, x, y, c);
                        position[c] = field(6, x, y, c);
                    }
                    Near(float(position[0]), (x + .5) / 12 - 1, "SSS world position x");
                    Near(float(position[1]), 1 - (y + .5) / 12, "SSS world position y");
                    Near(float(position[2]), owner(x, y) == 0x80000001u ? .4 : .3, "SSS world depth");
                    const auto profile = SssProfile(color, scale);
                    for (unsigned c = 0; c < 3; ++c)
                    {
                        Near(float(field(8, x, y, c)), profile.real[c], "SSS real depth");
                        Near(float(field(9, x, y, c)), profile.virtualDepth[c], "SSS virtual depth");
                        Near(float(field(10, x, y, c)), profile.attenuation[c], "SSS attenuation");
                        Near(float(field(11, x, y, c)), profile.normalization[c], "SSS normalization");
                    }
                    const Vector offset = Vector{.2, .1, 1.5} - position;
                    const double distance = std::sqrt(Dot(offset, offset));
                    const Vector light = offset * (1 / distance);
                    const double falloff = Clamp(1 - distance / 10) / (1 + distance * distance);
                    const Vector radiance = Vector{3, 2, 1} * falloff;
                    Vector incident = radiance * (light[2] / kPi);
                    if (fixture != 2)
                    {
                        incident = incident + Vector{2, 1, .5};
                    }
                    for (unsigned c = 0; c < 3; ++c)
                    {
                        Near(float(field(5, x, y, c)), incident[c], "SSS independent incident irradiance");
                        double expected = incident[c], sum = 0, denominator = 0;
                        if (scale != 0 && c != 2)
                        {
                            for (int dy = -4; dy <= 4; ++dy)
                            {
                                for (int dx = -4; dx <= 4; ++dx)
                                {
                                    const int nx = int(x) + dx, ny = int(y) + dy;
                                    if (nx < 0 || ny < 0 || nx >= 24 || ny >= 24 || owner(nx, ny) != owner(x, y))
                                    {
                                        ++boundaries;
                                        continue;
                                    }
                                    Vector separation{};
                                    for (unsigned axis = 0; axis < 3; ++axis)
                                    {
                                        separation[axis] = position[axis] - field(6, nx, ny, axis);
                                    }
                                    const double weight =
                                        SssDensity(profile, std::sqrt(Dot(separation, separation)), c) *
                                        field(5, nx, ny, 3);
                                    denominator += weight;
                                    sum += weight * field(5, nx, ny, c);
                                }
                            }
                            expected = denominator > 0 ? sum / denominator : incident[c];
                            if (std::abs(expected - incident[c]) > 1e-5)
                            {
                                ++spread;
                            }
                        }
                        float actual;
                        std::memcpy(&actual, mapped[13].data.data() + (y * 24 + x) * 16 + c * 4, 4);
                        Near(actual, expected, "SSS independent world-distance dipole transport");
                    }
                    // Independent Special closure: pure and mixed metal retain
                    // their separate reflection and diffuse energy budgets.
                    ProbeInput source{};
                    source.baseAlpha = {float(color[0]), float(color[1]), float(color[2]), 1};
                    source.normalRoughness = {0, 0, 1, .5f};
                    source.metalIorLevelAo = {owner(x, y) == 0x80000002u ? .45f : 0, 1.5f, .5f, .25f};
                    source.tintAnisotropy = {1, 1, 1, 0};
                    source.tangentRotation = {1, 0, 0, 0};
                    auto material = Evaluate(source, 0x7ff);
                    auto metalMaterial = material, dielectricMaterial = material;
                    metalMaterial.metal = 1;
                    dielectricMaterial.metal = 0;
                    const Vector viewDirection = Unit(Vector{0, 0, 2} - position);
                    const auto metalIntegral = Integrate(metalMaterial, viewDirection, false);
                    const auto dielectricIntegral = Integrate(dielectricMaterial, viewDirection, false);
                    // Independent double-precision GGX energy LUT evaluation;
                    // compensation is multiplicative, without the retired diffuse lobe.
                    auto energy = Energy(material, viewDirection);
                    const double remainder = std::max(1 - Maximum(energy.dielectricAlbedo), 0.0);
                    const Vector baseDiffuse = color * ((1 - material.metal) * remainder);
                    const Vector normalization = ReflectionBudget(
                        baseDiffuse, metalIntegral.single * material.metal +
                                         dielectricIntegral.single * (1 - material.metal));
                    energy.metal = energy.metal * normalization;
                    energy.dielectric = energy.dielectric * normalization;
                    const Vector diffuse = color * ((1 - material.metal) * remainder * .2);
                    const Vector halfVector = Unit(viewDirection + light);
                    const auto frame = MakeFrame(material, false);
                    Vector result =
                        (Fresnel(metalMaterial, Dot(viewDirection, halfVector)) * energy.metal * material.metal +
                         Fresnel(dielectricMaterial, Dot(viewDirection, halfVector)) * energy.dielectric *
                             (1 - material.metal)) *
                        (Distribution(frame, halfVector) * Visibility(frame, viewDirection, light));
                    result = (result + diffuse * (1 / kPi)) * light[2] * radiance;
                    const Vector environmentColor = fixture == 2 ? Vector{} : Vector{2, 1, .5};
                    const double ao = Clamp(std::pow(Clamp(Dot(material.normal, viewDirection)) + .25,
                                                     std::exp2(-16 * material.roughness - 1)) -
                                            1 + .25);
                    result =
                        result +
                        normalization * (metalIntegral.single * material.metal +
                                         dielectricIntegral.single * (1 - material.metal)) *
                            environmentColor * ao +
                        diffuse * environmentColor * .25;
                    for (unsigned c = 0; c < 3; ++c)
                    {
                        float irradiance;
                        std::memcpy(&irradiance, mapped[13].data.data() + (y * 24 + x) * 16 + c * 4, 4);
                        result[c] += Clamp(color[c]) * (1 - material.metal) * remainder * .8 * irradiance;
                        Near(mapped[2].At(x, y, c), result[c], "SSS independent Special HDR composition", 1e-3);
                    }
                    ++pixels;
                }
            }
            for (auto& readback : readbacks)
            {
                device.ReleaseReadback(readback);
            }
            graph.reset();
            ++frames;
        }
    }
    Check(spread > 0 && boundaries > 0 && pixels > 0 && maskedHoles > 0,
          "SSS exercises nonlocal transport and masked boundaries: pixels=" + std::to_string(pixels) +
              " spread=" + std::to_string(spread) + " boundaries=" + std::to_string(boundaries));
    host.ShutdownAfterIdle();
    gbuffer.Shutdown();
    deferred.Shutdown();
    meshes.Shutdown();
    std::string messages;
    const auto validationMessages = device.DrainDebugMessages(messages);
    Check(validationMessages == 0, "SSS native GPU validation: " + messages);
    std::cout << "LX_MATERIAL_SCENE_SUBSURFACE_OK frames=" << frames << " pixels=" << pixels << " spread=" << spread
              << " boundaries=" << boundaries << " maskedHoles=" << maskedHoles << " checks=" << checks
              << " gpuComponents=" << gpuComponents << " validation=0\n";
}
