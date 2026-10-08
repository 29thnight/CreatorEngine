#pragma once
#include <array>
#include <stdexcept>

// Included by the existing Editor render-test translation unit. No test project.
bool DX12Test::RunTransientAliasingTest(std::string& outLog)
{
    DX12DeviceResources resources;
    std::string error;
    if (!resources.Initialize(64, 64, error))
    {
        outLog += "RG7_DEVICE_FAILED " + error;
        return false;
    }
    if (!resources.HasDebugMessageQueue())
    {
        outLog += "RG7 requires an actual DX12 validation queue; use CREATOR_DX12_VALIDATION=gpu";
        return false;
    }
    std::array<std::vector<uint8_t>, 6> baseline;
    uint64_t baselineBytes = 0;
    RGTransientPool aliasPool;
    RHIBufferHandle cachedBuffer;
    DX12CommandListPool pool;
    if (!pool.Initialize(resources, 2, DX12DeviceResources::kFrameCount, error))
    {
        outLog += error;
        return false;
    }
    for (uint32_t mode = 0; mode < 9; ++mode)
    {
        if (!resources.BeginFrame(error))
        {
            outLog += error;
            return false;
        }
        EnhancedRenderGraph graph(resources, RGSchedulingMode::ExplicitVersioned);
        graph.SetTransientAliasing(mode != 0, mode == 2);
        if (mode >= 5)
        {
            graph.SetTransientPool(&aliasPool);
            aliasPool.maxFreeAliasGroups = mode == 7 ? 0 : 32;
        }
        if (mode == 4)
        {
            pool.BeginFrame(0);
            graph.SetParallelRecordCostThreshold(0);
            if (!graph.PrepareParallel(pool, error))
            {
                resources.AbortFrame();
                outLog += error;
                return false;
            }
        }
        std::array<RGHandle, 6> handles{};
        std::array<RHIReadback, 6> readbacks{};
        uint64_t measuredUnaliasedBytes = 0;
        auto cleanup = [&]
        {
            resources.AbortFrame();
            resources.WaitForGpu();
            for (auto& readback : readbacks)
            {
                resources.ReleaseReadback(readback);
            }
            graph.Reset();
        };
        try
        {
            for (uint32_t index = 0; index < handles.size(); ++index)
            {
                const bool buffer = index % 3 == 1;
                const bool depth = index % 3 == 2;
                RGHandle declared;
                const std::string name = "RG7_" + std::to_string(index);
                RHITransientResourceDesc allocationDesc{};
                allocationDesc.buffer = buffer;
                if (buffer)
                {
                    declared = graph.CreateBuffer({256, true, false, name});
                    allocationDesc.bufferDesc.bytes = 256;
                    allocationDesc.bufferDesc.allowUnorderedAccess = true;
                    if (!resources.CreateBufferReadback(256, readbacks[index], error))
                    {
                        throw std::runtime_error(error);
                    }
                }
                else
                {
                    RGTextureDesc desc{};
                    desc.width = desc.height = 32;
                    desc.arraySize = 2;
                    desc.format = depth ? RHIFormat::D32Float : RHIFormat::RGBA8Unorm;
                    desc.allowRenderTarget = !depth;
                    desc.allowDepthStencil = depth;
                    desc.clearColor[0] = index < 3 ? 0.25f : 0.75f;
                    desc.clearColor[3] = 1.0f;
                    desc.name = name;
                    declared = graph.CreateTexture(desc);
                    allocationDesc.textureDesc.width = allocationDesc.textureDesc.height = 32;
                    allocationDesc.textureDesc.depthOrArraySize = 2;
                    allocationDesc.textureDesc.format = desc.format;
                    allocationDesc.textureDesc.allowRenderTarget = !depth;
                    allocationDesc.textureDesc.allowDepthStencil = depth;
                    if (!resources.CreateReadback(32, 32, desc.format, 2, readbacks[index], error))
                    {
                        throw std::runtime_error(error);
                    }
                }
                RHITransientAllocationInfo allocationInfo{};
                if (!resources.DescribeTransientAllocation(allocationDesc, allocationInfo, error))
                {
                    throw std::runtime_error(error);
                }
                measuredUnaliasedBytes += allocationInfo.bytes;
                const auto written = graph.Write(declared);
                handles[index] = written;
                const auto state = buffer ? RHIResourceState::UnorderedAccess :
                    (depth ? RHIResourceState::DepthWrite : RHIResourceState::RenderTarget);
                graph.AddPass(name + "_write", {{written, state, RGAccessMode::Write}},
                    [&, written, index, buffer, depth](const EnhancedRenderGraph::ExecuteContext& context)
                    {
                        if (buffer)
                        {
                            auto view = RHIBindingDesc::UavBuffer(context.graph->ResolveBufferHandle(written), 64, 0);
                            view.format = RHIFormat::R32Float;
                            const float values[]{index < 3 ? 2.0f : 6.0f, 0, 0, 0};
                            context.encoder->ClearUnorderedAccess(view, values);
                        }
                        else
                        {
                            RHIRenderTargetBinding binding;
                            if (depth)
                            {
                                auto desc = RHIDepthTargetDesc::Depth(context.ResolveHandle(written), RHIFormat::D32Float);
                                desc.sliceCount = 2;
                                binding = resources.CreateRenderTargets(std::span<const RHITextureHandle>{}, &desc);
                                context.encoder->ClearDepthTarget(binding, 1.0f);
                            }
                            else
                            {
                                auto desc = RHIColorTargetDesc::Texture(context.ResolveHandle(written));
                                desc.sliceCount = 2;
                                binding = resources.CreateRenderTargets(std::span<const RHIColorTargetDesc>{&desc, 1});
                                const float color[]{index < 3 ? 0.25f : 0.75f, 0, 0, 1};
                                context.encoder->ClearRenderTargets(binding, color);
                            }
                            if (!binding.IsValid())
                            {
                                throw std::runtime_error("RG7 clear binding is invalid");
                            }
                        }
                    });
                graph.AddPass(name + "_read", {{written, RHIResourceState::CopySource, RGAccessMode::Read}},
                    [&, written, index, buffer](const EnhancedRenderGraph::ExecuteContext& context)
                    {
                        if (buffer)
                        {
                            context.encoder->CopyBufferToReadback(readbacks[index], context.graph->ResolveBufferHandle(written));
                        }
                        else
                        {
                            for (uint32_t slice = 0; slice < 2; ++slice)
                            {
                                context.encoder->CopyToReadback(readbacks[index], context.ResolveHandle(written), slice, slice);
                            }
                        }
                    }, true);
            }
            if (mode == 3)
            {
                graph.AddPass("overlap_guard", {{handles[0], RHIResourceState::CopySource, RGAccessMode::Read}},
                    nullptr, true);
            }
            const auto unused = graph.CreateBuffer({0, false, false, "culled_invalid_buffer"});
            graph.AddPass("unused", {{graph.Write(unused), RHIResourceState::CopyDest, RGAccessMode::Write}}, nullptr);
            if (!graph.Compile(error))
            {
                throw std::runtime_error(error);
            }
            const auto stats = graph.GetStats();
            const uint32_t expectedReuse = mode == 1 || mode >= 4 ? 4 : (mode == 3 ? 3 : 0);
            if (stats.aliasReuseCount != expectedReuse || graph.ResolveBufferHandle(unused).IsValid())
            {
                throw std::runtime_error("RG7 overlap/culling plan mismatch");
            }
            if (mode == 0)
            {
                baselineBytes = measuredUnaliasedBytes;
            }
            if (measuredUnaliasedBytes != baselineBytes || (mode != 0 && stats.transientUnaliasedBytes != baselineBytes) ||
                (expectedReuse && stats.transientCommittedBytes >= baselineBytes) ||
                (mode != 0 && !expectedReuse && stats.transientCommittedBytes != baselineBytes))
            {
                throw std::runtime_error("RG7 committed memory did not match allocation plan");
            }
            if (mode >= 5)
            {
                const bool warm = mode == 6 || mode == 7;
                if (stats.aliasHeapCreates != (warm ? 0 : 2) || stats.aliasHeapReuses != (warm ? 2 : 0) ||
                    stats.aliasResourceReuses != (warm ? 6 : 0) ||
                    (mode > 5 && stats.transientAllocationQueries != 0) ||
                    (warm && graph.ResolveBufferHandle(handles[1]) != cachedBuffer))
                {
                    throw std::runtime_error("RG7 completed-group cache did not reuse exact resources/query results");
                }
            }
            EnhancedRenderGraph::DiagnosticSnapshot snapshot;
            graph.CaptureDiagnosticSnapshot(snapshot);
            uint32_t snapshotBarrierCount = 0;
            uint32_t activationCount = 0;
            for (const auto& pass : snapshot.passes)
            {
                snapshotBarrierCount += static_cast<uint32_t>(pass.barriers.size());
                for (const auto& barrier : pass.barriers)
                {
                    activationCount += barrier.aliasing ? 1 : 0;
                }
            }
            if (snapshotBarrierCount != stats.barriersEmitted ||
                activationCount != (expectedReuse ? expectedReuse + 2 : 0))
            {
                throw std::runtime_error("RG7 activation diagnostic/statistics mismatch");
            }
            for (uint32_t left = 0; left < snapshot.resources.size(); ++left)
            {
                const auto& resource = snapshot.resources[left];
                for (uint32_t right = left + 1; right < snapshot.resources.size(); ++right)
                {
                    const auto& other = snapshot.resources[right];
                    if (resource.aliasGroup != UINT32_MAX && resource.aliasGroup == other.aliasGroup &&
                        resource.firstUse <= other.lastUse && other.firstUse <= resource.lastUse)
                    {
                        throw std::runtime_error("RG7 shared heap lifetimes overlap");
                    }
                }
            }
            RHISubmissionTicket ticket;
            if (mode == 4)
            {
                RHIRecordedBatchDesc desc{};
                desc.frameId = mode;
                desc.backendGeneration = GetRHISubmissionThread().GetOwnerGeneration(&resources);
                RHIRecordedBatch batch;
                if (!graph.RecordParallel(pool, 2, desc, batch, error) ||
                    !GetRHISubmissionThread().EnqueueRecordedBatch(&resources, resources, std::move(batch), ticket, error))
                {
                    throw std::runtime_error(error);
                }
            }
            else if (!graph.Execute(error))
            {
                throw std::runtime_error(error);
            }
            if (!resources.EndFrame(error))
            {
                throw std::runtime_error(error);
            }
            resources.WaitForGpu();
            if (mode == 4 && !GetRHISubmissionThread().Wait(ticket, error))
            {
                throw std::runtime_error(error);
            }
            for (uint32_t index = 0; index < readbacks.size(); ++index)
            {
                RHIReadbackImage image;
                if (!resources.MapReadback(readbacks[index], image, error))
                {
                    throw std::runtime_error(error);
                }
                // Readback row padding is undefined. Compare authored pixels only.
                std::vector<uint8_t> pixels;
                if (index % 3 == 1)
                {
                    pixels = image.data;
                    const auto* values = reinterpret_cast<const float*>(pixels.data());
                    for (uint32_t value = 0; value < 64; ++value)
                    {
                        if (values[value] != (index < 3 ? 2.0f : 6.0f))
                        {
                            throw std::runtime_error("RG7 buffer payload mismatch");
                        }
                    }
                }
                else
                {
                    for (uint32_t slice = 0; slice < 2; ++slice)
                    {
                        for (uint32_t row = 0; row < 32; ++row)
                        {
                            const auto begin = image.data.begin() + slice * readbacks[index].sliceBytes + row * readbacks[index].rowPitch;
                            pixels.insert(pixels.end(), begin, begin + 32 * 4);
                        }
                    }
                    for (size_t pixel = 0; pixel < pixels.size(); pixel += 4)
                    {
                        if (index % 3 == 2)
                        {
                            float depth = 0;
                            std::memcpy(&depth, pixels.data() + pixel, sizeof(depth));
                            if (depth != 1.0f)
                            {
                                throw std::runtime_error("RG7 depth payload mismatch");
                            }
                        }
                        else if (std::abs(int(pixels[pixel]) - (index < 3 ? 64 : 191)) > 1 ||
                            pixels[pixel + 1] != 0 || pixels[pixel + 2] != 0 || pixels[pixel + 3] != 255)
                        {
                            throw std::runtime_error("RG7 color payload mismatch");
                        }
                    }
                }
                if (mode == 0)
                {
                    baseline[index] = pixels;
                }
                else if (baseline[index] != pixels)
                {
                    throw std::runtime_error("RG7 alias off/on payload mismatch");
                }
            }
            const auto released = graph.ResolveBufferHandle(handles[1]);
            if (expectedReuse && graph.Compile(error))
            {
                throw std::runtime_error("RG7 recompiled placed resources without Reset");
            }
            graph.Reset();
            const bool retained = mode == 5 || mode == 6 || mode == 8;
            if (bool(resources.Resolve(released)) != retained ||
                (mode >= 5 && (aliasPool.freeAliasBytes != (retained ? 131072 : 0) ||
                    aliasPool.freeAliasedGroups.size() != (retained ? 2 : 0))))
            {
                throw std::runtime_error("RG7 cache retention/eviction did not match completion ownership");
            }
            cachedBuffer = released;
            for (auto& readback : readbacks)
            {
                resources.ReleaseReadback(readback);
            }
            std::string messages;
            if (resources.DrainDebugMessages(messages))
            {
                throw std::runtime_error("RG7 validation: " + messages);
            }
            outLog += "RG7_TRANSIENT_MODE_OK mode=" + std::to_string(mode) + " reuse=" +
                std::to_string(stats.aliasReuseCount) + " bytes=" + std::to_string(mode == 0 ? baselineBytes : stats.transientCommittedBytes) +
                " baseline=" + std::to_string(baselineBytes) + "\n";
            if (mode >= 5)
            {
                outLog += "RG7_CACHE_MODE_OK mode=" + std::to_string(mode) + " heapsCreated=" +
                    std::to_string(stats.aliasHeapCreates) + " heapsReused=" + std::to_string(stats.aliasHeapReuses) +
                    " resourcesReused=" + std::to_string(stats.aliasResourceReuses) + " queries=" +
                    std::to_string(stats.transientAllocationQueries) + "\n";
            }
        }
        catch (const std::exception& exception)
        {
            cleanup();
            aliasPool.ClearAliasingCache();
            outLog += "RG7_TRANSIENT_FAILED mode=" + std::to_string(mode) + " " + exception.what() + "\n";
            return false;
        }
    }
    aliasPool.ClearAliasingCache();
    if (resources.Resolve(cachedBuffer) || !aliasPool.allocationCache.empty() || aliasPool.freeAliasBytes != 0)
    {
        outLog += "RG7 cache drain retained resources or allocation metadata";
        return false;
    }
    {
        std::shared_ptr<RHITransientHeap> heap;
        RHITransientAllocationInfo allocation{};
        if (resources.CreateTransientHeap(allocation, heap, error) || heap)
        {
            outLog += "RG7 accepted an invalid heap allocation";
            return false;
        }
        RHITransientResourceDesc desc{};
        desc.buffer = true;
        desc.bufferDesc.bytes = 256;
        if (!resources.DescribeTransientAllocation(desc, allocation, error) ||
            !resources.CreateTransientHeap(allocation, heap, error))
        {
            outLog += error;
            return false;
        }
        RHITextureHandle texture;
        RHIBufferHandle buffer;
        const auto rejectsPlacement = [&]
        {
            return !resources.CreatePlacedTransient(desc, *heap, texture, buffer, error) &&
                !texture.IsValid() && !buffer.IsValid();
        };
        desc.bufferDesc.bytes = allocation.bytes + 1;
        if (!rejectsPlacement())
        {
            outLog += "RG7 accepted an oversized placed resource";
            return false;
        }
        desc.bufferDesc.bytes = 256;
        desc.bufferDesc.initialState = RHIResourceState::CopyDest;
        if (!rejectsPlacement())
        {
            outLog += "RG7 accepted a placed resource without COMMON initialization";
            return false;
        }
        desc.buffer = false;
        desc.textureDesc.width = desc.textureDesc.height = 32;
        desc.textureDesc.format = RHIFormat::RGBA8Unorm;
        desc.textureDesc.allowRenderTarget = true;
        if (!rejectsPlacement())
        {
            outLog += "RG7 accepted a texture in a buffer-only heap";
            return false;
        }
        RHITransientHeap foreignHeap;
        desc.buffer = true;
        desc.bufferDesc.initialState = RHIResourceState::Common;
        if (resources.CreatePlacedTransient(desc, foreignHeap, texture, buffer, error) ||
            texture.IsValid() || buffer.IsValid())
        {
            outLog += "RG7 accepted a foreign heap implementation";
            return false;
        }
    }
    if (!resources.BeginFrame(error))
    {
        outLog += error;
        return false;
    }
    {
        EnhancedRenderGraph legacy(resources);
        legacy.SetTransientAliasing(true);
        const auto legacyBuffer = legacy.CreateBuffer({256, true, false, "legacy_uav"});
        legacy.AddPass("legacy_uav", {{legacyBuffer, RHIResourceState::UnorderedAccess}}, nullptr, true);
        if (legacy.Compile(error))
        {
            resources.AbortFrame();
            outLog += "RG7 accepted inferred UAV access for aliasing";
            return false;
        }
    }
    {
        EnhancedRenderGraph invalid(resources, RGSchedulingMode::ExplicitVersioned);
        invalid.SetTransientAliasing(true);
        const auto buffer = invalid.Write(invalid.CreateBuffer({0, false, false, "invalid_used"}));
        invalid.AddPass("invalid_write", {{buffer, RHIResourceState::CopyDest, RGAccessMode::Write}}, nullptr, true);
        if (invalid.Compile(error))
        {
            resources.AbortFrame();
            outLog += "RG7 accepted a zero-size used buffer";
            return false;
        }
    }
    RHIBufferHandle persistent;
    RHIBufferDesc persistentDesc{};
    persistentDesc.bytes = 256;
    if (!resources.CreateBuffer(persistentDesc, persistent, error))
    {
        resources.AbortFrame();
        outLog += error;
        return false;
    }
    {
        EnhancedRenderGraph imported(resources, RGSchedulingMode::ExplicitVersioned);
        imported.SetTransientAliasing(true);
        const auto buffer = imported.ImportBuffer(persistent, RHIResourceState::Common, "history_buffer");
        imported.AddPass("history_read", {{buffer, RHIResourceState::CopySource, RGAccessMode::Read}}, nullptr, true);
        if (!imported.Compile(error) || !imported.Execute(error) || !resources.EndFrame(error))
        {
            resources.AbortFrame();
            resources.WaitForGpu();
            resources.ReleaseBuffer(persistent);
            outLog += error;
            return false;
        }
        resources.WaitForGpu();
        EnhancedRenderGraph::DiagnosticSnapshot snapshot;
        imported.CaptureDiagnosticSnapshot(snapshot);
        imported.Reset();
        if (!resources.Resolve(persistent) || snapshot.resources[0].aliasGroup != UINT32_MAX ||
            imported.GetStats().aliasReuseCount != 0)
        {
            resources.ReleaseBuffer(persistent);
            outLog += "RG7 imported buffer ownership changed";
            return false;
        }
    }
    resources.ReleaseBuffer(persistent);
    std::string messages;
    if (resources.DrainDebugMessages(messages))
    {
        outLog += "RG7 validation: " + messages;
        return false;
    }
    pool.Shutdown();
    resources.Shutdown();
    outLog += "RG7_TRANSIENT_OK modes=9 arrays=2 bufferPayload=64 parallel=2 cache=warm/evict/drain inferredAccess=reject invalidUsed=reject imported=retained placementGuards=5 recompile=reject validation=0\n";
    return true;
}
