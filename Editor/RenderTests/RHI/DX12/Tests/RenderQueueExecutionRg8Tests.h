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
#include "RHI/DX12/DX12GpuProfiler.h"

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
            RHIReadback readback;
            RHITextureHandle texture;
            RHIBufferHandle prefixBuffer;
            RHIResourceState state{RHIResourceState::Common};
            ~Storage()
            {
                device->ReleaseReadback(readback);
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
            const auto measuredHints = graph->MeasuredQueueHints([](const std::string& name)
            {
                return name == "transform" ? uint64_t{100} : uint64_t{0};
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
        outLog = "RG8_QUEUE_EXECUTION_OK checks=" + std::to_string(checks) +
            " payloadError=0 validationErrors=0 modes=2 delayedLifetime=true quarantineReleased=true frameRetirement=true\n";
        return true;
    }
    catch (const std::exception& exception)
    {
        outLog = std::string("RG8_QUEUE_EXECUTION_FAILED: ") + exception.what() + "\n";
        return false;
    }
}
