#pragma once

namespace
{
    class Rg7FailureServices final : public DX12DeviceResources
    {
    public:
        uint32_t failQuery{0}, failHeap{0}, failPlacement{0};
        mutable uint32_t queries{0};
        uint32_t heaps{0}, placements{0};
        std::vector<RHITextureHandle> textures;
        std::vector<RHIBufferHandle> buffers;
        std::vector<std::weak_ptr<RHITransientHeap>> heapOwners;

        bool DescribeTransientAllocation(const RHITransientResourceDesc& desc,
            RHITransientAllocationInfo& info, std::string& error) const override
        {
            if (++queries == failQuery)
            {
                info = {};
                error = "RG7 injected allocation-query failure";
                return false;
            }
            return DX12DeviceResources::DescribeTransientAllocation(desc, info, error);
        }
        bool CreateTransientHeap(const RHITransientAllocationInfo& info,
            std::shared_ptr<RHITransientHeap>& heap, std::string& error) override
        {
            if (++heaps == failHeap)
            {
                heap.reset();
                error = "RG7 injected heap-creation failure";
                return false;
            }
            const bool created = DX12DeviceResources::CreateTransientHeap(info, heap, error);
            if (created)
            {
                heapOwners.push_back(heap);
            }
            return created;
        }
        bool CreatePlacedTransient(const RHITransientResourceDesc& desc, RHITransientHeap& heap,
            RHITextureHandle& texture, RHIBufferHandle& buffer, std::string& error) override
        {
            if (++placements == failPlacement)
            {
                texture = {};
                buffer = {};
                error = "RG7 injected placed-resource failure";
                return false;
            }
            const bool created = DX12DeviceResources::CreatePlacedTransient(desc, heap, texture, buffer, error);
            if (created)
            {
                if (texture.IsValid())
                {
                    textures.push_back(texture);
                }
                if (buffer.IsValid())
                {
                    buffers.push_back(buffer);
                }
            }
            return created;
        }
        bool AllReleased() const
        {
            return std::ranges::all_of(textures, [this](auto handle) { return !Resolve(handle); }) &&
                std::ranges::all_of(buffers, [this](auto handle) { return !Resolve(handle); }) &&
                std::ranges::all_of(heapOwners, [](const auto& owner) { return owner.expired(); });
        }
        void ResetFault()
        {
            failQuery = failHeap = failPlacement = queries = heaps = placements = 0;
            textures.clear();
            buffers.clear();
            heapOwners.clear();
        }
    };

