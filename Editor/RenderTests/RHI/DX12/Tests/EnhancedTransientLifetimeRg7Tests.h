#pragma once

namespace
{
    class Rg7PostSubmitPool final : public DX12CommandListPool
    {
    public:
        DX12DeviceResources* resources{nullptr};
        bool reportFailure{false};
        uint32_t currentSlot{0};
        void BeginFrame(uint32_t slot) override
        {
            currentSlot = slot;
            DX12CommandListPool::BeginFrame(slot);
        }
    private:
        bool SubmitRecordedCommands(uint32_t slot, std::span<const uint32_t> order,
            RHICompletionPoint completion, std::string& error) override
        {
            if (slot != currentSlot)
            {
                error = "RG7 test changed a command slot before CPU submission completed";
                return false;
            }
            std::vector<ID3D12CommandList*> lists;
            for (auto worker : order)
            {
                lists.push_back(Get(worker));
            }
            if (!resources->SubmitCommandLists(lists, completion, error))
            {
                return false;
            }
            if (reportFailure)
            {
                error = "RG7 injected error after native execution/signal admission";
                return false;
            }
            return true;
        }
    };

    bool RunRg7LifetimeTests(std::string& outLog)
    {
        for (uint32_t mode = 0; mode < 2; ++mode)
        {
            Rg7FailureServices resources;
            RGTransientPool cache;
            Rg7PostSubmitPool pool;
            Microsoft::WRL::ComPtr<ID3D12Fence> gate;
            std::array<std::array<RHIReadback, 4>, 3> readbacks{};
            std::array<std::array<RHIBufferHandle, 3>, 3> handles{};
            std::array<std::weak_ptr<EnhancedRenderGraph>, 3> retained;
            std::array<RHISubmissionTicket, 3> tickets;
            RHIBufferHandle history;
            RHIResourceState historyState = RHIResourceState::Common;
            int inspectorOwner = 0;
            bool inspectorRegistered = false;
            uint64_t memoryDomain = 0;
            std::string error;
            auto& submission = GetRHISubmissionThread();
            const auto cleanup = [&]
            {
                if (gate)
                {
                    gate->Signal(1); // Always unblock GPU before any drain or pool destruction.
                }
                if (resources.GetCurrentUploadRecordingId() != 0)
                {
                    resources.AbortFrame();
                }
                resources.DrainForLifecycle(RHILifecycleCommand::OfflineReadbackCapture, error);
                submission.CollectCompletedLifetimes(&resources);
                for (auto& view : readbacks)
                {
                    for (auto& readback : view)
                    {
                        resources.ReleaseReadback(readback);
                    }
                }
                cache.ClearAliasingCache();
                resources.ReleaseBuffer(history);
                history = {};
                pool.Shutdown();
                if (inspectorRegistered)
                {
                    submission.ReleaseClient(&inspectorOwner);
                    inspectorRegistered = false;
                }
            };
            try
            {
                if (!resources.Initialize(32, 32, error) ||
                    !pool.Initialize(resources, 2, 3, error) ||
                    FAILED(resources.GetDevice()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate))))
                {
                    throw std::runtime_error("RG7 lifetime initialization failed " + error);
                }
                pool.resources = &resources;
                pool.reportFailure = mode == 1;
                RHIBufferDesc historyDesc;
                historyDesc.bytes = 256;
                historyDesc.allowUnorderedAccess = true;
                if (!resources.CreateBuffer(historyDesc, history, error) ||
                    !submission.AcquireClient(&inspectorOwner, error))
                {
                    throw std::runtime_error(error);
                }
                inspectorRegistered = true;
                // GPU delay is a real queue wait, recorded only through the RHI thread.
                if (!submission.ExecuteAndWait(&resources, "RG7 GPU gate", [&resources, gate](std::string& taskError)
                {
                    if (FAILED(resources.GetCommandQueue()->Wait(gate.Get(), 1)))
                    {
                        taskError = "Cannot gate the isolated DX12 queue";
                        return false;
                    }
                    return true;
                }, error))
                {
                    throw std::runtime_error(error);
                }
                const uint32_t views = mode == 0 ? 3 : 1;
                for (uint32_t view = 0; view < views; ++view)
                {
                    if (!resources.BeginFrame(error))
                    {
                        throw std::runtime_error(error);
                    }
                    pool.BeginFrame(view);
                    auto graph = std::make_shared<EnhancedRenderGraph>(resources, RGSchedulingMode::ExplicitVersioned);
                    graph->SetTransientAliasing(true);
                    graph->SetTransientPool(&cache);
                    graph->SetParallelRecordCostThreshold(0);
                    if (!graph->PrepareParallel(pool, error))
                    {
                        throw std::runtime_error(error);
                    }
                    auto imported = graph->ImportBuffer(history, historyState, "persistent_history", &historyState);
                    if (view == 0)
                    {
                        imported = graph->Write(imported);
                        graph->AddPass("history_write", {{imported, RHIResourceState::UnorderedAccess, RGAccessMode::Write}},
                            [imported](const auto& context)
                        {
                            auto binding = RHIBindingDesc::UavBuffer(context.graph->ResolveBufferHandle(imported), 64, 0);
                            binding.format = RHIFormat::R32Float;
                            const float values[]{900.0f, 0, 0, 0};
                            context.encoder->ClearUnorderedAccess(binding, values);
                        });
                    }
                    std::array<RGHandle, 3> written;
                    for (uint32_t index = 0; index < 3; ++index)
                    {
                        if (!resources.CreateBufferReadback(256, readbacks[view][index], error))
                        {
                            throw std::runtime_error(error);
                        }
                        written[index] = graph->Write(graph->CreateBuffer({256, true, false,
                            index == 0 ? "late_reader_output" : "view_transient"}));
                        const auto resource = written[index];
                        const float value = float(100 * (view + 1) + index);
                        graph->AddPass("view_write", {{resource, RHIResourceState::UnorderedAccess, RGAccessMode::Write}},
                            [resource, value](const auto& context)
                        {
                            auto binding = RHIBindingDesc::UavBuffer(context.graph->ResolveBufferHandle(resource), 64, 0);
                            binding.format = RHIFormat::R32Float;
                            const float values[]{value, 0, 0, 0};
                            context.encoder->ClearUnorderedAccess(binding, values);
                        });
                        if (index != 0)
                        {
                            graph->AddPass("view_read", {{resource, RHIResourceState::CopySource, RGAccessMode::Read}},
                                [&, view, index, resource](const auto& context)
                            {
                                context.encoder->CopyBufferToReadback(readbacks[view][index],
                                    context.graph->ResolveBufferHandle(resource));
                            }, true);
                        }
                    }
                    if (!resources.CreateBufferReadback(256, readbacks[view][3], error))
                    {
                        throw std::runtime_error(error);
                    }
                    graph->AddPass("late_history_reader", {{written[0], RHIResourceState::CopySource, RGAccessMode::Read},
                        {written[2], RHIResourceState::CopySource, RGAccessMode::Read},
                        {imported, RHIResourceState::CopySource, RGAccessMode::Read}},
                        [&, view, late = written[0], imported](const auto& context)
                    {
                        context.encoder->CopyBufferToReadback(readbacks[view][0], context.graph->ResolveBufferHandle(late));
                        context.encoder->CopyBufferToReadback(readbacks[view][3], context.graph->ResolveBufferHandle(imported));
                    }, true);
                    if (!graph->Compile(error) || graph->GetStats().aliasReuseCount != 1)
                    {
                        throw std::runtime_error("RG7 late reader lifetime did not exclude sharing " + error);
                    }
                    EnhancedRenderGraph::DiagnosticSnapshot snapshot;
                    graph->CaptureDiagnosticSnapshot(snapshot);
                    if (snapshot.aliasHeapMemory.domainId == 0 ||
                        snapshot.aliasHeapMemory.domainId != cache.GetAliasHeapMemory().domainId)
                    {
                        throw std::runtime_error("RG7 frozen graph lost its shared memory domain");
                    }
                    if (snapshot.resources[imported.index].aliasGroup != UINT32_MAX ||
                        snapshot.resources[written[0].index].aliasGroup != UINT32_MAX)
                    {
                        throw std::runtime_error("RG7 history/late-reader entered shared heap");
                    }
                    for (uint32_t index = 0; index < 3; ++index)
                    {
                        handles[view][index] = graph->ResolveBufferHandle(written[index]);
                        for (uint32_t earlier = 0; earlier < view; ++earlier)
                        {
                            if (handles[view][index] == handles[earlier][index])
                            {
                                throw std::runtime_error("RG7 reused an in-flight view resource");
                            }
                        }
                    }
                    RHIRecordedBatchDesc desc;
                    desc.frameId = view;
                    desc.displayToken = view + 1;
                    desc.backendGeneration = submission.GetOwnerGeneration(&resources);
                    desc.lifetimeToken = graph;
                    RHIRecordedBatch batch;
                    if (!graph->RecordParallel(pool, 2, desc, batch, error) ||
                        !submission.EnqueueRecordedBatch(&resources, resources, std::move(batch), tickets[view], error))
                    {
                        throw std::runtime_error(error);
                    }
                    desc.lifetimeToken.reset();
                    retained[view] = graph;
                    graph.reset();
                    const bool cpuSubmitted = submission.Wait(tickets[view], error);
                    if ((mode == 0 && !cpuSubmitted) || (mode == 1 &&
                        (cpuSubmitted || error.find("RG7 injected error after native") == std::string::npos)))
                    {
                        throw std::runtime_error("RG7 CPU submit outcome mismatch " + error);
                    }
                    if (mode == 0)
                    {
                        if (!resources.EndFrame(error))
                        {
                            throw std::runtime_error(error);
                        }
                    }
                    else
                    {
                        resources.AbortFrame();
                    }
                    submission.CollectCompletedLifetimes(&resources);
                    if (retained[view].expired() || !cache.freeAliasedGroups.empty() ||
                        resources.GetCompletedFenceValue() >= resources.GetLastSignaledFenceValue())
                    {
                        throw std::runtime_error("RG7 GPU-gated graph retired before completion");
                    }
                    const auto memory = cache.GetAliasHeapMemory();
                    if (memory.domainId == 0 || (memoryDomain != 0 && memoryDomain != memory.domainId))
                    {
                        throw std::runtime_error("RG7 three views changed a shared accounting domain");
                    }
                    memoryDomain = memory.domainId;
                    if (memory.retainedBytes != uint64_t(view + 1) * 65536 || memory.cachedBytes != 0 ||
                        memory.leasedBytes != memory.retainedBytes || memory.retainedHeaps != view + 1 ||
                        memory.peakRetainedBytes != memory.retainedBytes)
                    {
                        throw std::runtime_error("RG7 delayed GPU heap bytes were released or counted twice");
                    }
                }
                if (submission.GetOwnerStats(&resources).pendingRetirements != views)
                {
                    throw std::runtime_error("RG7 lost a submitted view lifetime token");
                }
                if (FAILED(gate->Signal(1)))
                {
                    throw std::runtime_error("Cannot open RG7 GPU gate");
                }
                HANDLE completed = CreateEventW(nullptr, FALSE, FALSE, nullptr);
                const bool eventArmed = completed && resources.SignalEventOnFenceValue(resources.GetLastSignaledFenceValue(), completed);
                const bool gpuCompleted = eventArmed && WaitForSingleObject(completed, 10000) == WAIT_OBJECT_0;
                if (completed)
                {
                    CloseHandle(completed);
                }
                if (!gpuCompleted || !submission.ExecuteAndWait(&inspectorOwner, "RG7 retirement poll",
                    [](std::string&) { return true; }, error))
                {
                    throw std::runtime_error("RG7 GPU completion/poll failed " + error);
                }
                submission.CollectCompletedLifetimes(&resources);
                if (mode == 1 && (retained[0].expired() || !cache.freeAliasedGroups.empty() ||
                    !submission.GetOwnerStats(&resources).submissionBlocked))
                {
                    throw std::runtime_error("RG7 failed submission retired without verified GPU idle");
                }
                if (mode == 1 && cache.GetAliasHeapMemory().leasedBytes != 65536)
                {
                    throw std::runtime_error("RG7 failed native submission lost retained heap bytes");
                }
                if (!resources.DrainForLifecycle(RHILifecycleCommand::OfflineReadbackCapture, error) ||
                    !resources.GetLastLifecycleResult().IsClean())
                {
                    throw std::runtime_error("RG7 explicit GPU-idle recovery failed " + error);
                }
                for (uint32_t view = 0; view < views; ++view)
                {
                    if (!retained[view].expired() || resources.Resolve(handles[view][0]) || !resources.Resolve(history))
                    {
                        throw std::runtime_error("RG7 completed view/history ownership mismatch");
                    }
                    for (uint32_t index = 0; index < 4; ++index)
                    {
                        RHIReadbackImage image;
                        if (!resources.MapReadback(readbacks[view][index], image, error) || image.data.size() < 256)
                        {
                            throw std::runtime_error(error);
                        }
                        for (uint32_t element = 0; element < 64; ++element)
                        {
                            float value;
                            std::memcpy(&value, image.data.data() + element * 4, 4);
                            if (value != (index == 3 ? 900.0f : float(100 * (view + 1) + index)))
                            {
                                throw std::runtime_error("RG7 delayed view/history payload mismatch");
                            }
                        }
                    }
                }
                if (cache.freeAliasedGroups.size() != views || cache.freeAliasBytes != views * 65536)
                {
                    throw std::runtime_error("RG7 completed groups did not return exactly once");
                }
                const auto cachedMemory = cache.GetAliasHeapMemory();
                if (cachedMemory.retainedBytes != views * 65536 || cachedMemory.cachedBytes != views * 65536 ||
                    cachedMemory.leasedBytes != 0 || cachedMemory.cachedHeaps != views ||
                    cachedMemory.peakRetainedBytes != views * 65536)
                {
                    throw std::runtime_error("RG7 heap byte accounting changed at cache return");
                }
                cleanup();
                const auto drainedMemory = cache.GetAliasHeapMemory();
                if (drainedMemory.domainId != memoryDomain)
                {
                    throw std::runtime_error("RG7 lifetime drain changed accounting domain identity");
                }
                if (drainedMemory.retainedBytes != 0 || drainedMemory.cachedBytes != 0 || drainedMemory.retainedHeaps != 0)
                {
                    throw std::runtime_error("RG7 heap byte accounting survived cache drain");
                }
                std::string messages;
                if (!resources.AllReleased() || resources.DrainDebugMessages(messages))
                {
                    throw std::runtime_error("RG7 lifetime drain/validation failed " + messages);
                }
                resources.Shutdown();
                outLog += "RG7_HEAP_MEMORY_OK mode=" + std::to_string(mode) + " peakBytes=" +
                    std::to_string(drainedMemory.peakRetainedBytes) + " drainBytes=0 cached=after-completion\n";
                outLog += "RG7_LIFETIME_OK mode=" + std::to_string(mode) + " views=" + std::to_string(views) +
                    " gpuGate=actual retained=until-completion history=borrowed lateReader=excluded payload=all validation=0\n";
            }
            catch (const std::exception& exception)
            {
                cleanup();
                outLog += "RG7_LIFETIME_FAILED mode=" + std::to_string(mode) + " " + exception.what() + "\n";
                return false;
            }
        }
        return true;
    }
}
