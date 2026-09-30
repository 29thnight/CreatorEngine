#include "MaterialGraphSceneHost.h"
#include "MaterialGraphSceneCompiler.h"

#include "PathFinder.h"
#include "../EngineDiagnostics/ProfileScope.h"
#include "RHI/RHIShaderSource.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <cstdio>
#include <stdexcept>

namespace material_graph
{
namespace
{
bool Fail(std::string& error, std::string message)
{
    error = std::move(message);
    return false;
}

struct SceneConstants
{
    math::matrix4x4 viewProjection;
    math::vector4 eye;
    std::uint32_t owner{}, coverage{};
    float cutoff{};
    std::uint32_t environment{}, lightCount{}, hasShadow{};
    float shadowBlend{};
    std::uint32_t width{}, height{}, padding[3]{};
    math::matrix4x4 shadowProjection[3];
    math::vector4 shadowSplits, shadowBias, cameraForward;
    EnhancedLight lights[64];
};
  static_assert(sizeof(SceneConstants) == 4464);
  static_assert(offsetof(SceneConstants, lights) == 368);

struct SceneShadowConstants
{
    math::matrix4x4 viewProjection;
    std::uint32_t coverage{};
    float cutoff{};
    std::uint32_t padding[2]{};
};
static_assert(sizeof(SceneShadowConstants) == 80);

std::array<RGHandle, 5> Colors(const EnhancedGBufferPass::Outputs& outputs)
{
    return {outputs.diffuse, outputs.metalRough, outputs.normal, outputs.emissive, outputs.bitmask};
}
} // namespace

struct SceneHost::Program
{
    std::shared_ptr<const Generation> generation;
    RHIShaderBinary backend{};
    PassLayout layout;
    PassLayout shadowLayout;
    RHIGraphicsPipelineRequest shadow;
    std::array<RHIGraphicsPipelineRequest, 2> gbuffer, color;
    std::array<std::array<RHIGraphicsPipelineRequest, 2>, 2> lookup;
    std::array<RHIGraphicsPipelineRequest, 2> subsurface;
    std::array<RHIGraphicsPipelineRequest, 2> refraction;
    PassLayout volumeLayout;
    RHIPipelineHandle volume;
    bool hasSurface{}, hasVolume{};
    bool hasSubsurface{}, hasTransmission{}, hasSpecial{};
};

struct SceneHost::Frame
{
    struct Draw
    {
        std::shared_ptr<const Program> program;
        std::shared_ptr<const RenderBindings> bindings;
        std::shared_ptr<const RenderBindings> shadowBindings;
        std::shared_ptr<const MeshSurfaceBatch> geometry;
        RHIBufferSlice indices, constants;
        std::array<RHIBufferSlice, 3> shadowConstants;
        bool doubleSided{};
    };
    IRenderDeviceServices* device{};
    std::uint64_t recording{}, descriptors{};
    std::shared_ptr<const SceneViewInput> input;
    std::vector<Draw> draws;
    RHITextureHandle environment;
    bool shadow{};
    std::shared_ptr<const SceneLookupFrame> lookup;
    std::shared_ptr<const SceneSubsurfaceFrame> subsurface;
    std::shared_ptr<const SceneRefractionFrame> refraction;
    std::shared_ptr<const SceneVolumeFrame> volume;
    std::vector<SceneVolumeBinding> volumeBindings;
    mutable const EnhancedRenderGraph* graph{};
    mutable std::uint64_t graphEpoch{};
    mutable EnhancedGBufferPass::Outputs gbuffer;
    mutable bool colorDeclared{};
    mutable bool gbufferDeclared{}, shadowDeclared{};
    mutable bool decalDeclared{};
    mutable std::array<RGHandle, 3> decalBaseline;
    std::array<RHIBufferSlice, 2> decalConstants;

    void AddDecalUses(std::vector<EnhancedRenderGraph::RGPassUsage>& uses, bool transmissionStage) const
    {
        if (!decalDeclared || transmissionStage)
        {
            return;
        }
        for (const auto handle : {gbuffer.diffuse, gbuffer.metalRough, gbuffer.normal})
        {
            uses.push_back({handle, RHIResourceState::PixelShaderResource});
        }
        for (const auto handle : decalBaseline)
        {
            uses.push_back({handle, RHIResourceState::PixelShaderResource});
        }
    }

    RHIBindingTable DecalTable(const EnhancedRenderGraph::ExecuteContext& execution, bool transmissionStage) const
    {
        const bool enabled = decalDeclared && !transmissionStage;
        const std::array<RGHandle, 6> handles{gbuffer.diffuse,  gbuffer.metalRough, gbuffer.normal,
                                              decalBaseline[0], decalBaseline[1],   decalBaseline[2]};
        std::array<RHIBindingDesc, 6> descriptions;
        for (unsigned i = 0; i < handles.size(); ++i)
        {
            descriptions[i] = RHIBindingDesc::Srv2D(enabled ? execution.ResolveHandle(handles[i]) : RHITextureHandle{},
                                                    RHIFormat::RGBA16Float)
                                  .OrNull();
        }
        const auto table = device->CreateBindings(descriptions);
        if (!table.IsValid())
        {
            throw std::runtime_error("LX Scene Decal binding failed.");
        }
        return table;
    }

    void BindDecal(RHIEncoder& encoder, const Draw& draw, RHIBindingTable table, bool transmissionStage) const
    {
        const unsigned index = 4 + (draw.program->hasSpecial ? 2 : 0) + (draw.program->hasTransmission ? 1 : 0);
        encoder.SetBindings(RHIBindPoint::Graphics, index, table);
        encoder.SetConstantBuffer(RHIBindPoint::Graphics, index + 1,
                                  decalConstants[decalDeclared && !transmissionStage]);
    }

