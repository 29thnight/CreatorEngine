#pragma once
#include "RHI/DX12/DX12CommandTranslator.h"
#include "RHI/RHISoftwareRecorder.h"
#include "RenderArena.h"
#include "Render/Graph/EnhancedRenderGraph.h"
#include "RHI/DX12/DX12GpuProfiler.h"

namespace
{
    class SoftwareNativeProbeValidator final : public IRHISoftwareCommandValidator
    {
      public:
        // Only void clear/barrier/copy commands are used by this probe.
        bool Validate(RHISoftwareCommandType, const void*, size_t, const RHISoftwareEncoderState&) const override
        {
            return false;
        }
    };

    bool ValidateSoftwareNativeCommands(std::string& error, std::string& log)
    {
        DX12DeviceResources resources;
        DX12CommandListPool pool;
        RHISubmissionTicket ticket;
        auto releaseTranslation = std::make_shared<std::promise<void>>();
        bool translationBlocked = false;
        try
        {
            const auto require = [](bool accepted, const std::string& message)
            {
                if (!accepted)
                {
                    throw std::runtime_error(message);
                }
            };
            {
                DX12GpuProfiler mergeProfiler;
                DX12GpuProfiler::FrameTimings input;
                std::vector<DX12GpuProfiler::PassTiming> output;
                input.ticksPerSecond = 1000;
                input.slices.resize(128);
                for (size_t index = 0; index < input.slices.size(); ++index)
                {
                    auto& slice = input.slices[index];
                    slice.name = "MergePass" + std::to_string(index % 32);
                    slice.beginTicks = index * 10;
                    slice.endTicks = slice.beginTicks + 2;
                }
                mergeProfiler.MergeSlices(input, output);
                require(output.size() == 32 && output.front().name == "MergePass0(x4)" &&
                            output.front().milliseconds == 8.0 && output.front().spanMilliseconds == 962.0,
                        "Flat GPU timing merge changed grouping, summed duration or span.");
                const auto* retained = output.data();
                for (size_t iteration = 0; iteration < 1000; ++iteration)
                {
                    mergeProfiler.MergeSlices(input, output);
                }
                require(output.data() == retained && output.back().name == "MergePass31(x4)",
                        "Flat GPU timing merge did not retain output storage or first-seen order.");
                input.slices.clear();
                input.slices.shrink_to_fit();
                mergeProfiler.MergeSlices(input, output);
                require(output.empty(), "Flat GPU timing merge retained an earlier frame.");
                input.slices.resize(1);
                input.slices[0].name = "Replacement";
                input.slices[0].beginTicks = input.slices[0].endTicks = 3;
                mergeProfiler.MergeSlices(input, output);
                require(output.size() == 1 && output[0].name == "Replacement" && output[0].milliseconds == 0.0,
                        "Flat GPU timing merge reused a freed name or dropped a zero-length slice.");
            }
            require(resources.Initialize(8, 8, error), error);
            {
                EnhancedRenderGraph graph(resources);
                auto owner = std::make_shared<int>(41);
                std::weak_ptr<int> weak = owner;
                size_t calls{};
                auto callback = graph.StoreExecute(
                    [owner, &calls](const auto&)
                    {
                        if (*owner == 41)
                        {
                            ++calls;
                        }
                    });
                owner.reset();
                // Force multiple backing blocks after storing the first callback.
                for (size_t index = 0; index < 10000; ++index)
                {
                    graph.RetainDeclaration<std::array<uint64_t, 16>>();
                }
                callback(EnhancedRenderGraph::ExecuteContext{});
                require(calls == 1 && !weak.expired(), "Declaration arena lost a live callback capture.");
                callback = {};
                graph.Reset();
                require(weak.expired(), "Graph reset retained declaration callback owners.");
                auto last = std::make_shared<int>(53);
                weak = last;
                graph.StoreExecute([last](const auto&) {});
                last.reset();
                require(!weak.expired(), "Graph discarded declaration data before destruction.");
                graph.Reset();
                require(weak.expired(), "Repeated graph reset retained declaration owners.");
            }

            require(pool.Initialize(resources, 1, 3, error), error);
            std::shared_ptr<IRHICommandQueue> primary;
            require(resources.GetPrimaryGraphicsQueue(primary, error), error);
            require(resources.BeginFrame(error), error);
            pool.BeginFrame(0);
            require(pool.Prepare(error), error);

            RHITextureDesc textureDesc;
            textureDesc.width = 8;
            textureDesc.height = 8;
            textureDesc.format = RHIFormat::RGBA8Unorm;
            textureDesc.allowRenderTarget = true;
            textureDesc.initialState = RHIResourceState::RenderTarget;
            textureDesc.clearColor[0] = 0.25f;
            textureDesc.clearColor[1] = 0.5f;
            textureDesc.clearColor[2] = 0.75f;
            textureDesc.clearColor[3] = 1.0f;
            RHITextureHandle texture;
            RHIReadback readback;
            require(resources.CreateTexture(textureDesc, texture, error), error);
            require(resources.CreateReadback(8, 8, textureDesc.format, 1, readback, error), error);
            const std::array<RHITextureHandle, 1> targets{texture};
            const auto binding = resources.CreateRenderTargets(std::span<const RHITextureHandle>{targets}, nullptr);
            require(binding.IsValid(), "Software native probe render target creation failed.");

            SoftwareNativeProbeValidator validator;
            RHISoftwareRecorder recorder(validator);
            RHISoftwareRecordingDesc recordingDesc{primary->GetIdentity(), 1, 65536, 65536};
            RHISoftwareBatchDesc batchDesc;
            batchDesc.frameId = 1;
            batchDesc.queue = recordingDesc.queue;
            batchDesc.backendGeneration = GetRHISubmissionThread().GetOwnerGeneration(&resources);
            batchDesc.uploadRecordingId = resources.GetCurrentUploadRecordingId();
            batchDesc.descriptorVersion = resources.GetDescriptorVersionToken();
            ce::RenderArenaPool frameArenas;
            struct ArenaFrame
            {
                ce::RenderArenaLease storage;
                ce::render_arena_vector<uint32_t> drawInputs;
                explicit ArenaFrame(ce::RenderArenaLease lease)
                    : storage(std::move(lease)), drawInputs(&storage->Resource(0))
                {
                    drawInputs.assign(4096, 0x12345678);
                }
            };
            auto arenaFrame = std::make_shared<ArenaFrame>(frameArenas.Acquire());
            std::weak_ptr<ArenaFrame> pendingFrame = arenaFrame;
            auto* pendingArena = arenaFrame->storage.get();
            batchDesc.lifetimeToken = arenaFrame;
            const std::array<uint32_t, 1> order{0};
            DX12CommandTranslator translator(pool, resources, primary->GetIdentity());
            RHITranslatedBatch translated;
            RHICommandFailure failure;

            require(recorder.BeginRecording(recordingDesc, error), error);
            recorder.AcquireEncoder(0).ClearDepthTarget({}, 1.0f);
            auto staleDesc = batchDesc;
            staleDesc.descriptorVersion ^= UINT64_MAX;
            RHISoftwareBatch stale;
            require(recorder.FinishRecording(order, staleDesc, stale, error), error);
            require(!translator.Translate(std::move(stale), {}, translated, failure) && !stale.IsValid(),
                    "Stale software recording was translated or not consumed.");

            require(recorder.BeginRecording(recordingDesc, error), error);
            auto& encoder = recorder.AcquireEncoder(0);
            encoder.BindRenderTargets(binding);
            float color[4]{0.25f, 0.5f, 0.75f, 1.0f};
            encoder.ClearRenderTargets(binding, color);
            color[0] = color[1] = color[2] = color[3] = 0;
            const std::array<RHITransition, 1> transition{
                RHITransition{texture, RHIResourceState::RenderTarget, RHIResourceState::CopySource}};
            RHIBarrierBatch barriers;
            barriers.textureTransitions = transition;
            encoder.ResourceBarriers(barriers);
            encoder.CopyToReadback(readback, texture, 0, 0);
            RHISoftwareBatch software;
            require(recorder.FinishRecording(order, batchDesc, software, error), error);
            const auto upload = resources.AllocateUpload(RHIUploadRequest{256, RHIUploadUsage::ConstantBuffer, 1});
            require(upload.IsValid() && upload.cpuAddress, "Upload lifetime probe allocation failed.");
            std::memset(upload.cpuAddress, 0x5a, 256);
            std::shared_ptr<DX12CommandListPool> frozen;
            require(pool.CreateTranslationPool(frozen, error), error);
            auto asyncTranslator = std::make_shared<DX12CommandTranslator>(*frozen, resources, primary->GetIdentity());
            auto gate = releaseTranslation->get_future().share();
            RHISubmissionTicket gateTicket;
            require(GetRHISubmissionThread().Enqueue(
                        &resources, "software translation lifetime gate",
                        [gate](std::string&)
                        {
                            gate.wait();
                            return true;
                        },
                        gateTicket, error),
                    error);
            translationBlocked = true;
            require(GetRHISubmissionThread().EnqueueSoftwareBatch(&resources, resources, frozen, asyncTranslator,
                                                                  std::move(software), ticket, error),
                    error);
            require(!software.IsValid() && !ticket.IsComplete() && ticket.GetRecordedBatch() == nullptr,
                    "Software queue performed synchronous translation or published a fake native batch.");
            require(resources.GetDescriptorVersionToken() != batchDesc.descriptorVersion &&
                        resources.GetCurrentUploadRecordingId() != batchDesc.uploadRecordingId,
                    "Software admission did not advance the producer recording.");
            const auto nextUpload = resources.AllocateUpload(RHIUploadRequest{256, RHIUploadUsage::ConstantBuffer, 1});
            require(nextUpload.IsValid() && nextUpload.buffer != upload.buffer,
                    "Pending software upload segment was reused by the next recording.");
            require(static_cast<const uint8_t*>(upload.cpuAddress)[0] == 0x5a &&
                        static_cast<const uint8_t*>(upload.cpuAddress)[255] == 0x5a,
                    "Pending software upload contents changed.");
            batchDesc.lifetimeToken.reset();
            staleDesc.lifetimeToken.reset();
            arenaFrame.reset();
            auto overlappingArena = frameArenas.Acquire();
            require(!pendingFrame.expired() && overlappingArena.get() != pendingArena,
                    "Pending RHI frame arena was reset or reused before translation.");
            require(pendingFrame.lock()->drawInputs.back() == 0x12345678,
                    "Pending frame arena contents changed while RHI was gated.");
            RHITextureHandle other;
            require(resources.CreateTexture(textureDesc, other, error), error);
            resources.GetDevice()->CreateRenderTargetView(
                resources.Resolve(other), nullptr, resources.GetRtvViewHeap().CpuAt(DX12RtvIndexOf(binding.backend)));
            releaseTranslation->set_value();
            translationBlocked = false;
            const bool submitted = GetRHISubmissionThread().Wait(ticket, error);
            resources.WaitForGpu();
            require(submitted && !translated.IsValid(), error);
            RHIReadbackImage image;
            require(resources.MapReadback(readback, image, error), error);
            const std::array<float, 4> expected{0.25f, 0.5f, 0.75f, 1.0f};
            for (uint32_t y = 0; y < 8; ++y)
            {
                for (uint32_t x = 0; x < 8; ++x)
                {
                    for (uint32_t channel = 0; channel < 4; ++channel)
                    {
                        require(std::abs(image.At(x, y, channel) - expected[channel]) <= 1.0f / 255.0f,
                                "Software translated clear/readback pixel mismatch.");
                    }
                }
            }
            std::string validation;
            require(resources.HasDebugMessageQueue() && resources.DrainDebugMessages(validation) == 0, validation);
            std::shared_ptr<DX12CommandListPool> heldFork;
            require(pool.CreateTranslationPool(heldFork, error), error);
            require(heldFork.get() != frozen.get(),
                    "Translation pool cache reused a fork with external lifetime owners.");
            frozen.reset();
            asyncTranslator.reset();
            GetRHISubmissionThread().CollectCompletedLifetimes(&resources);
            require(pendingFrame.expired(), "Completed GPU retirement still owns the arena frame.");
            auto recycledArena = frameArenas.Acquire();
            require(recycledArena.get() == pendingArena && recycledArena->Resource(0).stats().resets > 0 &&
                        recycledArena->Resource(0).stats().usedBytes == 0,
                    "GPU-retired arena was not bulk reset and reused.");
            auto* cachedPool = heldFork.get();
            heldFork.reset();
            // Snapshot capture seals one producer recording. Reuse is tested
            // on the next recording, as in the product frame path.
            resources.AbortFrame();
            require(resources.BeginFrame(error), error);
            pool.BeginFrame(0);
            require(resources.CreateRenderTargets(std::span<const RHITextureHandle>{targets}, nullptr).IsValid(),
                    "Reuse frame target creation failed.");
            std::shared_ptr<DX12CommandListPool> reusedFork;
            require(pool.CreateTranslationPool(reusedFork, error), error);
            require(reusedFork.get() == cachedPool, "Exclusive retired translation fork was not reused.");
            uint64_t reusedUpload{};
            uint64_t reusedDescriptors{};
            require(reusedFork->GetTranslationRecording(reusedUpload, reusedDescriptors) &&
                        reusedUpload == resources.GetCurrentUploadRecordingId() &&
                        reusedDescriptors == resources.GetDescriptorVersionToken(),
                    "Reused translation fork retained the previous recording identity.");
            reusedFork.reset();
            pool.Shutdown();
            resources.ReleaseTexture(other);
            resources.ReleaseReadback(readback);
            resources.ReleaseTexture(texture);
            resources.Shutdown();
            log += "RHI_SOFTWARE_NATIVE_OK commands=4 pixels=64 channels=4 staleRejected=true asynchronous=true "
                   "frozenTargets=true uploadRollover=true poolReuse=true arenaRetirement=true gpuMergeReuse=true "
                   "validationErrors=0\n";
            return true;
        }
        catch (const std::exception& exception)
        {
            error = exception.what();
            if (translationBlocked)
            {
                releaseTranslation->set_value();
            }
            resources.WaitForGpu();
            pool.Shutdown();
            resources.Shutdown();
            return false;
        }
    }
} // namespace
