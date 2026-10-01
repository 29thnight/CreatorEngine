#include "../EngineDiagnostics/ProfileScope.h"
#include "MaterialGraphSceneLookup.h"

#include "RHI/RHIShaderCompiler.h"
#include "RHI/RHIShaderSource.h"

#include <algorithm>
#include <atomic>
#include <stdexcept>

namespace material_graph
{
namespace
{
constexpr std::uint32_t kDispatchPixels = 4096;
constexpr std::uint64_t kBytesPerPixel = sizeof(IblBakePoint) + sizeof(IblBakeSample);
struct LookupConstants
{
    std::uint32_t width, height, count, reuse;
    std::uint32_t environment, first, dispatchCount, precomputed;
    std::uint32_t importance, source, reserved[2]{};
};
bool Fail(std::string& error, const char* message)
{
    error = message;
    return false;
}
} // namespace

// A frame remains alive until the submitted graph is retired. Retaining its
// render targets behind that fence avoids full-size texture allocations on
// every camera move while preserving the exact RGBA32F material values.
struct SceneLookupResourcePool
{
    struct Bundle
    {
        std::uint32_t width{}, height{};
        std::uint64_t completion{};
        std::array<RHITextureHandle, 11> inputs{};
        RHIBufferHandle samples, statistics;
        std::array<RHIResourceState, 11> inputStates{};
        RHIResourceState sampleState{RHIResourceState::Common};
        RHIResourceState statisticState{RHIResourceState::Common};
    };

    explicit SceneLookupResourcePool(IRenderDeviceServices& owner) : device(owner) {}
    ~SceneLookupResourcePool() { for (const auto& bundle : idle) Release(bundle); }

    void Release(const Bundle& bundle)
    {
        for (const auto input : bundle.inputs)
            if (input.IsValid()) device.ReleaseTexture(input);
        if (bundle.samples.IsValid()) device.ReleaseBuffer(bundle.samples);
        if (bundle.statistics.IsValid()) device.ReleaseBuffer(bundle.statistics);
    }

    bool Acquire(SceneLookupFrame& frame)
    {
        ce::profile_scope profile{ce::marker<"MaterialLookupPoolAcquire">()};
        std::lock_guard lock(mutex);
        for (auto it = idle.begin(); it != idle.end();)
        {
            if (it->completion > completed.load(std::memory_order_acquire))
            {
                ++it;
                continue;
            }
            if (it->width != frame.width_ || it->height != frame.height_)
            {
                Release(*it);
                it = idle.erase(it);
                continue;
            }
            frame.inputs_ = it->inputs;
            frame.samples_ = it->samples;
            frame.statistics_ = it->statistics;
            frame.inputStates_ = it->inputStates;
            frame.sampleState_ = it->sampleState;
            frame.statisticState_ = it->statisticState;
            idle.erase(it);
            return true;
        }
        ce::profile_instant(idle.empty() ? ce::marker<"MaterialLookupPoolEmpty">()
                                        : ce::marker<"MaterialLookupPoolFencePending">());
        return false;
    }

    bool Recycle(const SceneLookupFrame& frame)
    {
        if (!frame.completion_.IsValid())
        {
            ce::profile_instant(ce::marker<"MaterialLookupUnsubmittedDiscard">());
            return false;
        }
        std::lock_guard lock(mutex);
        // A view has a three-slot display ring. Completion/retirement can
        // return all three bundles in one burst before the next Prepare.
        // Keep that bounded burst instead of destroying one and reallocating
        // its eleven full-resolution textures on the next turn.
        if (idle.size() >= 3)
        {
            ce::profile_instant(ce::marker<"MaterialLookupPoolFullDiscard">());
            return false;
        }
        idle.push_back({frame.width_, frame.height_, frame.completion_.value, frame.inputs_,
                        frame.samples_, frame.statistics_, frame.inputStates_, frame.sampleState_,
                        frame.statisticState_});
        return true;
    }

    void Completed(std::uint64_t value)
    {
        auto prior = completed.load(std::memory_order_relaxed);
        while (prior < value && !completed.compare_exchange_weak(prior, value, std::memory_order_release,
                                                                  std::memory_order_relaxed)) {}
    }