    void CheckCurrent(const EnhancedRenderGraph* currentGraph = nullptr) const
    {
        if (device->GetCurrentUploadRecordingId() != recording || device->GetDescriptorVersionToken() != descriptors)
        {
            throw std::runtime_error("LX Scene frame has stale upload/descriptor ownership.");
        }
        if (currentGraph && (graph != currentGraph || currentGraph->ResourceEpoch() != graphEpoch))
        {
            throw std::runtime_error("LX Scene frame belongs to a reset graph.");
        }
    }
};

struct SceneHost::Preparation
{
    struct Work : SceneShaderSet
    {
        std::shared_ptr<const Generation> generation;
        RHIShaderBinary backend{};
        std::filesystem::path file, shaderDirectory;
        std::string error;
        bool worker{}, cooked{};
    };
    std::shared_ptr<const Generation> generation;
    RHIShaderBinary backend{};
    std::shared_ptr<Work> work;
    job_handle job;
    std::shared_ptr<Program> program;
    std::string error;
    bool checked{};
};

struct SceneHost::Slot
{
    std::shared_ptr<const Instance> requested, active;
    EnhancedMaterialCoverage requestedCoverage, activeCoverage;
    std::uint64_t revision{};
};

SceneHost::SceneHost(job_scheduler& scheduler) : scheduler_(scheduler) {}

SceneHost::~SceneHost()
{
    if (device_)
        device_->UnregisterUploadTransactionListener(this);
}

bool SceneHost::IsProgramReady(const std::shared_ptr<const Generation>& generation, RHIShaderBinary backend) const
{
    return std::ranges::any_of(programs_, [&](const auto& program) {
        return program->generation == generation && program->backend == backend;
    });
}

SceneProgramStats SceneHost::ProgramStats() const
{
    auto result = stats_;
    result.ready = programs_.size();
    result.pending = std::ranges::count_if(preparations_, [](const auto& item) { return item->error.empty(); });
    result.activeSlots = std::ranges::count_if(slots_, [](const auto& item) { return bool(item.second->active); });
    std::lock_guard lock(recordingMutex_);
    result.retainedRecordings = recordings_.size();
    return result;
}

bool SceneHost::RequestProgram(const EnhancedFrameContext& context, std::shared_ptr<const Generation> generation,
                               std::string& error)
{
    const auto backend = RHIShaderCompiler::GetOutput();
    if (!generation || !context.resources || !context.rootSignatures || !context.psoManager ||
        (programDevice_ && programDevice_ != context.resources))
        return Fail(error, "LX Scene program preparation needs its owning device and immutable generation.");
    programDevice_ = context.resources;
    if (IsProgramReady(generation, backend))
    {
        error.clear();
        return true;
    }
    for (const auto& item : preparations_)
        if (item->generation == generation && item->backend == backend)
        {
            error = item->error;
            return error.empty();
        }
    if (preparations_.size() >= 64)
        return Fail(error, "LX Scene program preparation exceeds its 64-request admission budget.");
    auto preparation = std::make_shared<Preparation>();
    preparation->generation = std::move(generation);
    preparation->backend = backend;
    const auto reject = [&](std::string message) {
        preparation->error = message;
        stats_.lastError = message;
        ++stats_.failedPreparations;
        preparations_.push_back(preparation);
        return Fail(error, std::move(message));
    };
    const auto& product = preparation->generation->cooked.product;
    Capabilities capabilities;
    capabilities.coreForward = true;
    capabilities.layeredLookup = true;
    capabilities.subsurface = true;
    capabilities.refraction = true;
    capabilities.volume = true;
    Selection selection;
    std::vector<LX::LXMaterialDiagnostic> diagnostics;
    if (!SelectRoute(product.program, capabilities, {}, selection, diagnostics))
        return reject(diagnostics.empty() ? "LX Scene route rejected." : diagnostics.front().message);
    if (product.program.volume &&
        product.program.slang.find("#define LX_MATERIAL_VOLUME_HOMOGENEOUS 1\n") == std::string::npos)
    {
        return reject("LX Scene Volume requires homogeneous coefficients; spatially varying Volume is unsupported.");
    }
    static std::atomic<std::uint64_t> serial{};
    auto work = std::make_shared<Preparation::Work>();
    work->generation = preparation->generation;
    work->backend = backend;
    if (product.program.semanticKey.ends_with(SceneHostIdentity))
    {
        if (!LoadSceneShaders(product, backend, *work, error))
        {
            return reject(error);
        }
        work->cooked = true;
    }
    else
    {
        if (!InternalPath::GetInstance()->AssetAuthoringEnabled)
        {
            return reject("LX Player requires the complete cooked Scene host stage set.");
        }
        auto directory = std::filesystem::path(InternalPath::GetInstance()->CacheRoot);
        if (directory.empty())
        {
            directory = std::filesystem::path(InternalPath::GetInstance()->BaseProjectPath) / "Cache";
        }
        work->file = directory / "LXScene" / ("scene-" + std::to_string(++serial) + ".slang");
        work->shaderDirectory = RHIShaderSource::Resolve("");
    }
    preparation->work = std::move(work);
    preparations_.push_back(std::move(preparation));
    error.clear();
    return true;
}

void SceneHost::PollPrograms(const EnhancedFrameContext& context)
{
    if (programDevice_ != context.resources || !context.rootSignatures || !context.psoManager)
        return;
    PollSubmittedFrames();
    // Start native requests for several materials in one frame. RequestGraphics
    // queues the DX12 work asynchronously; one request per frame made a model's
    // many material slots appear over a long series of frames.
    unsigned pollsRemaining = 8;
    for (const auto& preparation : preparations_)
    {
        const bool cooked = preparation->work && preparation->work->cooked;
        const bool prepared = preparation->checked && preparation->program;
        if (!preparation->error.empty() ||
            (!prepared && !cooked && (!preparation->job.valid() || !preparation->job.is_complete())))
            continue;
        const auto reject = [&](std::string message) {
            preparation->error = std::move(message);
            preparation->program.reset();
            preparation->work.reset();
            stats_.lastError = preparation->error;
            ++stats_.failedPreparations;
        };
        if (!preparation->checked)
        {
            try
            {
                if (preparation->job.valid())
                {
                    preparation->job.wait();
                }
            }
            catch (const std::exception& exception)
            {
                reject(exception.what());
                continue;
            }
            preparation->checked = true;
            const auto& compiled = preparation->work;
            if (compiled->worker)
                ++stats_.workerExecutions;
            if (!compiled->error.empty())
            {
                reject(compiled->error);
                continue;
            }
            const auto& vertex = compiled->vertex;
            const auto& gbuffer = compiled->gbuffer;
            const auto& color = compiled->color;
            const auto& lookup0 = compiled->lookup0;
            const auto& lookup1 = compiled->lookup1;
            const auto& layout = compiled->layout;
            const auto backend = preparation->backend;
            std::string error;
            const auto describe = [&]() -> bool {
                auto candidate = std::make_shared<Program>();
                candidate->generation = preparation->generation;
                candidate->backend = backend;
                candidate->hasSurface = candidate->generation->cooked.product.program.surface;
                candidate->hasVolume = candidate->generation->cooked.product.program.volume;
                candidate->hasSubsurface = (candidate->generation->cooked.product.program.features & 0x1000u) != 0;
                candidate->hasTransmission = (candidate->generation->cooked.product.program.features & 0x0800u) != 0;
                candidate->hasSpecial = candidate->hasSubsurface || candidate->hasTransmission;
                std::vector<RHIPipelineLayoutParam> host{RHILayout::Cbv(0),
                                                         RHILayout::Srv(0, RHIShaderVisibility::Vertex),
                                                         RHILayout::SrvTable(4, 1, RHIShaderVisibility::Pixel),
                                                         RHILayout::Srv(5, RHIShaderVisibility::Pixel)};
                if (candidate->hasSpecial)
                {
                    host.push_back(RHILayout::Srv(6, RHIShaderVisibility::Pixel));
                    host.push_back(RHILayout::Srv(7, RHIShaderVisibility::Pixel));
                }
                if (candidate->hasTransmission)
                {
                    host.push_back(RHILayout::Srv(8, RHIShaderVisibility::Pixel));
                }
                host.push_back(RHILayout::SrvTable(6, 9, RHIShaderVisibility::Pixel));
                host.push_back(RHILayout::Cbv(3, RHIShaderVisibility::Pixel));
                const RHIStaticSamplerDesc samplers[]{
                    {RHISampler::Linear(RHIAddressMode::Clamp), 0, RHIShaderVisibility::Pixel},
                    {RHISampler::Comparison(RHICompareOp::LessEqual, RHIAddressMode::Border,
                                            RHIBorderColor::OpaqueWhite),
                     1, RHIShaderVisibility::Pixel}};
                if (!CreatePassLayout(*context.rootSignatures, layout, host, samplers, true, candidate->layout, error))
                {
                    return false;
                }
                if (candidate->hasSurface)
                {
                    const RHIPipelineLayoutParam shadowHost[]{RHILayout::Cbv(4),
                                                              RHILayout::Srv(0, RHIShaderVisibility::Vertex)};
                    if (!CreatePassLayout(*context.rootSignatures, layout, shadowHost, {}, true,
                                          candidate->shadowLayout, error))
                    {
                        return false;
                    }
                    RHIGraphicsPipelineDesc desc;
                    desc.layout = candidate->shadowLayout.handle;
                    desc.vsBytecode = compiled->shadowVertex.bytecode.Data();
                    desc.vsSize = compiled->shadowVertex.bytecode.Size();
                    desc.psBytecode = compiled->shadow.bytecode.Data();
                    desc.psSize = compiled->shadow.bytecode.Size();
                    desc.depthEnable = true;
                    desc.depthWriteMask = RHIDepthWrite::All;
                    desc.depthFunc = RHICompareOp::Less;
                    desc.cullMode = RHICullMode::None;
                    desc.dsvFormat = RHIFormat::D32Float;
                    desc.numRenderTargets = 0;
                    if (!candidate->shadow.Prepare(desc, error))
                    {
                        return false;
                    }
                }
                if (candidate->hasVolume)
                {
                    const RHIPipelineLayoutParam volumeHost[]{RHILayout::Cbv(1), RHILayout::UavBufferTable(1, 0)};
                    if (!CreatePassLayout(*context.rootSignatures, layout, volumeHost, {}, false,
                                          candidate->volumeLayout, error))
                    {
                        return false;
                    }
                    RHIComputePipelineDesc desc;
                    desc.layout = candidate->volumeLayout.handle;
                    desc.csBytecode = compiled->volume.bytecode.Data();
                    desc.csSize = compiled->volume.bytecode.Size();
                    candidate->volume = context.psoManager->GetOrCreateCompute(desc, error);
                    if (!candidate->volume.IsValid())
                    {
                        return false;
                    }
                }
                for (unsigned side = 0; side < 2; ++side)
                {
                    RHIGraphicsPipelineDesc desc;
                    desc.layout = candidate->layout.handle;
                    desc.vsBytecode = vertex.bytecode.Data();
                    desc.vsSize = vertex.bytecode.Size();
                    desc.psBytecode = gbuffer.bytecode.Data();
                    desc.psSize = gbuffer.bytecode.Size();
                    desc.depthEnable = true;
                    desc.depthWriteMask = RHIDepthWrite::All;
                    desc.depthFunc = RHICompareOp::Less;
                    desc.cullMode = side ? RHICullMode::None : RHICullMode::Back;
                    desc.dsvFormat = RHIFormat::D32Float;
                    desc.numRenderTargets = 5;
                    for (unsigned i = 0; i < 5; ++i)
                    {
                        desc.rtvFormats[i] = EnhancedGBufferPass::GetRenderTargetFormat(i);
                    }
                    if (!candidate->gbuffer[side].Prepare(desc, error))
                    {
                        return false;
                    }
                    desc.psBytecode = color.bytecode.Data();
                    desc.psSize = color.bytecode.Size();
                    desc.depthWriteMask = RHIDepthWrite::Zero;
                    desc.depthFunc = RHICompareOp::Equal;
                    desc.numRenderTargets = 1;
                    desc.rtvFormats[0] = RHIFormat::RGBA16Float;
                    std::fill(std::begin(desc.rtvFormats) + 1, std::end(desc.rtvFormats), RHIFormat::Unknown);
                    if (!candidate->color[side].Prepare(desc, error))
                    {
                        return false;
                    }
                    if (candidate->hasSubsurface)
                    {
                        desc.psBytecode = compiled->subsurface.bytecode.Data();
                        desc.psSize = compiled->subsurface.bytecode.Size();
                        desc.numRenderTargets = 7;
                        std::fill_n(std::begin(desc.rtvFormats), 7, RHIFormat::RGBA32Float);
                        if (!candidate->subsurface[side].Prepare(desc, error))
                        {
                            return false;
                        }
                    }
                    if (candidate->hasTransmission)
                    {
                        desc.psBytecode = compiled->refraction.bytecode.Data();
                        desc.psSize = compiled->refraction.bytecode.Size();
                        desc.numRenderTargets = 2;
                        std::fill(std::begin(desc.rtvFormats), std::end(desc.rtvFormats), RHIFormat::Unknown);
                        std::fill_n(std::begin(desc.rtvFormats), 2, RHIFormat::RGBA32Float);
                        if (!candidate->refraction[side].Prepare(desc, error))
                        {
                            return false;
                        }
                    }
                    for (unsigned part = 0; part < 2; ++part)
                    {
                        const auto& shader = part ? lookup1 : lookup0;
                        desc.psBytecode = shader.bytecode.Data();
                        desc.psSize = shader.bytecode.Size();
                        desc.numRenderTargets = part ? 3 : 8;
                        std::fill(std::begin(desc.rtvFormats), std::end(desc.rtvFormats), RHIFormat::Unknown);
                        std::fill_n(std::begin(desc.rtvFormats), desc.numRenderTargets, RHIFormat::RGBA32Float);
                        if (!candidate->lookup[part][side].Prepare(desc, error))
                            return false;
                    }
                }

                preparation->program = std::move(candidate);
                return true;
            };
            if (!describe())
            {
                reject(error);
                continue;
            }
            preparation->work.reset();
        }
        if (!pollsRemaining)
            continue;
        auto& program = *preparation->program;
        std::vector<RHIGraphicsPipelineRequest*> requests{
            &program.gbuffer[0],   &program.gbuffer[1],   &program.color[0],     &program.color[1],
            &program.lookup[0][0], &program.lookup[0][1], &program.lookup[1][0], &program.lookup[1][1]};
        if (program.hasSurface)
        {
            requests.push_back(&program.shadow);
        }
        if (program.hasSubsurface)
        {
            requests.push_back(&program.subsurface[0]);
            requests.push_back(&program.subsurface[1]);
        }
        if (program.hasTransmission)
        {
            requests.push_back(&program.refraction[0]);
            requests.push_back(&program.refraction[1]);
        }
        std::string error;
        for (auto* request : requests)
            if (!request->IsValid())
            {
                --pollsRemaining;
                if (request->Poll(*context.psoManager, error) == RHIPipelineRequestState::Failed)
                    reject(error.empty() ? "LX Scene native PSO preparation failed." : error);
                break;
            }
        if (preparation->program &&
            std::ranges::all_of(requests, [](const auto* request) { return request->IsValid(); }))
        {
            programs_.push_back(preparation->program);
            if (!PathFinder::IsAssetAuthoringEnabled())
            {
                std::printf("[lx.scene.program] source=cooked graph=%s ready=%zu sceneCompiles=%llu\n",
                            Uuid::ToString(preparation->generation->assetId.value).c_str(), programs_.size(),
                            static_cast<unsigned long long>(stats_.compileSubmissions));
                std::fflush(stdout);
            }
            preparation->program.reset();
        }
    }
    std::erase_if(preparations_, [&](const auto& item) { return IsProgramReady(item->generation, item->backend); });
    unsigned workersRemaining = 4;
    for (const auto& preparation : preparations_)
        if (preparation->job.valid() && !preparation->job.is_complete())
            --workersRemaining;
    for (const auto& preparation : preparations_)
        if (workersRemaining && preparation->error.empty() && !preparation->checked && !preparation->job.valid())
        {
            const auto work = preparation->work;
            try
            {
                preparation->job = scheduler_.submit([work]() -> bool {
                    work->worker = thread_pool::is_worker_thread();
                    VerifiedProduct verified;
                    const auto& product = work->generation->cooked.product;
                    if (!CompileSceneProduct(product.program, work->shaderDirectory, work->file, {}, verified,
                                             work->error) ||
                        verified.layout != product.layout)
                    {
                        return Fail(work->error,
                                    work->error.empty()
                                        ? "LX Scene shader reflection differs from the exact instance layout."
                                        : work->error);
                    }
                    return LoadSceneShaders(verified, work->backend, *work, work->error);
                });
                ++stats_.compileSubmissions;
            }
            catch (const std::exception& exception)
            {
                preparation->error = exception.what();
                stats_.lastError = preparation->error;
                ++stats_.failedPreparations;
            }
            --workersRemaining;
        }
}

bool SceneHost::PrepareProgram(const EnhancedFrameContext&, const Instance& instance,
                               std::shared_ptr<const Program>& result, std::string& error)
{
    const auto backend = RHIShaderCompiler::GetOutput();
    for (const auto& program : programs_)
        if (program->generation == instance.generation && program->backend == backend)
        {
            result = program;
            error.clear();
            return true;
        }
    return Fail(error, "LX Scene program is not ready; request and poll before recording the frame.");
}

bool SceneHost::SelectReadyInput(const EnhancedFrameContext& context, std::shared_ptr<const SceneViewInput> requested,
                                 std::shared_ptr<const SceneViewInput>& result, std::string& error)
{
    if (!requested)
    {
        result = std::move(requested);
        error.clear();
        return true;
    }
    const auto& view = requested->View();
    if (view.frameId != context.frameId || view.sceneEpoch != context.sceneEpoch || view.width != context.width ||
        view.height != context.height)
        return Fail(error, "LX Scene selection needs the current identified view.");
    if (requested->Draws().empty())
    {
        std::erase_if(slots_, [&](const auto& item) { return std::get<1>(item.first) == view.viewId; });
        PollSubmittedFrames();
        result = std::move(requested);
        error.clear();
        return true;
    }
    const auto sameCoverage = [](const auto& a, const auto& b) {
        return a.flags == b.flags && a.cutoff == b.cutoff && a.baseAlpha == b.baseAlpha;
    };
    // Validate shared Material identities before mutating request revisions.
    std::map<std::uint64_t, const SceneDrawInput*> sources;
    for (const auto& draw : requested->Draws())
    {
        const auto [entry, inserted] = sources.emplace(draw.materialSlot, &draw);
        if (!draw.materialSlot || (!inserted && (entry->second->material != draw.material ||
                                                 !sameCoverage(entry->second->coverage, draw.coverage))))
            return Fail(error, "LX Scene Material slots must identify one exact instance and coverage per view.");
    }
    std::erase_if(slots_, [&](const auto& item) {
        const auto [epoch, viewId, slot] = item.first;
        return viewId == view.viewId && (epoch != view.sceneEpoch || !sources.contains(slot));
    });
    for (const auto& [id, draw] : sources)
    {
        auto& slot = slots_[{view.sceneEpoch, view.viewId, id}];
        if (!slot)
            slot = std::make_shared<Slot>();
        if (slot->requested != draw->material || !sameCoverage(slot->requestedCoverage, draw->coverage))
        {
            slot->requested = draw->material;
            slot->requestedCoverage = draw->coverage;
            slot->revision = ++selectionSerial_;
        }
        if (draw->queue == SceneCoverage::Blended)
            stats_.lastError = "LX Scene Blended replacement is not installed; keeping the submitted material.";
        else
        {
            std::string preparationError;
            if (!RequestProgram(context, draw->material->generation, preparationError))
                stats_.lastError = std::move(preparationError);
        }
    }
    PollPrograms(context);
    auto selected = std::shared_ptr<SceneViewInput>(new SceneViewInput(*requested));
    selected->draws_.clear();
    const auto backend = RHIShaderCompiler::GetOutput();
    // Publish an asset generation as a whole. On a cold load, one pending
    // material must not reveal only the already prepared meshes. On a reload,
    // keep the previous complete generation visible until every replacement
    // material is ready.
    std::map<assets::ModelAssetGenerationHandle, std::pair<bool, bool>> modelReadiness;
    for (const auto& draw : requested->Draws())
    {
        if (draw.queue == SceneCoverage::Blended)
            continue;
        const auto& slot = slots_.at({view.sceneEpoch, view.viewId, draw.materialSlot});
        auto& readiness = modelReadiness[draw.model];
        const bool requestedReady = IsProgramReady(draw.material->generation, backend);
        const bool activeReady = slot->active && IsProgramReady(slot->active->generation, backend);
        if (!requestedReady) readiness.first = true;
        if (!activeReady) readiness.second = true;
    }
    for (auto draw : requested->Draws())
    {
        if (draw.queue == SceneCoverage::Blended)
        {
            const auto& slot = slots_.at({view.sceneEpoch, view.viewId, draw.materialSlot});
            if (slot->active && IsProgramReady(slot->active->generation, backend))
            {
                draw.material = slot->active;
                draw.coverage = slot->activeCoverage;
                if (!ClassifySceneCoverage(draw.coverage, draw.queue, error)) return false;
                draw.selectionRevision = slot->revision;
                selected->draws_.push_back(std::move(draw));
            }
            continue;
        }
        const auto& slot = slots_.at({view.sceneEpoch, view.viewId, draw.materialSlot});
        const auto [hasPending, hasMissingActive] = modelReadiness.at(draw.model);
        if (hasPending && hasMissingActive)
            continue;
        if (hasPending)
        {
            draw.material = slot->active;
            draw.coverage = slot->activeCoverage;
            if (!ClassifySceneCoverage(draw.coverage, draw.queue, error))
                return false;
        }
        draw.selectionRevision = slot->revision;
        selected->draws_.push_back(std::move(draw));
    }
    // Only the cache's CPU record is pruned. Shared native handles stay in the
    // backend cache; global cache eviction remains a separate lifetime policy.
    std::erase_if(programs_, [&](const auto& program) {
        return programs_.size() > 32 && program.use_count() == 1 && std::ranges::none_of(slots_, [&](const auto& item) {
                   return (item.second->active && item.second->active->generation == program->generation) ||
                          (item.second->requested && item.second->requested->generation == program->generation);
               });
    });
    std::erase_if(preparations_, [&](const auto& preparation) {
        return preparations_.size() > 16 && !preparation->error.empty() &&
               std::ranges::none_of(slots_, [&](const auto& item) {
                   return item.second->requested && item.second->requested->generation == preparation->generation;
               });
    });
    result = std::move(selected);
    error.clear();
    return true;
}

bool SceneHost::PrepareResidency(const EnhancedFrameContext& context,
                                 const std::shared_ptr<const SceneViewInput>& input, std::string& error) const
{
    error.clear();
    if (!input || input->Draws().empty())
    {
        error.clear();
        return true;
    }
    if (!context.resources || !context.textureCache || context.resources->GetCurrentUploadRecordingId() == 0)
    {
        return Fail(error, "LX Scene texture residency needs an active owner upload recording.");
    }
    for (const auto& draw : input->Draws())
    {
        for (const auto& texture : draw.material->textures)
        {
            const auto failures = context.textureCache->GetUploadFailureCount();
            const auto entry = context.textureCache->GetOrUpload(texture.owner.get(), error);
            if (!entry.IsValid() || !error.empty() || context.textureCache->GetUploadFailureCount() != failures)
            {
                return Fail(error, error.empty() ? "LX Scene texture residency upload failed." : error);
            }
        }
    }
    error.clear();
    return true;
}

bool SceneHost::Prepare(const EnhancedFrameContext& context, std::shared_ptr<const SceneViewInput> input,
                        RHITextureHandle environment, RHITextureHandle irradiance, RHITextureHandle prefiltered,
                        const EnhancedShadowData& shadow, const SceneHostBudget& budget,
                        std::string& error, std::uint64_t environmentGeneration)
{
    ce::profile_scope profile{ce::marker<"MaterialGraphScenePrepare">()};
    if (!input || input->Draws().empty())
    {
        frame_.reset();
        error.clear();
        return true;
    }
    if (!context.resources || !context.psoManager || !context.rootSignatures || !context.textureCache ||
        (device_ && device_ != context.resources) || context.resources->GetCurrentUploadRecordingId() == 0 ||
        input->View().frameId != context.frameId || input->View().sceneEpoch != context.sceneEpoch ||
        input->View().width != context.width || input->View().height != context.height)
    {
        return Fail(error, "LX Scene host needs the exact current view/frame/device services.");
    }
    if (std::uint64_t(context.width) * context.height > budget.pixels || budget.pixels > 4096u * 4096u ||
        input->Draws().size() > budget.draws || budget.draws > 4096)
    {
        return Fail(error, "LX Scene composition exceeds its configured viewport/draw budget.");
    }
    for (const auto& draw : input->Draws())
    {
        if (draw.queue == SceneCoverage::Blended)
        {
            return Fail(error, "LX Scene Blended composition is not installed.");
        }
    }
    if (!device_)
    {
        RHIShaderBlob shader;
        if (!RHIShaderCompiler::CompileFile(RHIShaderSource::Resolve("MaterialGraphMeshSurface.slang").string(),
                                            "LXTransformMesh", "cs_6_0", shader, error) ||
            !geometry_.Initialize(*context.resources, *context.rootSignatures, *context.psoManager, shader, error))
        {
            return false;
        }
        device_ = context.resources;
        device_->RegisterUploadTransactionListener(this);
    }
    auto candidate = std::make_shared<Frame>();
    candidate->device = device_;
    candidate->recording = device_->GetCurrentUploadRecordingId();
    candidate->descriptors = device_->GetDescriptorVersionToken();
    candidate->input = std::move(input);
    candidate->environment = environment;
    candidate->shadow = shadow.enabled;
    if (!lookup_.Prepare(context, candidate->input->View().viewId, environment, irradiance, prefiltered,
                         environmentGeneration,
                         budget.lookupBytes, candidate->lookup, error))
        return false;
    const bool hasSpecial = std::ranges::any_of(candidate->input->Draws(), [](const auto& draw) {
        return (draw.material->generation->cooked.product.program.features & 0x1800u) != 0;
    });
    if (hasSpecial && !subsurface_.Prepare(context, environment, budget.subsurfaceBytes, candidate->subsurface, error))
    {
        return false;
    }
    const bool hasTransmission = std::ranges::any_of(candidate->input->Draws(), [](const auto& draw) {
        return (draw.material->generation->cooked.product.program.features & 0x0800u) != 0;
    });
    const bool hasVolume = std::ranges::any_of(candidate->input->Draws(), [](const auto& draw) {
        return draw.material->generation->cooked.product.program.volume;
    });
    if (hasVolume &&
        !volume_.Prepare(context, *candidate->input, environment, shadow, budget.volumeBytes, candidate->volume, error))
    {
        return false;
    }
    if (hasTransmission &&
        !refraction_.Prepare(context, candidate->input->ViewProjection(), environment, budget.refractionBytes,
                             candidate->refraction, error, candidate->volume))
    {
        return false;
    }
    for (unsigned enabled = 0; enabled < candidate->decalConstants.size(); ++enabled)
    {
        const std::array<std::uint32_t, 4> constants{enabled, 0, 0, 0};
        candidate->decalConstants[enabled] = device_->UploadConstants(constants.data(), sizeof(constants));
        if (!candidate->decalConstants[enabled].IsValid())
        {
            return Fail(error, "LX Scene Decal constants allocation failed.");
        }
    }
    std::uint32_t owner = 0x80000000u;
    for (const auto& draw : candidate->input->Draws())
    {
        std::shared_ptr<const Program> program;
        std::shared_ptr<const RenderBindings> bindings;
        if (!PrepareProgram(context, *draw.material, program, error) ||
            !bindings_.Prepare(*device_, *context.textureCache, draw.material, program->layout, bindings, error))
        {
            return false;
        }
        if (program->hasVolume)
        {
            SceneVolumeBinding volume;
            volume.pipeline = program->volume;
            if (!bindings_.Prepare(*device_, *context.textureCache, draw.material, program->volumeLayout,
                                   volume.material, error))
            {
                return false;
            }
            const std::array<std::uint32_t, 4> constants{static_cast<std::uint32_t>(candidate->volumeBindings.size()),
                                                         0, 0, 0};
            volume.constants = device_->UploadConstants(constants.data(), sizeof(constants));
            if (!volume.constants.IsValid())
            {
                return Fail(error, "LX Scene Volume coefficient constants allocation failed.");
            }
            candidate->volumeBindings.push_back(std::move(volume));
        }
        SceneConstants constants{};
        constants.viewProjection = math::transpose(candidate->input->ViewProjection());
        const auto& eye = candidate->input->Surface().eye;
        constants.eye = math::vector4(eye[0], eye[1], eye[2], 1);
        constants.owner = ++owner;
        constants.width = context.width;
        constants.height = context.height;
        constants.coverage = draw.coverage.flags;
        constants.cutoff = draw.coverage.cutoff;
        constants.environment = environment.IsValid();
        constants.hasShadow = shadow.enabled;
        constants.shadowBlend = shadow.cascadeBlendBand;
        constants.shadowSplits = shadow.splitDepths;
        constants.shadowBias = shadow.bias;
        constants.cameraForward = shadow.cameraForward;
        for (unsigned i = 0; i < 3; ++i)
        {
            constants.shadowProjection[i] = math::transpose(shadow.lightViewProjection[i]);
        }
        if (context.lights)
        {
            constants.lightCount = static_cast<std::uint32_t>((std::min)(context.lights->size(), std::size_t{64}));
            std::copy_n(context.lights->begin(), constants.lightCount, constants.lights);
        }
        const auto uploaded = device_->UploadConstants(&constants, sizeof(constants));
        if (!uploaded.IsValid())
        {
            return Fail(error, "LX Scene constants allocation failed.");
        }
        std::shared_ptr<const RenderBindings> shadowBindings;
        std::array<RHIBufferSlice, 3> shadowConstants;
        if (program->hasSurface && shadow.enabled)
        {
            if (!RenderBindingCache::RebindPass(*device_, *bindings, program->shadowLayout, shadowBindings, error))
            {
                return false;
            }
            for (unsigned cascade = 0; cascade < shadowConstants.size(); ++cascade)
            {
                SceneShadowConstants shadowValues;
                shadowValues.viewProjection = math::transpose(shadow.lightViewProjection[cascade]);
                shadowValues.coverage = draw.coverage.flags;
                shadowValues.cutoff = draw.coverage.cutoff;
                shadowConstants[cascade] = device_->UploadConstants(&shadowValues, sizeof(shadowValues));
                if (!shadowConstants[cascade].IsValid())
                {
                    return Fail(error, "LX Scene shadow constants allocation failed.");
                }
            }
        }
        for (const auto& chunk : draw.geometry->Chunks())
        {
            Frame::Draw item;
            item.program = program;
            item.bindings = bindings;
            item.shadowBindings = shadowBindings;
            item.shadowConstants = shadowConstants;
            item.constants = uploaded;
            item.doubleSided = (draw.coverage.flags & EnhancedMaterialCoverage::DoubleSided) != 0;
            if (!geometry_.Prepare(*device_, chunk.input, item.geometry, error, true))
            {
                return false;
            }
            const auto& source = chunk.input->Geometry();
            const auto bytes = std::size_t(source.indexCount) * sizeof(std::uint32_t);
            item.indices = item.geometry->Indices();
            if (!item.indices.IsValid())
            {
                item.indices = device_->AllocateUpload({bytes, RHIUploadUsage::IndexData, alignof(std::uint32_t)});
                if (!item.indices.IsValid() || !item.indices.cpuAddress)
                    return Fail(error, "LX Scene index upload failed.");
                std::memcpy(item.indices.cpuAddress, source.indexData, bytes);
            }
            candidate->draws.push_back(std::move(item));
        }
    }
    try
    {
        candidate->CheckCurrent();
    }
    catch (const std::exception& exception)
    {
        return Fail(error, exception.what());
    }
    {
        ce::profile_scope retire{ce::marker<"MaterialFrameRetirement">()};
        std::lock_guard lock(recordingMutex_);
        recordings_[candidate->recording].owners.push_back(candidate);
        std::erase_if(recordings_, [&](const auto& item) {
            return item.first != candidate->recording && !item.second.publication && item.second.submitted &&
                   item.second.completion <= completed_;
        });
    }
    {
        ce::profile_scope replace{ce::marker<"MaterialFrameReplace">()};
        frame_ = std::move(candidate);
    }
    error.clear();
    return true;
}

void SceneHost::DeclareGeometry(EnhancedRenderGraph& graph) const
{
    ce::profile_scope profile{ce::marker<"MaterialDeclareGeometry">()};
    const auto frame = frame_;
    if (!frame || frame->draws.empty())
    {
        return;
    }
    frame->CheckCurrent();
    if (frame->graph)
    {
        frame->CheckCurrent(&graph);
        return;
    }
    for (const auto& draw : frame->draws)
    {
        std::string error;
        if (!draw.geometry->Declare(graph, error))
        {
            throw std::runtime_error(error);
        }
        for (const auto& texture : draw.bindings->resources.textures)
        {
            auto handle = graph.FindImportedTexture(texture.resource);
            if (!handle.IsValid())
            {
                handle = graph.ImportTexture(texture.resource, RHIResourceState::PixelShaderResource, "LX.Scene.Image");
            }
        }
    }
    frame->graph = &graph;
    frame->graphEpoch = graph.ResourceEpoch();
}

void SceneHost::DeclareShadow(EnhancedRenderGraph& graph, RGHandle shadowMap) const
{
    ce::profile_scope profile{ce::marker<"MaterialDeclareShadow">()};
    const auto frame = frame_;
    if (!frame || !frame->shadow)
    {
        return;
    }
    frame->CheckCurrent();
    if (frame->shadowDeclared)
    {
        throw std::runtime_error("LX Scene shadow requires one declaration per prepared frame.");
    }
    DeclareGeometry(graph);
    std::vector<EnhancedRenderGraph::RGPassUsage> uses{{shadowMap, RHIResourceState::DepthWrite}};
    for (const auto& draw : frame->draws)
    {
        if (!draw.shadowBindings)
        {
            continue;
        }
        uses.push_back({draw.geometry->GraphOutput(graph), RHIResourceState::ShaderResource});
        for (const auto& texture : draw.shadowBindings->resources.textures)
        {
            uses.push_back({graph.FindImportedTexture(texture.resource), RHIResourceState::PixelShaderResource});
        }
    }
    graph.AddPass("LX.Scene.Shadow", uses, [frame, shadowMap](const auto& execution) {
        frame->CheckCurrent(execution.graph);
        auto& encoder = *execution.encoder;
        const auto map = execution.ResolveHandle(shadowMap);
        const auto description = frame->device->DescribeTexture(map);
        if (description.depthOrArraySize != 3 || description.format != RHIFormat::D32Float)
        {
            throw std::runtime_error("LX Scene shadow needs its three D32 cascades.");
        }
        encoder.SetViewportAndScissor(description.width, description.height);
        encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
        for (unsigned cascade = 0; cascade < 3; ++cascade)
        {
            const auto depth = RHIDepthTargetDesc::DepthSlice(map, RHIFormat::D32Float, cascade);
            const auto targets = frame->device->CreateRenderTargets(std::span<const RHITextureHandle>{}, &depth);
            if (!targets.IsValid())
            {
                throw std::runtime_error("LX Scene cascade target binding failed.");
            }
            encoder.BindRenderTargets(targets);
            for (const auto& draw : frame->draws)
            {
                if (!draw.shadowBindings)
                {
                    continue;
                }
                encoder.SetPipeline(RHIBindPoint::Graphics, draw.program->shadow.GetHandle());
                std::string error;
                if (!RenderBindingCache::Bind(*frame->device, encoder, RHIBindPoint::Graphics, *draw.shadowBindings,
                                              error))
                {
                    throw std::runtime_error(error);
                }
                encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0, draw.shadowConstants[cascade]);
                encoder.SetRootBuffer(RHIBindPoint::Graphics, 1, RHIBufferSlice::Whole(draw.geometry->Buffer()));
                encoder.SetIndexBuffer(draw.indices, RHIFormat::R32Uint);
                encoder.DrawIndexed(draw.geometry->Input()->Geometry().indexCount, 1);
            }
        }
    });
    frame->shadowDeclared = true;
}

