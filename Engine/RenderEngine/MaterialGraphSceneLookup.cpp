#include "../EngineDiagnostics/ProfileScope.h"
#include "MaterialGraphSceneLookup.h"
#include "MaterialGraphThinFilmSensitivity.h"

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
        constexpr std::uint64_t kResourceAlignment = 65536;
        constexpr std::uint64_t kConstantAlignment = 256;
        constexpr std::uint64_t AlignBytes(std::uint64_t bytes, std::uint64_t alignment)
        {
            return (bytes + alignment - 1) / alignment * alignment;
        }
        struct LookupConstants
        {
            std::uint32_t width, height, count, reuse;
            std::uint32_t environment, first, dispatchCount, precomputed;
            std::uint32_t importance, source, standalone, approximate;
            std::uint32_t runtimeEvaluation, reserved0, reserved1, reserved2;
        };
        struct RuntimeLookupConstants
        {
            std::uint32_t enabled, precomputed, source, environment;
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
            std::uint64_t completion{}, bytes{};
            bool runtimeEvaluation{}, capture{};
            std::array<RHITextureHandle, 11> inputs{};
            RHIBufferHandle samples, statistics;
            std::array<RHIResourceState, 11> inputStates{};
            RHIResourceState sampleState{RHIResourceState::Common};
            RHIResourceState statisticState{RHIResourceState::Common};
        };

        explicit SceneLookupResourcePool(IRenderDeviceServices& owner) : device(owner)
        {
        }
        ~SceneLookupResourcePool()
        {
            for (const auto& bundle : idle)
            {
                Release(bundle);
            }
        }

        void Release(const Bundle& bundle)
        {
            for (const auto input : bundle.inputs)
            {
                if (input.IsValid())
                {
                    device.ReleaseTexture(input);
                }
            }
            if (bundle.samples.IsValid())
            {
                device.ReleaseBuffer(bundle.samples);
            }
            if (bundle.statistics.IsValid())
            {
                device.ReleaseBuffer(bundle.statistics);
            }
            resourceBytes -= bundle.bytes;
        }

        bool Acquire(SceneLookupFrame& frame, std::uint64_t budget, std::uint64_t requiredResources,
                     std::uint64_t requiredExclusive, std::uint64_t requiredUploads, bool& recycled)
        {
            ce::profile_scope profile{ce::marker<"MaterialLookupPoolAcquire">()};
            std::lock_guard lock(mutex);
            recycled = false;
            const auto isReusable = [&](const Bundle& bundle) {
                return bundle.completion <= completed.load(std::memory_order_acquire) &&
                       bundle.width == frame.width_ && bundle.height == frame.height_ &&
                       bundle.runtimeEvaluation == frame.runtimeEvaluation_ && bundle.capture == frame.capture_;
            };
            auto selected = std::find_if(idle.begin(), idle.end(), isReusable);
            const bool reuseBundle = selected != idle.end();
            const auto selectedSamples = reuseBundle ? selected->samples : RHIBufferHandle{};
            const auto required = requiredUploads + requiredExclusive + (reuseBundle ? 0 : requiredResources);
            const auto fits = [&] {
                return required <= budget && resourceBytes <= budget - required &&
                       uploadBytes <= budget - required - resourceBytes;
            };
            // 완료된 유휴 자원만 회수한다. 이전 화면/다른 뷰/제출 대기 자원은
            // 같은 장부에 남으므로 replacement의 순간 사용량도 한도를 넘지 않는다.
            for (auto it = idle.begin(); it != idle.end();)
            {
                if (it->completion > completed.load(std::memory_order_acquire) ||
                    (reuseBundle && it->samples == selectedSamples))
                {
                    ++it;
                    continue;
                }
                if (!fits() || it->width != frame.width_ || it->height != frame.height_ ||
                    it->runtimeEvaluation != frame.runtimeEvaluation_ || it->capture != frame.capture_)
                {
                    Release(*it);
                    it = idle.erase(it);
                    continue;
                }
                ++it;
            }
            if (!fits() || (!uploads.contains(frame.recording_) && uploads.size() >= 128))
            {
                ce::profile_instant(ce::marker<"MaterialLookupBudgetDeferred">());
                return false;
            }
            selected = std::find_if(idle.begin(), idle.end(), isReusable);
            if (selected != idle.end())
            {
                frame.inputs_ = selected->inputs;
                frame.samples_ = selected->samples;
                frame.statistics_ = selected->statistics;
                frame.inputStates_ = selected->inputStates;
                frame.sampleState_ = selected->sampleState;
                frame.statisticState_ = selected->statisticState;
                idle.erase(selected);
                recycled = true;
            }
            else
            {
                resourceBytes += requiredResources;
            }
            frame.resourceBytes_ = requiredResources;
            frame.exclusiveBytes_ = requiredExclusive;
            resourceBytes += requiredExclusive;
            uploads[frame.recording_].bytes += requiredUploads;
            uploadBytes += requiredUploads;
            return true;
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
            idle.push_back({frame.width_, frame.height_, frame.completion_.value, frame.resourceBytes_,
                            frame.runtimeEvaluation_, frame.capture_, frame.inputs_, frame.samples_, frame.statistics_,
                            frame.inputStates_, frame.sampleState_, frame.statisticState_});
            return true;
        }

        void Released(std::uint64_t bytes)
        {
            std::lock_guard lock(mutex);
            resourceBytes -= bytes;
        }

        void Submitted(std::uint64_t recording, RHICompletionPoint completion)
        {
            std::lock_guard lock(mutex);
            if (const auto found = uploads.find(recording); found != uploads.end())
            {
                if (completion.value <= completed.load(std::memory_order_acquire))
                {
                    uploadBytes -= found->second.bytes;
                    uploads.erase(found);
                }
                else
                {
                    found->second.completion = completion.value;
                }
            }
        }

        void Aborted(std::uint64_t recording)
        {
            std::lock_guard lock(mutex);
            if (const auto found = uploads.find(recording); found != uploads.end())
            {
                uploadBytes -= found->second.bytes;
                uploads.erase(found);
            }
        }

        void Completed(std::uint64_t value)
        {
            std::lock_guard lock(mutex);
            auto prior = completed.load(std::memory_order_relaxed);
            while (prior < value &&
                   !completed.compare_exchange_weak(prior, value, std::memory_order_release, std::memory_order_relaxed))
            {
            }
            for (auto it = uploads.begin(); it != uploads.end();)
            {
                if (it->second.completion && it->second.completion <= value)
                {
                    uploadBytes -= it->second.bytes;
                    it = uploads.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }

        struct UploadReservation
        {
            std::uint64_t bytes{}, completion{};
        };
        IRenderDeviceServices& device;
        std::mutex mutex;
        std::atomic<std::uint64_t> completed{};
        std::vector<Bundle> idle;
        std::map<std::uint64_t, UploadReservation> uploads;
        std::uint64_t resourceBytes{}, uploadBytes{};
    };

    SceneLookupFrame::~SceneLookupFrame()
    {
        if (!device_)
        {
            return;
        }
        if (!resourcePool_ || !resourcePool_->Recycle(*this))
        {
            for (const auto input : inputs_)
            {
                if (input.IsValid())
                {
                    device_->ReleaseTexture(input);
                }
            }
            if (samples_.IsValid())
            {
                device_->ReleaseBuffer(samples_);
            }
            if (statistics_.IsValid())
            {
                device_->ReleaseBuffer(statistics_);
            }
            if (resourcePool_)
            {
                resourcePool_->Released(resourceBytes_);
            }
        }
        if (emptyPrevious_.IsValid())
        {
            device_->ReleaseBuffer(emptyPrevious_);
        }
        if (resourcePool_)
        {
            resourcePool_->Released(exclusiveBytes_);
        }
    }

    bool SceneLookupCache::Initialize(const EnhancedFrameContext& context, std::string& error)
    {
        if (device_)
        {
            return device_ == context.resources || Fail(error, "Scene lookup cache belongs to another device.");
        }
        const auto backend = RHIShaderCompiler::GetOutput();
        LX::Runtime::CompiledCompute bake, clear;
        RHIShaderCompileOptions options;
        options.strictMath = true;
        const auto file = RHIShaderSource::Resolve("MaterialGraphSceneLookup.slang").string();
        if (!LX::Runtime::CompileCompute(file, "LXSceneLookupBake", {}, options, bake, error) ||
            !LX::Runtime::CompileCompute(file, "LXSceneLookupClear", {}, options, clear, error))
        {
            return false;
        }
        const auto other = backend == RHIShaderBinary::Dxil ? RHIShaderBinary::SpirV : RHIShaderBinary::Dxil;
        RHIShaderCompiler::VerifiedShader verification;
        if (!RHIShaderCompiler::VerifyFile(file, "LXSceneLookupBake", "cs_6_0", other, {}, verification, error,
                                           options) ||
            !RHIShaderCompiler::VerifyFile(file, "LXSceneLookupClear", "cs_6_0", other, {}, verification, error,
                                           options))
        {
            return false;
        }
        const RHIPipelineLayoutParam parameters[]{
            RHILayout::Cbv(0),  RHILayout::SrvTable(24, 0), RHILayout::UavBufferTable(2, 0),
            RHILayout::Srv(24), RHILayout::SrvTable(6, 25), RHILayout::Cbv(1)};
        const RHIStaticSamplerDesc samplers[]{{RHISampler::Point(RHIAddressMode::Clamp), 0},
                                              {RHISampler::Linear(RHIAddressMode::Clamp), 1}};
        const auto layout = context.rootSignatures->GetOrCreate({parameters, samplers}, error);
        if (!layout.IsValid())
        {
            return false;
        }
        RHIComputePipelineDesc desc;
        desc.layout = layout;
        desc.csBytecode = bake.stage.bytecode.Data();
        desc.csSize = bake.stage.bytecode.Size();
        LX::Runtime::ComputePipeline bakePipeline, clearPipeline;
        if (!bakePipeline.Create(*context.psoManager, desc, std::move(bake.description), error))
        {
            return false;
        }
        desc.csBytecode = clear.stage.bytecode.Data();
        desc.csSize = clear.stage.bytecode.Size();
        if (!clearPipeline.Create(*context.psoManager, desc, std::move(clear.description), error))
        {
            return false;
        }
        device_ = context.resources;
        bake_ = std::move(bakePipeline);
        clear_ = std::move(clearPipeline);
        device_->RegisterUploadTransactionListener(this);
        return true;
    }

    bool SceneLookupCache::Prepare(const EnhancedFrameContext& context, std::uint64_t viewId,
                                   RHITextureHandle environment, RHITextureHandle irradiance,
                                   RHITextureHandle prefiltered, std::uint64_t environmentGeneration,
                                   std::uint64_t memoryBudget, std::shared_ptr<const SceneLookupFrame>& result,
                                   std::string& error, std::array<RHITextureHandle, 3> importance,
                                   RHITextureHandle source, bool standalone, bool approximate, bool capture,
                                   bool runtimeEvaluation)
    {
        ce::profile_scope profile{ce::marker<"MaterialLookupPrepare">()};
        ResetPreparationStatus();
        const auto count = std::uint64_t(context.width) * context.height;
        if (!context.resources || !context.rootSignatures || !context.psoManager || !context.frameId || !viewId ||
            !count || count > UINT32_MAX ||
            !context.resources->GetCurrentUploadRecordingId() || (environment.IsValid() && !environmentGeneration))
        {
            return Fail(error, "Scene lookup needs current services and an identified view/environment.");
        }
        if (runtimeEvaluation && !approximate)
        {
            return Fail(error, "Scene runtime evaluation requires the explicit split-sum quality policy.");
        }
        std::shared_ptr<const SceneLookupFrame> previous;
        for (const auto& cached : published_)
        {
            if (!runtimeEvaluation && !standalone && cached->viewId_ == viewId && cached->sceneEpoch_ == context.sceneEpoch &&
                cached->width_ == context.width && cached->height_ == context.height &&
                cached->environment_ == environment && cached->irradiance_ == irradiance &&
                cached->prefiltered_ == prefiltered && cached->environmentGeneration_ == environmentGeneration &&
                cached->importance_ == importance && cached->source_ == source)
            {
                previous = cached;
            }
        }
        if (!Initialize(context, error))
        {
            return false;
        }
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
        const bool hasImportance = std::ranges::all_of(importance, [](auto handle) { return handle.IsValid(); });
        if (std::ranges::any_of(importance, [](auto handle) { return handle.IsValid(); }) && !hasImportance)
        {
            return Fail(error, "Scene environment importance requires all three maps.");
        }
        if (hasImportance)
        {
            const auto rows = device_->DescribeTexture(importance[0]);
            const auto marginal = device_->DescribeTexture(importance[1]);
            const auto samples = device_->DescribeTexture(importance[2]);
            if (!environment.IsValid() || !rows.width || rows.height != 6 * rows.width || marginal.width != 1 ||
                marginal.height != rows.height || (samples.width != 1024 && samples.width != 5120) ||
                samples.height != 2 || rows.format != RHIFormat::RGBA32Float ||
                marginal.format != RHIFormat::RGBA32Float || samples.format != RHIFormat::RGBA32Float ||
                rows.depthOrArraySize != 1 || marginal.depthOrArraySize != 1 || samples.depthOrArraySize != 1)
            {
                return Fail(error, "Scene environment importance layout differs.");
            }
        }
        if (source.IsValid())
        {
            const auto description = device_->DescribeTexture(source);
            if (!environment.IsValid() || !description.width || !description.height ||
                description.format != RHIFormat::RGBA32Float || description.depthOrArraySize != 1 ||
                description.mipLevels != 1)
            {
                return Fail(error, "Scene decoded environment source layout differs.");
            }
        }
        candidate->source_ = source;
        candidate->importance_ = importance;
        candidate->environmentGeneration_ = environmentGeneration;
        candidate->recording_ = device_->GetCurrentUploadRecordingId();
        candidate->descriptors_ = device_->GetDescriptorVersionToken();
        candidate->bake_ = bake_.GetGeneration();
        candidate->clear_ = clear_.GetGeneration();
        candidate->previous_ = previous;
        candidate->reuse_ = previous != nullptr;
        candidate->standalone_ = standalone;
        candidate->runtimeEvaluation_ = runtimeEvaluation;
        candidate->capture_ = !runtimeEvaluation || capture;
        {
            std::lock_guard lock(submissionMutex_);
            if (!resourcePool_)
            {
                resourcePool_ = std::make_shared<SceneLookupResourcePool>(*device_);
            }
            candidate->resourcePool_ = resourcePool_;
        }
        const auto sampleCount = runtimeEvaluation ? std::uint64_t{1} : count;
        const auto requiredResources =
            (candidate->capture_ ? 11 * AlignBytes(count * 16, kResourceAlignment) : 0) +
            AlignBytes(sampleCount * sizeof(IblBakeSample), kResourceAlignment) +
            AlignBytes(sizeof(SceneLookupStats), kResourceAlignment);
        const auto dispatchCount = runtimeEvaluation ? std::uint64_t{1} : (count + kDispatchPixels - 1) / kDispatchPixels;
        const auto requiredExclusive = (!runtimeEvaluation && !previous)
                                           ? AlignBytes(sizeof(IblBakeSample), kResourceAlignment)
                                           : 0;
        const auto requiredUploads = AlignBytes(sizeof(RuntimeLookupConstants), kConstantAlignment) +
                                     dispatchCount * AlignBytes(sizeof(LookupConstants), kConstantAlignment) +
                                     (runtimeEvaluation ? 0 : AlignBytes(sizeof(kFilmSensitivityConstants), kConstantAlignment));
        bool recycled = false;
        if (!candidate->resourcePool_->Acquire(*candidate, memoryBudget, requiredResources, requiredExclusive,
                                              requiredUploads, recycled))
        {
            preparationDeferred_ = true;
            return Fail(error, "Scene lookup admission is waiting for GPU memory retirement.");
        }
        if (!runtimeEvaluation)
        {
            candidate->filmSensitivityConstants_ =
                device_->UploadConstants(kFilmSensitivityConstants.data(), sizeof(kFilmSensitivityConstants));
            if (!candidate->filmSensitivityConstants_.IsValid())
            {
                return Fail(error, "Scene lookup sensitivity table upload failed.");
            }
        }
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
            {
                if (candidate->capture_ && !device_->CreateTexture(texture, input, error))
                {
                    return false;
                }
            }
            buffer.bytes = sampleCount * sizeof(IblBakeSample);
            buffer.allowUnorderedAccess = true;
            buffer.debugName = L"LX.Scene.LookupSamples";
            if (!device_->CreateBuffer(buffer, candidate->samples_, error))
            {
                return false;
            }
            buffer.bytes = sizeof(SceneLookupStats);
            buffer.debugName = L"LX.Scene.LookupStatistics";
            if (!device_->CreateBuffer(buffer, candidate->statistics_, error))
            {
                return false;
            }
        }
        if (!runtimeEvaluation && !previous)
        {
            buffer.bytes = sizeof(IblBakeSample);
            buffer.allowUnorderedAccess = false;
            buffer.debugName = L"LX.Scene.EmptyPreviousLookup";
            if (!device_->CreateBuffer(buffer, candidate->emptyPrevious_, error))
            {
                return false;
            }
        }
        const RuntimeLookupConstants runtime{runtimeEvaluation, irradiance.IsValid() && prefiltered.IsValid(),
                                               source.IsValid(), environment.IsValid()};
        candidate->runtimeConstants_ = device_->UploadConstants(&runtime, sizeof(runtime));
        const RHIBindingDesc runtimeInputs[]{
            RHIBindingDesc::SrvCube(irradiance, irradiance.IsValid() ? device_->DescribeTexture(irradiance).format
                                                                  : RHIFormat::RGBA16Float, 1).OrNull(),
            RHIBindingDesc::SrvCube(prefiltered, prefiltered.IsValid() ? device_->DescribeTexture(prefiltered).format
                                                                    : RHIFormat::RGBA16Float, 6).OrNull(),
            RHIBindingDesc::Srv2D(source, RHIFormat::RGBA32Float).OrNull()};
        candidate->runtimeInputs_ = device_->CreateBindings(runtimeInputs);
        if (!candidate->runtimeConstants_.IsValid() || !candidate->runtimeInputs_.IsValid())
        {
            return Fail(error, "Scene runtime lookup constants or bindings are awaiting upload capacity.");
        }
        const RHIBindingDesc outputs[]{
            RHIBindingDesc::UavBuffer(candidate->samples_, static_cast<std::uint32_t>(sampleCount), sizeof(IblBakeSample)),
            RHIBindingDesc::UavBuffer(candidate->statistics_, sizeof(SceneLookupStats) / sizeof(std::uint32_t),
                                      sizeof(std::uint32_t))};
        candidate->outputs_ = device_->CreateBindings(outputs);
        if (!candidate->outputs_.IsValid())
        {
            return Fail(error, "Scene lookup output binding failed.");
        }
        candidate->constants_.reserve(static_cast<std::size_t>(dispatchCount));
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
                irradiance.IsValid() && prefiltered.IsValid(),
                hasImportance,
                source.IsValid(),
                standalone,
                approximate,
                runtimeEvaluation,
                0, 0, 0};
            const auto upload = device_->UploadConstants(&constants, sizeof(constants));
            if (!upload.IsValid())
            {
                return Fail(error, "Scene lookup dispatch constants allocation failed.");
            }
            candidate->constants_.push_back(upload);
            if (runtimeEvaluation)
            {
                break;
            }
        }
        if (candidate->recording_ != device_->GetCurrentUploadRecordingId() ||
            candidate->descriptors_ != device_->GetDescriptorVersionToken())
        {
            return Fail(error, "Scene lookup preparation changed upload/descriptor ownership.");
        }
        candidate->self_ = candidate;
        result = std::move(candidate);
        error.clear();
        return true;
    }

    void SceneLookupFrame::CheckCurrent(const EnhancedRenderGraph& graph) const
    {
        if (graph_ != &graph || graphEpoch_ != graph.ResourceEpoch() ||
            recording_ != device_->GetCurrentUploadRecordingId() ||
            descriptors_ != device_->GetDescriptorVersionToken())
        {
            throw std::runtime_error("Scene lookup frame has stale graph/upload/descriptor ownership.");
        }
    }

    void SceneLookupFrame::DeclareShadingInputs(EnhancedRenderGraph& graph,
                                               std::vector<EnhancedRenderGraph::RGPassUsage>& uses) const
    {
        CheckCurrent(graph);
        if (!runtimeEvaluation_)
        {
            return;
        }
        const auto read = graph.GetSchedulingMode() == RGSchedulingMode::DeclarationOrder
                              ? RGAccessMode::LegacyState
                              : RGAccessMode::Read;
        for (const auto texture : {environment_, irradiance_, prefiltered_, source_})
        {
            if (!texture.IsValid())
            {
                continue;
            }
            auto handle = graph.FindImportedTexture(texture);
            if (!handle.IsValid())
            {
                handle = graph.ImportTexture(texture, RHIResourceState::PixelShaderResource, "LX.Scene.RuntimeIBL");
            }
            uses.push_back({handle, RHIResourceState::PixelShaderResource, read});
        }
    }

    void SceneLookupFrame::BindRuntime(RHIEncoder& encoder, unsigned firstRootSlot) const
    {
        encoder.SetBindings(RHIBindPoint::Graphics, firstRootSlot, runtimeInputs_);
        encoder.SetConstantBuffer(RHIBindPoint::Graphics, firstRootSlot + 1, runtimeConstants_);
    }

    const std::array<RGHandle, 11>& SceneLookupFrame::DeclareInputs(EnhancedRenderGraph& graph) const
    {
        if (graph_)
        {
            throw std::runtime_error("Scene lookup inputs require one declaration.");
        }
        graph_ = &graph;
        graphEpoch_ = graph.ResourceEpoch();
        CheckCurrent(graph);
        for (unsigned i = 0; i < inputs_.size(); ++i)
        {
            if (inputs_[i].IsValid())
            {
                graphInputs_[i] =
                    graph.ImportTexture(inputs_[i], inputStates_[i], "LX.Scene.LookupInput", &inputStates_[i]);
            }
        }
        graphSamples_ = graph.ImportBuffer(samples_, sampleState_, "LX.Scene.LookupSamples", &sampleState_);
        graphStatistics_ =
            graph.ImportBuffer(statistics_, statisticState_, "LX.Scene.LookupStatistics", &statisticState_);
        return graphInputs_;
    }

    const std::array<RGHandle, 11>& SceneLookupFrame::GraphInputs(const EnhancedRenderGraph& graph) const
    {
        CheckCurrent(graph);
        return graphInputs_;
    }

    std::array<RGHandle, 11> SceneLookupFrame::DeclareCaptureOutputs(EnhancedRenderGraph& graph, unsigned first,
                                                                     unsigned count) const
    {
        CheckCurrent(graph);
        if (!capture_ || !count || first >= graphInputs_.size() || count > graphInputs_.size() - first)
        {
            throw std::runtime_error("Scene lookup capture range is invalid.");
        }
        if (graph.GetSchedulingMode() == RGSchedulingMode::ExplicitVersioned)
        {
            for (unsigned i = first; i < first + count; ++i)
            {
                graphInputs_[i] = graph.Write(graphInputs_[i]);
            }
        }
        return graphInputs_;
    }

    void SceneLookupFrame::DeclareBake(EnhancedRenderGraph& graph, RGHandle owners) const
    {
        CheckCurrent(graph);
        const bool explicitAccess = graph.GetSchedulingMode() != RGSchedulingMode::DeclarationOrder;
        const auto readAccess = explicitAccess ? RGAccessMode::Read : RGAccessMode::LegacyState;
        const auto writeAccess = explicitAccess ? RGAccessMode::Write : RGAccessMode::LegacyState;
        if (graph.GetSchedulingMode() == RGSchedulingMode::ExplicitVersioned)
        {
            if (!runtimeEvaluation_)
            {
                graphSamples_ = graph.Write(graphSamples_);
            }
            graphStatistics_ = graph.Write(graphStatistics_);
        }
        const auto owner = self_.lock();
        if (runtimeEvaluation_)
        {
            // 런타임 sample은 fragment 레지스터에서 계산한다. 이 패스는
            // offline sample 통계로 오해하지 않도록 mode 표지만 기록한다.
            graph.AddPass("LX.Scene.RuntimeLookupMetadata",
                          {{graphStatistics_, RHIResourceState::UnorderedAccess, writeAccess}},
                          [owner](const auto& execution) {
                              owner->CheckCurrent(*execution.graph);
                              auto& encoder = *execution.encoder;
                              encoder.SetPipeline(RHIBindPoint::Compute, owner->clear_->GetHandle());
                              encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, owner->constants_.front());
                              encoder.SetBindings(RHIBindPoint::Compute, 2, owner->outputs_);
                              owner->stages_.fetch_or(1);
                              encoder.Dispatch(1, 1, 1);
                          }, true);
            return;
        }
        const auto previous = previous_.lock();
        if (reuse_ && !previous)
        {
            throw std::runtime_error("Scene lookup lost its previous cache owner before declaration.");
        }
        std::vector<EnhancedRenderGraph::RGPassUsage> uses{
            {owners, RHIResourceState::ShaderResource, readAccess},
            {graphSamples_, RHIResourceState::UnorderedAccess, writeAccess},
            {graphStatistics_, RHIResourceState::UnorderedAccess, writeAccess}};
        for (const auto input : graphInputs_)
        {
            uses.push_back({input, RHIResourceState::ShaderResource, readAccess});
        }
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
                uses.push_back({imported, RHIResourceState::ShaderResource, readAccess});
            }
            auto imported = graph.FindImportedBuffer(previous->samples_);
            if (!imported.IsValid())
            {
                imported = graph.ImportBuffer(previous->samples_, previous->sampleState_, "LX.Scene.PreviousLookup",
                                              &previous->sampleState_);
            }
            uses.push_back({imported, RHIResourceState::ShaderResource, readAccess});
        }
        else
        {
            auto imported = graph.FindImportedBuffer(emptyPrevious_);
            if (!imported.IsValid())
            {
                imported = graph.ImportBuffer(emptyPrevious_, RHIResourceState::Common, "LX.Scene.EmptyPreviousLookup");
            }
            uses.push_back({imported, RHIResourceState::ShaderResource, readAccess});
        }
        if (environment_.IsValid())
        {
            auto imported = graph.FindImportedTexture(environment_);
            if (!imported.IsValid())
            {
                imported = graph.ImportTexture(environment_, RHIResourceState::PixelShaderResource, "LX.Scene.HDR");
            }
            uses.push_back({imported, RHIResourceState::ShaderResource, readAccess});
        }
        for (const auto texture : {irradiance_, prefiltered_, importance_[0], importance_[1], importance_[2], source_})
        {
            if (!texture.IsValid())
            {
                continue;
            }
            auto imported = graph.FindImportedTexture(texture);
            if (!imported.IsValid())
            {
                imported = graph.ImportTexture(texture, RHIResourceState::PixelShaderResource, "LX.Scene.IBL");
            }
            uses.push_back({imported, RHIResourceState::ShaderResource, readAccess});
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
                RHIBindingDesc::SrvCube(owner->irradiance_,
                                        owner->irradiance_.IsValid()
                                            ? owner->device_->DescribeTexture(owner->irradiance_).format
                                            : RHIFormat::RGBA16Float,
                                        1)
                    .OrNull(),
                RHIBindingDesc::SrvCube(owner->prefiltered_,
                                        owner->prefiltered_.IsValid()
                                            ? owner->device_->DescribeTexture(owner->prefiltered_).format
                                            : RHIFormat::RGBA16Float,
                                        6)
                    .OrNull(),
                RHIBindingDesc::Srv2D(owner->importance_[0], RHIFormat::RGBA32Float).OrNull(),
                RHIBindingDesc::Srv2D(owner->importance_[1], RHIFormat::RGBA32Float).OrNull(),
                RHIBindingDesc::Srv2D(owner->importance_[2], RHIFormat::RGBA32Float).OrNull(),
                RHIBindingDesc::Srv2D(owner->source_, RHIFormat::RGBA32Float).OrNull()};
            const auto iblMaps = owner->device_->CreateBindings(maps);
            owner->CheckCurrent(*execution.graph);
            if (!inputs.IsValid() || !iblMaps.IsValid())
            {
                throw std::runtime_error("Scene lookup input binding failed.");
            }
            auto& encoder = *execution.encoder;
            encoder.SetPipeline(RHIBindPoint::Compute, owner->clear_->GetHandle());
            encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, owner->constants_.front());
            encoder.SetConstantBuffer(RHIBindPoint::Compute, 5, owner->filmSensitivityConstants_);
            encoder.SetBindings(RHIBindPoint::Compute, 1, inputs);
            encoder.SetBindings(RHIBindPoint::Compute, 2, owner->outputs_);
            encoder.SetRootBuffer(RHIBindPoint::Compute, 3,
                                  RHIBufferSlice::Whole(previous ? previous->samples_ : owner->emptyPrevious_));
            encoder.SetBindings(RHIBindPoint::Compute, 4, iblMaps);
            encoder.Dispatch(1, 1, 1);
            const RHIBufferHandle statistics[]{owner->statistics_};
            encoder.UavBarrierBuffers(statistics);
            encoder.SetPipeline(RHIBindPoint::Compute, owner->bake_->GetHandle());
            encoder.SetConstantBuffer(RHIBindPoint::Compute, 5, owner->filmSensitivityConstants_);
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
        const auto readAccess = graph.GetSchedulingMode() == RGSchedulingMode::DeclarationOrder
                                    ? RGAccessMode::LegacyState
                                    : RGAccessMode::Read;
        const auto owner = self_.lock();
        std::vector<EnhancedRenderGraph::RGPassUsage> uses{
            {graphSamples_, RHIResourceState::ShaderResource, readAccess}};
        // Deferred and the next frame consume these retained maps as pixel SRVs.
        // Restore their exact state after the compute bake before graph submission.
        for (const auto texture :
             {environment_, irradiance_, prefiltered_, importance_[0], importance_[1], importance_[2], source_})
        {
            if (texture.IsValid() && !runtimeEvaluation_)
            {
                uses.push_back({graph.FindImportedTexture(texture), RHIResourceState::PixelShaderResource, readAccess});
            }
        }
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
        {
            return Fail(error,
                        "Scene lookup publication needs the exact fully recorded frame and successful submission.");
        }
        {
            std::lock_guard lock(submissionMutex_);
            const auto submitted = submitted_.find(frame.recording_);
            if (submitted == submitted_.end() || completion.value < submitted->second.value)
            {
                return Fail(error, "Scene lookup recording has not been submitted on its owning GPU timeline.");
            }
        }
        const auto candidate = frame.self_.lock();
        if (!candidate)
        {
            return Fail(error, "Scene lookup publication lost its immutable owner.");
        }
        if (std::ranges::any_of(published_, [&](const auto& item) {
                return item->viewId_ == frame.viewId_ && item->frameId_ >= frameId;
            }))
        {
            ce::profile_instant(ce::marker<"MaterialLookupPublicationOutOfOrder">());
            return Fail(error, "Scene lookup refuses duplicate or older frame publication.");
        }
        frame.completion_ = completion;
        if (frame.resourcePool_)
        {
            frame.resourcePool_->Submitted(frame.recording_, completion);
        }
        if (frame.standalone_ || frame.runtimeEvaluation_)
        {
            if (frame.runtimeEvaluation_ && !frame.standalone_)
            {
                std::erase_if(published_, [&](const auto& item) { return item->viewId_ == frame.viewId_; });
            }
            if (!frame.standalone_)
            {
                std::lock_guard lock(submissionMutex_);
                submitted_.erase(frame.recording_);
            }
            error.clear();
            return true;
        }
        // The callback retains the previous frame through submission completion.
        // The published frame has only a weak history link, preventing a chain.
        std::erase_if(published_, [&](const auto& item) { return item->viewId_ == frame.viewId_; });
        if (published_.size() == 2)
        {
            published_.erase(published_.begin());
        }
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
        {
            device_->UnregisterUploadTransactionListener(this);
        }
        published_.clear();
        {
            std::lock_guard lock(submissionMutex_);
            resourcePool_.reset();
        }
        submitted_.clear();
        bake_ = {};
        clear_ = {};
        device_ = nullptr;
    }

    void SceneLookupCache::OnUploadSubmitted(std::uint64_t recordingId, RHICompletionPoint completion)
    {
        std::lock_guard lock(submissionMutex_);
        submitted_[recordingId] = completion;
        // Legacy-only frames also notify this listener. Keep recent evidence bounded.
        while (submitted_.size() > 128)
        {
            submitted_.erase(submitted_.begin());
        }
    }

    void SceneLookupCache::OnUploadCompleted(std::uint64_t completed)
    {
        std::shared_ptr<SceneLookupResourcePool> pool;
        {
            std::lock_guard lock(submissionMutex_);
            pool = resourcePool_;
        }
        if (pool)
        {
            pool->Completed(completed);
        }
    }

    void SceneLookupCache::OnUploadAborted(std::uint64_t recordingId)
    {
        std::lock_guard lock(submissionMutex_);
        submitted_.erase(recordingId);
        if (resourcePool_)
        {
            resourcePool_->Aborted(recordingId);
        }
    }
} // namespace material_graph
