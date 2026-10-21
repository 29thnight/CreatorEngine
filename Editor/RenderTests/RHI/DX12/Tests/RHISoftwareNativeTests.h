#pragma once
#include "RHI/DX12/DX12CommandTranslator.h"
#include "RHI/RHISoftwareRecorder.h"
#include "RHI/RHISoftwareQueueRecorder.h"
#include "RHI/DX12/DX12QueueRecorder.h"
#include "RHI/DX12/DX12QueueService.h"
#include "RenderArena.h"
#include "Render/Graph/EnhancedRenderGraph.h"
#include "Render/Core/EnhancedLivePipelineDesc.h"
#include "JobScheduler.h"
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
            std::string blackboardLog;
            require(LivePipelineDesc::RunSelfTest(blackboardLog), blackboardLog);
            log += blackboardLog;
            require(resources.Initialize(8, 8, error), error);
            {
                // Declaration-only handles exercise full generation identity and
                // probe collisions without allocating thousands of GPU objects.
                EnhancedRenderGraph graph(resources, RGSchedulingMode::ExplicitVersioned);
                for (uint32_t iteration = 0; iteration < 3; ++iteration)
                {
                    std::array<RGHandle, 2048> imported;
                    for (uint32_t index = 0; index < imported.size(); ++index)
                    {
                        const uint32_t id = RHIHandleBits::Encode(index % 128, index / 128 + iteration * 32);
                        imported[index] = graph.ImportBuffer({id}, RHIResourceState::Common, "IndexProbe");
                        const auto duplicate = graph.ImportBuffer({id}, RHIResourceState::Common, "Duplicate");
                        const auto texture = graph.ImportTexture({id}, RHIResourceState::Common, "TextureProbe");
                        require(imported[index].IsValid() && duplicate.index != imported[index].index &&
                                    graph.FindImportedBuffer({id}).index == imported[index].index &&
                                    graph.FindImportedTexture({id}).index == texture.index,
                                "Import index lost first declaration, full generation or resource kind.");
                    }
                    for (uint32_t index = 0; index < imported.size(); ++index)
                    {
                        const uint32_t id = RHIHandleBits::Encode(index % 128, index / 128 + iteration * 32);
                        require(graph.FindImportedBuffer({id}).index == imported[index].index,
                                "Import index rehash lost an earlier entry.");
                    }
                    const auto output = graph.Write(imported.front());
                    std::array<EnhancedRenderGraph::RGPassUsage, 1> uses{
                        {{output, RHIResourceState::UnorderedAccess, RGAccessMode::Write}}};
                    graph.AddPass("UsageCopyProbe", std::span{uses}, nullptr, true);
                    uses.front().handle = {};
                    // Grow multiple arena blocks while the first pass remains live.
                    for (size_t index = 0; index < 4096; ++index)
                    {
                        graph.AddPass("UsageGrowthProbe",
                                      {{output, RHIResourceState::ShaderResource, RGAccessMode::Read}}, nullptr);
                    }
                    require(graph.HasDeclaredWriter(output, "UsageCopyProbe"),
                            "Pass usages borrowed caller storage or moved after arena growth.");
                    graph.Reset();
                    require(!graph.FindImportedBuffer({RHIHandleBits::Encode(0, iteration * 32)}).IsValid() &&
                                !graph.HasDeclaredWriter(output, "UsageCopyProbe"),
                            "Graph reset retained a previous import or usage epoch.");
                }
            }
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

            {
                EnhancedRenderGraph serial(resources, RGSchedulingMode::ExplicitVersioned);
                EnhancedRenderGraph packets(resources, RGSchedulingMode::ExplicitVersioned);
                const auto declare = [&](EnhancedRenderGraph& graph, bool packetMode, size_t count)
                {
                    auto value = graph.ImportTexture(texture, RHIResourceState::RenderTarget, "PacketInput");
                    graph.AddPass("BeforePacketCollection",
                                  {{value, RHIResourceState::ShaderResource, RGAccessMode::Read}}, nullptr, true);
                    if (packetMode)
                    {
                        graph.BeginDeclarationPackets();
                    }
                    RGTextureDesc setup;
                    setup.width = setup.height = 4;
                    setup.name = "PacketSetup";
                    require(graph.CreateTexture(setup).IsValid(), "Packet setup reservation failed.");
                    for (size_t index = 0; index < count; ++index)
                    {
                        if (packetMode)
                        {
                            graph.NextDeclarationPacket();
                        }
                        std::string name = index % 2 == 0
                                               ? "P" + std::to_string(index)
                                               : "OwnedPacket" + std::to_string(index) + std::string(128, 'x');
                        RGTextureDesc unused;
                        unused.width = unused.height = 4;
                        unused.name = name;
                        require(graph.CreateTexture(unused).IsValid(), "Packet transient reservation failed.");
                        value = graph.Modify(value);
                        require(value.IsValid() && value.version == index + 1,
                                "Packet version reservation changed the producer chain.");
                        std::array<EnhancedRenderGraph::RGPassUsage, 1> usages{
                            {{value, RHIResourceState::RenderTarget, RGAccessMode::ReadWrite}}};
                        if (index % 3 == 0)
                        {
                            std::string phaseName = name + ".Phase";
                            const std::array<EnhancedRenderGraph::RepeatedPhaseView, 1> phases{{{phaseName, usages}}};
                            graph.AddRepeatedPassView(
                                name, usages, phases, 2,
                                [](const EnhancedRenderGraph::ExecuteContext&, uint32_t, uint32_t) {}, true);
                            phaseName.assign(256, 'z');
                        }
                        else if (index % 3 == 1)
                        {
                            graph.AddSplitPass(name, {{value, RHIResourceState::RenderTarget, RGAccessMode::ReadWrite}},
                                               nullptr, 2, true);
                        }
                        else
                        {
                            graph.AddPass(name, std::span{usages}, nullptr, true);
                        }
                        require(graph.HasDeclaredWriter(value, name), "Pending packet writer lookup failed.");
                        usages.front().handle = {};
                        name.assign(256, 'z');
                        unused.name.assign(256, 'z');
                    }
                    if (packetMode)
                    {
                        require(!graph.Compile(error), "Unmerged declaration packets were compiled.");
                        error.clear();
                        graph.FinishDeclarationPackets();
                        require(graph.DeclarationPacketCount() == count + 1, "Declaration packet boundaries changed.");
                        require(graph.DeclarationPacketJobs() <= 1,
                                "Small declaration payload unexpectedly dispatched workers.");
                    }
                    graph.AddPass("AfterPacketCollection",
                                  {{value, RHIResourceState::ShaderResource, RGAccessMode::Read}}, nullptr, true);
                    std::vector<EnhancedRenderGraph::RGPassUsage> movedUsages{
                        {value, RHIResourceState::ShaderResource, RGAccessMode::Read}};
                    const auto movedPass = graph.AddPass("MovedUsagePayload", std::move(movedUsages), nullptr, true);
                    require(movedUsages.empty(), "Rvalue usage input was not consumed.");
                    movedUsages.resize(1);
                    movedUsages.front().handle = {};
                    require(graph.Compile(error), error);
                    EnhancedRenderGraph::DiagnosticSnapshot snapshot;
                    require(graph.CaptureDiagnosticSnapshot(snapshot), "Cannot inspect graph-owned usage storage.");
                    const auto stored = std::find_if(snapshot.passes.begin(), snapshot.passes.end(),
                        [&](const auto& pass) { return pass.authoredIndex == movedPass.index; });
                    require(stored != snapshot.passes.end() && stored->usages.size() == 1 &&
                                stored->usages[0].resource == value.index && stored->usages[0].version == value.version &&
                                stored->usages[0].kind == value.kind && stored->usages[0].access == RGAccessMode::Read,
                            "Reusing the caller changed graph-owned usage storage.");
                };
                for (size_t epoch = 0; epoch < 3; ++epoch)
                {
                    const size_t count = epoch == 0 ? 24 : 600;
                    declare(serial, false, count);
                    declare(packets, true, count);
                    EnhancedRenderGraph::DiagnosticSnapshot left, right;
                    require(serial.CaptureDiagnosticSnapshot(left) && packets.CaptureDiagnosticSnapshot(right),
                            "Declaration packet diagnostic snapshot failed.");
                    require(left.dependencyHash == right.dependencyHash && left.executeOrder == right.executeOrder &&
                                left.passes.size() == right.passes.size() &&
                                left.resources.size() == right.resources.size(),
                            "Packet merge changed declaration ordering or dependency structure.");
                    for (size_t index = 0; index < left.passes.size(); ++index)
                    {
                        require(left.passes[index].name == right.passes[index].name &&
                                    left.passes[index].phases.size() == right.passes[index].phases.size(),
                                "Packet names or repeated phases borrowed overwritten input storage.");
                    }
                    for (size_t index = 0; index < left.resources.size(); ++index)
                    {
                        require(left.resources[index].name == right.resources[index].name,
                                "Packet resource names changed after caller mutation.");
                    }
                    serial.Reset();
                    packets.Reset();
                }
                packets.BeginDeclarationPackets();
                for (size_t index = 0; index < 16; ++index)
                {
                    packets.AddPass("CancelledPacket", {}, nullptr);
                    packets.NextDeclarationPacket();
                }
                packets.CancelDeclarationPackets();
                require(!packets.Compile(error), "Cancelled declaration packets were compiled.");
                error.clear();
                packets.Reset();
                // Small/reused metadata correctly stays inline. Exercise worker
                // dispatch with independent payload ranges above its cost gate.
                auto value = packets.ImportTexture(texture, RHIResourceState::RenderTarget, "LargePacketInput");
                const std::string largePhaseName(2 * 1024 * 1024, 'p');
                packets.BeginDeclarationPackets();
                for (size_t index = 0; index < 8; ++index)
                {
                    packets.NextDeclarationPacket();
                    value = packets.Modify(value);
                    const std::array<EnhancedRenderGraph::RGPassUsage, 1> usages{
                        {{value, RHIResourceState::RenderTarget, RGAccessMode::ReadWrite}}};
                    const std::array<EnhancedRenderGraph::RepeatedPhaseView, 1> phases{{{largePhaseName, usages}}};
                    packets.AddRepeatedPassView("LargePacket", usages, phases, 2,
                        [](const EnhancedRenderGraph::ExecuteContext&, uint32_t, uint32_t) {}, true);
                }
                packets.FinishDeclarationPackets();
                if (ce::get_job_scheduler().is_running() && ce::get_thread_pool().size() > 1)
                {
                    require(packets.DeclarationPacketJobs() > 1, "Large declaration payload did not dispatch workers.");
                }
                require(packets.Compile(error), error);
            }

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
            auto sourceDesc = textureDesc;
            sourceDesc.clearColor[0] = 1;
            sourceDesc.clearColor[1] = sourceDesc.clearColor[2] = 0;
            require(resources.CreateTexture(sourceDesc, other, error), error);
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
            resources.AbortFrame();
            require(pool.Initialize(resources, 2, 3, error), error);
            require(resources.BeginFrame(error), error);
            pool.BeginFrame(0);
            require(pool.Prepare(error), error);
            const std::array<RHITextureHandle, 1> sourceTargets{other};
            const auto parallelBinding =
                resources.CreateRenderTargets(std::span<const RHITextureHandle>{sourceTargets}, nullptr);
            require(parallelBinding.IsValid(), "Parallel translation target creation failed.");
            recordingDesc.targetCount = 2;
            batchDesc.frameId = 2;
            batchDesc.backendGeneration = GetRHISubmissionThread().GetOwnerGeneration(&resources);
            batchDesc.uploadRecordingId = resources.GetCurrentUploadRecordingId();
            batchDesc.descriptorVersion = resources.GetDescriptorVersionToken();
            batchDesc.lifetimeToken = std::make_shared<int>(2);
            require(recorder.BeginRecording(recordingDesc, error), error);
            auto& first = recorder.AcquireEncoder(1);
            first.BindRenderTargets(parallelBinding);
            const std::array<float, 4> parallelExpected{1, 0, 0, 1};
            first.ClearRenderTargets(parallelBinding, parallelExpected.data());
            const std::array<RHITransition, 1> sourceTransition{
                RHITransition{other, RHIResourceState::RenderTarget, RHIResourceState::CopySource}};
            barriers.textureTransitions = sourceTransition;
            first.ResourceBarriers(barriers);
            auto& last = recorder.AcquireEncoder(0);
            const std::array<RHITransition, 1> toCopyDest{
                RHITransition{texture, RHIResourceState::CopySource, RHIResourceState::CopyDest}};
            barriers.textureTransitions = toCopyDest;
            last.ResourceBarriers(barriers);
            last.CopyResource(texture, other);
            const std::array<RHITransition, 1> toCopySource{
                RHITransition{texture, RHIResourceState::CopyDest, RHIResourceState::CopySource}};
            barriers.textureTransitions = toCopySource;
            last.ResourceBarriers(barriers);
            last.CopyToReadback(readback, texture, 0, 0);
            const std::array<uint32_t, 2> reversedOrder{1, 0};
            require(recorder.FinishRecording(reversedOrder, batchDesc, software, error), error);
            require(pool.CreateTranslationPool(frozen, error), error);
            DX12CommandTranslator parallelTranslator(*frozen, resources, primary->GetIdentity());
            const RHICommandTranslationOptions parallelOptions{RHICommandTranslationMode::Parallel, 2, 0};
            require(parallelTranslator.Translate(std::move(software), parallelOptions, translated, failure),
                    failure.message);
            require(translated.GetTranslationStats().translationJobCount == 2 &&
                        translated.GetTranslationStats().commandCount == 7,
                    "Parallel translation did not run both targets exactly once.");
            releaseTranslation = std::make_shared<std::promise<void>>();
            auto notificationGate = releaseTranslation->get_future().share();
            require(GetRHISubmissionThread().Enqueue(
                        &resources, "parallel submission gate",
                        [notificationGate](std::string&)
                        {
                            notificationGate.wait();
                            return true;
                        },
                        gateTicket, error),
                    error);
            translationBlocked = true;
            require(GetRHISubmissionThread().EnqueueTranslatedBatch(&resources, resources, std::move(translated),
                                                                    ticket, error),
                    error);
            require(resources.EndFrame(error), error);
            require(!ticket.IsComplete(), "Parallel native submission blocked its producer.");
            releaseTranslation->set_value();
            translationBlocked = false;
            require(resources.WaitForLastFrameSubmission(error), error);
            resources.WaitForGpu();
            require(resources.MapReadback(readback, image, error), error);
            for (uint32_t y = 0; y < 8; ++y)
            {
                for (uint32_t x = 0; x < 8; ++x)
                {
                    for (uint32_t channel = 0; channel < 4; ++channel)
                    {
                        if (std::abs(image.At(x, y, channel) - parallelExpected[channel]) > 1.0f / 255.0f)
                        {
                            require(false, "Parallel replay pixel mismatch: channel=" + std::to_string(channel) +
                                               " actual=" + std::to_string(image.At(x, y, channel)) +
                                               " expected=" + std::to_string(parallelExpected[channel]));
                        }
                    }
                }
            }
            require(resources.DrainDebugMessages(validation) == 0, validation);
            frozen.reset();
            pool.Shutdown();
            {
                auto nativeRecorder = std::make_shared<DX12QueueRecorder>(resources, 2);
                auto ownedValidator = std::make_shared<SoftwareNativeProbeValidator>();
                RHISoftwareQueueOptions ownedOptions;
                ownedOptions.translation = {RHICommandTranslationMode::Parallel, 2, 0};
                RHISoftwareQueueRecorder ownedRecorder(nativeRecorder, ownedValidator, ownedOptions);
                std::shared_ptr<IRHICommandQueue> ownedGraphics;
                require(ownedRecorder.WrapQueue(primary, ownedGraphics, error) &&
                            ownedGraphics->GetIdentity() == primary->GetIdentity(),
                        "Software owned queue changed the native queue identity.");
                RHISoftwareQueueFactory ownedFactory(resources, ownedRecorder);
                std::shared_ptr<IRHICommandQueue> ownedCompute;
                require(ownedFactory.CreateQueue(RHIQueueKind::Compute, ownedCompute, error), error);
                std::array<RHITextureHandle, 2> ownedTextures;
                for (uint32_t index = 0; index < ownedTextures.size(); ++index)
                {
                    auto ownedTextureDesc = textureDesc;
                    ownedTextureDesc.initialState = RHIResourceState::CopySource;
                    ownedTextureDesc.clearColor[0] = 0;
                    ownedTextureDesc.clearColor[1] = index == 0 ? 1.0f : 0.0f;
                    ownedTextureDesc.clearColor[2] = index == 0 ? 0.0f : 1.0f;
                    require(resources.CreateTexture(ownedTextureDesc, ownedTextures[index], error), error);
                }
                for (uint32_t iteration = 0; iteration < 2; ++iteration)
                {
                    const auto ownedTexture = ownedTextures[iteration];
                    const std::array ownedTargets{ownedTexture};
                    require(resources.BeginFrame(error) && resources.BeginQueueFrame(primary, error), error);
                    RHISoftwareBatchDesc ownedDesc;
                    ownedDesc.frameId = 3 + iteration;
                    ownedDesc.backendGeneration = GetRHISubmissionThread().GetOwnerGeneration(&resources);
                    ownedRecorder.SetFrameDesc(ownedDesc);
                    const auto beforeReuse = DX12QueueService::QueryRecordingPool(primary);
                    const auto ownedBinding =
                        resources.CreateRenderTargets(std::span<const RHITextureHandle>{ownedTargets}, nullptr);
                    require(ownedBinding.IsValid(), "Owned software target creation failed.");
                    auto ownedLifetime = std::make_shared<int>(3 + iteration);
                    std::weak_ptr<int> pendingOwned = ownedLifetime;
                    std::shared_ptr<IRHIQueueRecording> ownedRecording;
                    require(ownedRecorder.BeginRecording(ownedGraphics, 2, ownedLifetime, ownedRecording, error),
                            error);
                    auto& clear = ownedRecording->AcquireEncoder(0);
                    const std::array<RHITransition, 1> toRenderTarget{
                        RHITransition{ownedTexture, RHIResourceState::CopySource, RHIResourceState::RenderTarget}};
                    barriers.textureTransitions = toRenderTarget;
                    clear.ResourceBarriers(barriers);
                    clear.BindRenderTargets(ownedBinding);
                    const std::array<float, 4> ownedExpected{0, iteration == 0 ? 1.0f : 0.0f,
                                                             iteration == 0 ? 0.0f : 1.0f, 1};
                    clear.ClearRenderTargets(ownedBinding, ownedExpected.data());
                    auto& copy = ownedRecording->AcquireEncoder(1);
                    const std::array<RHITransition, 1> ownedToCopySource{
                        RHITransition{ownedTexture, RHIResourceState::RenderTarget, RHIResourceState::CopySource}};
                    barriers.textureTransitions = ownedToCopySource;
                    copy.ResourceBarriers(barriers);
                    copy.CopyToReadback(readback, ownedTexture, 0, 0);
                    std::shared_ptr<IRHIQueueCommandBatch> ownedBatch;
                    require(ownedRecording->Finish(ownedBatch, error), error);
                    ownedRecording.reset();
                    ownedLifetime.reset();
                    const uint64_t graphicsSignal = 2ull * iteration + 1;
                    RHITimelinePoint ownedCompletion;
                    require(ownedGraphics->Submit(ownedBatch, graphicsSignal, ownedCompletion, error) &&
                                ownedCompletion.IsValid(),
                            error);
                    require(!pendingOwned.expired(), "Owned native batch lost its GPU lifetime after CPU translation.");
                    RHITimelinePoint repeated;
                    require(!ownedGraphics->Submit(ownedBatch, graphicsSignal + 1, repeated, error) &&
                                !repeated.IsValid(),
                            "Owned software batch was submitted twice or published an unissued signal.");
                    ownedBatch.reset();

                    // Native cross-queue waits/signals use the actual backend timeline.
                    // The second frame also verifies post-retirement encoder/list reuse.
                    RHITimelinePoint computeCompletion;
                    RHITimelinePoint joinedCompletion;
                    require(ownedCompute->Wait(ownedCompletion, error) &&
                                ownedCompute->Signal(iteration + 1, computeCompletion, error) &&
                                computeCompletion.IsValid() && ownedGraphics->Wait(computeCompletion, error) &&
                                ownedGraphics->Signal(graphicsSignal + 1, joinedCompletion, error),
                            error);
                    require(resources.JoinQueueFrame(joinedCompletion, error) && resources.EndFrame(error) &&
                                resources.WaitForLastFrameSubmission(error),
                            error);
                    resources.WaitForGpu();
                    ownedGraphics->CollectCompleted();
                    ownedCompute->CollectCompleted();
                    require(pendingOwned.expired(), "Owned native completion retained the software recording owner.");
                    const auto afterReuse = DX12QueueService::QueryRecordingPool(primary);
                    require(afterReuse.leased == 0,
                            "GPU-complete owned targets were not returned to their native pool.");
                    if (iteration != 0)
                    {
                        require(afterReuse.created == beforeReuse.created &&
                                    afterReuse.reused >= beforeReuse.reused + 2,
                                "Warm owned software translation recreated native target storage.");
                    }
                    require(resources.MapReadback(readback, image, error), error);
                    for (uint32_t y = 0; y < 8; ++y)
                    {
                        for (uint32_t x = 0; x < 8; ++x)
                        {
                            for (uint32_t channel = 0; channel < 4; ++channel)
                            {
                                require(std::abs(image.At(x, y, channel) - ownedExpected[channel]) <= 1.0f / 255.0f,
                                        "Owned software translation changed native target order or pixels.");
                            }
                        }
                    }
                    require(resources.DrainDebugMessages(validation) == 0, validation);
                }
                for (const auto ownedTexture : ownedTextures)
                {
                    resources.ReleaseTexture(ownedTexture);
                }
            }
            {
                ce::RenderArenaPool pendingPool;
                std::weak_ptr<int> weak;
                int executions = 0;
                const void* address = nullptr;
                {
                    DX12PendingCommands pending(pendingPool);
                    std::array<uint32_t, 3> input{7, 11, 19};
                    const auto copy = pending.Copy<uint32_t>(input);
                    address = copy.data();
                    auto owner = std::make_shared<int>(41);
                    weak = owner;
                    pending.Add([owner, copy, &executions](ID3D12GraphicsCommandList*)
                    {
                        if (*owner != 41 || copy[0] != 7 || copy[2] != 19)
                        {
                            throw std::runtime_error("Pending payload lost its owning copy.");
                        }
                        ++executions;
                    });
                    owner.reset();
                    input.fill(0);
                    require(!weak.expired(), "Pending command released its resource before retirement.");
                    pending.Execute(reinterpret_cast<ID3D12GraphicsCommandList*>(uintptr_t{1}));
                    bool rejected = false;
                    try
                    {
                        pending.Execute(reinterpret_cast<ID3D12GraphicsCommandList*>(uintptr_t{1}));
                    }
                    catch (const std::logic_error&)
                    {
                        rejected = true;
                    }
                    require(rejected && executions == 1, "Pending command executed more than once.");
                }
                require(weak.expired(), "Retired pending payload retained its owner.");
                DX12PendingCommands reused(pendingPool);
                const std::array<uint32_t, 3> input{1, 2, 3};
                require(reused.Copy<uint32_t>(input).data() == address, "Pending arena storage was not reused.");
            }
            {
                DX12ResourceTable table;
                Microsoft::WRL::ComPtr<ID3D12Resource> pinned = resources.Resolve(texture);
                const auto original = table.AddTexture(pinned);
                auto frozen = table.CaptureRecordingSnapshot();
                table.Release(original);
                const auto replacement = table.AddTexture(pinned);
                require(original != replacement && !table.Resolve(original) &&
                            frozen->Resolve(original) == pinned.Get() && !frozen->Resolve(replacement),
                        "Pending resource snapshot followed a released handle into its replacement generation.");
                table.Release(replacement);
            }
            resources.SetDeferredImmediateRecording(true);
            require(resources.BeginFrame(error), error);
            RHISparseBufferDesc refusedAllocation;
            RHIBufferHandle refusedHandle;
            require(!resources.CreateSparseBuffer(refusedAllocation, refusedHandle, error) &&
                        !GetRHISubmissionThread().GetOwnerStats(&resources).faulted,
                    "A refused resource allocation poisoned the RHI submission owner.");
            error.clear();
            const std::array<RHITextureHandle, 1> deferredTargets{texture};
            const auto deferredBinding = resources.CreateRenderTargets(deferredTargets, nullptr);
            require(deferredBinding.IsValid(), "Deferred target reservation failed.");
            const std::array<float, 4> deferredColor{0.25f, 0.5f, 0.75f, 1.0f};
            auto& deferredEncoder = resources.GetImmediateEncoder();
            const std::array<RHITransition, 1> toTarget{
                RHITransition{texture, RHIResourceState::CopySource, RHIResourceState::RenderTarget}};
            RHIBarrierBatch beforeClear;
            beforeClear.textureTransitions = toTarget;
            deferredEncoder.ResourceBarriers(beforeClear);
            deferredEncoder.ClearRenderTargets(deferredBinding, deferredColor.data());
            const std::array<RHITransition, 1> toCopy{
                RHITransition{texture, RHIResourceState::RenderTarget, RHIResourceState::CopySource}};
            RHIBarrierBatch afterClear;
            afterClear.textureTransitions = toCopy;
            deferredEncoder.ResourceBarriers(afterClear);
            deferredEncoder.CopyToReadback(readback, texture);
            require(resources.EndFrame(error), error);
            resources.WaitForGpu();
            RHIReadbackImage deferredImage;
            require(resources.MapReadback(readback, deferredImage, error), error);
            for (uint32_t channel = 0; channel < 4; ++channel)
            {
                require(std::abs(deferredImage.At(0, 0, channel) - deferredColor[channel]) <= 1.0f / 255.0f,
                        "RHI immediate translation changed target pixels.");
            }
            require(resources.DrainDebugMessages(validation) == 0, validation);
            log += "RHI_PENDING_NATIVE_OK arenaReuse=true resourceCapture=true oneShot=true pixels=true\n";
            resources.ReleaseTexture(other);
            resources.ReleaseReadback(readback);
            resources.ReleaseTexture(texture);
            resources.Shutdown();
            log += "RHI_SOFTWARE_NATIVE_OK commands=4 pixels=64 channels=4 staleRejected=true asynchronous=true "
                   "frozenTargets=true uploadRollover=true poolReuse=true arenaRetirement=true gpuMergeReuse=true "
                   "declarationMetadata=true parallelTranslation=true declarationPackets=true retainedBlackboard=true "
                   "singleCopyPassUsages=true ownedQueue=true ownedPoolReuse=true multiqueueTimeline=true "
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