void SceneHost::DeclareGBuffer(EnhancedRenderGraph& graph, const EnhancedGBufferPass::Outputs& inputs) const
{
    ce::profile_scope profile{ce::marker<"MaterialDeclareGBuffer">()};
    const auto frame = frame_;
    if (!frame || frame->draws.empty())
    {
        return;
    }
    frame->CheckCurrent();
    if (frame->gbufferDeclared)
    {
        throw std::runtime_error("LX Scene GBuffer requires one declaration per prepared frame.");
    }
    DeclareGeometry(graph);
    std::vector<EnhancedRenderGraph::RGPassUsage> uses;
    for (const auto& draw : frame->draws)
    {
        uses.push_back({draw.geometry->GraphOutput(graph), RHIResourceState::ShaderResource});
        for (const auto& texture : draw.bindings->resources.textures)
        {
            uses.push_back({graph.FindImportedTexture(texture.resource), RHIResourceState::PixelShaderResource});
        }
    }
    for (const auto target : Colors(inputs))
    {
        uses.push_back({target, RHIResourceState::RenderTarget});
    }
    uses.push_back({inputs.depth, RHIResourceState::DepthWrite});
    graph.AddPass("LX.Scene.GBuffer", uses, [frame, inputs](const auto& execution) {
        frame->CheckCurrent(execution.graph);
        auto& encoder = *execution.encoder;
        std::array<RHITextureHandle, 5> colors;
        const auto handles = Colors(inputs);
        for (unsigned i = 0; i < 5; ++i)
        {
            colors[i] = execution.ResolveHandle(handles[i]);
        }
        const auto depth = RHIDepthTargetDesc::Depth(execution.ResolveHandle(inputs.depth), RHIFormat::D32Float);
        const auto targets = frame->device->CreateRenderTargets(colors, &depth);
        if (!targets.IsValid())
        {
            throw std::runtime_error("LX Scene shared GBuffer target binding failed.");
        }
        encoder.BindRenderTargets(targets);
        encoder.SetViewportAndScissor(frame->input->View().width, frame->input->View().height);
        encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
        for (const auto& draw : frame->draws)
        {
            if (!draw.program->hasSurface || draw.program->hasTransmission)
            {
                continue;
            }
            encoder.SetPipeline(RHIBindPoint::Graphics, draw.program->gbuffer[draw.doubleSided].GetHandle());
            std::string error;
            if (!RenderBindingCache::Bind(*frame->device, encoder, RHIBindPoint::Graphics, *draw.bindings, error))
            {
                throw std::runtime_error(error);
            }
            encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0, draw.constants);
            encoder.SetRootBuffer(RHIBindPoint::Graphics, 1, RHIBufferSlice::Whole(draw.geometry->Buffer()));
            encoder.SetIndexBuffer(draw.indices, RHIFormat::R32Uint);
            encoder.DrawIndexed(draw.geometry->Input()->Geometry().indexCount, 1);
        }
    });
    frame->gbufferDeclared = true;
    frame->gbuffer = inputs;
}

