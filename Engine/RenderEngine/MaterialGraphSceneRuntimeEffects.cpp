#include "MaterialGraphSceneRuntimeEffects.h"

#include "RHI/RHIShaderCompiler.h"
#include "RHI/RHIShaderSource.h"

#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <stdexcept>

namespace material_graph
{
    namespace
    {
        constexpr std::uint64_t kRuntimeSampleCount =
            std::uint64_t{SceneRuntimeTileExtent} * SceneRuntimeTileExtent;
        constexpr std::uint64_t kMaximumTiles = EnhancedRenderGraph::kMaxPassRepetitions;
        constexpr std::uint64_t AlignRuntimeBytes(std::uint64_t bytes)
        {
            return (bytes + 65535) / 65536 * 65536;
        }
        struct RuntimeEffectConstants
        {
            math::matrix4x4 inverseViewProjection;
            std::uint32_t sourceX, sourceY, sourceWidth, sourceHeight;
            std::uint32_t width, height, steps, rays;
            std::uint32_t backgroundHasMedium, transmission, reserved0, reserved1;
        };
        static_assert(sizeof(RuntimeEffectConstants) == 112);

        bool FailRuntimeEffect(std::string& error, const char* message)
        {
            error = message;
            return false;
        }
    } // namespace

    struct SceneRuntimeEffectBundle
    {
        explicit SceneRuntimeEffectBundle(IRenderDeviceServices& services) : device(services)
        {
        }
        ~SceneRuntimeEffectBundle()
        {
            Release();
        }
        void Release()
        {
            for (auto& input : inputs)
            {
                if (input.IsValid())
                {
                    device.ReleaseTexture(input);
                    input = {};
                }
            }
            for (auto* texture : {&tileDepth, &color, &depth})
            {
                if (texture->IsValid())
                {
                    device.ReleaseTexture(*texture);
                    *texture = {};
                }
            }
        }
        IRenderDeviceServices& device;
        std::array<RHITextureHandle, 14> inputs{};
        RHITextureHandle tileDepth, color, depth;
        std::array<RHIResourceState, 14> inputStates{};
        RHIResourceState tileDepthState = RHIResourceState::Common;
        RHIResourceState colorState = RHIResourceState::Common;
        RHIResourceState depthState = RHIResourceState::Common;
        std::uint32_t width{}, height{};
        std::uint64_t effectBytes{}, snapshotBytes{}, recording{}, completion{}, prefixCompletion{};
        std::atomic<std::uint64_t> lease{};
        bool transmission{}, busy{}, submitted{}, accepted{}, nativeConfirmed{}, ready{};
    };

    struct SceneRuntimeEffectPool
    {
        explicit SceneRuntimeEffectPool(IRenderDeviceServices& services) : device(services)
        {
        }
        struct UploadReservation
        {
            std::uint64_t bytes{}, completion{}, prefixCompletion{};
            bool accepted{}, nativeConfirmed{};
        };

