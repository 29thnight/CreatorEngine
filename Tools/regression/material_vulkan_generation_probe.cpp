#include "MaterialGraphSceneHost.h"
#include "MaterialGraphSurfaceBatch.h"
#include "PathFinder.h"
#include "RHI/Vulkan/VulkanDeviceResources.h"
#include "RHI/Vulkan/VulkanPipelineCache.h"
#include "RHI/Vulkan/VulkanEncoder.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <thread>
#ifdef _DEBUG
#include <crtdbg.h>
#endif

namespace
{
using namespace material_graph;
using namespace LX;
unsigned checks{}, pixels{}, scenePrograms{}, scenePsoWorkers{}, sceneReadyRequests{};

void Check(bool condition, const std::string& message)
{
    ++checks;
    if (!condition)
        throw std::runtime_error(message);
}

class WorkerGate
{
  public:
    explicit WorkerGate(job_scheduler& jobs)
    {
        completion_ = jobs.submit([this] {
            entered_.store(true, std::memory_order_release);
            while (!released_.load(std::memory_order_acquire))
                std::this_thread::yield();
        });
        while (!entered_.load(std::memory_order_acquire))
            std::this_thread::yield();
    }
    ~WorkerGate() { Release(); }
    void Release()
    {
        released_.store(true, std::memory_order_release);
        completion_.wait();
    }