void SceneHost::DeclareDecalInputs(EnhancedRenderGraph& graph, const EnhancedGBufferPass::Outputs& inputs,
                                   const std::array<RGHandle, 3>& baseline) const
{
    const auto frame = frame_;
    if (!frame || frame->draws.empty())
    {
        return;
    }
    frame->CheckCurrent(&graph);
    const auto expected = Colors(frame->gbuffer), actual = Colors(inputs);
    if (!frame->gbufferDeclared || frame->colorDeclared || frame->decalDeclared ||
        !std::equal(expected.begin(), expected.end(), actual.begin(),
                    [](RGHandle a, RGHandle b) { return a.index == b.index; }) ||
        frame->gbuffer.depth.index != inputs.depth.index ||
        std::ranges::any_of(baseline, [](RGHandle handle) { return !handle.IsValid(); }))
    {
        throw std::runtime_error(
            "LX Scene Decal requires its GBuffer, a valid snapshot and one declaration before Color.");
    }
    frame->decalBaseline = baseline;
    frame->decalDeclared = true;
}

void SceneHost::DeclareColor(EnhancedRenderGraph& graph, const EnhancedGBufferPass::Outputs& inputs, RGHandle lighting,
                             RGHandle ambientOcclusion, RGHandle shadowMap) const
{
    const auto frame = frame_;
    if (!frame || frame->draws.empty())
    {
        return;
    }
    frame->CheckCurrent();
    const auto expected = Colors(frame->gbuffer), actual = Colors(inputs);
    const bool sameTargets = std::equal(expected.begin(), expected.end(), actual.begin(),
                                        [](RGHandle a, RGHandle b) { return a.index == b.index; });
    if (!frame->gbufferDeclared || frame->graph != &graph || frame->graphEpoch != graph.ResourceEpoch() ||
        frame->colorDeclared || !sameTargets || frame->gbuffer.depth.index != inputs.depth.index)
    {
        throw std::runtime_error("LX Scene color requires its own GBuffer producer and one declaration.");
    }
    frame->lookup->DeclareInputs(graph);
    if (frame->volume)
    {
        frame->volume->DeclareCoefficients(graph, frame->volumeBindings);
    }
    if (frame->subsurface)
    {
        frame->subsurface->DeclareInputs(graph);
    }
    DeclareShading(graph, inputs, lighting, ambientOcclusion, shadowMap, false);
    if (frame->refraction)
    {
        frame->refraction->DeclareInputs(graph);
        frame->refraction->DeclareBackground(graph, lighting, inputs.depth);
        DeclareTransmissionGBuffer(graph, inputs);
        DeclareShading(graph, inputs, lighting, ambientOcclusion, shadowMap, true);
    }
    frame->colorDeclared = true;
    frame->lookup->DeclareReady(graph);
}