        std::shared_ptr<SceneRuntimeEffectBundle> Acquire(std::uint32_t width, std::uint32_t height,
                                                          bool transmission, std::uint64_t recording,
                                                          std::uint64_t effectBudget, std::uint64_t snapshotBudget,
                                                          std::uint64_t scratch)
        {
            std::lock_guard lock(mutex);
            const auto requiredEffects = 14 * AlignRuntimeBytes(kRuntimeSampleCount * 16) +
                                         AlignRuntimeBytes(kRuntimeSampleCount * 4);
            const auto pixels = std::uint64_t(width) * height;
            const auto requiredSnapshots = transmission ? AlignRuntimeBytes(pixels * 8) + AlignRuntimeBytes(pixels * 4) : 0;
            const auto matches = [&](const auto& bundle) {
                return !bundle->busy && bundle->ready && bundle->transmission == transmission &&
                       (!transmission || (bundle->width == width && bundle->height == height));
            };
            auto found = std::find_if(bundles.begin(), bundles.end(), matches);
            const auto reusable = found == bundles.end() ? std::shared_ptr<SceneRuntimeEffectBundle>{} : *found;
            const auto extraEffects = scratch + (reusable ? 0 : requiredEffects);
            const auto extraSnapshots = reusable ? 0 : requiredSnapshots;
            const auto fits = [&] {
                return extraEffects <= effectBudget && effectBytes <= effectBudget - extraEffects &&
                       uploadBytes <= effectBudget - extraEffects - effectBytes &&
                       extraSnapshots <= snapshotBudget && snapshotBytes <= snapshotBudget - extraSnapshots;
            };
            for (auto it = bundles.begin(); it != bundles.end();)
            {
                const auto& bundle = *it;
                if (!bundle->busy && bundle != reusable && (!fits() || !matches(bundle)))
                {
                    // GPU 완료가 확인된 lease만 무효화한다. 이전 표시 프레임의
                    // HDR에는 손대지 않고 효과 scratch의 중복 소유만 끝낸다.
                    bundle->lease.fetch_add(1);
                    bundle->Release();
                    effectBytes -= bundle->effectBytes;
                    snapshotBytes -= bundle->snapshotBytes;
                    it = bundles.erase(it);
                }
                else
                {
                    ++it;
                }
            }
            if (!fits() || (!uploads.contains(recording) && uploads.size() >= 128))
            {
                return {};
            }
            auto bundle = reusable;
            if (!bundle)
            {
                bundle = std::make_shared<SceneRuntimeEffectBundle>(device);
                bundle->width = width;
                bundle->height = height;
                bundle->transmission = transmission;
                bundle->effectBytes = requiredEffects;
                bundle->snapshotBytes = requiredSnapshots;
                effectBytes += requiredEffects;
                snapshotBytes += requiredSnapshots;
                bundles.push_back(bundle);
            }
            bundle->busy = true;
            bundle->submitted = false;
            bundle->accepted = false;
            bundle->nativeConfirmed = false;
            bundle->recording = recording;
            bundle->completion = bundle->prefixCompletion = 0;
            bundle->lease.fetch_add(1);
            uploads[recording].bytes += scratch;
            uploadBytes += scratch;
            return bundle;
        }

        void Abandon(const std::shared_ptr<SceneRuntimeEffectBundle>& bundle, std::uint64_t lease)
        {
            std::lock_guard lock(mutex);
            if (bundle->lease.load() != lease || bundle->completion)
            {
                return;
            }
            if (bundle->submitted)
            {
                // 접수 여부가 불명확한 제출은 격리한다. 명시적인 never-enqueued
                // 거절 통지나 전체 그래프의 접수 증거 없이 prefix로 해제하지 않는다.
                bundle->ready = false;
                return;
            }
            bundle->lease.fetch_add(1);
            bundle->Release();
            effectBytes -= bundle->effectBytes;
            snapshotBytes -= bundle->snapshotBytes;
            std::erase(bundles, bundle);
        }

        void Submitted(std::uint64_t recording, RHICompletionPoint completion)
        {
            std::lock_guard lock(mutex);
            for (const auto& bundle : bundles)
            {
                if (bundle->busy && bundle->recording == recording)
                {
                    bundle->submitted = true;
                    bundle->prefixCompletion = completion.value;
                }
            }
            if (const auto found = uploads.find(recording); found != uploads.end())
            {
                found->second.prefixCompletion = completion.value;
            }
        }

        void Publish(const std::shared_ptr<SceneRuntimeEffectBundle>& bundle, std::uint64_t lease,
                     RHICompletionPoint completion)
        {
            std::lock_guard lock(mutex);
            if (!completion.IsValid() || bundle->lease.load() != lease || !bundle->submitted ||
                completion.value < bundle->prefixCompletion)
            {
                throw std::runtime_error("Runtime effects require their exact complete graph submission.");
            }
            bundle->completion = completion.value;
            bundle->accepted = true;
            bundle->busy = !bundle->nativeConfirmed || completion.value > completed;
            if (const auto found = uploads.find(bundle->recording); found != uploads.end())
            {
                found->second.completion = completion.value;
                found->second.accepted = true;
            }
            RetireUploads();
        }

        void Completed(std::uint64_t value)
        {
            std::lock_guard lock(mutex);
            completed = (std::max)(completed, value);
            for (const auto& bundle : bundles)
            {
                if (bundle->nativeConfirmed && bundle->completion && bundle->completion <= completed)
                {
                    bundle->busy = false;
                }
            }
            RetireUploads();
        }