  private:
    std::atomic<bool> entered_{}, released_{};
    job_handle completion_;
};

RHIPipelineRequestState Wait(VulkanPipelineCache& cache, const RHIGraphicsPipelineDesc& desc, RHIPipelineHandle& result,
                             std::string& error)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(90);
    for (;;)
    {
        const auto state = cache.RequestGraphics(desc, result, error);
        if (state != RHIPipelineRequestState::Pending)
            return state;
        Check(std::chrono::steady_clock::now() < deadline, "Native Vulkan PSO timeout");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void Draw(VulkanDeviceResources& device, RHIPipelineHandle pipeline)
{
    std::string error;
    RHITextureDesc description;
    description.width = description.height = 16;
    description.format = RHIFormat::RGBA32Float;
    description.allowRenderTarget = true;
    RHITextureHandle color;
    RHIReadback readback;
    Check(device.CreateTexture(description, color, error), "Native draw texture " + error);
    Check(device.CreateReadback(16, 16, description.format, 1, readback, error), "Native readback " + error);
    Check(device.BeginFrame(error), "Native begin " + error);
    const RHITransition before{color, RHIResourceState::Common, RHIResourceState::RenderTarget};
    device.TransitionResources({&before, 1});
    const auto targets = device.CreateRenderTargets({&color, 1});
    Check(targets.IsValid(), "Native render target");
    auto& encoder = static_cast<VulkanEncoder&>(device.GetImmediateEncoder());
    const float clear[4]{};
    encoder.BindRenderTargets(targets);
    encoder.ClearRenderTargets(targets, clear);
    encoder.SetViewportAndScissor(16, 16);
    encoder.SetPipeline(RHIBindPoint::Graphics, pipeline);
    encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
    const float vertices[]{-1, -1, 3, -1, -1, 3};
    const auto upload = device.AllocateUpload(sizeof(vertices), 16);
    Check(upload.IsWritable(), "Native vertex upload");
    std::memcpy(upload.cpuAddress, vertices, sizeof(vertices));
    encoder.SetVertexBuffer(upload, 2 * sizeof(float));
    encoder.Draw(3, 1);
    encoder.EndRenderTargets();
    const RHITransition after{color, RHIResourceState::RenderTarget, RHIResourceState::CopySource};
    device.TransitionResources({&after, 1});
    encoder.CopyToReadback(readback, color);
    Check(device.EndFrame(error), "Native submit " + error);
    device.WaitForGpu();
    RHIReadbackImage image;
    Check(device.MapReadback(readback, image, error), "Native map " + error);
    constexpr float expected[]{.25f, .5f, .75f, 1.f};
    for (unsigned y = 0; y < 16; ++y)
    {
        for (unsigned x = 0; x < 16; ++x)
        {
            for (unsigned channel = 0; channel < 4; ++channel)
            {
                Check(std::abs(image.At(x, y, channel) - expected[channel]) < 1e-6f,
                      "Worker PSO output differs from independent constant color");
            }
            ++pixels;
        }
    }
    device.ReleaseReadback(readback);
    device.ReleaseTexture(color);
}

Id FindPin(const LXGraph& graph, Id node, const char* identifier, Direction direction)
{
    for (const auto& pin : graph.FindNode(node)->pins)
    {
        if (pin.Identifier() == identifier && pin.direction == direction)
            return pin.id;
    }
    throw std::runtime_error("Missing fixture socket");
}

std::shared_ptr<const Generation> SceneGeneration(const std::filesystem::path& root, bool layered)
{
    LXMaterialAsset asset;
    const auto surface = asset.CreateNode("ShaderNodeBsdfPrincipled", 0, 0);
    const auto output = asset.CreateNode("ShaderNodeOutputMaterial", 300, 0);
    asset.activeOutput = output;
    if (layered)
    {
        Check(asset.graph.SetSocketValue(FindPin(asset.graph, surface, "Coat Weight", Direction::Input), .5),
              "Layered fixture coat");
    }
    Check(asset.graph
              .Connect(FindPin(asset.graph, surface, "BSDF", Direction::Output),
                       FindPin(asset.graph, output, "Surface", Direction::Input))
              .has_value(),
          "Fixture link");
    std::vector<LXMaterialDiagnostic> diagnostics;
    const auto program = GenerateMaterialSlang(asset, &diagnostics);
    Check(!!program, "Scene fixture code generation");
    const auto file =
        root / "Build/Obj/MaterialProductProbe" / (layered ? "vulkan-layered.slang" : "vulkan-core.slang");
    std::ofstream(file, std::ios::binary | std::ios::trunc) << BuildSurfaceSource(*program);
    const CompileTarget targets[]{
        {RHIShaderBinary::Dxil, "LXEvaluateSurface", "cs_6_0"}, {RHIShaderBinary::Dxil, "LXSurfaceVS", "vs_6_0"},
        {RHIShaderBinary::Dxil, "LXSurfacePS", "ps_6_0"},       {RHIShaderBinary::SpirV, "LXEvaluateSurface", "cs_6_0"},
        {RHIShaderBinary::SpirV, "LXSurfaceVS", "vs_6_0"},      {RHIShaderBinary::SpirV, "LXSurfacePS", "ps_6_0"}};
    RHIShaderCompileOptions options;
    options.strictMath = true;
    options.includeDirectories.push_back(root / "Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes");
    VerifiedProduct product;
    const bool verified = VerifySurfaceProduct(*program, file, targets, options, {}, product, diagnostics);
    std::string message;
    for (const auto& diagnostic : diagnostics)
        message += diagnostic.message + "\n";
    Check(verified, "Scene fixture verify " + message);
    auto generation = std::make_shared<Generation>();
    generation->generation = layered ? 2 : 1;
    generation->cooked = {product, WriteMaterialProgramMetadata(product.program), BuildBoundSource(product.program)};
    return generation;
}

class TrackingPipelines final : public IRenderPipelineCache
{
  public:
    explicit TrackingPipelines(VulkanPipelineCache& cache) : cache_(cache) {}
    RHIPipelineHandle GetOrCreate(const RHIGraphicsPipelineDesc& desc, std::string& error) override
    {
        return cache_.GetOrCreate(desc, error);
    }
    RHIPipelineHandle GetOrCreateCompute(const RHIComputePipelineDesc& desc, std::string& error) override
    {
        return cache_.GetOrCreateCompute(desc, error);
    }
    bool InvalidatePipeline(RHIPipelineHandle handle, RHICompletionPoint completion = {}) override
    {
        return cache_.InvalidatePipeline(handle, completion);
    }
    uint32_t InvalidatePipelines(RHICompletionPoint completion = {}) override
    {
        return cache_.InvalidatePipelines(completion);
    }
    uint32_t CollectRetiredPipelines(RHICompletionPoint completion) override
    {
        return cache_.CollectRetiredPipelines(completion);
    }
    RHIPipelineRequestState RequestGraphics(const RHIGraphicsPipelineDesc& desc, RHIPipelineHandle& result,
                                            std::string& error) override
    {
        const auto state = cache_.RequestGraphics(desc, result, error);
        if (state == RHIPipelineRequestState::Ready)
            ++ready;
        return state;
    }
    unsigned ready{};

  private:
    VulkanPipelineCache& cache_;
};

void CheckScenePrograms(const std::filesystem::path& root, VulkanDeviceResources& device, VulkanPipelineCache& cache,
                        job_scheduler& jobs)
{
    SceneHost host(jobs);
    TrackingPipelines pipelines(cache);
    EnhancedFrameContext context;
    context.resources = &device;
    context.rootSignatures = &cache;
    context.psoManager = &pipelines;
    const auto baseline = cache.GetStats().asyncWorkerExecutions;
    for (const bool layered : {false, true})
    {
        const auto generation = SceneGeneration(root, layered);
        std::string error;
        Check(host.RequestProgram(context, generation, error), "Scene request " + error);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(90);
        while (!host.IsProgramReady(generation, RHIShaderBinary::SpirV))
        {
            host.PollPrograms(context);
            Check(!host.ProgramStats().failedPreparations, "Scene prepare " + host.ProgramStats().lastError);
            Check(std::chrono::steady_clock::now() < deadline, "Scene program timeout");
            if (pipelines.ready < (scenePrograms + 1) * 15)
                Check(!host.IsProgramReady(generation, RHIShaderBinary::SpirV), "Partial PSO set cannot publish");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        ++scenePrograms;
        Check(host.RequestProgram(context, generation, error), "Scene repeated ready request " + error);
    }
    Check(host.ProgramStats().workerExecutions == 2 && host.ProgramStats().ready == 2,
          "Both Scene generations verified on workers");
    scenePsoWorkers = cache.GetStats().asyncWorkerExecutions - baseline;
    sceneReadyRequests = pipelines.ready;
    Check(scenePsoWorkers >= 15 && scenePsoWorkers <= 30 && sceneReadyRequests == 30,
          "Both fifteen-PSO sets include shadow, opaque and alpha variants and share identical descriptors");
    host.ShutdownAfterIdle();
}

void Run(const std::filesystem::path& root)
{
    auto* paths = InternalPath::GetInstance();
    paths->BaseProjectPath = root / "Dynamic_CPP";
    paths->ShaderSourcePath = root / "Dynamic_CPP/Assets/Shaders";
    paths->CacheRoot = root / "Build/Obj/MaterialProductProbe/VulkanSceneCache";
    paths->AssetAuthoringEnabled = true;
    RHIShaderCompiler::ScopedOutput output(RHIShaderBinary::SpirV);
    const auto file = root / "Build/Obj/MaterialProductProbe/vulkan-async-draw.slang";
    std::ofstream(file, std::ios::binary | std::ios::trunc)
        << "float4 VSMain(float2 position : POSITION) : SV_Position { return float4(position, 0, 1); }\n"
           "float4 PSMain() : SV_Target0 { return float4(.25, .5, .75, 1); }\n"
           "[numthreads(1, 1, 1)] void CSMain() {}\n"
           "[numthreads(1, 1, 1)] void LXNamedCompute() {}\n";
    std::string error;
    RHIShaderBlob vs, ps;
    Check(RHIShaderCompiler::CompileFile(file.string(), "VSMain", "vs_6_0", vs, error), "Vulkan vertex " + error);
    Check(RHIShaderCompiler::CompileFile(file.string(), "PSMain", "ps_6_0", ps, error), "Vulkan pixel " + error);
    thread_pool pool;
    job_scheduler jobs(pool);
    jobs.start(1);
    VulkanDeviceResources device;
    Check(device.Initialize(16, 16, true, error) && device.IsValidationEnabled(), "Vulkan validation device " + error);
    VulkanPipelineCache cache(jobs);
    cache.Initialize(device.GetDevice());
    device.SetPipelineCache(&cache);
    struct Drain
    {
        VulkanDeviceResources& device;
        VulkanPipelineCache& cache;
        ~Drain()
        {
            device.WaitForGpu();
            device.SetPipelineCache(nullptr);
            cache.Shutdown();
            std::string messages;
            if (device.DrainDebugMessages(messages) || !messages.empty())
                std::cerr << messages;
        }
    } drain{device, cache};
    const auto layout = cache.GetOrCreate(RHIPipelineLayoutDesc{}, error);
    Check(layout.IsValid(), "Vulkan empty layout " + error);
    for (const char* name : {"CSMain", "LXNamedCompute"})
    {
        RHIShaderBlob compute;
        Check(RHIShaderCompiler::CompileFile(file.string(), name, "cs_6_0", compute, error),
              "Vulkan compute artifact " + error);
        RHIComputePipelineDesc request;
        request.csBytecode = compute.Data();
        request.csSize = compute.Size();
        request.layout = layout;
        const auto native = cache.GetOrCreateCompute(request, error);
        Check(native.IsValid() && cache.Resolve(native).IsValid(), "Legacy/named compute entry " + error);
        Check(cache.GetOrCreateCompute(request, error) == native, "Compute descriptor cache reuse");
    }
    RHIComputePipelineDesc wrongStage;
    wrongStage.csBytecode = vs.Data();
    wrongStage.csSize = vs.Size();
    wrongStage.layout = layout;
    Check(!cache.GetOrCreateCompute(wrongStage, error).IsValid() &&
              error.find("entry point for its stage") != std::string::npos,
          "Wrong SPIR-V execution model rejected before native compute creation");
    const RHIInputElement element{"POSITION", 0, RHIFormat::RG32Float};
    RHIGraphicsPipelineDesc desc;
    desc.vsBytecode = vs.Data();
    desc.vsSize = vs.Size();
    desc.psBytecode = ps.Data();
    desc.psSize = ps.Size();
    desc.layout = layout;
    desc.inputElements = &element;
    desc.inputElementCount = 1;
    desc.rtvFormats[0] = RHIFormat::RGBA32Float;
    desc.dsvFormat = RHIFormat::Unknown;
    RHIPipelineHandle handle;
    const auto compileBaseline = cache.GetStats().compiles;
    {
        WorkerGate gate(jobs);
        auto temporaryVs = vs, temporaryPs = ps;
        std::string semantic = "POSITION";
        RHIInputElement temporaryElement{semantic.c_str(), 0, RHIFormat::RG32Float};
        auto borrowed = desc;
        borrowed.vsBytecode = temporaryVs.Data();
        borrowed.psBytecode = temporaryPs.Data();
        borrowed.inputElements = &temporaryElement;
        Check(cache.RequestGraphics(borrowed, handle, error) == RHIPipelineRequestState::Pending && !handle.IsValid(),
              "Delayed request must return pending without native publication");
        temporaryVs = {};
        temporaryPs = {};
        semantic.assign(200, 'x');
        temporaryElement = {};
        for (unsigned repeat = 0; repeat < 16; ++repeat)
            Check(cache.RequestGraphics(desc, handle, error) == RHIPipelineRequestState::Pending,
                  "Duplicate polling cannot run on owner or enqueue twice");
        Check(cache.GetStats().asyncSubmissions == 1 && cache.GetStats().compiles == compileBaseline,
              "One accepted owned request; pending graphics does not compile on owner");
        gate.Release();
    }
    Check(Wait(cache, desc, handle, error) == RHIPipelineRequestState::Ready && cache.Resolve(handle).IsValid(),
          "Owned request ready " + error);
    const auto first = handle;
    Check(cache.GetOrCreate(desc, error) == first, "Legacy sync consumer shares async handle");
    Draw(device, first);
    auto replacement = desc;
    replacement.cullMode = RHICullMode::Back;
    {
        WorkerGate gate(jobs);
        Check(cache.RequestGraphics(replacement, handle, error) == RHIPipelineRequestState::Pending,
              "Stale candidate accepted");
        cache.InvalidatePipelines();
        Check(!cache.Resolve(first).IsValid(), "Published old handle becomes stale immediately");
        Check(cache.GetStats().asyncPending == 1, "Invalidation does not wait on accepted work");
        gate.Release();
    }
    Check(Wait(cache, replacement, handle, error) == RHIPipelineRequestState::Ready,
          "Fresh request after invalidation " + error);
    Check(cache.GetStats().asyncStaleCompletions == 1 && handle != first, "Abandoned PSO never re-enters cache");
    cache.CollectRetiredPipelines({});
    auto badElement = element;
    badElement.inputSlot = 1;
    auto bad = desc;
    bad.inputElements = &badElement;
    Check(Wait(cache, bad, handle, error) == RHIPipelineRequestState::Failed && !error.empty(),
          "Worker failure diagnosis");
    const auto failedCount = cache.GetStats().asyncSubmissions;
    const auto failure = error;
    Check(Wait(cache, bad, handle, error) == RHIPipelineRequestState::Failed && error == failure &&
              cache.GetStats().asyncSubmissions == failedCount,
          "Failed request memoized without another native job");
    auto malformed = desc;
    malformed.inputElements = nullptr;
    Check(cache.RequestGraphics(malformed, handle, error) == RHIPipelineRequestState::Failed,
          "Malformed borrowed inputs rejected before hashing");
    malformed = desc;
    malformed.numRenderTargets = 9;
    Check(cache.RequestGraphics(malformed, handle, error) == RHIPipelineRequestState::Failed, "Target count bounded");
    {
        WorkerGate gate(jobs);
        Check(cache.RequestGraphics(desc, handle, error) == RHIPipelineRequestState::Pending, "Sync/async candidate");
        gate.Release();
    }
    handle = cache.GetOrCreate(desc, error);
    Check(handle.IsValid() && cache.RequestGraphics(desc, handle, error) == RHIPipelineRequestState::Ready,
          "Synchronous consumer adopts accepted job");
    const auto syncSubmissions = cache.GetStats().asyncSubmissions;
    Draw(device, handle);
    Check(cache.GetStats().asyncSubmissions == syncSubmissions, "Draw/repeated query does not compile");
    cache.InvalidatePipelines();
    {
        WorkerGate gate(jobs);
        for (unsigned i = 0; i < 64; ++i)
        {
            const std::string semantic = "POSITION" + std::to_string(i);
            const RHIInputElement input{semantic.c_str(), 0, RHIFormat::RG32Float};
            auto bounded = desc;
            bounded.inputElements = &input;
            Check(cache.RequestGraphics(bounded, handle, error) == RHIPipelineRequestState::Pending,
                  "Bounded queued request owns temporary semantic");
        }
        Check(cache.RequestGraphics(desc, handle, error) == RHIPipelineRequestState::Failed &&
                  error.find("budget") != std::string::npos,
              "65th native request exceeds admission");
        cache.InvalidatePipelines();
        Check(cache.GetStats().asyncPending == 64, "Invalidated jobs remain budgeted until completion");
        gate.Release();
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(90);
    while (cache.GetStats().asyncPending)
    {
        cache.CollectRetiredPipelines({});
        Check(std::chrono::steady_clock::now() < deadline, "Abandoned native work timeout");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Check(cache.GetStats().asyncStaleCompletions == 65, "All invalidated native results collected");
    CheckScenePrograms(root, device, cache, jobs);
    const auto stats = cache.GetStats();
    Check(stats.asyncSubmissions == stats.asyncWorkerExecutions && stats.asyncSubmissions == 69 + scenePsoWorkers,
          "Every completed accepted request ran on a worker, including abandoned work");
    {
        WorkerGate gate(jobs);
        auto pendingShutdown = desc;
        pendingShutdown.cullMode = RHICullMode::Front;
        Check(cache.RequestGraphics(pendingShutdown, handle, error) == RHIPipelineRequestState::Pending,
              "Shutdown fixture queued work");
        std::thread release([&gate] {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            gate.Release();
        });
        cache.Shutdown();
        release.join();
    }
    Check(!cache.Resolve(handle).IsValid() && cache.GetStats().asyncPending == 0,
          "Shutdown joins queued work before destroying native layouts");
    cache.Initialize(device.GetDevice());
    auto restarted = desc;
    restarted.layout = cache.GetOrCreate(RHIPipelineLayoutDesc{}, error);
    jobs.shutdown();
    Check(cache.RequestGraphics(restarted, handle, error) == RHIPipelineRequestState::Failed &&
              error.find("stopped") != std::string::npos,
          "Stopped scheduler does not silently compile on owner");
    jobs.start(1);
    Check(Wait(cache, restarted, handle, error) == RHIPipelineRequestState::Ready, "Restarted scheduler recovery");
    Draw(device, handle);
    device.WaitForGpu();
    cache.Shutdown();
    const auto adapter = device.GetAdapterName();
    device.Shutdown();
    std::string messages;
    const auto validation = device.DrainDebugMessages(messages);
    Check(validation == 0, "Vulkan WARNING+ validation " + messages);
    std::cout << "LX_MATERIAL_VULKAN_GENERATION_OK checks=" << checks << " pixels=" << pixels
              << " scenePrograms=" << scenePrograms << " sceneReadyRequests=" << sceneReadyRequests
              << " scenePsoWorkers=" << scenePsoWorkers << " submissions=" << stats.asyncSubmissions
              << " workers=" << stats.asyncWorkerExecutions << " stale=" << stats.asyncStaleCompletions
              << " failures=" << stats.failures << " validation=" << validation << " adapter=" << adapter << '\n';
}
} // namespace

int main(int argc, char** argv)
{
#ifdef _DEBUG
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    try
    {
        Run(argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::current_path());
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "LX_MATERIAL_VULKAN_GENERATION_FAILED " << exception.what() << '\n';
        return 1;
    }
}