RGHandle SceneHost::DeclareVolume(EnhancedRenderGraph& graph, RGHandle lighting, RGHandle depth,
                                  RGHandle shadowMap) const
{
    const auto frame = frame_;
    if (!frame || !frame->volume)
    {
        return lighting;
    }
    frame->CheckCurrent(&graph);
    if (!frame->colorDeclared)
    {
        throw std::runtime_error("LX Scene Volume requires completed surface shading.");
    }
    return frame->volume->DeclareComposite(graph, lighting, depth, shadowMap);
}
void SceneHost::DeclareTransmissionGBuffer(EnhancedRenderGraph& graph, const EnhancedGBufferPass::Outputs& inputs) const
{
    const auto frame = frame_;
    std::vector<EnhancedRenderGraph::RGPassUsage> uses{{inputs.depth, RHIResourceState::DepthWrite}};
    for (const auto target : Colors(inputs))
    {
        uses.push_back({target, RHIResourceState::RenderTarget});
    }
    for (const auto& draw : frame->draws)
    {
        if (!draw.program->hasTransmission)
        {
            continue;
        }
        uses.push_back({draw.geometry->GraphOutput(graph), RHIResourceState::ShaderResource});
        for (const auto& texture : draw.bindings->resources.textures)
        {
            uses.push_back({graph.FindImportedTexture(texture.resource), RHIResourceState::PixelShaderResource});
        }
    }
    graph.AddPass("LX.Scene.TransmissionGBuffer", uses, [frame, inputs](const auto& execution) {
        frame->CheckCurrent(execution.graph);
        std::array<RHITextureHandle, 5> colors;
        const auto handles = Colors(inputs);
        for (unsigned i = 0; i < colors.size(); ++i)
        {
            colors[i] = execution.ResolveHandle(handles[i]);
        }
        const auto depth = RHIDepthTargetDesc::Depth(execution.ResolveHandle(inputs.depth), RHIFormat::D32Float);
        const auto targets = frame->device->CreateRenderTargets(colors, &depth);
        if (!targets.IsValid())
        {
            throw std::runtime_error("LX Scene transmission GBuffer binding failed.");
        }
        auto& encoder = *execution.encoder;
        encoder.BindRenderTargets(targets);
        encoder.SetViewportAndScissor(frame->input->View().width, frame->input->View().height);
        encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
        for (const auto& draw : frame->draws)
        {
            if (!draw.program->hasTransmission)
            {
                continue;
            }
            encoder.SetPipeline(RHIBindPoint::Graphics, draw.program->gbuffer[draw.doubleSided].GetHandle());
            std::string error;
            if (!RenderBindingCache::Bind(*frame->device, encoder, RHIBindPoint::Graphics, *draw.bindings, error))
            {
                throw std::runtime_error(error);
            }
            encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0, draw.constants);
            encoder.SetRootBuffer(RHIBindPoint::Graphics, 1, RHIBufferSlice::Whole(draw.geometry->Buffer()));
            encoder.SetIndexBuffer(draw.indices, RHIFormat::R32Uint);
            encoder.DrawIndexed(draw.geometry->Input()->Geometry().indexCount, 1);
        }
    });
}