    IRenderDeviceServices& device;
    std::mutex mutex;
    std::atomic<std::uint64_t> completed{};
    std::vector<Bundle> idle;
};

SceneLookupFrame::~SceneLookupFrame()
{
    if (!device_)
        return;
    if (!resourcePool_ || !resourcePool_->Recycle(*this))
    {
        for (const auto input : inputs_)
            if (input.IsValid()) device_->ReleaseTexture(input);
        if (samples_.IsValid()) device_->ReleaseBuffer(samples_);
        if (statistics_.IsValid()) device_->ReleaseBuffer(statistics_);
    }
    if (emptyPrevious_.IsValid())
        device_->ReleaseBuffer(emptyPrevious_);
}

bool SceneLookupCache::Initialize(const EnhancedFrameContext& context, std::string& error)
{
    if (device_)
        return device_ == context.resources || Fail(error, "Scene lookup cache belongs to another device.");
    const auto backend = RHIShaderCompiler::GetOutput();
    RHIShaderCompiler::VerifiedShader bake, clear;
    RHIShaderCompileOptions options;
    options.strictMath = true;
    const auto file = RHIShaderSource::Resolve("MaterialGraphSceneLookup.slang").string();
    if (!RHIShaderCompiler::VerifyFile(file, "LXSceneLookupBake", "cs_6_0", backend, {}, bake, error, options) ||
        !RHIShaderCompiler::VerifyFile(file, "LXSceneLookupClear", "cs_6_0", backend, {}, clear, error, options))
        return false;
    const auto other = backend == RHIShaderBinary::Dxil ? RHIShaderBinary::SpirV : RHIShaderBinary::Dxil;
    RHIShaderCompiler::VerifiedShader verification;
    if (!RHIShaderCompiler::VerifyFile(file, "LXSceneLookupBake", "cs_6_0", other, {}, verification, error, options) ||
        !RHIShaderCompiler::VerifyFile(file, "LXSceneLookupClear", "cs_6_0", other, {}, verification, error, options))
        return false;
    const RHIPipelineLayoutParam parameters[]{RHILayout::Cbv(0), RHILayout::SrvTable(24, 0),
                                              RHILayout::UavBufferTable(2, 0), RHILayout::Srv(24),
                                              RHILayout::SrvTable(6, 25)};
    const RHIStaticSamplerDesc samplers[]{
        {RHISampler::Point(RHIAddressMode::Clamp), 0},
        {RHISampler::Linear(RHIAddressMode::Clamp), 1}};
    const auto layout = context.rootSignatures->GetOrCreate({parameters, samplers}, error);
    if (!layout.IsValid())
        return false;
    RHIComputePipelineDesc desc;
    desc.layout = layout;
    desc.csBytecode = bake.bytecode.Data();
    desc.csSize = bake.bytecode.Size();
    const auto bakePipeline = context.psoManager->GetOrCreateCompute(desc, error);
    desc.csBytecode = clear.bytecode.Data();
    desc.csSize = clear.bytecode.Size();
    const auto clearPipeline = context.psoManager->GetOrCreateCompute(desc, error);
    if (!bakePipeline.IsValid() || !clearPipeline.IsValid())
        return false;
    device_ = context.resources;
    bake_ = bakePipeline;
    clear_ = clearPipeline;
    device_->RegisterUploadTransactionListener(this);
    return true;
}

bool SceneLookupCache::Prepare(const EnhancedFrameContext& context, std::uint64_t viewId, RHITextureHandle environment,
                               RHITextureHandle irradiance, RHITextureHandle prefiltered,
                               std::uint64_t environmentGeneration, std::uint64_t memoryBudget,
                               std::shared_ptr<const SceneLookupFrame>& result, std::string& error,
                               std::array<RHITextureHandle,3> importance, RHITextureHandle source)
{
    ce::profile_scope profile{ce::marker<"MaterialLookupPrepare">()};
    const auto count = std::uint64_t(context.width) * context.height;
    if (!context.resources || !context.rootSignatures || !context.psoManager || !context.frameId || !viewId || !count ||
        count > UINT32_MAX || count * kBytesPerPixel + sizeof(SceneLookupStats) > memoryBudget ||
        !context.resources->GetCurrentUploadRecordingId() || (environment.IsValid() && !environmentGeneration))
        return Fail(error, "Scene lookup needs an identified view/environment and sufficient GPU memory budget.");
    std::shared_ptr<const SceneLookupFrame> previous;
    for (const auto& cached : published_)
        if (cached->viewId_ == viewId && cached->sceneEpoch_ == context.sceneEpoch && cached->width_ == context.width &&
            cached->height_ == context.height && cached->environment_ == environment &&
            cached->irradiance_ == irradiance && cached->prefiltered_ == prefiltered &&
            cached->environmentGeneration_ == environmentGeneration && cached->importance_ == importance && cached->source_ == source)
            previous = cached;
    if ((previous ? 2 : 1) * count * kBytesPerPixel + sizeof(SceneLookupStats) > memoryBudget)
        return Fail(error, "Scene lookup candidate plus previous owner exceeds its GPU memory budget.");
    if (!Initialize(context, error))
        return false;
    auto candidate = std::make_shared<SceneLookupFrame>();
    candidate->device_ = device_;
    candidate->width_ = context.width;
    candidate->height_ = context.height;
    candidate->frameId_ = context.frameId;
    candidate->viewId_ = viewId;
    candidate->sceneEpoch_ = context.sceneEpoch;
    candidate->environment_ = environment;
    candidate->irradiance_ = irradiance;
    candidate->prefiltered_ = prefiltered;
    const bool hasImportance=std::ranges::all_of(importance,[](auto handle){return handle.IsValid();});
    if (std::ranges::any_of(importance,[](auto handle){return handle.IsValid();}) && !hasImportance)
        return Fail(error,"Scene environment importance requires all three maps.");
    if (hasImportance)
    {
        const auto rows=device_->DescribeTexture(importance[0]);
        const auto marginal=device_->DescribeTexture(importance[1]);
        const auto samples=device_->DescribeTexture(importance[2]);
        if (!environment.IsValid() || !rows.width || rows.height!=6*rows.width || marginal.width!=1 ||
            marginal.height!=rows.height || (samples.width!=1024 && samples.width!=5120) || samples.height!=2 ||
            rows.format!=RHIFormat::RGBA32Float || marginal.format!=RHIFormat::RGBA32Float ||
            samples.format!=RHIFormat::RGBA32Float || rows.depthOrArraySize!=1 ||
            marginal.depthOrArraySize!=1 || samples.depthOrArraySize!=1)
            return Fail(error,"Scene environment importance layout differs.");
    }
    if (source.IsValid())
    {
        const auto description=device_->DescribeTexture(source);
        if (!environment.IsValid() || !description.width || !description.height ||
            description.format!=RHIFormat::RGBA32Float || description.depthOrArraySize!=1 || description.mipLevels!=1)
            return Fail(error,"Scene decoded environment source layout differs.");
    }
    candidate->source_=source;
    candidate->importance_=importance;
    candidate->environmentGeneration_ = environmentGeneration;
    candidate->recording_ = device_->GetCurrentUploadRecordingId();
    candidate->descriptors_ = device_->GetDescriptorVersionToken();
    candidate->bake_ = bake_;
    candidate->clear_ = clear_;
    candidate->previous_ = previous;
    candidate->reuse_ = previous != nullptr;
    {
        std::lock_guard lock(submissionMutex_);
        if (!resourcePool_) resourcePool_ = std::make_shared<SceneLookupResourcePool>(*device_);
        candidate->resourcePool_ = resourcePool_;
    }
    const bool recycled = candidate->resourcePool_->Acquire(*candidate);
    RHIBufferDesc buffer;
    if (!recycled)
    {
        ce::profile_scope allocate{ce::marker<"MaterialLookupAllocate">()};
        RHITextureDesc texture;
        texture.width = context.width;
        texture.height = context.height;
        texture.format = RHIFormat::RGBA32Float;
        texture.allowRenderTarget = true;
        texture.debugName = L"LX.Scene.LookupInput";
        for (auto& input : candidate->inputs_)
            if (!device_->CreateTexture(texture, input, error)) return false;
        buffer.bytes = count * sizeof(IblBakeSample);
        buffer.allowUnorderedAccess = true;
        buffer.debugName = L"LX.Scene.LookupSamples";
        if (!device_->CreateBuffer(buffer, candidate->samples_, error)) return false;
        buffer.bytes = sizeof(SceneLookupStats);
        buffer.debugName = L"LX.Scene.LookupStatistics";
        if (!device_->CreateBuffer(buffer, candidate->statistics_, error)) return false;
    }
    if (!previous)
    {
        buffer.bytes = sizeof(IblBakeSample);
        buffer.allowUnorderedAccess = false;
        buffer.debugName = L"LX.Scene.EmptyPreviousLookup";
        if (!device_->CreateBuffer(buffer, candidate->emptyPrevious_, error))
            return false;
    }
    const RHIBindingDesc outputs[]{
        RHIBindingDesc::UavBuffer(candidate->samples_, static_cast<std::uint32_t>(count), sizeof(IblBakeSample)),
        RHIBindingDesc::UavBuffer(candidate->statistics_, 4, sizeof(std::uint32_t))};
    candidate->outputs_ = device_->CreateBindings(outputs);
    if (!candidate->outputs_.IsValid())
        return Fail(error, "Scene lookup output binding failed.");
    for (std::uint32_t first = 0; first < count; first += kDispatchPixels)
    {
        const LookupConstants constants{
            context.width,
            context.height,
            static_cast<std::uint32_t>(count),
            candidate->reuse_,
            environment.IsValid(),
            first,
            static_cast<std::uint32_t>((std::min)(count - first, std::uint64_t{kDispatchPixels})),
            irradiance.IsValid() && prefiltered.IsValid(), hasImportance, source.IsValid()};
        const auto upload = device_->UploadConstants(&constants, sizeof(constants));
        if (!upload.IsValid())
            return Fail(error, "Scene lookup dispatch constants allocation failed.");
        candidate->constants_.push_back(upload);
    }
    if (candidate->recording_ != device_->GetCurrentUploadRecordingId() ||
        candidate->descriptors_ != device_->GetDescriptorVersionToken())
        return Fail(error, "Scene lookup preparation changed upload/descriptor ownership.");
    candidate->self_ = candidate;
    result = std::move(candidate);
    error.clear();
    return true;
}

void SceneLookupFrame::CheckCurrent(const EnhancedRenderGraph& graph) const
{
    if (graph_ != &graph || graphEpoch_ != graph.ResourceEpoch() ||
        recording_ != device_->GetCurrentUploadRecordingId() || descriptors_ != device_->GetDescriptorVersionToken())
        throw std::runtime_error("Scene lookup frame has stale graph/upload/descriptor ownership.");
}

const std::array<RGHandle, 11>& SceneLookupFrame::DeclareInputs(EnhancedRenderGraph& graph) const
{
    if (graph_)
        throw std::runtime_error("Scene lookup inputs require one declaration.");
    graph_ = &graph;
    graphEpoch_ = graph.ResourceEpoch();
    CheckCurrent(graph);
    for (unsigned i = 0; i < inputs_.size(); ++i)
        graphInputs_[i] = graph.ImportTexture(inputs_[i], inputStates_[i], "LX.Scene.LookupInput", &inputStates_[i]);
    graphSamples_ = graph.ImportBuffer(samples_, sampleState_, "LX.Scene.LookupSamples", &sampleState_);
    graphStatistics_ = graph.ImportBuffer(statistics_, statisticState_, "LX.Scene.LookupStatistics", &statisticState_);
    return graphInputs_;
}

void SceneLookupFrame::DeclareBake(EnhancedRenderGraph& graph, RGHandle owners) const
{
    CheckCurrent(graph);
    const auto owner = self_.lock();
    const auto previous = previous_.lock();
    if (reuse_ && !previous)
        throw std::runtime_error("Scene lookup lost its previous cache owner before declaration.");
    std::vector<EnhancedRenderGraph::RGPassUsage> uses{{owners, RHIResourceState::ShaderResource},
                                                       {graphSamples_, RHIResourceState::UnorderedAccess},
                                                       {graphStatistics_, RHIResourceState::UnorderedAccess}};
    for (const auto input : graphInputs_)
        uses.push_back({input, RHIResourceState::ShaderResource});
    if (previous)
    {
        for (unsigned i = 0; i < inputs_.size(); ++i)
        {
            auto imported = graph.FindImportedTexture(previous->inputs_[i]);
            if (!imported.IsValid())
            {
                imported = graph.ImportTexture(previous->inputs_[i], previous->inputStates_[i],
                                               "LX.Scene.PreviousLookupInput", &previous->inputStates_[i]);
            }
            uses.push_back({imported, RHIResourceState::ShaderResource});
        }
        auto imported = graph.FindImportedBuffer(previous->samples_);
        if (!imported.IsValid())
        {
            imported = graph.ImportBuffer(previous->samples_, previous->sampleState_, "LX.Scene.PreviousLookup",
                                          &previous->sampleState_);
        }
        uses.push_back({imported, RHIResourceState::ShaderResource});
    }
    else
    {
        auto imported = graph.FindImportedBuffer(emptyPrevious_);
        if (!imported.IsValid())
        {
            imported = graph.ImportBuffer(emptyPrevious_, RHIResourceState::Common, "LX.Scene.EmptyPreviousLookup");
        }
        uses.push_back({imported, RHIResourceState::ShaderResource});
    }
    if (environment_.IsValid())
    {
        auto imported = graph.FindImportedTexture(environment_);
        if (!imported.IsValid())
            imported = graph.ImportTexture(environment_, RHIResourceState::PixelShaderResource, "LX.Scene.HDR");
        uses.push_back({imported, RHIResourceState::ShaderResource});
    }
    for (const auto texture : {irradiance_, prefiltered_,importance_[0],importance_[1],importance_[2],source_})
    {
        if (!texture.IsValid()) continue;
        auto imported = graph.FindImportedTexture(texture);
        if (!imported.IsValid())
            imported = graph.ImportTexture(texture, RHIResourceState::PixelShaderResource, "LX.Scene.IBL");
        uses.push_back({imported, RHIResourceState::ShaderResource});
    }
    graph.AddPass("LX.Scene.LookupBake", uses, [owner, previous, owners](const auto& execution) {
        owner->CheckCurrent(*execution.graph);
        std::array<RHIBindingDesc, 24> descriptions;
        for (unsigned i = 0; i < 11; ++i)
        {
            descriptions[i] = RHIBindingDesc::Srv2D(owner->inputs_[i], RHIFormat::RGBA32Float);
            descriptions[i + 11] =
                RHIBindingDesc::Srv2D(previous ? previous->inputs_[i] : RHITextureHandle{}, RHIFormat::RGBA32Float)
                    .OrNull();
        }
        descriptions[22] = RHIBindingDesc::Srv2D(execution.ResolveHandle(owners), RHIFormat::R32Uint);
        descriptions[23] = RHIBindingDesc::SrvCube(owner->environment_,
                                                   owner->environment_.IsValid()
                                                       ? owner->device_->DescribeTexture(owner->environment_).format
                                                       : RHIFormat::RGBA16Float,
                                                   1)
                               .OrNull();
        const auto inputs = owner->device_->CreateBindings(descriptions);
        const RHIBindingDesc maps[]{
            RHIBindingDesc::SrvCube(owner->irradiance_, owner->irradiance_.IsValid()
                ? owner->device_->DescribeTexture(owner->irradiance_).format : RHIFormat::RGBA16Float, 1).OrNull(),
            RHIBindingDesc::SrvCube(owner->prefiltered_, owner->prefiltered_.IsValid()
                ? owner->device_->DescribeTexture(owner->prefiltered_).format : RHIFormat::RGBA16Float, 6).OrNull(),
            RHIBindingDesc::Srv2D(owner->importance_[0],RHIFormat::RGBA32Float).OrNull(),
            RHIBindingDesc::Srv2D(owner->importance_[1],RHIFormat::RGBA32Float).OrNull(),
            RHIBindingDesc::Srv2D(owner->importance_[2],RHIFormat::RGBA32Float).OrNull(),
            RHIBindingDesc::Srv2D(owner->source_,RHIFormat::RGBA32Float).OrNull()};
        const auto iblMaps = owner->device_->CreateBindings(maps);
        owner->CheckCurrent(*execution.graph);
        if (!inputs.IsValid() || !iblMaps.IsValid())
            throw std::runtime_error("Scene lookup input binding failed.");
        auto& encoder = *execution.encoder;
        encoder.SetPipeline(RHIBindPoint::Compute, owner->clear_);
        encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, owner->constants_.front());
        encoder.SetBindings(RHIBindPoint::Compute, 1, inputs);
        encoder.SetBindings(RHIBindPoint::Compute, 2, owner->outputs_);
        encoder.SetRootBuffer(RHIBindPoint::Compute, 3,
                              RHIBufferSlice::Whole(previous ? previous->samples_ : owner->emptyPrevious_));
        encoder.SetBindings(RHIBindPoint::Compute, 4, iblMaps);
        encoder.Dispatch(1, 1, 1);
        const RHIBufferHandle statistics[]{owner->statistics_};
        encoder.UavBarrierBuffers(statistics);
        encoder.SetPipeline(RHIBindPoint::Compute, owner->bake_);
        encoder.SetBindings(RHIBindPoint::Compute, 1, inputs);
        encoder.SetBindings(RHIBindPoint::Compute, 2, owner->outputs_);
        encoder.SetRootBuffer(RHIBindPoint::Compute, 3,
                              RHIBufferSlice::Whole(previous ? previous->samples_ : owner->emptyPrevious_));
        const auto count = owner->width_ * owner->height_;
        for (unsigned dispatch = 0; dispatch < owner->constants_.size(); ++dispatch)
        {
            encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, owner->constants_[dispatch]);
            const auto pixels = (std::min)(count - dispatch * kDispatchPixels, kDispatchPixels);
            encoder.Dispatch((pixels + 31) / 32, 1, 1);
        }
        owner->stages_.fetch_or(1);
    });
}