        void Confirm(const std::shared_ptr<SceneRuntimeEffectBundle>& bundle, std::uint64_t lease,
                     RHICompletionPoint completion)
        {
            std::lock_guard lock(mutex);
            if (bundle->lease.load() != lease || !bundle->accepted || bundle->completion != completion.value)
            {
                throw std::runtime_error("Runtime effects confirmation differs from their accepted graph.");
            }
            bundle->nativeConfirmed = true;
            bundle->busy = completion.value > completed;
            if (const auto found = uploads.find(bundle->recording); found != uploads.end())
            {
                found->second.nativeConfirmed = true;
            }
            RetireUploads();
        }

        void Aborted(std::uint64_t recording)
        {
            std::lock_guard lock(mutex);
            for (const auto& bundle : bundles)
            {
                if (bundle->busy && bundle->recording == recording && !bundle->completion)
                {
                    // 그래프 계획은 아직 실행되지 않은 최종 상태도 외부 상태
                    // 슬롯에 쓴다. abort된 자원은 완료 뒤에도 재사용하지 않는다.
                    bundle->ready = false;
                    bundle->nativeConfirmed = true;
                    bundle->completion = bundle->prefixCompletion;
                    bundle->busy = bundle->completion > completed;
                }
            }
            if (const auto found = uploads.find(recording); found != uploads.end())
            {
                if (!found->second.completion)
                {
                    found->second.completion = found->second.prefixCompletion;
                }
                if (!found->second.completion)
                {
                    uploadBytes -= found->second.bytes;
                    uploads.erase(found);
                }
            }
            RetireUploads();
        }

        void Rejected(std::uint64_t recording, RHICompletionPoint completion)
        {
            std::lock_guard lock(mutex);
            for (const auto& bundle : bundles)
            {
                if (bundle->recording == recording && !bundle->accepted &&
                    ((!bundle->submitted && !bundle->prefixCompletion) ||
                     (bundle->submitted && bundle->prefixCompletion == completion.value)))
                {
                    bundle->lease.fetch_add(1);
                    bundle->ready = false;
                    bundle->busy = bundle->submitted = false;
                    bundle->completion = bundle->prefixCompletion = 0;
                }
            }
            if (const auto found = uploads.find(recording); found != uploads.end() &&
                (!found->second.prefixCompletion || found->second.prefixCompletion == completion.value) &&
                !found->second.accepted)
            {
                uploadBytes -= found->second.bytes;
                uploads.erase(found);
            }
        }