void SceneHost::DeclareRefractionCapture(EnhancedRenderGraph& graph, const EnhancedGBufferPass::Outputs& inputs) const
{
    const auto frame = frame_;
    std::vector<EnhancedRenderGraph::RGPassUsage> uses{{inputs.depth, RHIResourceState::DepthRead},
                                                       {inputs.bitmask, RHIResourceState::ShaderResource}};
    for (const auto input : frame->refraction->Inputs())
    {
        uses.push_back({graph.FindImportedTexture(input), RHIResourceState::RenderTarget});
    }
    for (const auto& draw : frame->draws)
    {
        if (!draw.program->hasTransmission)
        {
            continue;
        }
        uses.push_back({draw.geometry->GraphOutput(graph), RHIResourceState::ShaderResource});
        for (const auto& texture : draw.bindings->resources.textures)
        {
            uses.push_back({graph.FindImportedTexture(texture.resource), RHIResourceState::PixelShaderResource});
        }
    }
    graph.AddPass("LX.Scene.RefractionCapture", uses, [frame, inputs](const auto& execution) {
        frame->CheckCurrent(execution.graph);
        const auto depth =
            RHIDepthTargetDesc::DepthReadOnly(execution.ResolveHandle(inputs.depth), RHIFormat::D32Float);
        const auto targets = frame->device->CreateRenderTargets(frame->refraction->Inputs(), &depth);
        const RHIBindingDesc descriptions[]{
            RHIBindingDesc::SrvCube({}, RHIFormat::RGBA16Float, 1).OrNull(),
            RHIBindingDesc::Srv2D(execution.ResolveHandle(inputs.bitmask), RHIFormat::R32Uint),
            RHIBindingDesc::Srv2D({}, RHIFormat::RG16Float).OrNull(),
            RHIBindingDesc::SrvArray({}, RHIFormat::R32Float, 3).OrNull()};
        const auto table = frame->device->CreateBindings(descriptions);
        const auto decalTable = frame->DecalTable(execution, true);
        if (!targets.IsValid() || !table.IsValid())
        {
            throw std::runtime_error("LX Scene refraction capture binding failed.");
        }
        auto& encoder = *execution.encoder;
        encoder.BindRenderTargets(targets);
        const float clear[]{0, 0, 0, 0};
        encoder.ClearRenderTargets(targets, clear);
        encoder.SetViewportAndScissor(frame->input->View().width, frame->input->View().height);
        encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
        for (const auto& draw : frame->draws)
        {
            if (!draw.program->hasTransmission)
            {
                continue;
            }
            encoder.SetPipeline(RHIBindPoint::Graphics, draw.program->refraction[draw.doubleSided].GetHandle());
            std::string error;
            if (!RenderBindingCache::Bind(*frame->device, encoder, RHIBindPoint::Graphics, *draw.bindings, error))
            {
                throw std::runtime_error(error);
            }
            encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0, draw.constants);
            encoder.SetRootBuffer(RHIBindPoint::Graphics, 1, RHIBufferSlice::Whole(draw.geometry->Buffer()));
            encoder.SetBindings(RHIBindPoint::Graphics, 2, table);
            frame->BindDecal(encoder, draw, decalTable, true);
            encoder.SetIndexBuffer(draw.indices, RHIFormat::R32Uint);
            encoder.DrawIndexed(draw.geometry->Input()->Geometry().indexCount, 1);
        }
    });
}