void SceneLookupFrame::DeclareReady(EnhancedRenderGraph& graph) const
{
    CheckCurrent(graph);
    const auto owner = self_.lock();
    std::vector<EnhancedRenderGraph::RGPassUsage> uses{
        {graphSamples_, RHIResourceState::ShaderResource}};
    // Deferred and the next frame consume these retained maps as pixel SRVs.
    // Restore their exact state after the compute bake before graph submission.
    for (const auto texture : {environment_, irradiance_, prefiltered_,importance_[0],importance_[1],importance_[2],source_})
        if (texture.IsValid())
            uses.push_back({graph.FindImportedTexture(texture), RHIResourceState::PixelShaderResource});
    graph.AddPass(
        "LX.Scene.LookupReady", uses,
        [owner](const auto& execution) {
            owner->CheckCurrent(*execution.graph);
            owner->stages_.fetch_or(2);
        },
        true);
}

RGHandle SceneLookupFrame::GraphSamples(const EnhancedRenderGraph& graph) const
{
    CheckCurrent(graph);
    return graphSamples_;
}

RGHandle SceneLookupFrame::GraphStatistics(const EnhancedRenderGraph& graph) const
{
    CheckCurrent(graph);
    return graphStatistics_;
}

bool SceneLookupCache::PublishSubmitted(const SceneLookupFrame& frame, std::uint64_t frameId,
                                        RHICompletionPoint completion, std::string& error)
{
    if (frame.device_ != device_ || frame.frameId_ != frameId || !completion.IsValid() || frame.stages_.load() != 3)
        return Fail(error, "Scene lookup publication needs the exact fully recorded frame and successful submission.");
    {
        std::lock_guard lock(submissionMutex_);
        const auto submitted = submitted_.find(frame.recording_);
        if (submitted == submitted_.end() || completion.value < submitted->second.value)
            return Fail(error, "Scene lookup recording has not been submitted on its owning GPU timeline.");
    }
    const auto candidate = frame.self_.lock();
    if (!candidate)
        return Fail(error, "Scene lookup publication lost its immutable owner.");
    if (std::ranges::any_of(
            published_, [&](const auto& item) { return item->viewId_ == frame.viewId_ && item->frameId_ >= frameId; }))
    {
        ce::profile_instant(ce::marker<"MaterialLookupPublicationOutOfOrder">());
        return Fail(error, "Scene lookup refuses duplicate or older frame publication.");
    }
    frame.completion_ = completion;
    // The callback retains the previous frame through submission completion.
    // The published frame has only a weak history link, preventing a chain.
    std::erase_if(published_, [&](const auto& item) { return item->viewId_ == frame.viewId_; });
    if (published_.size() == 2)
        published_.erase(published_.begin());
    published_.push_back(std::move(candidate));
    {
        std::lock_guard lock(submissionMutex_);
        submitted_.erase(frame.recording_);
    }
    error.clear();
    return true;
}

void SceneLookupCache::ShutdownAfterIdle()
{
    if (device_)
        device_->UnregisterUploadTransactionListener(this);
    published_.clear();
    {
        std::lock_guard lock(submissionMutex_);
        resourcePool_.reset();
    }
    submitted_.clear();
    bake_ = clear_ = {};
    device_ = nullptr;
}

void SceneLookupCache::OnUploadSubmitted(std::uint64_t recordingId, RHICompletionPoint completion)
{
    std::lock_guard lock(submissionMutex_);
    submitted_[recordingId] = completion;
    // Legacy-only frames also notify this listener. Keep recent evidence bounded.
    while (submitted_.size() > 128)
        submitted_.erase(submitted_.begin());
}

void SceneLookupCache::OnUploadCompleted(std::uint64_t completed)
{
    std::shared_ptr<SceneLookupResourcePool> pool;
    {
        std::lock_guard lock(submissionMutex_);
        pool = resourcePool_;
    }
    if (pool) pool->Completed(completed);
}

void SceneLookupCache::OnUploadAborted(std::uint64_t recordingId)
{
    std::lock_guard lock(submissionMutex_);
    submitted_.erase(recordingId);
}
} // namespace material_graph