        void RetireUploads()
        {
            for (auto it = uploads.begin(); it != uploads.end();)
            {
                if (it->second.completion && it->second.completion <= completed &&
                    (!it->second.accepted || it->second.nativeConfirmed))
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

        IRenderDeviceServices& device;
        std::mutex mutex;
        std::vector<std::shared_ptr<SceneRuntimeEffectBundle>> bundles;
        std::map<std::uint64_t, UploadReservation> uploads;
        std::uint64_t effectBytes{}, snapshotBytes{}, uploadBytes{}, completed{};
    };

    SceneRuntimeEffectsFrame::~SceneRuntimeEffectsFrame()
    {
        if (pool_ && bundle_)
        {
            pool_->Abandon(bundle_, lease_);
        }
    }

    bool SceneRuntimeEffectsResources::Initialize(const EnhancedFrameContext& context, std::string& error)
    {
        if (device_)
        {
            return device_ == context.resources || FailRuntimeEffect(error, "Runtime effects belong to another device.");
        }
        const auto file = RHIShaderSource::Resolve("MaterialGraphSceneRuntimeEffects.slang").string();
        LX::Runtime::CompiledGraphics shader;
        if (!LX::Runtime::CompileGraphics(file, "LXRuntimeDepthVS", "LXRuntimeDepthPS", {}, {}, shader, error))
        {
            return false;
        }
        const RHIPipelineLayoutParam parameters[]{RHILayout::Cbv(7), RHILayout::SrvTable(1, 0)};
        const auto layout = context.rootSignatures->GetOrCreate({parameters, {}}, error);
        if (!layout.IsValid())
        {
            return false;
        }
        RHIGraphicsPipelineDesc description;
        description.layout = layout;
        description.vsBytecode = shader.vertex.bytecode.Data();
        description.vsSize = shader.vertex.bytecode.Size();
        description.psBytecode = shader.pixel.bytecode.Data();
        description.psSize = shader.pixel.bytecode.Size();
        description.depthEnable = true;
        description.depthWriteMask = RHIDepthWrite::All;
        description.depthFunc = RHICompareOp::Always;
        description.cullMode = RHICullMode::None;
        description.dsvFormat = RHIFormat::D32Float;
        description.numRenderTargets = 0;
        LX::Runtime::GraphicsShaderDescription identity;
        identity.compile = std::move(shader.identity);
        if (!depthCopy_.Create(*context.psoManager, description, std::move(identity), error))
        {
            return false;
        }
        device_ = context.resources;
        pool_ = std::make_shared<SceneRuntimeEffectPool>(*device_);
        device_->RegisterUploadTransactionListener(this);
        return true;
    }

    bool SceneRuntimeEffectsResources::Prepare(const EnhancedFrameContext& context,
                                               const math::matrix4x4& viewProjection, bool transmission,
                                               bool backgroundHasMedium, std::uint64_t effectBudget,
                                               std::uint64_t refractionBudget,
                                               std::shared_ptr<const SceneRuntimeEffectsFrame>& result,
                                               std::string& error)
    {
        ResetPreparationStatus();
        const auto count = std::uint64_t(context.width) * context.height;
        const auto columns = (std::uint64_t(context.width) + SceneRuntimeTileSize - 1) / SceneRuntimeTileSize;
        const auto rows = (std::uint64_t(context.height) + SceneRuntimeTileSize - 1) / SceneRuntimeTileSize;
        if (!context.resources || !context.rootSignatures || !context.psoManager || !count || count > UINT32_MAX ||
            !context.resources->GetCurrentUploadRecordingId())
        {
            return FailRuntimeEffect(error, "Runtime effects need current services and a nonempty identified view.");
        }
        if (columns * rows > kMaximumTiles)
        {
            preparationDeferred_ = true;
            return FailRuntimeEffect(error, "Runtime effects exceed the bounded CPU tile declaration capacity.");
        }
        if (!Initialize(context, error))
        {
            return false;
        }
        auto candidate = std::make_shared<SceneRuntimeEffectsFrame>();
        candidate->device_ = device_;
        candidate->recording_ = device_->GetCurrentUploadRecordingId();
        candidate->descriptors_ = device_->GetDescriptorVersionToken();
        candidate->pool_ = pool_;
        candidate->bundle_ = pool_->Acquire(context.width, context.height, transmission, candidate->recording_,
                                           effectBudget, refractionBudget, columns * rows * 256);
        if (!candidate->bundle_)
        {
            preparationDeferred_ = true;
            return FailRuntimeEffect(error, "Runtime effect admission is waiting for GPU memory retirement.");
        }
        auto& bundle = *candidate->bundle_;
        candidate->lease_ = bundle.lease.load();
        candidate->transmission_ = transmission;
        candidate->depthCopy_ = depthCopy_.GetGeneration();
        if (!bundle.ready)
        {
            RHITextureDesc capture;
            capture.width = SceneRuntimeTileExtent;
            capture.height = SceneRuntimeTileExtent;
            capture.format = RHIFormat::RGBA32Float;
            capture.allowRenderTarget = true;
            capture.debugName = L"LX.Scene.RuntimeEffectTile";
            for (auto& input : bundle.inputs)
            {
                if (!device_->CreateTexture(capture, input, error))
                {
                    return false;
                }
            }
            capture.format = RHIFormat::D32Float;
            capture.allowRenderTarget = false;
            capture.allowDepthStencil = true;
            capture.debugName = L"LX.Scene.RuntimeTileDepth";
            if (!device_->CreateTexture(capture, bundle.tileDepth, error))
            {
                return false;
            }
            if (transmission)
            {
                RHITextureDesc texture;
                texture.width = context.width;
                texture.height = context.height;
                texture.format = RHIFormat::RGBA16Float;
                texture.debugName = L"LX.Scene.RuntimeRefractionColor";
                if (!device_->CreateTexture(texture, bundle.color, error))
                {
                    return false;
                }
                texture.format = RHIFormat::D32Float;
                texture.allowDepthStencil = true;
                texture.debugName = L"LX.Scene.RuntimeRefractionDepth";
                if (!device_->CreateTexture(texture, bundle.depth, error))
                {
                    return false;
                }
            }
            bundle.ready = true;
        }
        std::array<RHIBindingDesc, 14> inputs;
        for (unsigned i = 0; i < inputs.size(); ++i)
        {
            inputs[i] = RHIBindingDesc::Srv2D(bundle.inputs[i], RHIFormat::RGBA32Float);
        }
        candidate->inputs_ = device_->CreateBindings(inputs);
        const RHIBindingDesc background[]{RHIBindingDesc::Srv2D(bundle.color, RHIFormat::RGBA16Float).OrNull(),
                                           RHIBindingDesc::Srv2D(bundle.depth, RHIFormat::R32Float).OrNull()};
        candidate->background_ = device_->CreateBindings(background);
        if (!candidate->inputs_.IsValid() || !candidate->background_.IsValid())
        {
            return FailRuntimeEffect(error, "Runtime effect tile or snapshot bindings could not be allocated.");
        }
        const auto writableDepth = RHIDepthTargetDesc::Depth(bundle.tileDepth, RHIFormat::D32Float);
        candidate->depthTarget_ = device_->CreateRenderTargets(std::span<const RHITextureHandle>{}, &writableDepth);
        const auto readonlyDepth = RHIDepthTargetDesc::DepthReadOnly(bundle.tileDepth, RHIFormat::D32Float);
        for (unsigned part = 0; part < 2; ++part)
        {
            candidate->captureTargets_[part] = device_->CreateRenderTargets(
                std::span<const RHITextureHandle>{bundle.inputs}.subspan(part ? 8 : 0, part ? 6 : 8), &readonlyDepth);
        }
        if (!candidate->depthTarget_.IsValid() || !candidate->captureTargets_[0].IsValid() ||
            !candidate->captureTargets_[1].IsValid())
        {
            return FailRuntimeEffect(error, "Runtime effect immutable target bindings could not be allocated.");
        }
        candidate->tiles_.reserve(static_cast<std::size_t>(columns * rows));
        const auto inverseViewProjection = math::transpose(math::inverse(viewProjection));
        for (std::uint32_t y = 0; y < context.height; y += SceneRuntimeTileSize)
        {
            for (std::uint32_t x = 0; x < context.width; x += SceneRuntimeTileSize)
            {
                SceneRuntimeEffectTile tile;
                tile.x = x;
                tile.y = y;
                tile.width = (std::min)(SceneRuntimeTileSize, context.width - x);
                tile.height = (std::min)(SceneRuntimeTileSize, context.height - y);
                tile.sourceX = x > SceneRuntimeSubsurfaceRadius ? x - SceneRuntimeSubsurfaceRadius : 0;
                tile.sourceY = y > SceneRuntimeSubsurfaceRadius ? y - SceneRuntimeSubsurfaceRadius : 0;
                tile.sourceWidth = (std::min)(context.width - tile.sourceX,
                    x - tile.sourceX + tile.width + SceneRuntimeSubsurfaceRadius);
                tile.sourceHeight = (std::min)(context.height - tile.sourceY,
                    y - tile.sourceY + tile.height + SceneRuntimeSubsurfaceRadius);
                const RuntimeEffectConstants constants{inverseViewProjection,
                    tile.sourceX, tile.sourceY, tile.sourceWidth, tile.sourceHeight,
                    context.width, context.height, 64, 32, backgroundHasMedium, transmission, 0, 0};
                tile.constants = device_->UploadConstants(&constants, sizeof(constants));
                if (!tile.constants.IsValid())
                {
                    return FailRuntimeEffect(error, "Runtime effect tile constants could not be allocated.");
                }
                candidate->tiles_.push_back(tile);
            }
        }
        if (candidate->recording_ != device_->GetCurrentUploadRecordingId() ||
            candidate->descriptors_ != device_->GetDescriptorVersionToken())
        {
            return FailRuntimeEffect(error, "Runtime effect preparation changed upload or descriptor ownership.");
        }
        candidate->self_ = candidate;
        result = std::move(candidate);
        error.clear();
        return true;
    }

    void SceneRuntimeEffectsFrame::CheckCurrent(const EnhancedRenderGraph& graph) const
    {
        if (graph_ != &graph || graphEpoch_ != graph.ResourceEpoch() || bundle_->lease.load() != lease_ ||
            recording_ != device_->GetCurrentUploadRecordingId() || descriptors_ != device_->GetDescriptorVersionToken())
        {
            throw std::runtime_error("Runtime effects have stale graph, lease, or upload ownership.");
        }
    }

    void SceneRuntimeEffectsFrame::DeclareInputs(EnhancedRenderGraph& graph) const
    {
        if (graph_)
        {
            throw std::runtime_error("Runtime effects need one graph declaration per lease.");
        }
        graph_ = &graph;
        graphEpoch_ = graph.ResourceEpoch();
        CheckCurrent(graph);
        for (unsigned i = 0; i < graphInputs_.size(); ++i)
        {
            graphInputs_[i] = graph.ImportTexture(bundle_->inputs[i], bundle_->inputStates[i],
                                                 "LX.Scene.RuntimeEffectTile", &bundle_->inputStates[i]);
        }
        graphTileDepth_ = graph.ImportTexture(bundle_->tileDepth, bundle_->tileDepthState,
                                              "LX.Scene.RuntimeTileDepth", &bundle_->tileDepthState);
        if (transmission_)
        {
            graphColor_ = graph.ImportTexture(bundle_->color, bundle_->colorState, "LX.Scene.RuntimeRefractionColor",
                                              &bundle_->colorState);
            graphDepth_ = graph.ImportTexture(bundle_->depth, bundle_->depthState, "LX.Scene.RuntimeRefractionDepth",
                                              &bundle_->depthState);
        }
    }

    void SceneRuntimeEffectsFrame::DeclareBackground(EnhancedRenderGraph& graph, RGHandle color, RGHandle depth) const
    {
        CheckCurrent(graph);
        if (!transmission_)
        {
            throw std::runtime_error("Runtime refraction snapshot requires a transmitting frame.");
        }
        const bool versioned = graph.GetSchedulingMode() == RGSchedulingMode::ExplicitVersioned;
        const bool explicitAccess = graph.GetSchedulingMode() != RGSchedulingMode::DeclarationOrder;
        if (versioned)
        {
            graphColor_ = graph.Write(graphColor_);
            graphDepth_ = graph.Write(graphDepth_);
        }
        const auto read = explicitAccess ? RGAccessMode::Read : RGAccessMode::LegacyState;
        const auto write = explicitAccess ? RGAccessMode::Write : RGAccessMode::LegacyState;
        const auto owner = self_.lock();
        graph.AddPass("LX.Scene.RuntimeRefractionBackground",
                      {{color, RHIResourceState::CopySource, read}, {depth, RHIResourceState::CopySource, read},
                       {graphColor_, RHIResourceState::CopyDest, write}, {graphDepth_, RHIResourceState::CopyDest, write}},
                      [owner, color, depth](const auto& execution) {
                          owner->CheckCurrent(*execution.graph);
                          execution.encoder->CopyResource(owner->bundle_->color, execution.ResolveHandle(color));
                          execution.encoder->CopyResource(owner->bundle_->depth, execution.ResolveHandle(depth));
                      });
    }

    RGHandle SceneRuntimeEffectsFrame::DeclareDepthOutput(EnhancedRenderGraph& graph) const
    {
        CheckCurrent(graph);
        if (graph.GetSchedulingMode() == RGSchedulingMode::ExplicitVersioned)
        {
            graphTileDepth_ = graph.Write(graphTileDepth_);
        }
        return graphTileDepth_;
    }

    void SceneRuntimeEffectsFrame::RecordDepth(const EnhancedRenderGraph::ExecuteContext& execution,
                                               RGHandle source, std::size_t tile) const
    {
        CheckCurrent(*execution.graph);
        if (tile >= tiles_.size())
        {
            throw std::runtime_error("Runtime effect depth needs its prepared tile.");
        }
        const auto& region = tiles_[tile];
        const auto texture = execution.ResolveHandle(source);
        RHIBindingTable binding;
        {
            std::lock_guard lock(depthBindingMutex_);
            auto& cached = depthBindings_[texture.id];
            if (!cached.IsValid())
            {
                const auto input = RHIBindingDesc::Srv2D(texture, RHIFormat::R32Float);
                cached = device_->CreateBindings({&input, 1});
            }
            binding = cached;
        }
        if (!binding.IsValid())
        {
            throw std::runtime_error("Runtime tile depth source binding failed.");
        }
        auto& encoder = *execution.encoder;
        encoder.SetPipeline(RHIBindPoint::Graphics, depthCopy_->pipeline.GetHandle());
        encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0, region.constants);
        encoder.SetBindings(RHIBindPoint::Graphics, 1, binding);
        encoder.BindRenderTargets(depthTarget_);
        encoder.SetViewportAndScissor(region.sourceWidth, region.sourceHeight);
        encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
        encoder.Draw(3, 1);
    }

