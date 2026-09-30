// Reuse the native raster probe's device fixtures without changing its gates.
#define main MaterialRasterProbeMain
#include "material_raster_surface_probe.cpp"
#undef main
#include <numeric>
#include <iomanip>

namespace
{
struct MatchedGeometry
{
    std::vector<std::byte> vertices;
    std::vector<std::uint32_t> indices;
    EnhancedDrawItem draw;
};

MatchedGeometry ReadMatchedGeometry(const std::filesystem::path& file)
{
    std::ifstream stream(file, std::ios::binary);
    std::uint32_t count{};
    stream.read(reinterpret_cast<char*>(&count), sizeof(count));
    Check(count > 0 && count <= 1000000 && count % 3 == 0, "Matched sphere vertex count");
    MatchedGeometry result;
    auto& mesh = result.draw.modelMeshView;
    Check(Uuid::TryParse("11111111-1111-8111-8111-111111111111", mesh.handle.modelId) &&
              Uuid::TryParse("22222222-2222-8222-8222-222222222222", mesh.handle.meshId), "Matched geometry identity");
    mesh.handle.generation = 1;
    mesh.vertexAttributeMask = assets::kModelVertexMasks.front();
    mesh.vertexStride = assets::StrideOf(mesh.vertexAttributeMask);
    mesh.vertexLayoutHash = assets::VertexLayoutHash(mesh.vertexAttributeMask);
    result.vertices.resize(std::size_t(count) * mesh.vertexStride);
    result.indices.resize(count);
    std::iota(result.indices.begin(), result.indices.end(), 0u);
    for (unsigned vertex = 0; vertex < count; ++vertex)
    {
        std::array<float, 12> input;
        stream.read(reinterpret_cast<char*>(input.data()), sizeof(input));
        Check(bool(stream), "Matched sphere binary completeness");
        const auto write = [&](assets::VertexAttribute attribute, unsigned source, unsigned size) {
            std::memcpy(result.vertices.data() + vertex * mesh.vertexStride +
                            assets::OffsetOf(mesh.vertexAttributeMask, attribute), input.data() + source, size);
        };
        write(assets::VertexAttribute::Position, 0, 12);
        write(assets::VertexAttribute::Normal, 3, 12);
        write(assets::VertexAttribute::Tangent, 6, 16);
        write(assets::VertexAttribute::Uv0, 10, 8);
    }
    // Reflection in the camera view changes Blender's CCW face convention.
    for (unsigned triangle = 0; triangle < count; triangle += 3)
        std::swap(result.indices[triangle + 1], result.indices[triangle + 2]);
    mesh.vertexData = result.vertices.data();
    mesh.vertexBytes = result.vertices.size();
    mesh.indexData = result.indices.data();
    mesh.indexCount = count;
    result.draw.geometryKey = 1;
    result.draw.worldMatrix = math::matrix4x4::identity();
    result.draw.coverage.flags = EnhancedMaterialCoverage::Enabled;
    return result;
}

std::shared_ptr<const Instance> MatchedInstance(const std::filesystem::path& root,
                                               const std::filesystem::path& inputs,
                                               const std::filesystem::path& output,
                                               GenerationStore& store)
{
    LXMaterialAsset asset;
    const auto surface = asset.CreateNode("ShaderNodeBsdfPrincipled", 0, 0);
    const auto materialOutput = asset.CreateNode("ShaderNodeOutputMaterial", 400, 0);
    asset.activeOutput = materialOutput;
    std::ifstream stream(inputs);
    std::string name;
    unsigned components{};
    while (stream >> std::quoted(name) >> components)
    {
        std::array<double, 4> values{};
        Check(components == 1 || components == 3 || components == 4, "Matched input shape");
        for (unsigned c = 0; c < components; ++c)
            stream >> values[c];
        Check(bool(stream), "Matched input components");
        LXSocketValue value = components == 1 ? LXSocketValue{values[0]} : components == 3
                                  ? LXSocketValue{std::array<double, 3>{values[0], values[1], values[2]}}
                                  : LXSocketValue{values};
        Check(asset.graph.SetSocketValue(Pin(asset.graph, surface, name, Direction::Input), value),
              "Matched graph input " + name);
    }
    Check(stream.eof(), "Matched input parse");
    Check(asset.graph.Connect(Pin(asset.graph, surface, "BSDF", Direction::Output),
                              Pin(asset.graph, materialOutput, "Surface", Direction::Input)).has_value(),
          "Matched output connection");
    std::vector<LXMaterialDiagnostic> diagnostics;
    const auto generated = GenerateMaterialSlang(asset, &diagnostics);
    Check(!!generated, "Matched graph code generation");
    std::string error;
    VerifiedProduct product;
    Check(CompileSceneProduct(*generated, root / "Dynamic_CPP/Assets/Shaders/DefaultPassShader",
                              output / (inputs.stem().string() + ".slang"), {}, product, error),
          "Matched complete Scene compiler " + error);
    experiment::AssetId id;
    Check(Uuid::TryParse("33333333-3333-8333-8333-333333333333", id.value), "Matched graph identity");
    const auto generation = store.Load(id, [&](CookedProgram& cooked, std::string&) {
        cooked = {product, WriteMaterialProgramMetadata(product.program), BuildBoundSource(product.program)};
        return true;
    }, true, error);
    std::shared_ptr<const Instance> result;
    Check(generation && BuildInstance(generation, {id, {}, {}}, {}, result, error), "Matched instance " + error);
    return result;
}

void RunMatched(const std::filesystem::path& root, const std::filesystem::path& fixture,
                const std::filesystem::path& output)
{
    Check(!std::filesystem::exists(output), "Output must be new");
    std::filesystem::create_directories(output);
    auto& jobs = ce::get_job_scheduler();
    jobs.start(4);
    struct JobDrain { job_scheduler& jobs; ~JobDrain() { jobs.shutdown(); } } drainJobs{jobs};
    auto* paths = InternalPath::GetInstance();
    paths->BaseProjectPath = root / "Dynamic_CPP";
    paths->CacheRoot = output / "ShaderCache";
    paths->ShaderSourcePath = root / "Dynamic_CPP/Assets/Shaders";
    paths->AssetAuthoringEnabled = true;
    RecordingChangeDevice device;
    std::string error;
    Check(device.Initialize(64, 64, error), "Matched native device " + error);
    ProbeRoots roots;
    ProbePipelines pipelines;
    ProbeTextures textures;
    ProbeMeshes meshes;
    Check(roots.Initialize(&device, error) && pipelines.Initialize(&device, L"", error) &&
              textures.Initialize(&device, error) && meshes.Initialize(&device, error), "Matched caches " + error);
    SceneHost host;
    EnhancedGBufferPass gbuffer;
    EnhancedDeferredPass deferred;
    EnhancedFrameContext context;
    context.resources = &device;
    context.rootSignatures = &roots;
    context.psoManager = &pipelines;
    context.textureCache = &textures;
    context.meshCache = &meshes;
    context.width = context.height = 64;
    FrameCameraSnapshot camera;
    camera.view = math::matrix4x4::identity();
    camera.view.m[2][2] = -1;
    camera.view.m[3][2] = 3;
    camera.projection = math::matrix4x4{};
    camera.projection.m[0][0] = camera.projection.m[1][1] = float(1 / std::tan(3.141592653589793 / 8));
    camera.projection.m[2][2] = 10.f / 9.9f;
    camera.projection.m[2][3] = 1;
    camera.projection.m[3][2] = -1.f / 9.9f;
    camera.eyePosition = math::vector3(0, 0, 3);
    context.camera = &camera;
    std::vector<EnhancedDrawItem> empty;
    context.draws = &empty;
    std::vector<EnhancedLight> lights;
    context.lights = &lights;
    Check(gbuffer.Initialize(context, error) && deferred.Initialize(context, error), "Matched Scene passes " + error);
    std::filesystem::copy_file(fixture / "sphere.bin", output / "sphere.bin");
    auto geometry = ReadMatchedGeometry(output / "sphere.bin");
    const Environment environment{{{1,1,1},{1,1,1},{1,1,1},{1,1,1},{1,1,1},{1,1,1}}};
    const auto cube = Cube(environment);
    GenerationStore store;
    std::vector<std::filesystem::path> cases;
    for (const auto& file : std::filesystem::directory_iterator(fixture))
        if (file.path().extension() == ".inputs") cases.push_back(file.path());
    std::ranges::sort(cases);
    unsigned frame{};
    for (const auto& inputFile : cases)
    {
        const auto name = inputFile.stem().string();
        const bool furnace = name.starts_with("furnace-");
        std::cerr << "MAT9_MATCHED_FRAME " << name << '\n';
        const auto capturedInput = output / inputFile.filename();
        std::filesystem::copy_file(inputFile, capturedInput);
        geometry.draw.materialGraphInstance = MatchedInstance(root, capturedInput, output, store);
        geometry.draw.materialGraphSlot = 1;
        WaitSceneProgram(host, context, geometry.draw.materialGraphInstance->generation);
        context.frameId = ++frame;
        context.sceneEpoch = 1;
        lights.clear();
        if (!furnace)
        {
            const auto direction = Unit({.35, -.2, .8});
            EnhancedLight light;
            light.direction = math::vector4(float(-direction[0]), float(-direction[1]), float(-direction[2]), 0);
            light.color = math::color(1,1,1,1);
            lights.push_back(light);
        }
        SceneInputView view{context.frameId, context.sceneEpoch, 1, 1, 64, 64, camera};
        std::shared_ptr<const SceneViewInput> input;
        Check(SceneViewInput::Seal(view, {&geometry.draw, 1}, {}, input, error), "Matched Scene seal " + error);
        Check(device.BeginFrame(error), "Matched begin " + error);
        textures.BeginFrame(context.frameId);
        meshes.BeginFrame(static_cast<unsigned>(context.frameId));
        RHITextureHandle environmentHandle;
        if (furnace)
        {
            const auto uploaded = textures.GetOrUpload(cube.get(), error);
            Check(uploaded.IsValid(), "Matched furnace cube " + error);
            environmentHandle = uploaded.handle;
        }
        Check(gbuffer.PrepareFrame(context, error) && deferred.PrepareFrame(context, error) &&
                  host.PrepareResidency(context, input, error) &&
                  host.Prepare(context, input, environmentHandle, {}, {}, {}, {}, error, 1), "Matched prepare " + error);
        auto graph = std::make_shared<EnhancedRenderGraph>(device);
        gbuffer.Declare(*graph, context);
        const auto outputs = gbuffer.GetOutputs();
        host.DeclareGBuffer(*graph, outputs);
        RGTextureDesc aoDesc;
        aoDesc.width = aoDesc.height = 64;
        aoDesc.format = RHIFormat::RG16Float;
        aoDesc.allowRenderTarget = true;
        aoDesc.clearColor[0] = 1;
        const auto ao = graph->CreateTexture(aoDesc);
        graph->AddPass("MAT9.NeutralAO", {{ao, RHIResourceState::RenderTarget}},
                       [&device,ao](const auto& execution) {
                           const auto handle = execution.ResolveHandle(ao);
                           const auto target = device.CreateRenderTargets({&handle,1}, nullptr);
                           Check(target.IsValid(), "Matched AO render target");
                           const float clear[]{1,0,0,0};
                           execution.encoder->BindRenderTargets(target);
                           execution.encoder->ClearRenderTargets(target, clear);
                       });
        deferred.SetInputs(outputs);
        deferred.SetAmbientOcclusion(ao);
        deferred.Declare(*graph, context);
        host.DeclareColor(*graph, outputs, deferred.GetOutput(), ao, {});
        RHIReadback readback;
        Check(device.CreateReadback(64, 64, RHIFormat::RGBA16Float, 1, readback, error), "Matched readback " + error);
        const auto color = deferred.GetOutput();
        graph->AddPass("MAT9.ImageReadback", {{color, RHIResourceState::CopySource}},
                       [readback,color](const auto& execution) {
                           execution.encoder->CopyToReadback(readback, execution.ResolveHandle(color));
                       }, true);
        std::array<RHIReadback, 3> diagnostics{};
        if (frame == 1)
        {
            const auto lookup = host.LookupFrame();
            const std::array fields{1u,9u,10u};
            for (unsigned i = 0; i < fields.size(); ++i)
            {
                Check(device.CreateReadback(64,64,RHIFormat::RGBA32Float,1,diagnostics[i],error),
                      "Matched geometry diagnostic readback");
                const auto resource = graph->FindImportedTexture(lookup->Inputs()[fields[i]]);
                graph->AddPass("MAT9.GeometryReadback", {{resource,RHIResourceState::CopySource}},
                               [readback=diagnostics[i],resource](const auto& execution) {
                                   execution.encoder->CopyToReadback(readback,execution.ResolveHandle(resource));
                               },true);
                graph->AddPass("MAT9.GeometryRestore", {{resource,RHIResourceState::ShaderResource}},
                               [](const auto&) {},true);
            }
        }
        if (!graph->Compile(error)) throw std::runtime_error("Matched graph compile: " + error);
        if (!graph->Execute(error)) throw std::runtime_error("Matched graph execute: " + error);
        if (!device.EndFrame(error)) throw std::runtime_error("Matched frame submit: " + error);
        if (!GetRHISubmissionThread().DrainSubmissions(&device, error))
            throw std::runtime_error("Matched submission drain: " + error);
        Check(host.PublishSubmittedCache(context.frameId, {device.GetLastSignaledFenceValue()}, error),
              "Matched publication " + error);
        device.WaitForGpu();
        Check(GetRHISubmissionThread().Drain(&device, error), "Matched retirement " + error);
        RHIReadbackImage mapped;
        Check(device.MapReadback(readback, mapped, error), "Matched map " + error);
        std::ofstream image(output / (name + ".f32"), std::ios::binary);
        // Canonical image coordinates match Blender's bottom-up pixel array.
        for (unsigned y = 0; y < 64; ++y)
            for (unsigned x = 0; x < 64; ++x)
                for (unsigned c = 0; c < 4; ++c)
                {
                    const float value = mapped.At(x, 63-y, c);
                    Check(std::isfinite(value), "Matched finite output");
                    image.write(reinterpret_cast<const char*>(&value), sizeof(value));
                }
        device.ReleaseReadback(readback);
        if (frame == 1)
        {
            const std::array stems{"debug-normal","debug-tangent","debug-view"};
            for (unsigned i = 0; i < diagnostics.size(); ++i)
            {
                RHIReadbackImage mappedDiagnostic;
                Check(device.MapReadback(diagnostics[i],mappedDiagnostic,error), "Matched diagnostic map");
                std::ofstream diagnosticImage(output / (std::string(stems[i])+".f32"), std::ios::binary);
                for (unsigned y=0;y<64;++y) for (unsigned x=0;x<64;++x) for (unsigned c=0;c<4;++c)
                {
                    const float value=c<3 ? mappedDiagnostic.At(x,63-y,c)*.5f+.5f : mappedDiagnostic.At(x,63-y,c);
                    diagnosticImage.write(reinterpret_cast<const char*>(&value),sizeof(value));
                }
                device.ReleaseReadback(diagnostics[i]);
            }
        }
        graph.reset();
        std::string validation;
        Check(device.DrainDebugMessages(validation) == 0, "Matched GPU validation " + validation);
    }
    host.ShutdownAfterIdle();
    gbuffer.Shutdown(); deferred.Shutdown(); meshes.Shutdown(); textures.Shutdown(); pipelines.Shutdown(); roots.Shutdown();
    device.Shutdown();
    std::filesystem::copy_file(fixture / "manifest.json",output / "reference-manifest.json");
    std::cout << "MAT9_MATCHED_IMAGES_OK cases=" << cases.size() << " validation=0 checks=" << checks << '\n';
}
}

int main(int argc, char** argv)
{
#ifdef _DEBUG
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportHook(ReportAssertion);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    try
    {
        Check(argc == 4, "Expected repository, matched fixture and new output directory");
        RunMatched(std::filesystem::absolute(argv[1]), std::filesystem::absolute(argv[2]), std::filesystem::absolute(argv[3]));
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "MAT9_MATCHED_IMAGES_FAIL " << error.what() << '\n';
        return 1;
    }
}
