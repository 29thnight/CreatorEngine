#pragma once
#include "Render/Graph/EnhancedRenderGraph.h"
#include "RHI/DX12/DX12DeviceResources.h"
#include "RHI/DX12/DX12QueueRecorder.h"
#include "RHI/DX12/DX12PSOManager.h"
#include "RHI/DX12/DX12RootSignatureCache.h"
#include "RHI/RHIShaderCompiler.h"
#include <algorithm>
#include <chrono>
#include <thread>
#include <stdexcept>
#include <atomic>
#include <memory>
#include <utility>
#include <vector>
#include "RHI/DX12/DX12GpuProfiler.h"

namespace Rg8QueueExecutionTests
{
    // Forward real native operations and inject only the selected failure. The
    // recorder unwraps this endpoint so DX12 still owns every command list.
    class FaultQueue final : public IRHICommandQueue
    {
    public:
        FaultQueue(std::shared_ptr<IRHICommandQueue> queue, DX12QueueService& service,
            bool rejectWait, bool rejectSubmit)
            : m_queue(std::move(queue)), m_service(service), m_rejectWait(rejectWait), m_rejectSubmit(rejectSubmit)
        {
        }
        const std::shared_ptr<IRHICommandQueue>& Native() const { return m_queue; }
        RHIQueueIdentity GetIdentity() const override { return m_queue->GetIdentity(); }
        bool Signal(uint64_t value, RHITimelinePoint& point, std::string& error) override
        {
            return m_queue->Signal(value, point, error);
        }
        bool Wait(const RHITimelinePoint& point, std::string& error) override
        {
            if (m_rejectWait)
            {
                error = "injected overlap wait failure";
                return false;
            }
            return m_queue->Wait(point, error);
        }
        bool Submit(const std::shared_ptr<IRHIQueueCommandBatch>& batch, uint64_t value,
            RHITimelinePoint& point, std::string& error) override
        {
            if (m_rejectSubmit)
            {
                m_service.RejectNextTestSubmission(true);
                m_rejectSubmit = false;
            }
            return m_queue->Submit(batch, value, point, error);
        }
        size_t CollectCompleted() override { return m_queue->CollectCompleted(); }
        size_t GetPendingBatchCount() const override { return m_queue->GetPendingBatchCount(); }
    private:
        std::shared_ptr<IRHICommandQueue> m_queue;
        DX12QueueService& m_service;
        bool m_rejectWait;
        bool m_rejectSubmit;
    };

    class FaultRecorder final : public IRHIQueueRecorder
    {
    public:
        explicit FaultRecorder(IRHIQueueRecorder& recorder) : m_recorder(recorder) {}
        IRenderDeviceServices& DeviceServices() const override { return m_recorder.DeviceServices(); }
        bool Record(const std::shared_ptr<IRHICommandQueue>& queue,
            const std::function<void(RHIEncoder&)>& commands, std::shared_ptr<const void> token,
            std::shared_ptr<IRHIQueueCommandBatch>& batch, std::string& error) override
        {
            const auto fault = std::dynamic_pointer_cast<FaultQueue>(queue);
            return m_recorder.Record(fault ? fault->Native() : queue, commands, std::move(token), batch, error);
        }
    private:
        IRHIQueueRecorder& m_recorder;
    };
}

