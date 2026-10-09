#pragma once

namespace
{
    class Rg7DeviceLossPool final : public DX12CommandListPool
    {
    public:
        DX12DeviceResources* resources{nullptr};
        HRESULT removalReason{S_OK};
        bool submissionRejected{false};
    private:
        bool SubmitRecordedCommands(uint32_t, std::span<const uint32_t> order,
            RHICompletionPoint completion, std::string& error) override
        {
            Microsoft::WRL::ComPtr<ID3D12Device5> device;
            if (FAILED(resources->GetDevice()->QueryInterface(IID_PPV_ARGS(&device))))
            {
                error = "RG7 device removal interface unavailable";
                return false;
            }
            std::vector<ID3D12CommandList*> lists;
            for (auto worker : order)
            {
                lists.push_back(Get(worker));
            }
            // Runs on the real RHI thread: execute, remove the isolated WARP
            // device, then exercise the production native Signal failure path.
            resources->GetCommandQueue()->ExecuteCommandLists(static_cast<UINT>(lists.size()), lists.data());
            device->RemoveDevice();
            removalReason = device->GetDeviceRemovedReason();
            submissionRejected = !resources->SubmitCommandLists({}, completion, error);
            return !submissionRejected;
        }
    };

    bool RunRg7DeviceLossTest(std::string& outLog)
    {
        Rg7FailureServices resources;
        RGTransientPool cache;
        Rg7DeviceLossPool pool;
        std::shared_ptr<EnhancedRenderGraph> graph;
        std::weak_ptr<EnhancedRenderGraph> retained;
        Microsoft::WRL::ComPtr<ID3D12Fence> gate;
        std::string error;
        const auto cleanup = [&]
        {
            if (gate && SUCCEEDED(resources.GetDevice()->GetDeviceRemovedReason()))
            {
                gate->Signal(1);
            }
            if (resources.GetCurrentUploadRecordingId() != 0)
            {
                resources.AbortFrame();
            }
            resources.DrainForLifecycle(RHILifecycleCommand::OfflineReadbackCapture, error);
            if (GetRHISubmissionThread().GetOwnerStats(&resources).faulted)
            {
                resources.DrainForLifecycle(RHILifecycleCommand::UnrecoverableDeviceError, error);
            }
            graph.reset();
            cache.ClearAliasingCache();
            pool.Shutdown();
        };
        try
        {
            Microsoft::WRL::ComPtr<IDXGIFactory6> factory;
            Microsoft::WRL::ComPtr<IDXGIAdapter1> warp, preferred;
            DXGI_ADAPTER_DESC1 warpDesc{}, preferredDesc{};
            if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))) ||
                FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))) ||
                FAILED(factory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                    IID_PPV_ARGS(&preferred))) || FAILED(warp->GetDesc1(&warpDesc)) ||
                FAILED(preferred->GetDesc1(&preferredDesc)) ||
                (warpDesc.AdapterLuid.LowPart == preferredDesc.AdapterLuid.LowPart &&
                 warpDesc.AdapterLuid.HighPart == preferredDesc.AdapterLuid.HighPart))
            {
                throw std::runtime_error("RG7 cannot isolate WARP from the preferred Editor adapter");
            }
            if (!resources.Initialize(32, 32, error, warpDesc.AdapterLuid) ||
                !pool.Initialize(resources, 2, 3, error) ||
                FAILED(resources.GetDevice()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate))))
            {
                throw std::runtime_error("RG7 WARP initialization failed " + error);
            }
            pool.resources = &resources;
            auto& submission = GetRHISubmissionThread();
            if (!submission.ExecuteAndWait(&resources, "RG7 WARP GPU gate", [&resources, gate](std::string&)
            {
                return SUCCEEDED(resources.GetCommandQueue()->Wait(gate.Get(), 1));
            }, error) || !resources.BeginFrame(error))
            {
                throw std::runtime_error(error);
            }
            pool.BeginFrame(0);
            graph = std::make_shared<EnhancedRenderGraph>(resources, RGSchedulingMode::ExplicitVersioned);
            graph->SetTransientPool(&cache);
            graph->SetTransientAliasing(true);
            graph->SetParallelRecordCostThreshold(0);
            if (!graph->PrepareParallel(pool, error))
            {
                throw std::runtime_error(error);
            }
            for (uint32_t index = 0; index < 2; ++index)
            {
                auto buffer = graph->Write(graph->CreateBuffer({256, true, false, "device_loss_transient"}));
                graph->AddPass("device_loss_write", {{buffer, RHIResourceState::UnorderedAccess, RGAccessMode::Write}},
                    [buffer](const auto& context)
                {
                    auto binding = RHIBindingDesc::UavBuffer(context.graph->ResolveBufferHandle(buffer), 64, 0);
                    binding.format = RHIFormat::R32Float;
                    const float values[]{42, 0, 0, 0};
                    context.encoder->ClearUnorderedAccess(binding, values);
                }, true);
            }
            if (!graph->Compile(error) || graph->GetStats().aliasReuseCount != 1)
            {
                throw std::runtime_error("RG7 WARP placed fixture failed " + error);
            }
            RHIRecordedBatchDesc desc;
            desc.backendGeneration = submission.GetOwnerGeneration(&resources);
            desc.displayToken = 1;
            desc.lifetimeToken = graph;
            RHIRecordedBatch batch;
            RHISubmissionTicket ticket;
            if (!graph->RecordParallel(pool, 2, desc, batch, error) ||
                !submission.EnqueueRecordedBatch(&resources, resources, std::move(batch), ticket, error))
            {
                throw std::runtime_error(error);
            }
            retained = graph;
            graph.reset();
            desc.lifetimeToken.reset();
            const bool submitted = submission.Wait(ticket, error);
            if (submitted || !pool.submissionRejected ||
                !FAILED(pool.removalReason) ||
                !submission.GetOwnerStats(&resources).faulted || retained.expired() ||
                submission.GetOwnerStats(&resources).pendingRetirements != 1 ||
                resources.GetCompletedFenceValue() != 0 || !cache.freeAliasedGroups.empty())
            {
                throw std::runtime_error("RG7 native removal/Signal/retention mismatch submitted=" +
                    std::to_string(submitted) + " reason=" + std::to_string(static_cast<uint32_t>(pool.removalReason)) +
                    " faulted=" + std::to_string(submission.GetOwnerStats(&resources).faulted) +
                    " retained=" + std::to_string(!retained.expired()) + " pending=" +
                    std::to_string(submission.GetOwnerStats(&resources).pendingRetirements) +
                    " completed=" + std::to_string(resources.GetCompletedFenceValue()) + " " + error);
            }
            resources.AbortFrame();
            if (cache.GetAliasHeapMemory().leasedBytes != 65536 || cache.GetAliasHeapMemory().cachedBytes != 0)
            {
                throw std::runtime_error("RG7 removed device lost quarantined heap bytes");
            }
            if (!resources.DrainForLifecycle(RHILifecycleCommand::UnrecoverableDeviceError, error) ||
                !resources.GetLastLifecycleResult().IsClean() || !retained.expired())
            {
                throw std::runtime_error("RG7 native device-loss abandon failed " + error);
            }
            cleanup();
            if (cache.GetAliasHeapMemory().retainedBytes != 0 || cache.GetAliasHeapMemory().cachedBytes != 0)
            {
                throw std::runtime_error("RG7 removed-device heap bytes survived abandonment/drain");
            }
            std::string messages;
            if (!resources.AllReleased() || resources.DrainDebugMessages(messages))
            {
                throw std::runtime_error("RG7 device-loss release/validation failed " + messages);
            }
            resources.Shutdown();
            outLog += "RG7_DEVICE_LOSS_OK adapter=WARP remove=native submission=rejected retention=until-abandon released=all validation=0\n";
            return true;
        }
        catch (const std::exception& exception)
        {
            cleanup();
            outLog += "RG7_DEVICE_LOSS_FAILED " + std::string(exception.what()) + "\n";
            return false;
        }
    }
}