    std::array<RGHandle, 14> SceneRuntimeEffectsFrame::DeclareCaptureOutputs(EnhancedRenderGraph& graph,
                                                                           unsigned part) const
    {
        CheckCurrent(graph);
        if (part > 1)
        {
            throw std::runtime_error("Runtime effect capture requires one of two MRT ranges.");
        }
        const unsigned first = part ? 8 : 0, count = part ? 6 : 8;
        if (graph.GetSchedulingMode() == RGSchedulingMode::ExplicitVersioned)
        {
            for (unsigned i = first; i < first + count; ++i)
            {
                graphInputs_[i] = graph.Write(graphInputs_[i]);
            }
        }
        return graphInputs_;
    }

    std::span<const RHITextureHandle> SceneRuntimeEffectsFrame::Inputs() const
    {
        return bundle_->inputs;
    }

    RHITextureHandle SceneRuntimeEffectsFrame::Depth() const
    {
        return bundle_->tileDepth;
    }

    RHIRenderTargetBinding SceneRuntimeEffectsFrame::CaptureTargets(unsigned part) const
    {
        if (part >= captureTargets_.size())
        {
            throw std::runtime_error("Runtime effect capture target range is invalid.");
        }
        return captureTargets_[part];
    }

    void SceneRuntimeEffectsFrame::DeclareShadingInputs(EnhancedRenderGraph& graph,
                                                        std::vector<EnhancedRenderGraph::RGPassUsage>& uses,
                                                        bool samples, bool background) const
    {
        CheckCurrent(graph);
        const auto read = graph.GetSchedulingMode() == RGSchedulingMode::DeclarationOrder
                              ? RGAccessMode::LegacyState : RGAccessMode::Read;
        if (samples)
        {
            for (const auto input : graphInputs_)
            {
                uses.push_back({input, RHIResourceState::PixelShaderResource, read});
            }
        }
        if (!samples)
        {
            uses.push_back({graphTileDepth_, RHIResourceState::DepthRead, read});
        }
        if (transmission_ && background)
        {
            uses.push_back({graphColor_, RHIResourceState::PixelShaderResource, read});
            uses.push_back({graphDepth_, RHIResourceState::PixelShaderResource, read});
        }
    }

