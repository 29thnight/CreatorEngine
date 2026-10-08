// Reuse the native raster probe's device fixtures without changing its gates.
#define main MaterialRasterProbeMain
#include "material_raster_surface_probe.cpp"
#undef main
#include <numeric>
#include <iomanip>
#include <map>
#include <DirectXPackedVector.h>
#include "RHI/DX12/EnhancedIBLGenerator.h"
#include "Assets/CookedEnvironment.h"
// Reads `error` only after `condition` ran. `Check(f(error), "label " + error)` builds
// the message first under MSVC (unspecified argument order) and reports an empty error.
void CheckWith(bool condition, const char* label, const std::string& error)
{
    Check(condition, condition ? std::string{} : label + error);
}

namespace
{
// Record each graph pass in its own GPU timestamp interval. A frame-wide
// interval can include submission gaps and must not substitute for bake cost.
struct MatchedPassTimer final : IRHIGpuProfiler
{
    static constexpr unsigned capacity = 128;
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> heap;
    std::vector<std::string> names;
    uint32_t BeginPass(RHIEncoder& encoder, const std::string& name) override
    {
        Check(names.size() < capacity, "Matched pass timestamp capacity");
        const auto slot = static_cast<uint32_t>(names.size());
        names.push_back(name);
        static_cast<DX12Encoder&>(encoder).GetCommandList()->EndQuery(heap.Get(),D3D12_QUERY_TYPE_TIMESTAMP,2*slot);
        return slot;
    }
    void EndPass(RHIEncoder& encoder, uint32_t slot) override
    {
        Check(slot < names.size(), "Matched pass timestamp pairing");
        static_cast<DX12Encoder&>(encoder).GetCommandList()->EndQuery(heap.Get(),D3D12_QUERY_TYPE_TIMESTAMP,2*slot+1);
    }
};

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

own::shared_owner<const Instance> MatchedInstance(const std::filesystem::path& root,
                                               const std::filesystem::path& shaderRoot,
                                               const std::filesystem::path& inputs,
                                               const std::filesystem::path& output,
                                               GenerationStore& store)
{
    LXMaterialAsset asset;
    const auto surface = asset.CreateNode("ShaderNodeBsdfPrincipled", 0, 0);
    const auto materialOutput = asset.CreateNode("ShaderNodeOutputMaterial", 400, 0);
    asset.activeOutput = materialOutput;
    Id volume{};
    bool volumeOnly{};
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
        if (name == "Volume Only")
        {
            Check(components == 1 && values[0] == 1.0, "Matched volume-only declaration");
            volumeOnly = true;
            continue;
        }
        const bool volumeInput = name.starts_with("Volume.");
        if (volumeInput && volume == 0)
            volume = asset.CreateNode("LXPrincipledVolume", 0, 300);
        const auto pin = Pin(asset.graph, volumeInput ? volume : surface,
                             volumeInput ? name.substr(7) : name, Direction::Input);
        asset.graph.SetSocketValue(pin, value); // A default-value assignment is a valid no-op.
        Check(asset.graph.FindPin(pin)->value == value, "Matched graph input " + name);
    }
    Check(stream.eof(), "Matched input parse");
    Check(!volumeOnly || volume != 0, "Matched volume-only requires volume");
    if (volume != 0)
        Check(asset.graph.Connect(Pin(asset.graph, volume, "Volume", Direction::Output),
                                  Pin(asset.graph, materialOutput, "Volume", Direction::Input)).has_value(),
              "Matched volume output connection");
    if (!volumeOnly)
    Check(asset.graph.Connect(Pin(asset.graph, surface, "BSDF", Direction::Output),
                              Pin(asset.graph, materialOutput, "Surface", Direction::Input)).has_value(),
          "Matched output connection");
    std::vector<LXMaterialDiagnostic> diagnostics;
    const auto generated = GenerateMaterialSlang(asset, &diagnostics);
    Check(!!generated, "Matched graph code generation");
    std::string error;
    VerifiedProduct product;
    CheckWith(CompileSceneProduct(*generated, shaderRoot,
                              output / (inputs.stem().string() + ".slang"), {}, product, error),
          "Matched complete Scene compiler ", error);
    experiment::AssetId id;
    Check(Uuid::TryParse("33333333-3333-8333-8333-333333333333", id.value), "Matched graph identity");
    const auto generation = store.Load(id, [&](CookedProgram& cooked, std::string&) {
        cooked = {product, WriteMaterialProgramMetadata(product.program), BuildBoundSource(product.program)};
        return true;
    }, true, error);
    own::shared_owner<const Instance> result;
    CheckWith(generation && BuildInstance(generation, {id, {}, {}}, {}, result, error), "Matched instance ", error);
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
    auto shaderRoot = paths->ShaderSourcePath / "DefaultPassShader";
    if (const auto* overrideRoot = std::getenv("CREATOR_MAT9_SHADER_ROOT"))
    {
        shaderRoot = std::filesystem::absolute(overrideRoot);
        Check(std::filesystem::is_regular_file(shaderRoot / "Includes/PrincipledEnvironmentBake.slang"),
              "Matched diagnostic shader root");
        paths->ShaderSourcePath = shaderRoot.parent_path();
    }
    std::ofstream(output / "shader-root.txt") << shaderRoot.string() << '\n';
    paths->AssetAuthoringEnabled = true;
    RecordingChangeDevice device;
    std::string error;
    CheckWith(device.Initialize(64, 64, error), "Matched native device ", error);
    ProbeRoots roots;
    ProbePipelines pipelines;
    ProbeTextures textures;
    ProbeMeshes meshes;
    CheckWith(roots.Initialize(&device, error) && pipelines.Initialize(&device, L"", error) &&
              textures.Initialize(&device, error) && meshes.Initialize(&device, error), "Matched caches ", error);
    SceneHost host;
    EnhancedGBufferPass gbuffer;
    EnhancedDeferredPass deferred;
    EnhancedForwardPass forward;
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
    CheckWith(gbuffer.Initialize(context, error) && deferred.Initialize(context, error) &&
              forward.Initialize(context, error), "Matched Scene passes ", error);
    std::filesystem::copy_file(fixture / "sphere.bin", output / "sphere.bin");
    auto geometry = ReadMatchedGeometry(output / "sphere.bin");
    const Environment environment{{{1,1,1},{1,1,1},{1,1,1},{1,1,1},{1,1,1},{1,1,1}}};
    const auto cube = Cube(environment);
    struct MatchedEnvironment { std::filesystem::path file; float strength{}; assets::CookedEnvironment cooked; };
    std::map<std::string, MatchedEnvironment> hdrEnvironments;
    if (std::filesystem::exists(fixture / "environment.config"))
    {
        std::ifstream configuration(fixture / "environment.config");
        std::string mode, path;
        float strength{};
        while (configuration >> mode >> std::quoted(path) >> strength)
        {
            Check((mode == "forest" || mode == "autumn") && strength == .35f &&
                      !hdrEnvironments.contains(mode), "Matched environment config");
            MatchedEnvironment value{std::filesystem::path(path), strength};
            CheckWith(assets::ReadCookedEnvironment(value.file, value.cooked, error), "Matched cooked environment ", error);
            // Strength scales every lighting map once, preserving the cook on disk.
            for (unsigned map = 0; map < 4; ++map)
            {
                auto& image = map < 3 ? value.cooked.images[map] : value.cooked.source;
                if (!image.IsValid()) continue;
                const bool half = image.Format() == RHIFormat::RGBA16Float;
                Check(half || image.Format() == RHIFormat::RGBA32Float, "Matched cook lighting format");
                const auto imageView = image.View();
                for (unsigned slice = 0; slice < imageView.SubresourceCount(); ++slice)
                {
                    const auto* description = imageView.At(slice);
                    auto* bytes = image.MutablePixelsAt(*description);
                    auto* pixels = reinterpret_cast<std::uint16_t*>(bytes);
                    auto* floats = reinterpret_cast<float*>(bytes);
                    for (std::size_t pixel = 0; pixel < description->slicePitch / (half ? 8 : 16); ++pixel)
                        for (unsigned c = 0; c < 3; ++c)
                            if (half) pixels[pixel*4+c] = DirectX::PackedVector::XMConvertFloatToHalf(
                                DirectX::PackedVector::XMConvertHalfToFloat(pixels[pixel*4+c]) * strength);
                            else floats[pixel*4+c] *= strength;
                }
            }
            hdrEnvironments.emplace(mode, std::move(value));
        }
        Check(configuration.eof() && hdrEnvironments.size() == 2, "Matched complete HDRI pair");
        std::filesystem::copy_file(fixture / "environment.config", output / "environment.config");
    }
    EnhancedIBLGenerator hdri;
    CheckWith(hdri.Initialize(context, error), "Matched cooked IBL owner ", error);
    std::string currentEnvironment;
    GenerationStore store;
    std::vector<std::filesystem::path> cases;
    for (const auto& file : std::filesystem::directory_iterator(fixture))
        if (file.path().extension() == ".inputs") cases.push_back(file.path());
    std::ranges::sort(cases);
    unsigned frame{};
    const bool measureTiming=std::getenv("CREATOR_MAT9_TIMING")!=nullptr;
    const bool forceRebake=std::getenv("CREATOR_MAT9_TIMING_REBAKE")!=nullptr;
    const bool disableMis=std::getenv("CREATOR_MAT9_DISABLE_MIS")!=nullptr;
    if(disableMis) std::ofstream(output/"mis-disabled.txt")<<"Diagnostic baseline; not product acceptance.\n";
    // Live-view lookup path (split-sum approximation). The images report how far
    // the live view sits from Blender; they are not reference-path acceptance.
    material_graph::SceneHostBudget lookupBudget{};
    lookupBudget.lookupApproximate=std::getenv("CREATOR_MAT9_LOOKUP_APPROXIMATE")!=nullptr;
    if(lookupBudget.lookupApproximate) std::ofstream(output/"lookup-approximate.txt")<<"Live split-sum lookup; difference report, not reference acceptance.\n";
    std::ofstream timing(output/"timing.csv");
    timing << "case,repeat,program_prepare_ms,frame_prepare_ms,gpu_frame_ms\n";
    std::ofstream passTiming(output/"pass-timing.csv");
    passTiming << "case,repeat,pass,gpu_ms\n";
    for (const auto& inputFile : cases)
    {
        const auto name = inputFile.stem().string();
        const bool furnace = name.starts_with("furnace-");
        const auto mode = name.substr(0, name.find('-'));
        const bool hdr = hdrEnvironments.contains(mode);
        Check(furnace || hdr || mode == "sun", "Matched lighting mode");
        std::cerr << "MAT9_MATCHED_FRAME " << name << '\n';
        const auto capturedInput = output / inputFile.filename();
        std::filesystem::copy_file(inputFile, capturedInput);
        geometry.draw.materialGraphInstance = MatchedInstance(root, shaderRoot, capturedInput, output, store);
        geometry.draw.materialGraphSlot = 1;
        const auto programStart=std::chrono::steady_clock::now();
        WaitSceneProgram(host, context, geometry.draw.materialGraphInstance->generation);
        const auto programMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-programStart).count();
        for (unsigned repeat=0;repeat<(measureTiming?9u:1u);++repeat)
        {
        context.frameId = ++frame;
        context.sceneEpoch = forceRebake ? context.frameId : 1;
        lights.clear();
        if (!furnace && !hdr)
        {
            const auto direction = Unit({.35, -.2, .8});
            EnhancedLight light;
            light.direction = math::vector4(float(-direction[0]), float(-direction[1]), float(-direction[2]), 0);
            light.color = math::color(1,1,1,1);
            lights.push_back(light);
        }
        SceneInputView view{context.frameId, context.sceneEpoch, 1, 1, 64, 64, camera};
        own::shared_owner<const SceneViewInput> input;
        CheckWith(SceneViewInput::Seal(view, {&geometry.draw, 1}, {}, input, error), "Matched Scene seal ", error);
        CheckWith(device.BeginFrame(error), "Matched begin ", error);
        textures.BeginFrame(context.frameId);
        meshes.BeginFrame(static_cast<unsigned>(context.frameId));
        RHITextureHandle environmentHandle;
        RHITextureHandle irradianceHandle, prefilteredHandle;
        if (furnace)
        {
            const auto uploaded = textures.GetOrUpload((cube ? &*cube.borrow() : nullptr), cube ? cube->NonRehydratableImage() : own::shared_owner<const Texture::CodecImage>{}, error);
            CheckWith(uploaded.IsValid(), "Matched furnace cube ", error);
            environmentHandle = uploaded.handle;
        }
        if (hdr)
        {
            if (currentEnvironment != mode)
            {
                CheckWith(hdri.InstallCooked(context, std::move(hdrEnvironments.at(mode).cooked), error),
                      "Matched HDRI install ", error);
                currentEnvironment = mode;
            }
            CheckWith(hdri.TouchCooked(context, error), "Matched HDRI residency ", error);
            environmentHandle = hdri.GetCubeMap();
            irradianceHandle = hdri.GetIrradianceMap();
            prefilteredHandle = hdri.GetPrefilteredMap();
        }
        Microsoft::WRL::ComPtr<ID3D12QueryHeap> timer;
        RHIReadback timerReadback;
        RHIReadback passTimerReadback;
        MatchedPassTimer passTimer;
        uint64_t timerFrequency{};
        if (measureTiming)
        {
            const D3D12_QUERY_HEAP_DESC timerDesc{D3D12_QUERY_HEAP_TYPE_TIMESTAMP,2,0};
            Check(SUCCEEDED(device.GetDevice()->CreateQueryHeap(&timerDesc,IID_PPV_ARGS(&timer))) &&
                SUCCEEDED(device.GetCommandQueue()->GetTimestampFrequency(&timerFrequency)) &&
                device.CreateBufferReadback(16,timerReadback,error),"Matched GPU timer");
            const D3D12_QUERY_HEAP_DESC passDesc{D3D12_QUERY_HEAP_TYPE_TIMESTAMP,2*MatchedPassTimer::capacity,0};
            Check(SUCCEEDED(device.GetDevice()->CreateQueryHeap(&passDesc,IID_PPV_ARGS(&passTimer.heap))) &&
                  device.CreateBufferReadback(16*MatchedPassTimer::capacity,passTimerReadback,error),
                  "Matched pass GPU timers");
        }
        const auto prepareStart=std::chrono::steady_clock::now();
        // The old frame-1 check required the host to reject a Blended draw
        // ("Blended composition is not installed"). 8bfd0be5 installed the common
        // Forward+ composition for Blended and transmission draws, so that premise
        // no longer holds; those draws now go through the Forward+ pass below.
        const bool prepared = gbuffer.PrepareFrame(context, error) && deferred.PrepareFrame(context, error) &&
                              forward.PrepareFrame(context, error) &&
                              host.PrepareResidency(context, input, error) &&
                              host.Prepare(context, input, environmentHandle, irradianceHandle, prefilteredHandle, {}, lookupBudget, error,
                                           hdr ? hdri.GetGeneration() : 1,
                                           hdr && !disableMis ? hdri.GetImportanceMaps() : std::array<RHITextureHandle,3>{},
                                           hdr ? hdri.GetSourceMap() : RHITextureHandle{});
        CheckWith(prepared, "Matched prepare ", error);
        const auto prepareMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-prepareStart).count();
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
        const auto lit = host.DeclareColor(*graph, outputs, deferred.GetOutput(), ao, {});
        // Same order as the product renderer: the Forward+ pass declares every
        // Blended/transmission draw (glass) over the lit color, and the graph
        // Volume when it has forward draws; the LX.Scene.Volume step then declares
        // the Volume if Forward+ skipped (DeclareVolume runs once per frame).
        // Declaring only the Volume left glass out of the Forward+ stream, and the
        // cache publication rejected the incomplete stream.
        forward.SetInputs({outputs.depth, lit});
        forward.SetGraphMaterials(&host);
        forward.Declare(*graph, context);
        const auto forwardColor = forward.GetOutput().IsValid() ? forward.GetOutput() : lit;
        const auto color = host.DeclareVolume(*graph, forwardColor, outputs.depth, {});
        RHIReadback readback;
        CheckWith(device.CreateReadback(64, 64, RHIFormat::RGBA16Float, 1, readback, error), "Matched readback ", error);
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
        if (measureTiming)
        {
            graph->SetProfiler(&passTimer);
            static_cast<DX12Encoder&>(device.GetImmediateEncoder()).GetCommandList()->EndQuery(timer.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0);
        }
        if (!graph->Execute(error)) throw std::runtime_error("Matched graph execute: " + error);
        if (measureTiming)
        {
            auto* list=static_cast<DX12Encoder&>(device.GetImmediateEncoder()).GetCommandList();
            list->EndQuery(timer.Get(),D3D12_QUERY_TYPE_TIMESTAMP,1);
            list->ResolveQueryData(timer.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0,2,device.Resolve(timerReadback.buffer),0);
            list->ResolveQueryData(passTimer.heap.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0,
                                   static_cast<UINT>(2*passTimer.names.size()),device.Resolve(passTimerReadback.buffer),0);
        }
        if (!device.EndFrame(error)) throw std::runtime_error("Matched frame submit: " + error);
        if (!GetRHISubmissionThread().DrainSubmissions(&device, error))
            throw std::runtime_error("Matched submission drain: " + error);
        CheckWith(host.PublishSubmittedCache(context.frameId, {device.GetLastSignaledFenceValue()}, error),
              "Matched publication ", error);
        device.WaitForGpu();
        CheckWith(GetRHISubmissionThread().Drain(&device, error), "Matched retirement ", error);
        if (measureTiming)
        {
            RHIReadbackImage times;
            Check(device.MapReadback(timerReadback,times,error),"Matched GPU timer map");
            std::array<uint64_t,2> ticks;
            std::memcpy(ticks.data(),times.data.data(),16);
            Check(ticks[1]>=ticks[0] && timerFrequency>0,"Matched GPU timer order");
            timing << name << ',' << repeat << ',' << programMs << ',' << prepareMs << ','
                   << double(ticks[1]-ticks[0])*1000/timerFrequency << '\n';
            device.ReleaseReadback(timerReadback);
            RHIReadbackImage passTimes;
            Check(device.MapReadback(passTimerReadback,passTimes,error),"Matched pass GPU timer map");
            for (unsigned slot=0;slot<passTimer.names.size();++slot)
            {
                std::array<uint64_t,2> passTicks;
                std::memcpy(passTicks.data(),passTimes.data.data()+16*slot,16);
                Check(passTicks[1]>=passTicks[0],"Matched pass GPU timer order");
                passTiming << name << ',' << repeat << ',' << std::quoted(passTimer.names[slot]) << ','
                           << double(passTicks[1]-passTicks[0])*1000/timerFrequency << '\n';
            }
            device.ReleaseReadback(passTimerReadback);
        }
        RHIReadbackImage mapped;
        CheckWith(device.MapReadback(readback, mapped, error), "Matched map ", error);
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
    }
    host.ShutdownAfterIdle();
    hdri.Shutdown();
    gbuffer.Shutdown(); deferred.Shutdown(); forward.Shutdown(); meshes.Shutdown(); textures.Shutdown(); pipelines.Shutdown(); roots.Shutdown();
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