void SceneHost::DeclareShading(EnhancedRenderGraph& graph, const EnhancedGBufferPass::Outputs& inputs,
                               RGHandle lighting, RGHandle ambientOcclusion, RGHandle shadowMap,
                               bool transmissionStage) const
{
    const auto frame = frame_;
    std::vector<EnhancedRenderGraph::RGPassUsage> uses{{lighting, RHIResourceState::RenderTarget},
                                                       {inputs.depth, RHIResourceState::DepthRead},
                                                       {inputs.bitmask, RHIResourceState::ShaderResource},
                                                       {ambientOcclusion, RHIResourceState::ShaderResource}};
    frame->AddDecalUses(uses, transmissionStage);
    if (frame->environment.IsValid())
    {
        auto environment = graph.FindImportedTexture(frame->environment);
        if (!environment.IsValid())
        {
            environment =
                graph.ImportTexture(frame->environment, RHIResourceState::PixelShaderResource, "LX.Scene.HDR");
        }
        uses.push_back({environment, RHIResourceState::PixelShaderResource});
    }
    if (frame->shadow)
    {
        uses.push_back({shadowMap, RHIResourceState::ShaderResource});
    }
    for (const auto& draw : frame->draws)
    {
        uses.push_back({draw.geometry->GraphOutput(graph), RHIResourceState::ShaderResource});
        for (const auto& texture : draw.bindings->resources.textures)
        {
            uses.push_back({graph.FindImportedTexture(texture.resource), RHIResourceState::PixelShaderResource});
        }
    }
    std::array<RGHandle, 11> lookupInputs;
    for (unsigned i = 0; i < lookupInputs.size(); ++i)
    {
        lookupInputs[i] = graph.FindImportedTexture(frame->lookup->Inputs()[i]);
    }
    for (unsigned part = 0; part < 2; ++part)
    {
        std::vector<EnhancedRenderGraph::RGPassUsage> captureUses{{inputs.depth, RHIResourceState::DepthRead},
                                                                  {inputs.bitmask, RHIResourceState::ShaderResource}};
        frame->AddDecalUses(captureUses, transmissionStage);
        const unsigned first = part ? 8 : 0, count = part ? 3 : 8;
        for (unsigned i = first; i < first + count; ++i)
            captureUses.push_back({lookupInputs[i], RHIResourceState::RenderTarget});
        for (const auto& draw : frame->draws)
        {
            captureUses.push_back({draw.geometry->GraphOutput(graph), RHIResourceState::ShaderResource});
            for (const auto& texture : draw.bindings->resources.textures)
                captureUses.push_back(
                    {graph.FindImportedTexture(texture.resource), RHIResourceState::PixelShaderResource});
        }
        graph.AddPass(
            "LX.Scene.LookupCapture", captureUses,
            [frame, inputs, first, count, part, transmissionStage](const auto& execution) {
                frame->CheckCurrent(execution.graph);
                auto& encoder = *execution.encoder;
                const auto handles = frame->lookup->Inputs().subspan(first, count);
                const auto depth =
                    RHIDepthTargetDesc::DepthReadOnly(execution.ResolveHandle(inputs.depth), RHIFormat::D32Float);
                const auto targets = frame->device->CreateRenderTargets(handles, &depth);
                const RHIBindingDesc descriptions[]{
                    RHIBindingDesc::SrvCube({}, RHIFormat::RGBA16Float, 1).OrNull(),
                    RHIBindingDesc::Srv2D(execution.ResolveHandle(inputs.bitmask), RHIFormat::R32Uint),
                    RHIBindingDesc::Srv2D({}, RHIFormat::RG16Float).OrNull(),
                    RHIBindingDesc::SrvArray({}, RHIFormat::R32Float, 3).OrNull()};
                const auto table = frame->device->CreateBindings(descriptions);
                const auto decalTable = frame->DecalTable(execution, transmissionStage);
                frame->CheckCurrent();
                if (!targets.IsValid() || !table.IsValid())
                    throw std::runtime_error("LX Scene lookup capture binding failed.");
                encoder.BindRenderTargets(targets);
                const float clear[]{0, 0, 0, 0};
                encoder.ClearRenderTargets(targets, clear);
                encoder.SetViewportAndScissor(frame->input->View().width, frame->input->View().height);
                encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
                for (const auto& draw : frame->draws)
                {
                    if (!draw.program->hasSurface)
                    {
                        continue;
                    }
                    encoder.SetPipeline(RHIBindPoint::Graphics,
                                        draw.program->lookup[part][draw.doubleSided].GetHandle());
                    std::string error;
                    if (!RenderBindingCache::Bind(*frame->device, encoder, RHIBindPoint::Graphics, *draw.bindings,
                                                  error))
                        throw std::runtime_error(error);
                    encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0, draw.constants);
                    encoder.SetRootBuffer(RHIBindPoint::Graphics, 1, RHIBufferSlice::Whole(draw.geometry->Buffer()));
                    encoder.SetBindings(RHIBindPoint::Graphics, 2, table);
                    frame->BindDecal(encoder, draw, decalTable, transmissionStage);
                    encoder.SetIndexBuffer(draw.indices, RHIFormat::R32Uint);
                    encoder.DrawIndexed(draw.geometry->Input()->Geometry().indexCount, 1);
                }
            });
    }
    frame->lookup->DeclareBake(graph, inputs.bitmask);
    uses.push_back({frame->lookup->GraphSamples(graph), RHIResourceState::PixelShaderResource});
    if (transmissionStage)
    {
        DeclareRefractionCapture(graph, inputs);
        frame->refraction->DeclareBake(graph, *frame->lookup, inputs.bitmask, shadowMap);
        uses.push_back({frame->refraction->GraphSamples(graph), RHIResourceState::PixelShaderResource});
    }
    if (frame->subsurface)
    {
        std::array<RGHandle, 7> targets;
        for (unsigned i = 0; i < targets.size(); ++i)
        {
            targets[i] = graph.FindImportedTexture(frame->subsurface->Inputs()[i]);
        }
        frame->subsurface->DeclareReflection(graph, *frame->lookup, inputs.bitmask);
        std::vector<EnhancedRenderGraph::RGPassUsage> captureUses{
            {inputs.depth, RHIResourceState::DepthRead},
            {inputs.bitmask, RHIResourceState::ShaderResource},
            {frame->lookup->GraphSamples(graph), RHIResourceState::PixelShaderResource},
            {frame->subsurface->GraphReflection(graph), RHIResourceState::PixelShaderResource}};
        frame->AddDecalUses(captureUses, transmissionStage);
        for (const auto target : targets)
        {
            captureUses.push_back({target, RHIResourceState::RenderTarget});
        }
        if (frame->shadow)
        {
            captureUses.push_back({shadowMap, RHIResourceState::ShaderResource});
        }
        for (const auto& draw : frame->draws)
        {
            if (!draw.program->hasSubsurface)
            {
                continue;
            }
            captureUses.push_back({draw.geometry->GraphOutput(graph), RHIResourceState::ShaderResource});
            for (const auto& texture : draw.bindings->resources.textures)
            {
                captureUses.push_back(
                    {graph.FindImportedTexture(texture.resource), RHIResourceState::PixelShaderResource});
            }
        }
        if (transmissionStage)
        {
            captureUses.push_back({frame->refraction->GraphSamples(graph), RHIResourceState::PixelShaderResource});
        }
        graph.AddPass(
            "LX.Scene.SubsurfaceCapture", captureUses,
            [frame, inputs, shadowMap, transmissionStage](const auto& execution) {
                frame->CheckCurrent(execution.graph);
                auto& encoder = *execution.encoder;
                const auto depth =
                    RHIDepthTargetDesc::DepthReadOnly(execution.ResolveHandle(inputs.depth), RHIFormat::D32Float);
                const auto targets = frame->device->CreateRenderTargets(frame->subsurface->Inputs(), &depth);
                const RHIBindingDesc descriptions[]{
                    RHIBindingDesc::SrvCube({}, RHIFormat::RGBA16Float, 1).OrNull(),
                    RHIBindingDesc::Srv2D(execution.ResolveHandle(inputs.bitmask), RHIFormat::R32Uint),
                    RHIBindingDesc::Srv2D({}, RHIFormat::RG16Float).OrNull(),
                    RHIBindingDesc::SrvArray(frame->shadow ? execution.ResolveHandle(shadowMap) : RHITextureHandle{},
                                             RHIFormat::R32Float, 3)
                        .OrNull()};
                const auto table = frame->device->CreateBindings(descriptions);
                const auto decalTable = frame->DecalTable(execution, transmissionStage);
                if (!targets.IsValid() || !table.IsValid())
                {
                    throw std::runtime_error("LX Scene SSS capture target binding failed.");
                }
                encoder.BindRenderTargets(targets);
                const float clear[]{0, 0, 0, 0};
                encoder.ClearRenderTargets(targets, clear);
                encoder.SetViewportAndScissor(frame->input->View().width, frame->input->View().height);
                encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
                for (const auto& draw : frame->draws)
                {
                    if (!draw.program->hasSubsurface || (draw.program->hasTransmission && !transmissionStage))
                    {
                        continue;
                    }
                    encoder.SetPipeline(RHIBindPoint::Graphics, draw.program->subsurface[draw.doubleSided].GetHandle());
                    std::string error;
                    if (!RenderBindingCache::Bind(*frame->device, encoder, RHIBindPoint::Graphics, *draw.bindings,
                                                  error))
                    {
                        throw std::runtime_error(error);
                    }
                    encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0, draw.constants);
                    encoder.SetRootBuffer(RHIBindPoint::Graphics, 1, RHIBufferSlice::Whole(draw.geometry->Buffer()));
                    encoder.SetBindings(RHIBindPoint::Graphics, 2, table);
                    frame->BindDecal(encoder, draw, decalTable, transmissionStage);
                    encoder.SetRootBuffer(RHIBindPoint::Graphics, 3, RHIBufferSlice::Whole(frame->lookup->Samples()));
                    encoder.SetRootBuffer(RHIBindPoint::Graphics, 4,
                                          RHIBufferSlice::Whole(frame->subsurface->Reflection()));
                    if (draw.program->hasTransmission)
                    {
                        encoder.SetRootBuffer(RHIBindPoint::Graphics, 6,
                                              RHIBufferSlice::Whole(frame->refraction->Samples()));
                    }
                    encoder.SetIndexBuffer(draw.indices, RHIFormat::R32Uint);
                    encoder.DrawIndexed(draw.geometry->Input()->Geometry().indexCount, 1);
                }
            });
        frame->subsurface->DeclareFilter(graph, inputs.bitmask);
        uses.push_back({frame->subsurface->GraphReflection(graph), RHIResourceState::PixelShaderResource});
        uses.push_back({frame->subsurface->GraphIrradiance(graph), RHIResourceState::PixelShaderResource});
    }
    graph.AddPass(
        transmissionStage ? "LX.Scene.TransmissionColor" : "LX.Scene.Color", uses,
        [frame, inputs, lighting, ambientOcclusion, shadowMap, transmissionStage](const auto& execution) {
            frame->CheckCurrent(execution.graph);
            auto& encoder = *execution.encoder;
            const auto color = execution.ResolveHandle(lighting);
            const auto depth =
                RHIDepthTargetDesc::DepthReadOnly(execution.ResolveHandle(inputs.depth), RHIFormat::D32Float);
            const auto target = frame->device->CreateRenderTargets({&color, 1}, &depth);
            std::array<RHIBindingDesc, 4> descriptions{
                RHIBindingDesc::SrvCube(frame->environment,
                                        frame->environment.IsValid()
                                            ? frame->device->DescribeTexture(frame->environment).format
                                            : RHIFormat::RGBA16Float,
                                        1)
                    .OrNull(),
                RHIBindingDesc::Srv2D(execution.ResolveHandle(inputs.bitmask), RHIFormat::R32Uint),
                RHIBindingDesc::Srv2D(execution.ResolveHandle(ambientOcclusion), RHIFormat::RG16Float),
                RHIBindingDesc::SrvArray(frame->shadow ? execution.ResolveHandle(shadowMap) : RHITextureHandle{},
                                         RHIFormat::R32Float, 3)
                    .OrNull()};
            const auto table = frame->device->CreateBindings(descriptions);
            const auto decalTable = frame->DecalTable(execution, transmissionStage);
            frame->CheckCurrent();
            if (!target.IsValid() || !table.IsValid())
            {
                throw std::runtime_error("LX Scene color/depth/AO/environment binding failed.");
            }
            encoder.BindRenderTargets(target);
            encoder.SetViewportAndScissor(frame->input->View().width, frame->input->View().height);
            encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
            for (const auto& draw : frame->draws)
            {
                if (!draw.program->hasSurface || draw.program->hasTransmission != transmissionStage)
                {
                    continue;
                }
                encoder.SetPipeline(RHIBindPoint::Graphics, draw.program->color[draw.doubleSided].GetHandle());
                std::string error;
                if (!RenderBindingCache::Bind(*frame->device, encoder, RHIBindPoint::Graphics, *draw.bindings, error))
                {
                    throw std::runtime_error(error);
                }
                encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0, draw.constants);
                encoder.SetRootBuffer(RHIBindPoint::Graphics, 1, RHIBufferSlice::Whole(draw.geometry->Buffer()));
                encoder.SetBindings(RHIBindPoint::Graphics, 2, table);
                frame->BindDecal(encoder, draw, decalTable, transmissionStage);
                encoder.SetRootBuffer(RHIBindPoint::Graphics, 3, RHIBufferSlice::Whole(frame->lookup->Samples()));
                if (draw.program->hasSpecial)
                {
                    encoder.SetRootBuffer(RHIBindPoint::Graphics, 4,
                                          RHIBufferSlice::Whole(frame->subsurface->Reflection()));
                    encoder.SetRootBuffer(RHIBindPoint::Graphics, 5,
                                          RHIBufferSlice::Whole(frame->subsurface->Irradiance()));
                }
                if (draw.program->hasTransmission)
                {
                    encoder.SetRootBuffer(RHIBindPoint::Graphics, 6,
                                          RHIBufferSlice::Whole(frame->refraction->Samples()));
                }
                encoder.SetIndexBuffer(draw.indices, RHIFormat::R32Uint);
                encoder.DrawIndexed(draw.geometry->Input()->Geometry().indexCount, 1);
            }
        });
}