    void SceneRuntimeEffectsFrame::Bind(RHIEncoder& encoder, unsigned firstRootSlot, std::size_t tile) const
    {
        if (tile >= tiles_.size() || bundle_->lease.load() != lease_)
        {
            throw std::runtime_error("Runtime effect binding needs its current prepared tile lease.");
        }
        encoder.SetConstantBuffer(RHIBindPoint::Graphics, firstRootSlot, tiles_[tile].constants);
        encoder.SetBindings(RHIBindPoint::Graphics, firstRootSlot + 1, inputs_);
        encoder.SetBindings(RHIBindPoint::Graphics, firstRootSlot + 2, background_);
    }

    void SceneRuntimeEffectsFrame::MarkSubmitted(RHICompletionPoint completion) const
    {
        pool_->Publish(bundle_, lease_, completion);
    }

    void SceneRuntimeEffectsFrame::ConfirmSubmitted(RHICompletionPoint completion) const
    {
        pool_->Confirm(bundle_, lease_, completion);
    }

    void SceneRuntimeEffectsResources::OnUploadSubmitted(std::uint64_t recording, RHICompletionPoint completion)
    {
        pool_->Submitted(recording, completion);
    }

    void SceneRuntimeEffectsResources::OnUploadCompleted(std::uint64_t completed)
    {
        pool_->Completed(completed);
    }

    void SceneRuntimeEffectsResources::OnUploadAborted(std::uint64_t recording)
    {
        pool_->Aborted(recording);
    }

    void SceneRuntimeEffectsResources::OnUploadSubmissionRejected(std::uint64_t recording,
                                                                 RHICompletionPoint completion)
    {
        pool_->Rejected(recording, completion);
    }

    void SceneRuntimeEffectsResources::ShutdownAfterIdle()
    {
        if (device_)
        {
            device_->UnregisterUploadTransactionListener(this);
        }
        pool_.reset();
        depthCopy_ = {};
        device_ = nullptr;
    }
} // namespace material_graph