bool DX12Test::RunQueueExecutionTest(std::string& outLog)
{
    uint32_t checks = 0;
    const auto require = [&](bool accepted, const std::string& message)
    {
        ++checks;
        if (!accepted)
        {
            throw std::runtime_error(message);
        }
    };
    try
    {
        DX12DeviceResources device;
        std::string error;
        require(device.Initialize(32, 32, error), error);
        require(device.HasDebugMessageQueue(), "GPU validation must be enabled.");
        DX12RootSignatureCache roots;
        DX12PSOManager pipelines;
        require(roots.Initialize(&device, error) && pipelines.Initialize(&device, L"", error), error);
        RHIShaderBlob produceShader, transformShader;
        require(RHIShaderCompiler::CompileFile("SelfTest/QueueRg8.slang", "Produce", "cs_6_0", produceShader, error) &&
            RHIShaderCompiler::CompileFile("SelfTest/QueueRg8.slang", "Transform", "cs_6_0", transformShader, error), error);
        const RHIPipelineLayoutParam producerParams[]{RHILayout::UavTable(1, 0)};
        const RHIPipelineLayoutParam transformParams[]{RHILayout::Srv(0), RHILayout::UavTable(1, 0)};
        RHIComputePipelineDesc desc{};
        desc.layout = roots.GetOrCreate({producerParams, {}}, error);
        desc.csBytecode = produceShader.Data();
        desc.csSize = produceShader.Size();
        const auto producerPipeline = pipelines.GetOrCreateCompute(desc, error);
        desc.layout = roots.GetOrCreate({transformParams, {}}, error);
        desc.csBytecode = transformShader.Data();
        desc.csSize = transformShader.Size();
        const auto transformPipeline = pipelines.GetOrCreateCompute(desc, error);
        require(producerPipeline.IsValid() && transformPipeline.IsValid(), error);
        DX12GpuProfiler graphicsProfiler, computeProfiler;
        DX12QueueService service(device.GetDevice()); // Drains before pipelines and device die.
        DX12QueueRecorder recorder(device);
        EnhancedRenderGraph::QueueEndpoint graphics, compute;
        require(service.CreateQueue(RHIQueueKind::Graphics, graphics.queue, error) &&
            service.CreateQueue(RHIQueueKind::Compute, compute.queue, error), error);
        require(graphicsProfiler.Initialize(device.GetDevice(), DX12QueueService::NativeQueue(graphics.queue), 16, 3, error) &&
            computeProfiler.Initialize(device.GetDevice(), DX12QueueService::NativeQueue(compute.queue), 16, 3, error), error);
        const auto waitComplete = [&](const RHITimelinePoint& point)
        {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
            while (!point.IsComplete() && std::chrono::steady_clock::now() < deadline)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            require(point.IsComplete(), "Queue join did not complete.");
        };
        struct Storage
        {
            DX12DeviceResources* device;
            RHIReadback readback, graphicsReadback, readReadback;
            RHITextureHandle texture;
            RHIBufferHandle prefixBuffer;
            RHIResourceState state{RHIResourceState::Common};
            ~Storage()
            {
                device->ReleaseReadback(readback);
                device->ReleaseReadback(graphicsReadback);
                device->ReleaseReadback(readReadback);
                device->ReleaseTexture(texture);
                device->ReleaseBuffer(prefixBuffer);
            }
        };
        std::vector<uint8_t> baseline;
        RHITimelinePoint foreignCompletion;
        for (uint32_t mode = 0; mode < 2; ++mode)
        {
            require(device.BeginFrame(error), error);
            auto storage = std::make_shared<Storage>();
            storage->device = &device;
            require(device.CreateBufferReadback(256, storage->readback, error), error);
            RHITextureDesc textureDesc{};
            textureDesc.width = textureDesc.height = 8;
            textureDesc.allowRenderTarget = true;
            textureDesc.format = RHIFormat::RGBA8Unorm;
            require(device.CreateTexture(textureDesc, storage->texture, error), error);
            const auto graphicsToken = graphicsProfiler.BeginFrame(mode + 1, mode + 1, 7);
            const auto computeToken = computeProfiler.BeginFrame(mode + 1, mode + 1, 7);
            graphics.profiler = &graphicsProfiler;
            compute.profiler = &computeProfiler;
            auto graph = std::make_shared<EnhancedRenderGraph>(device, RGSchedulingMode::ExplicitVersioned);
            auto first = graph->Write(graph->CreateBuffer({256, true, false, "queue-input"}));
            auto second = graph->Write(graph->CreateBuffer({256, true, false, "queue-output"}));
            const auto produce = graph->AddRepeatedPass("produce",
                {{first, RHIResourceState::UnorderedAccess, RGAccessMode::Write}},
                {{"write", {{first, RHIResourceState::UnorderedAccess, RGAccessMode::Write}}}}, 3,
                [first, producerPipeline, &device](const auto& context, uint32_t, uint32_t)
                {
                    context.encoder->SetPipeline(RHIBindPoint::Compute, producerPipeline);
                    const auto view = RHIBindingDesc::UavBuffer(context.graph->ResolveBufferHandle(first), 64, 4);
                    const auto bindings = device.CreateBindings(std::span<const RHIBindingDesc>{&view, 1});
                    context.encoder->SetBindings(RHIBindPoint::Compute, 0, bindings);
                    context.encoder->Dispatch(1, 1, 1);
                });
            // These independent passes must remain outside cross-queue waits:
            // batching across either handoff would remove available overlap.
            graph->AddPass("independent-before-compute", {}, nullptr, true);
            auto transform = graph->AddPass("transform", {
                {first, RHIResourceState::ShaderResource, RGAccessMode::Read},
                {second, RHIResourceState::UnorderedAccess, RGAccessMode::Write}},
                [first, second, transformPipeline, &device](const auto& context)
                {
                    context.encoder->SetPipeline(RHIBindPoint::Compute, transformPipeline);
                    context.encoder->SetRootBuffer(RHIBindPoint::Compute, 0, {context.graph->ResolveBufferHandle(first), 0, 256});
                    const auto view = RHIBindingDesc::UavBuffer(context.graph->ResolveBufferHandle(second), 64, 4);
                    const auto bindings = device.CreateBindings(std::span<const RHIBindingDesc>{&view, 1});
                    context.encoder->SetBindings(RHIBindPoint::Compute, 1, bindings);
                    context.encoder->Dispatch(1, 1, 1);
                });
            graph->DeclareComputeCompatible(transform);
            graph->AddPass("independent-after-compute", {}, nullptr, true);
            graph->AddPass("readback", {{second, RHIResourceState::CopySource, RGAccessMode::Read}},
                [second, storage](const auto& context)
                {
                    context.encoder->CopyBufferToReadback(storage->readback, context.graph->ResolveBufferHandle(second));
                }, true);
            auto imported = graph->Write(graph->ImportTexture(storage->texture, storage->state, "imported-final-state", &storage->state));
            graph->AddPass("independent-clear", {{imported, RHIResourceState::RenderTarget, RGAccessMode::Write}},
                [&device, imported](const auto& context)
                {
                    const auto target = context.ResolveHandle(imported);
                    const auto color = RHIColorTargetDesc::Texture(target);
                    auto binding = device.CreateRenderTargets(std::span<const RHIColorTargetDesc>{&color, 1});
                    if (!binding.IsValid())
                    {
                        throw std::runtime_error("Render target creation failed.");
                    }
                    const float value[]{0, 0, 0, 0};
                    context.encoder->ClearRenderTargets(binding, value);
                }, true);
            graph->RequireImportedFinalState(imported, RHIResourceState::ShaderResource);
            require(graph->Compile(error), error);
            Microsoft::WRL::ComPtr<ID3D12Fence> gate;
            require(SUCCEEDED(device.GetDevice()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate))), "Gate creation failed.");
            struct ReleaseGate
            {
                ID3D12Fence* gate;
                ~ReleaseGate() { gate->Signal(1); }
            } release{gate.Get()};
            require(service.EnqueueTestGate(graphics.queue, gate.Get(), 1, error), error);
            EnhancedRenderGraph::QueueExecution execution;
            const auto measuredHints = graph->MeasuredQueueHints([&](const GpuPassTimingIdentity& identity)
            {
                return identity.passIndex == transform.index ? uint64_t{100} : uint64_t{0};
            });
            const auto transformHint = std::find_if(measuredHints.begin(), measuredHints.end(),
                [&](const auto& hint) { return hint.pass.index == transform.index; });
            require(measuredHints.size() == graph->GetExecuteOrder().size() && transformHint != measuredHints.end() &&
                transformHint->computeCompatible && transformHint->measuredGpuNanoseconds == 100 &&
                std::count_if(measuredHints.begin(), measuredHints.end(),
                    [](const auto& hint) { return hint.computeCompatible; }) == 1,
                "Author compute declaration did not produce measured hint.");
            compute.profiler = &graphicsProfiler;
            require(!graph->SubmitQueues(graph, recorder, graphics, &compute, measuredHints, 100, storage, execution, error) &&
                !execution.submissionAttempted, "Shared graphics/compute clock was accepted.");
            compute.profiler = &computeProfiler;
            require(graph->SubmitQueues(graph, recorder, graphics, mode ? &compute : nullptr,
                measuredHints, 100, storage, execution, error), error);
            require(execution.plannedBarriers == (mode ? 15u : 8u) &&
                graph->GetPassBarrierCount(produce) == (mode ? 4u : 3u),
                "Repeated UAV ordering or batch-local state retention was lost.");
            const uint32_t expectedBatches = mode ? 7u : 3u;
            require(execution.submittedBatches == expectedBatches && !execution.recoveryRequired &&
                !execution.completion.IsComplete(), "Incorrect submission/join state.");
            const auto blockedGraphicsPool = DX12QueueService::QueryRecordingPool(graphics.queue);
            const auto blockedComputePool = DX12QueueService::QueryRecordingPool(compute.queue);
            require(blockedGraphicsPool.leased + blockedComputePool.leased == expectedBatches &&
                (mode != 0 || blockedGraphicsPool.cached == 0), "Blocked GPU recording storage was recycled.");
            require(storage->state == RHIResourceState::ShaderResource, "Final state writeback missing.");
            bool resetRejected = false;
            try
            {
                graph->Reset();
            }
            catch (const std::logic_error&)
            {
                resetRejected = true;
            }
            require(resetRejected, "Pending graph storage was reset.");
            EnhancedRenderGraph::QueueExecution duplicate;
            require(!graph->SubmitQueues(graph, recorder, graphics, mode ? &compute : nullptr,
                {}, 100, storage, duplicate, error) && !duplicate.submissionAttempted, "Pending graph was submitted twice.");
            std::weak_ptr<EnhancedRenderGraph> graphWeak = graph;
            graph.reset();
            require(graphics.queue->CollectCompleted() == 0 && compute.queue->CollectCompleted() == 0 &&
                !graphWeak.expired(), "Blocked GPU released graph storage.");
            std::shared_ptr<IRHICommandQueue> unrelated;
            require(service.CreateQueue(RHIQueueKind::Copy, unrelated, error), error);
            RHITimelinePoint high;
            require(unrelated->Signal(100000, high, error), error);
            foreignCompletion = high;
            waitComplete(high);
            require(!execution.completion.IsComplete() && !graphWeak.expired(), "Unrelated timeline retired a graph.");
            require(SUCCEEDED(gate->Signal(1)), "Gate release failed.");
            waitComplete(execution.completion);
            graphicsProfiler.ResolveFrame(device.GetCommandList(), graphicsToken);
            computeProfiler.ResolveFrame(device.GetCommandList(), computeToken);
            require(device.FlushCommandList(error), error);
            device.WaitForGpu();
            DX12GpuProfiler::FrameTimings graphicsTiming, computeTiming;
            require(graphicsProfiler.Collect(graphicsToken, graphicsTiming, error) &&
                computeProfiler.Collect(computeToken, computeTiming, error), error);
            require(graphicsTiming.slices.size() == (mode ? 5u : 6u) && computeTiming.slices.size() == mode &&
                graphicsTiming.overflowedPasses == 0 && computeTiming.overflowedPasses == 0 &&
                graphicsTiming.droppedSlices == 0 && computeTiming.droppedSlices == 0 && execution.computeBatches == mode,
                "Queue-local profiler lost or misattributed slices.");
            require(graphicsTiming.cpuAligned && (!mode || computeTiming.cpuAligned) &&
                (!mode || computeTiming.slices.front().name == "transform"), "Queue clocks or compute attribution unavailable.");
            RHIReadbackImage image;
            require(device.MapReadback(storage->readback, image, error), error);
            require(image.ElementCount<uint32_t>() == 64, "Readback size mismatch.");
            bool correct = true;
            for (uint32_t index = 0; index < 64; ++index)
            {
                correct = correct && image.Elements<uint32_t>()[index] == index * 3 + 40;
            }
            require(correct, "Compute payload differs from expected values.");
            if (mode == 0)
            {
                baseline = image.data;
            }
            require(image.data == baseline, "Single/multi queue payload differs.");
            const auto collected = graphics.queue->CollectCompleted() + compute.queue->CollectCompleted();
            require(collected == expectedBatches && graphWeak.expired(), "Completed graph storage was not released.");
            const auto retiredPool = DX12QueueService::QueryRecordingPool(graphics.queue);
            require(retiredPool.leased == 0 && retiredPool.cached >= 3 &&
                DX12QueueService::QueryRecordingPool(compute.queue).leased == 0, "Completed recording storage was not cached.");
            // Validate actual epilogue state, not only the CPU writeback field.
            std::shared_ptr<IRHIQueueCommandBatch> restoreProbe;
            require(recorder.Record(graphics.queue, [&](RHIEncoder& encoder)
            {
                RHITransition transitions[]{ {storage->texture, RHIResourceState::ShaderResource, RHIResourceState::Common} };
                RHIBarrierBatch barriers{};
                barriers.textureTransitions = transitions;
                encoder.ResourceBarriers(barriers);
            }, storage, restoreProbe, error), error);
            const auto reusedPool = DX12QueueService::QueryRecordingPool(graphics.queue);
            require(reusedPool.created == retiredPool.created && reusedPool.reused == retiredPool.reused + 1 &&
                reusedPool.leased == 1, "Warm recording allocated instead of reusing completed storage.");
            RHITimelinePoint restored;
            require(graphics.queue->Submit(restoreProbe, graphics.nextSignal++, restored, error), error);
            waitComplete(restored);
            graphics.queue->CollectCompleted();
            require(DX12QueueService::QueryRecordingPool(graphics.queue).leased == 1,
                "Caller-retained batch lost its exclusive recording lease.");
            restoreProbe.reset();
            require(DX12QueueService::QueryRecordingPool(graphics.queue).leased == 0,
                "Last batch owner did not release recording storage.");
            storage.reset();
            device.AbortFrame();
        }
        // Analytical costs select a deterministic plan; readbacks establish actual
        // GPU work on each selected queue. Submission counts do not prove measured
        // hardware overlap or speedup, and this fixture makes no such assertion.
        enum class OverlapCase
        {
            GraphicsReference,
            Reordered,
            NoBenefit,
            PreserveDeclarationOrder,
            RecordingFailure,
            WaitFailure,
            UnfencedSubmission
        };
        std::vector<uint8_t> overlapBaseline, readReadBaseline;
        for (const auto mode : {OverlapCase::GraphicsReference, OverlapCase::Reordered, OverlapCase::NoBenefit,
            OverlapCase::PreserveDeclarationOrder, OverlapCase::RecordingFailure,
            OverlapCase::WaitFailure, OverlapCase::UnfencedSubmission})
        {
            const bool withShadow = mode != OverlapCase::NoBenefit;
            const bool moved = mode == OverlapCase::Reordered || mode == OverlapCase::RecordingFailure ||
                mode == OverlapCase::WaitFailure || mode == OverlapCase::UnfencedSubmission;
            const bool recordingFailure = mode == OverlapCase::RecordingFailure;
            const bool submissionFailure = mode == OverlapCase::WaitFailure || mode == OverlapCase::UnfencedSubmission;
            const bool failureExpected = recordingFailure || submissionFailure;
            require(device.BeginFrame(error), error);
            DX12QueueService overlapService(device.GetDevice());
            EnhancedRenderGraph::QueueEndpoint overlapGraphics, overlapCompute;
            require(overlapService.CreateQueue(RHIQueueKind::Graphics, overlapGraphics.queue, error) &&
                overlapService.CreateQueue(RHIQueueKind::Compute, overlapCompute.queue, error), error);
            const auto nativeCompute = overlapCompute.queue;
            if (submissionFailure)
            {
                overlapCompute.queue = std::make_shared<Rg8QueueExecutionTests::FaultQueue>(nativeCompute,
                    overlapService, mode == OverlapCase::WaitFailure, mode == OverlapCase::UnfencedSubmission);
            }
            Rg8QueueExecutionTests::FaultRecorder overlapRecorder(recorder);
            auto storage = std::make_shared<Storage>();
            storage->device = &device;
            require(device.CreateBufferReadback(256, storage->readback, error) &&
                device.CreateBufferReadback(256, storage->graphicsReadback, error) &&
                device.CreateBufferReadback(256, storage->readReadback, error), error);
            RHIBufferDesc importedDesc{};
            importedDesc.bytes = 256;
            importedDesc.allowUnorderedAccess = true;
            RHITextureDesc finalTextureDesc{};
            finalTextureDesc.width = finalTextureDesc.height = 8;
            finalTextureDesc.allowRenderTarget = true;
            finalTextureDesc.format = RHIFormat::RGBA8Unorm;
            require(device.CreateBuffer(importedDesc, storage->prefixBuffer, error) &&
                device.CreateTexture(finalTextureDesc, storage->texture, error), error);
            auto graph = std::make_shared<EnhancedRenderGraph>(device, RGSchedulingMode::ExplicitVersioned,
                mode == OverlapCase::PreserveDeclarationOrder ? RGOrderPolicy::PreserveDeclarationOrder :
                    RGOrderPolicy::DependencyOrder);
            EnhancedRenderGraph::QueueCostModel model;
            model.placement = RGQueuePlacement::Overlap;
            model.handoffNanoseconds = 20'000;
            model.minimumGainNanoseconds = 50'000;
            graph->SetQueueCostModel(model);
            const auto depth = graph->Write(graph->ImportBuffer(storage->prefixBuffer, RHIResourceState::Common,
                "overlap-depth"));
            const auto finalTexture = graph->Write(graph->ImportTexture(storage->texture, storage->state,
                "overlap-final-texture", &storage->state));
            const auto raw = graph->Write(graph->CreateBuffer({256, true, false, "overlap-raw"}));
            const auto filtered = graph->Write(graph->CreateBuffer({256, true, false, "overlap-filtered"}));
            const auto output = graph->Write(graph->CreateBuffer({256, true, false, "overlap-output"}));
            const auto readRead = graph->Write(graph->CreateBuffer({256, true, false, "overlap-read-read"}));
            const auto dispatch = [&device](const auto& context, RHIPipelineHandle pipeline,
                RGHandle input, RGHandle destination)
            {
                context.encoder->SetPipeline(RHIBindPoint::Compute, pipeline);
                const bool reads = input.IsValid();
                if (reads)
                {
                    context.encoder->SetRootBuffer(RHIBindPoint::Compute, 0,
                        {context.graph->ResolveBufferHandle(input), 0, 256});
                }
                const auto view = RHIBindingDesc::UavBuffer(context.graph->ResolveBufferHandle(destination), 64, 4);
                const auto bindings = device.CreateBindings(std::span<const RHIBindingDesc>{&view, 1});
                if (!bindings.IsValid())
                {
                    throw std::runtime_error("Overlap descriptor allocation failed.");
                }
                context.encoder->SetBindings(RHIBindPoint::Compute, reads ? 1 : 0, bindings);
                context.encoder->Dispatch(1, 1, 1);
            };
            const auto clearFinalTexture = [&device, finalTexture](const auto& context)
            {
                const auto color = RHIColorTargetDesc::Texture(context.ResolveHandle(finalTexture));
                const auto targets = device.CreateRenderTargets(std::span<const RHIColorTargetDesc>{&color, 1});
                if (!targets.IsValid())
                {
                    throw std::runtime_error("Overlap render target creation failed.");
                }
                const float value[]{0.25f, 0.5f, 0.75f, 1.0f};
                context.encoder->ClearRenderTargets(targets, value);
            };
            RGHandle shadow{};
            RGPassId shadowPass{};
            std::vector<EnhancedRenderGraph::QueueHint> hints;
            std::vector<uint16_t> callbacks;
            if (withShadow)
            {
                shadow = graph->Write(graph->CreateBuffer({256, true, false, "overlap-shadow"}));
                shadowPass = graph->AddPass("overlap-shadow",
                    {{shadow, RHIResourceState::UnorderedAccess, RGAccessMode::Write},
                     {finalTexture, RHIResourceState::RenderTarget, RGAccessMode::Write}},
                    [&, shadow](const auto& context)
                    {
                        callbacks.push_back(shadowPass.index);
                        dispatch(context, producerPipeline, {}, shadow);
                        clearFinalTexture(context);
                    });
                hints.push_back({shadowPass, false, 400'000});
            }
            RGPassId gbuffer, rawPass, filterPass, deferred, readback;
            std::vector<EnhancedRenderGraph::RGPassUsage> gbufferUsages{
                {depth, RHIResourceState::UnorderedAccess, RGAccessMode::Write}};
            if (!withShadow)
            {
                gbufferUsages.push_back({finalTexture, RHIResourceState::RenderTarget, RGAccessMode::Write});
            }
            gbuffer = graph->AddPass("overlap-gbuffer", gbufferUsages,
                [&, depth](const auto& context)
                {
                    callbacks.push_back(gbuffer.index);
                    dispatch(context, producerPipeline, {}, depth);
                    if (!withShadow)
                    {
                        clearFinalTexture(context);
                    }
                });
            rawPass = graph->AddPass("overlap-raw", {
                {depth, RHIResourceState::ShaderResource, RGAccessMode::Read},
                {raw, RHIResourceState::UnorderedAccess, RGAccessMode::Write}},
                [&, depth, raw](const auto& context)
                {
                    callbacks.push_back(rawPass.index);
                    dispatch(context, transformPipeline, depth, raw);
                });
            filterPass = graph->AddPass("overlap-filter", {
                {raw, RHIResourceState::ShaderResource, RGAccessMode::Read},
                {filtered, RHIResourceState::UnorderedAccess, RGAccessMode::Write}},
                [&, raw, filtered](const auto& context)
                {
                    callbacks.push_back(filterPass.index);
                    if (recordingFailure)
                    {
                        throw std::runtime_error("injected reordered recording failure");
                    }
                    dispatch(context, transformPipeline, raw, filtered);
                });
            std::vector<EnhancedRenderGraph::RGPassUsage> deferredUsages{
                {depth, RHIResourceState::ShaderResource, RGAccessMode::Read},
                {filtered, RHIResourceState::ShaderResource, RGAccessMode::Read},
                {output, RHIResourceState::UnorderedAccess, RGAccessMode::Write},
                {readRead, RHIResourceState::UnorderedAccess, RGAccessMode::Write}};
            if (withShadow)
            {
                deferredUsages.push_back({shadow, RHIResourceState::CopySource, RGAccessMode::Read});
            }
            deferred = graph->AddPass("overlap-deferred", deferredUsages,
                [&, depth, filtered, output, readRead, shadow, storage](const auto& context)
                {
                    callbacks.push_back(deferred.index);
                    dispatch(context, transformPipeline, filtered, output);
                    // This is a real second reader of depth, in the same SRV state
                    // as rawPass on compute; resource ownership still serializes it.
                    dispatch(context, transformPipeline, depth, readRead);
                    if (shadow.IsValid())
                    {
                        context.encoder->CopyBufferToReadback(storage->graphicsReadback,
                            context.graph->ResolveBufferHandle(shadow));
                    }
                });
            readback = graph->AddPass("overlap-readback", {
                {output, RHIResourceState::CopySource, RGAccessMode::Read},
                {readRead, RHIResourceState::CopySource, RGAccessMode::Read}},
                [&, output, readRead, storage](const auto& context)
                {
                    callbacks.push_back(readback.index);
                    context.encoder->CopyBufferToReadback(storage->readback, context.graph->ResolveBufferHandle(output));
                    context.encoder->CopyBufferToReadback(storage->readReadback,
                        context.graph->ResolveBufferHandle(readRead));
                }, true);
            graph->DeclareComputeCompatible(rawPass);
            graph->DeclareComputeCompatible(filterPass);
            graph->RequireImportedFinalState(finalTexture, RHIResourceState::RenderTarget);
            hints.insert(hints.end(), {{gbuffer, false, 500'000}, {rawPass, true, 200'000},
                {filterPass, true, 200'000}, {deferred, false, 300'000}, {readback, false, 1000}});
            require(graph->Compile(error), error);
            EnhancedRenderGraph::QueueSchedule planned;
            require(graph->BuildQueueSchedule({true, mode != OverlapCase::GraphicsReference, false,
                mode != OverlapCase::GraphicsReference}, hints, 1000, planned, error), error);
            const auto compiledOrder = graph->GetExecuteOrder();
            require(compiledOrder.front() == (withShadow ? shadowPass.index : gbuffer.index),
                "Overlap fixture did not preserve its authored independent-first order.");
            Microsoft::WRL::ComPtr<ID3D12Fence> gate;
            require(SUCCEEDED(device.GetDevice()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate))),
                "Overlap gate creation failed.");
            struct OverlapGateRelease
            {
                ID3D12Fence* gate;
                ~OverlapGateRelease() { gate->Signal(1); }
            } gateRelease{gate.Get()};
            require(overlapService.EnqueueTestGate(overlapGraphics.queue, gate.Get(), 1, error), error);
            EnhancedRenderGraph::QueueExecution execution;
            const bool accepted = graph->SubmitQueues(graph, overlapRecorder, overlapGraphics,
                mode == OverlapCase::GraphicsReference ? nullptr : &overlapCompute,
                hints, 1000, storage, execution, error);
            require(accepted == !failureExpected, "Overlap submission returned the wrong outcome: " + error);
            const auto diagnostics = graph->GetQueueDiagnostics();
            const auto& schedule = diagnostics.schedule;
            require(schedule.usesCompute == moved && schedule.predictionComplete &&
                !schedule.predictionCalibrated && schedule.entries.size() == compiledOrder.size(),
                "Overlap execution did not expose its complete uncalibrated plan.");
            std::vector<uint16_t> scheduledOrder;
            for (const auto& entry : schedule.entries)
            {
                scheduledOrder.push_back(entry.pass);
            }
            require(graph->GetExecuteOrder() == scheduledOrder &&
                (moved ? scheduledOrder.front() == gbuffer.index && scheduledOrder != compiledOrder :
                    scheduledOrder == compiledOrder), "Executed barrier order differs from the selected schedule.");
            require(schedule.predictedSerialNanoseconds == (withShadow ? 1'601'000u : 1'201'000u) &&
                schedule.predictedNanoseconds == (moved ? 1'241'000u : schedule.predictedSerialNanoseconds),
                "Overlap execution prediction differs from the analytical fixture.");
            if (recordingFailure)
            {
                require(!execution.submissionAttempted && !execution.completed && !execution.recoveryRequired &&
                    overlapGraphics.queue->GetPendingBatchCount() == 0 && nativeCompute->GetPendingBatchCount() == 0 &&
                    DX12QueueService::QueryRecordingPool(overlapGraphics.queue).leased == 0 &&
                    DX12QueueService::QueryRecordingPool(nativeCompute).leased == 0,
                    "Reordered recording failure submitted work or retained a recording lease.");
                require(callbacks == std::vector<uint16_t>{gbuffer.index, shadowPass.index, rawPass.index, filterPass.index} &&
                    storage->state == RHIResourceState::Common && !graph->Execute(error),
                    "Reordered recording failure wrote back state or allowed ordinary execution.");
                graph->Reset();
                require(graph->GetExecuteOrder().empty(), "Recording failure could not reset after local leases retired.");
                graph.reset();
                storage.reset();
                require(SUCCEEDED(gate->Signal(1)) && overlapService.Shutdown(error), error);
                device.AbortFrame();
                continue;
            }
            require(callbacks == scheduledOrder, "Native callbacks were not recorded in selected execution order.");
            const uint32_t expectedBatches = moved ? 6u : 3u;
            require(execution.plannedBatches == expectedBatches && diagnostics.batches.size() == expectedBatches &&
                execution.plannedComputeBatches == (moved ? 1u : 0u) &&
                execution.plannedWaits == (moved ? 2u : 0u) &&
                diagnostics.waits.size() == execution.plannedWaits,
                "Overlap batch boundaries or reduced waits differ from the selected schedule.");
            if (moved)
            {
                const auto resourceWait = [&](RGPassId producer, RGPassId consumer, RGHandle resource)
                {
                    return std::any_of(planned.waits.begin(), planned.waits.end(), [&](const auto& wait)
                    {
                        return wait.producer == producer.index && wait.consumer == consumer.index &&
                            wait.resource == resource.index;
                    });
                };
                require(resourceWait(gbuffer, rawPass, depth) && resourceWait(rawPass, deferred, depth) &&
                    resourceWait(filterPass, deferred, filtered), "Native overlap lost RAW or read/read ownership edges.");
                require(diagnostics.batches[1].passes == std::vector<uint16_t>{gbuffer.index} &&
                    diagnostics.batches[2].passes == std::vector<uint16_t>{shadowPass.index} &&
                    diagnostics.batches[3].passes == std::vector<uint16_t>{rawPass.index, filterPass.index} &&
                    diagnostics.batches[3].queue == RHIQueueKind::Compute &&
                    diagnostics.batches[4].passes == std::vector<uint16_t>{deferred.index, readback.index} &&
                    diagnostics.batches[4].queue == RHIQueueKind::Graphics &&
                    diagnostics.waits[0].producerBatch == 1 && diagnostics.waits[0].consumerBatch == 3 &&
                    diagnostics.waits[1].producerBatch == 3 && diagnostics.waits[1].consumerBatch == 4,
                    "Independent graphics work was trapped behind the compute wait frontier.");
            }
            if (submissionFailure)
            {
                require(execution.submissionAttempted && execution.recoveryRequired && !execution.completed &&
                    !execution.completion.IsValid() && execution.submittedBatches == 3 && execution.computeBatches == 0 &&
                    execution.submittedWaits == (mode == OverlapCase::WaitFailure ? 0u : 1u) &&
                    storage->state == RHIResourceState::Common,
                    "Partial reordered failure published completion or final states.");
                std::weak_ptr<EnhancedRenderGraph> graphWeak = graph;
                graph.reset();
                require(overlapGraphics.queue->CollectCompleted() == 0 && nativeCompute->CollectCompleted() == 0 &&
                    !graphWeak.expired() && DX12QueueService::QueryRecordingPool(overlapGraphics.queue).leased == 3 &&
                    DX12QueueService::QueryRecordingPool(nativeCompute).leased ==
                        (mode == OverlapCase::UnfencedSubmission ? 1u : 0u),
                    "Partial/unfenced reordered work was reclaimed before drain.");
                require(SUCCEEDED(gate->Signal(1)) && overlapService.Shutdown(error) && graphWeak.expired(),
                    "Reordered failure drain did not retire graph storage: " + error);
                require(DX12QueueService::QueryRecordingPool(overlapGraphics.queue).leased == 0 &&
                    DX12QueueService::QueryRecordingPool(nativeCompute).leased == 0,
                    "Reordered failure drain retained recording leases.");
                storage.reset();
                device.AbortFrame();
                continue;
            }
            require(execution.completed && !execution.recoveryRequired && !execution.completion.IsComplete() &&
                execution.submittedBatches == expectedBatches && execution.computeBatches == (moved ? 1u : 0u) &&
                execution.submittedWaits == execution.plannedWaits &&
                std::all_of(diagnostics.batches.begin(), diagnostics.batches.end(),
                    [](const auto& batch) { return batch.submitted; }) &&
                std::all_of(diagnostics.waits.begin(), diagnostics.waits.end(),
                    [](const auto& wait) { return wait.submitted; }),
                "Overlap native submissions/waits were not accepted exactly as planned.");
            require(overlapGraphics.nextSignal - 1 == expectedBatches - execution.computeBatches &&
                overlapCompute.nextSignal - 1 == execution.computeBatches &&
                overlapGraphics.queue->GetPendingBatchCount() + nativeCompute->GetPendingBatchCount() == expectedBatches &&
                DX12QueueService::QueryRecordingPool(overlapGraphics.queue).leased +
                    DX12QueueService::QueryRecordingPool(nativeCompute).leased == expectedBatches,
                "Native queue counters or blocked recording leases disagree with diagnostics.");
            require(storage->state == RHIResourceState::RenderTarget, "Reordered imported final-state writeback missing.");
            bool resetRejected = false;
            try
            {
                graph->Reset();
            }
            catch (const std::logic_error&)
            {
                resetRejected = true;
            }
            require(resetRejected, "Blocked reordered graph was reset.");
            std::weak_ptr<EnhancedRenderGraph> graphWeak = graph;
            std::weak_ptr<Storage> storageWeak = storage;
            graph.reset();
            storage.reset();
            require(overlapGraphics.queue->CollectCompleted() == 0 && nativeCompute->CollectCompleted() == 0 &&
                !graphWeak.expired() && !storageWeak.expired(), "Blocked reordered graph or imported storage retired.");
            require(SUCCEEDED(gate->Signal(1)), "Overlap gate release failed.");
            waitComplete(execution.completion);
            storage = storageWeak.lock();
            require(storage != nullptr, "Joined overlap graph lost pinned readback storage.");
            const auto verifyPayload = [&](const RHIReadback& readbackHandle, uint32_t multiplier, uint32_t addend,
                std::vector<uint8_t>* expected)
            {
                RHIReadbackImage image;
                require(device.MapReadback(readbackHandle, image, error), error);
                bool correct = image.ElementCount<uint32_t>() == 64;
                for (uint32_t index = 0; correct && index < 64; ++index)
                {
                    correct = image.Elements<uint32_t>()[index] == index * multiplier + addend;
                }
                require(correct, "Native overlap dispatch/readback payload mismatch.");
                if (expected)
                {
                    if (expected->empty())
                    {
                        *expected = image.data;
                    }
                    require(image.data == *expected, "Reordered/fallback payload differs from graphics reference.");
                }
            };
            verifyPayload(storage->readback, 27, 388, &overlapBaseline);
            verifyPayload(storage->readReadback, 3, 40, &readReadBaseline);
            if (withShadow)
            {
                verifyPayload(storage->graphicsReadback, 1, 11, nullptr);
            }
            require(overlapGraphics.queue->CollectCompleted() + nativeCompute->CollectCompleted() == expectedBatches &&
                graphWeak.expired(), "Completed reordered graph did not retire exactly its submitted batches.");
            require(DX12QueueService::QueryRecordingPool(overlapGraphics.queue).leased == 0 &&
                DX12QueueService::QueryRecordingPool(nativeCompute).leased == 0,
                "Completed reordered recording storage was still leased.");
            // Non-simultaneous render-target textures do not get buffer-style
            // implicit COMMON decay, and RenderTarget cannot promote from COMMON.
            // Validate the native epilogue state with an
            // explicit transition, rather than trusting only CPU writeback.
            std::shared_ptr<IRHIQueueCommandBatch> finalStateProbe;
            require(recorder.Record(overlapGraphics.queue, [&](RHIEncoder& encoder)
            {
                const RHITransition transition{storage->texture,
                    RHIResourceState::RenderTarget, RHIResourceState::Common};
                RHIBarrierBatch barriers{};
                barriers.textureTransitions = {&transition, 1};
                encoder.ResourceBarriers(barriers);
            }, storage, finalStateProbe, error), error);
            RHITimelinePoint restored;
            require(overlapGraphics.queue->Submit(finalStateProbe, overlapGraphics.nextSignal++, restored, error), error);
            waitComplete(restored);
            overlapGraphics.queue->CollectCompleted();
            finalStateProbe.reset();
            storage.reset();
            require(storageWeak.expired() && DX12QueueService::QueryRecordingPool(overlapGraphics.queue).leased == 0,
                "Final-state verification retained imported storage or a recording lease.");
            require(overlapService.Shutdown(error), error);
            device.AbortFrame();
        }
        graphics.profiler = compute.profiler = nullptr;
        auto owner = std::make_shared<int>(1);
        auto failure = std::make_shared<EnhancedRenderGraph>(device, RGSchedulingMode::ExplicitVersioned);
        failure->AddPass("recording-failure", {}, [](const auto&)
        {
            throw std::runtime_error("injected recording failure");
        }, true);
        require(failure->Compile(error), error);
        EnhancedRenderGraph::QueueExecution rejected;
        require(!failure->SubmitQueues(failure, recorder, graphics, &compute, {}, 100, owner, rejected, error) &&
            !rejected.submissionAttempted && graphics.queue->GetPendingBatchCount() == 0, "Recording failure submitted work.");
        require(!failure->Execute(error), "Queue-specialized plan was accepted by the ordinary executor after failure.");
        failure->Reset();
        failure->AddPass("quarantined-submit", {}, nullptr, true);
        require(failure->Compile(error), error);
        service.RejectNextTestSubmission(true);
        require(!failure->SubmitQueues(failure, recorder, graphics, &compute, {}, 100, owner, rejected, error) &&
            rejected.submissionAttempted && rejected.recoveryRequired && !rejected.completion.IsValid(), "Unfenced submit failure was accepted.");
        std::weak_ptr<EnhancedRenderGraph> failedWeak = failure;
        failure.reset();
        require(graphics.queue->CollectCompleted() == 0 && !failedWeak.expired(), "Unfenced graph was reclaimed early.");
        require(DX12QueueService::QueryRecordingPool(graphics.queue).leased > 0,
            "Unfenced recording returned to the reusable pool.");
        require(service.Shutdown(error) && failedWeak.expired(), "Queue drain did not release failed graph.");
        require(DX12QueueService::QueryRecordingPool(graphics.queue).leased == 0 &&
            DX12QueueService::QueryRecordingPool(graphics.queue).cached == 0,
            "Shutdown retained recording pool storage.");
        RHIShaderBlob frameShader;
        require(RHIShaderCompiler::CompileFile("SelfTest/QueueRg8.slang", "FrameProduce", "cs_6_0", frameShader, error), error);
        const RHIPipelineLayoutParam frameParams[]{RHILayout::Cbv(0), RHILayout::Srv(0), RHILayout::UavTable(1, 0)};
        desc.layout = roots.GetOrCreate({frameParams, {}}, error);
        desc.csBytecode = frameShader.Data();
        desc.csSize = frameShader.Size();
        const auto framePipeline = pipelines.GetOrCreateCompute(desc, error);
        require(framePipeline.IsValid(), error);
        EnhancedRenderGraph::QueueEndpoint frameGraphics, frameCompute;
        require(device.CreateQueue(RHIQueueKind::Graphics, frameGraphics.queue, error) &&
            device.CreateQueue(RHIQueueKind::Compute, frameCompute.queue, error), error);
        Microsoft::WRL::ComPtr<ID3D12Fence> frameGate;
        require(SUCCEEDED(device.GetDevice()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&frameGate))), "Frame gate creation failed.");
        struct FrameGateRelease
        {
            ID3D12Fence* fence;
            ~FrameGateRelease() { fence->Signal(2); }
        } frameRelease{frameGate.Get()};
        struct FrameCleanup
        {
            DX12DeviceResources& device;
            ID3D12Fence* gate;
            ~FrameCleanup()
            {
                gate->Signal(2);
                device.AbortFrame();
                device.WaitForGpu();
            }
        } frameCleanup{device, frameGate.Get()};
        require(device.EnqueueQueueTestGate(frameGraphics.queue, frameGate.Get(), 1, error), error);
        std::vector<std::shared_ptr<Storage>> frameStorage;
        std::vector<std::weak_ptr<EnhancedRenderGraph>> frameGraphs;
        uint64_t firstFrameFence = 0;
        for (uint32_t frame = 0; frame < 3; ++frame)
        {
            require(device.BeginFrame(error), error);
            auto storage = std::make_shared<Storage>();
            storage->device = &device;
            require(device.CreateBufferReadback(256, storage->readback, error), error);
            auto graph = std::make_shared<EnhancedRenderGraph>(device, RGSchedulingMode::ExplicitVersioned);
            RHIBufferDesc prefixDesc{};
            prefixDesc.bytes = 256;
            prefixDesc.allowUnorderedAccess = true;
            require(device.CreateBuffer(prefixDesc, storage->prefixBuffer, error), error);
            const auto input = graph->ImportBuffer(storage->prefixBuffer, RHIResourceState::Common, "prefix-input");
            const auto output = graph->Write(graph->CreateBuffer({256, true, false, "frame-output"}));
            const uint32_t offset = 100 + frame;
            const auto constants = device.UploadConstants(&offset, sizeof(offset));
            require(constants.IsValid(), "Frame constant allocation failed.");
            RHIBindingTable bindings{};
            const auto pass = graph->AddPass("frame-compute", {
                {input, RHIResourceState::ShaderResource, RGAccessMode::Read},
                {output, RHIResourceState::UnorderedAccess, RGAccessMode::Write}},
                [constants, input, &bindings, framePipeline](const auto& context)
                {
                    context.encoder->SetPipeline(RHIBindPoint::Compute, framePipeline);
                    context.encoder->SetConstantBuffer(RHIBindPoint::Compute, 0, constants);
                    context.encoder->SetRootBuffer(RHIBindPoint::Compute, 1,
                        {context.graph->ResolveBufferHandle(input), 0, 256});
                    context.encoder->SetBindings(RHIBindPoint::Compute, 2, bindings);
                    context.encoder->Dispatch(1, 1, 1);
                });
            graph->AddPass("frame-readback", {{output, RHIResourceState::CopySource, RGAccessMode::Read}},
                [output, storage](const auto& context)
                {
                    context.encoder->CopyBufferToReadback(storage->readback, context.graph->ResolveBufferHandle(output));
                }, true);
            require(graph->Compile(error), error);
            const auto view = RHIBindingDesc::UavBuffer(graph->ResolveBufferHandle(output), 64, 4);
            bindings = device.CreateBindings(std::span<const RHIBindingDesc>{&view, 1});
            require(bindings.IsValid(), "Frame descriptor allocation failed.");
            auto& prefixEncoder = device.GetImmediateEncoder();
            RHIBufferTransition prefixTransition{storage->prefixBuffer, RHIResourceState::Common, RHIResourceState::UnorderedAccess};
            RHIBarrierBatch prefixBarriers{};
            prefixBarriers.bufferTransitions = {&prefixTransition, 1};
            prefixEncoder.ResourceBarriers(prefixBarriers);
            prefixEncoder.SetPipeline(RHIBindPoint::Compute, producerPipeline);
            const auto prefixView = RHIBindingDesc::UavBuffer(storage->prefixBuffer, 64, 4);
            const auto prefixBindings = device.CreateBindings(std::span<const RHIBindingDesc>{&prefixView, 1});
            require(prefixBindings.IsValid(), "Prefix descriptor allocation failed.");
            prefixEncoder.SetBindings(RHIBindPoint::Compute, 0, prefixBindings);
            prefixEncoder.Dispatch(1, 1, 1);
            prefixTransition = {storage->prefixBuffer, RHIResourceState::UnorderedAccess, RHIResourceState::Common};
            prefixEncoder.ResourceBarriers(prefixBarriers);
            const auto recording = device.GetCurrentUploadRecordingId();
            require(device.BeginQueueFrame(frameGraphics.queue, error) &&
                device.GetCurrentUploadRecordingId() == recording, "Prefix retired graph constants/descriptors.");
            require(!device.EndFrame(error) && !device.BeginFrame(error) && !device.FlushCommandList(error),
                "Unjoined queue frame allowed retirement/reset.");
            EnhancedRenderGraph::QueueExecution execution;
            require(graph->SubmitQueues(graph, recorder, frameGraphics, &frameCompute,
                {{pass, true, 100}}, 100, storage, execution, error), error);
            require(!device.JoinQueueFrame(foreignCompletion, error), "Foreign service completion joined a frame.");
            require(device.JoinQueueFrame(execution.completion, error) && device.EndFrame(error), error);
            if (frame == 0)
            {
                firstFrameFence = device.GetLastSignaledFenceValue();
            }
            frameGraphs.push_back(graph);
            frameStorage.push_back(std::move(storage));
        }
        require(device.GetCompletedFenceValue() < firstFrameFence &&
            device.GetUploadStats().pendingSegments >= 3 && device.GetDescriptorRecycler().GetVersionStats().pending >= 3,
            "Primary frame fence retired blocked compute storage.");
        device.GetUploadAllocator().Collect(device.GetCompletedFenceValue());
        device.GetDescriptorRecycler().Collect(RHICompletionPoint{device.GetCompletedFenceValue()});
        require(device.GetUploadStats().pendingSegments >= 3 &&
            frameGraphics.queue->CollectCompleted() == 0 && frameCompute.queue->CollectCompleted() == 0,
            "Blocked frame storage was collected.");
        std::atomic<bool> gateOpened{false};
        std::jthread openGate([&]
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            gateOpened.store(true);
            frameGate->Signal(1);
        });
        require(device.BeginFrame(error) && gateOpened.load(), "Frame-slot reuse bypassed compute completion.");
        openGate.join();
        device.AbortFrame();
        require(device.DrainForLifecycle(RHILifecycleCommand::OfflineReadbackCapture, error), error);
        frameGraphics.queue->CollectCompleted();
        frameCompute.queue->CollectCompleted();
        for (uint32_t frame = 0; frame < frameStorage.size(); ++frame)
        {
            RHIReadbackImage image;
            require(device.MapReadback(frameStorage[frame]->readback, image, error), error);
            bool correct = image.ElementCount<uint32_t>() == 64;
            for (uint32_t index = 0; correct && index < 64; ++index)
            {
                correct = image.Elements<uint32_t>()[index] == index + 111 + frame;
            }
            require(correct && frameGraphs[frame].expired(), "Frame upload payload or retirement differs.");
        }
        frameStorage.clear();
        require(device.BeginFrame(error) && device.BeginQueueFrame(frameGraphics.queue, error), error);
        require(device.EnqueueQueueTestGate(frameGraphics.queue, frameGate.Get(), 2, error), error);
        auto abortGraph = std::make_shared<EnhancedRenderGraph>(device, RGSchedulingMode::ExplicitVersioned);
        abortGraph->AddPass("abort-pending-frame", {}, nullptr, true);
        require(abortGraph->Compile(error), error);
        EnhancedRenderGraph::QueueExecution abortExecution;
        require(abortGraph->SubmitQueues(abortGraph, recorder, frameGraphics, &frameCompute,
            {}, 100, owner, abortExecution, error), error);
        std::weak_ptr<EnhancedRenderGraph> abortWeak = abortGraph;
        abortGraph.reset();
        gateOpened.store(false);
        std::jthread openAbortGate([&]
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            gateOpened.store(true);
            frameGate->Signal(2);
        });
        device.AbortFrame();
        openAbortGate.join();
        require(gateOpened.load() && abortWeak.expired() && !device.QueryQueueCapabilities().graphics,
            "Abort released queue-owned storage without drain/revocation.");
        require(device.BeginFrame(error), "Legacy frame did not recover after queue-frame abort: " + error);
        device.AbortFrame();
        std::string validation;
        require(device.DrainDebugMessages(validation) == 0, validation);
        constexpr uint32_t kExpectedChecks = 361;
        if (checks != kExpectedChecks)
        {
            throw std::runtime_error("Queue execution acceptance check count changed.");
        }
        outLog = "RG8_QUEUE_EXECUTION_OK schema=2 checks=" + std::to_string(checks) +
            " payloadError=0 validationErrors=0 modes=2 overlapCases=7 positiveOverlapExecution=true" +
            " negativeFallbackExecution=true declarationOrder=true readRead=true reorderedFailure=true" +
            " delayedLifetime=true quarantineReleased=true frameRetirement=true measuredOverlap=false\n";
        return true;
    }
    catch (const std::exception& exception)
    {
        outLog = std::string("RG8_QUEUE_EXECUTION_FAILED: ") + exception.what() + "\n";
        return false;
    }
}