    bool RunRg7FailureTests(std::string& outLog)
    {
        Rg7FailureServices resources;
        std::string error;
        if (!resources.Initialize(32, 32, error))
        {
            outLog += error;
            return false;
        }
        const auto declare = [](EnhancedRenderGraph& graph)
        {
            std::array<RGHandle, 4> handles;
            for (uint32_t index = 0; index < handles.size(); ++index)
            {
                const bool buffer = index >= 2;
                RGHandle declared;
                if (buffer)
                {
                    declared = graph.CreateBuffer({256, true, false, "fault_buffer"});
                }
                else
                {
                    RGTextureDesc desc;
                    desc.width = desc.height = 32;
                    desc.format = RHIFormat::RGBA8Unorm;
                    desc.allowRenderTarget = true;
                    desc.name = "fault_color";
                    declared = graph.CreateTexture(desc);
                }
                handles[index] = graph.Write(declared);
                graph.AddPass("fault_write", {{handles[index], buffer ? RHIResourceState::UnorderedAccess :
                    RHIResourceState::RenderTarget, RGAccessMode::Write}}, nullptr);
                graph.AddPass("fault_read", {{handles[index], RHIResourceState::CopySource, RGAccessMode::Read}}, nullptr, true);
            }
            return handles;
        };
        for (uint32_t fault = 0; fault < 5; ++fault)
        {
            resources.ResetFault();
            resources.failQuery = fault == 0 ? 2 : 0;
            resources.failHeap = fault == 1 ? 2 : 0;
            resources.failPlacement = fault == 2 ? 2 : (fault == 3 ? 4 : 0);
            if (!resources.BeginFrame(error))
            {
                outLog += error;
                return false;
            }
            EnhancedRenderGraph graph(resources, RGSchedulingMode::ExplicitVersioned);
            graph.SetTransientAliasing(true);
            const auto handles = declare(graph);
            bool rejected = false;
            if (fault < 4)
            {
                rejected = !graph.Compile(error) && error.find("RG7 injected") != std::string::npos;
            }
            else if (graph.Compile(error))
            {
                // Stale activation is rejected before emitting a native barrier.
                resources.ReleaseTexture(resources.textures.front());
                rejected = !graph.Execute(error) && error.find("stale resource") != std::string::npos;
            }
            resources.AbortFrame();
            resources.WaitForGpu();
            graph.Reset();
            const auto failedMemory = graph.GetAliasHeapMemory();
            const uint64_t expectedPeak = fault == 0 ? 0 : (fault < 3 ? 65536 : 131072);
            if (!rejected || !resources.AllReleased() || failedMemory.retainedBytes != 0 ||
                failedMemory.peakRetainedBytes != expectedPeak)
            {
                outLog += "RG7 failure cleanup failed fault=" + std::to_string(fault) + " " + error;
                return false;
            }
            resources.ResetFault();
            if (!resources.BeginFrame(error))
            {
                outLog += error;
                return false;
            }
            declare(graph);
            if (!graph.Compile(error) || !graph.Execute(error) || !resources.EndFrame(error))
            {
                resources.AbortFrame();
                resources.WaitForGpu();
                outLog += "RG7 failure recovery failed " + error;
                return false;
            }
            resources.WaitForGpu();
            graph.Reset();
            if (!resources.AllReleased())
            {
                outLog += "RG7 recovery retained placed resources/heaps";
                return false;
            }
            outLog += "RG7_FAILURE_OK fault=" + std::to_string(fault) + " released=all recovery=submitted\n";
        }
        resources.ResetFault();
        RGTransientPool cache;
        EnhancedRenderGraph graph(resources, RGSchedulingMode::ExplicitVersioned);
        graph.SetTransientAliasing(true);
        graph.SetTransientPool(&cache);
        for (uint32_t frame = 0; frame < 3; ++frame)
        {
            if (!resources.BeginFrame(error))
            {
                cache.ClearAliasingCache();
                outLog += error;
                return false;
            }
            declare(graph);
            bool ok = graph.Compile(error) && graph.Execute(error);
            if (ok && frame == 1)
            {
                // Remove only this isolated device's queue admission. No GPU work was enqueued.
                GetRHISubmissionThread().ReleaseClient(&resources);
                const bool submitted = resources.EndFrame(error);
                std::string restoreError;
                const bool restored = GetRHISubmissionThread().AcquireClient(&resources, restoreError);
                ok = !submitted && restored;
                resources.AbortFrame();
            }
            else if (ok)
            {
                ok = resources.EndFrame(error);
            }
            if (!ok)
            {
                resources.AbortFrame();
            }
            resources.WaitForGpu();
            graph.Reset();
            const bool statesPreserved = std::ranges::all_of(cache.freeAliasedGroups, [](const auto& group)
            {
                return std::ranges::all_of(group->entries, [](const auto& entry)
                {
                    return entry.state == RHIResourceState::CopySource;
                });
            });
            if (!ok || cache.freeAliasedGroups.size() != 2 || !statesPreserved)
            {
                cache.ClearAliasingCache();
                outLog += "RG7 rejected submission corrupted cached state " + error;
                return false;
            }
        }
        cache.ClearAliasingCache();
        std::string messages;
        if (!resources.AllReleased() || resources.DrainDebugMessages(messages))
        {
            outLog += "RG7 failure validation/drain failed " + messages;
            return false;
        }
        resources.Shutdown();
        outLog += "RG7_FAILURE_OK admission=reject cacheState=preserved recovery=submitted validation=0\n";
        return true;
    }
}