bool SceneHost::PublishSubmittedCache(std::uint64_t frameId, RHICompletionPoint completion, std::string& error,
                                      RHISubmissionTicket ticket)
{
    if (!HasDraws())
    {
        error.clear();
        return true;
    }
    {
        std::lock_guard lock(recordingMutex_);
        const auto found = recordings_.find(frame_->recording);
        if (found == recordings_.end() || !found->second.submitted || found->second.decided ||
            found->second.publication || found->second.completion != completion.value ||
            frame_->input->View().frameId != frameId)
            return Fail(error, "LX Scene material publication needs its exact successful native submission.");
    }
    if (ticket.IsValid())
    {
        const auto* batch = ticket.GetRecordedBatch();
        if (!batch || batch->GetFrameId() != frameId || batch->GetCompletionPoint().value != completion.value)
            return Fail(error, "LX Scene publication ticket must identify its exact recorded graph batch.");
        if (!ticket.IsComplete())
        {
            std::lock_guard lock(recordingMutex_);
            auto& recording = recordings_.at(frame_->recording);
            recording.publication = frame_;
            recording.ticket = std::move(ticket);
            error.clear();
            return true;
        }
        // is_complete makes this a status read, not a submission wait.
        if (!GetRHISubmissionThread().Wait(ticket, error))
            return false;
    }
    // A newer ticket can finish between PollPrograms and this call. Its
    // immediate publication must not overtake older deferred publications:
    // those frames would be rejected as stale and lose their recyclable owner.
    // The FIFO has completed older submissions when this ticket is complete.
    PollSubmittedFrames();
    return CommitSubmittedFrame(frame_, completion, error);
}

bool SceneHost::CommitSubmittedFrame(const std::shared_ptr<const Frame>& frame, RHICompletionPoint completion,
                                     std::string& error)
{
    if (!lookup_.PublishSubmitted(*frame->lookup, frame->input->View().frameId, completion, error))
        return false;
    for (const auto& draw : frame->draws)
        draw.geometry->MarkSubmitted(completion);
    const auto& view = frame->input->View();
    for (const auto& draw : frame->input->Draws())
    {
        if (!draw.selectionRevision)
            continue;
        const auto found = slots_.find({view.sceneEpoch, view.viewId, draw.materialSlot});
        if (found == slots_.end() || found->second->revision != draw.selectionRevision)
        {
            ++stats_.stalePublications;
            continue;
        }
        found->second->active = draw.material;
        found->second->activeCoverage = draw.coverage;
        ++stats_.publications;
    }
    {
        std::lock_guard lock(recordingMutex_);
        auto& recording = recordings_.at(frame->recording);
        recording.decided = true;
        recording.publication.reset();
        recording.ticket = {};
        if (recording.completion <= completed_)
            recordings_.erase(frame->recording);
    }
    error.clear();
    return true;
}

void SceneHost::PollSubmittedFrames()
{
    struct Pending
    {
        std::shared_ptr<const Frame> frame;
        RHISubmissionTicket ticket;
        RHICompletionPoint completion;
    };
    std::vector<Pending> ready;
    {
        std::lock_guard lock(recordingMutex_);
        for (const auto& [id, recording] : recordings_)
            if (recording.publication && recording.ticket.IsComplete())
                ready.push_back({recording.publication, recording.ticket, {recording.completion}});
    }
    for (const auto& item : ready)
    {
        std::string error;
        if (GetRHISubmissionThread().Wait(item.ticket, error) &&
            CommitSubmittedFrame(item.frame, item.completion, error))
            continue;
        stats_.lastError = std::move(error);
        ++stats_.failedSubmissions;
        std::lock_guard lock(recordingMutex_);
        const auto found = recordings_.find(item.frame->recording);
        if (found != recordings_.end())
        {
            found->second.decided = true;
            found->second.publication.reset();
            found->second.ticket = {};
            if (found->second.completion <= completed_)
                recordings_.erase(found);
        }
    }
}

RHIBufferHandle SceneHost::LookupStatistics() const
{
    return HasDraws() ? frame_->lookup->Statistics() : RHIBufferHandle{};
}

RGHandle SceneHost::GraphLookupStatistics(const EnhancedRenderGraph& graph) const
{
    return HasDraws() ? frame_->lookup->GraphStatistics(graph) : RGHandle{};
}

std::shared_ptr<const SceneLookupFrame> SceneHost::LookupFrame() const
{
    return HasDraws() ? frame_->lookup : nullptr;
}

std::shared_ptr<const SceneSubsurfaceFrame> SceneHost::SubsurfaceFrame() const
{
    return HasDraws() ? frame_->subsurface : nullptr;
}

std::shared_ptr<const SceneRefractionFrame> SceneHost::RefractionFrame() const
{
    return HasDraws() ? frame_->refraction : nullptr;
}

std::shared_ptr<const SceneVolumeFrame> SceneHost::VolumeFrame() const
{
    return HasDraws() ? frame_->volume : nullptr;
}
bool SceneHost::HasDraws() const
{
    return frame_ && !frame_->draws.empty();
}

void SceneHost::ShutdownAfterIdle()
{
    if (device_)
        device_->UnregisterUploadTransactionListener(this);
    frame_.reset();
    lookup_.ShutdownAfterIdle();
    subsurface_.ShutdownAfterIdle();
    refraction_.ShutdownAfterIdle();
    volume_.ShutdownAfterIdle();
    programs_.clear();
    bindings_.Clear();
    geometry_ = {};
    device_ = nullptr;
    programDevice_ = nullptr;
    preparations_.clear();
    slots_.clear();
    {
        std::lock_guard lock(recordingMutex_);
        recordings_.clear();
        completed_ = 0;
    }
    stats_ = {};
    selectionSerial_ = 0;
}

void SceneHost::OnUploadSubmitted(std::uint64_t recording, RHICompletionPoint completion)
{
    std::lock_guard lock(recordingMutex_);
    const auto found = recordings_.find(recording);
    if (found != recordings_.end())
    {
        found->second.submitted = true;
        found->second.completion = completion.value;
    }
}

void SceneHost::OnUploadCompleted(std::uint64_t completed)
{
    geometry_.NotifyCompleted(completed);
    std::lock_guard lock(recordingMutex_);
    completed_ = (std::max)(completed_, completed);
    for (auto& [recording, owner] : recordings_)
        if (owner.submitted && owner.completion <= completed_)
            owner.owners.clear();
    std::erase_if(recordings_, [&](const auto& item) {
        return item.second.submitted && item.second.decided && item.second.completion <= completed_;
    });
}

void SceneHost::OnUploadAborted(std::uint64_t recording)
{
    std::lock_guard lock(recordingMutex_);
    recordings_.erase(recording);
}
} // namespace material_graph
