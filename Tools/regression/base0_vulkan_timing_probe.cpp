#include "RHI/Vulkan/VulkanCaptureGpuProfiler.h"
#include "RHI/Vulkan/VulkanCommandBufferPool.h"
#include "RHI/RHISubmissionThread.h"
#include "Render/Graph/EnhancedRenderGraph.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

int main()
{
    try
    {
        std::string error;
        VulkanDeviceResources resources;
        if (!resources.Initialize(32, 32, true, error)) throw std::runtime_error(error);
        if (!resources.IsValidationEnabled()) throw std::runtime_error("Validation unavailable");
        thread_pool workers;
        job_scheduler scheduler(workers);
        scheduler.start(4);
        VulkanCommandBufferPool pool(scheduler);
        if (!pool.Initialize(resources, 4, VulkanDeviceResources::kFrameCount, error))
            throw std::runtime_error(error);
        RHIReadback readback{};
        if (!resources.CreateReadback(32, 32, RHIFormat::RGBA8Unorm, 1, readback, error))
            throw std::runtime_error(error);
        // Recreate query pools after complete submissions: no stale availability,
        // no ring-slot reuse, parallel begin/end/reset, and teardown validation.
        for (unsigned repeat = 0; repeat < 3; ++repeat)
        {
            if (!resources.BeginFrame(error)) throw std::runtime_error(error);
            pool.BeginFrame(repeat % VulkanDeviceResources::kFrameCount);
            EnhancedRenderGraph graph(static_cast<IRenderDeviceServices&>(resources));
            graph.SetParallelRecordCostThreshold(0);
            VulkanCaptureGpuProfiler profiler(resources);
            if (!profiler.Initialize(error)) throw std::runtime_error(error);
            graph.SetProfiler(&profiler);
            RGTextureDesc desc{};
            desc.width = desc.height = 32;
            desc.format = RHIFormat::RGBA8Unorm;
            desc.allowRenderTarget = true;
            const auto target = graph.CreateTexture(desc);
            for (unsigned i = 0; i < 4; ++i)
                graph.AddPass("clear." + std::to_string(i), {{target, RHIResourceState::RenderTarget}},
                    [&, i](const EnhancedRenderGraph::ExecuteContext& context)
                    {
                        const auto texture = context.ResolveHandle(target);
                        const auto binding = resources.CreateRenderTargets({&texture, 1});
                        const float color[]{float(i) / 4.f, .5f, .25f, 1.f};
                        context.encoder->BindRenderTargets(binding);
                        context.encoder->ClearRenderTargets(binding, color);
                    }, true);
            graph.AddPass("readback", {{target, RHIResourceState::CopySource}},
                [&](const EnhancedRenderGraph::ExecuteContext& context)
                { context.encoder->CopyToReadback(readback, context.ResolveHandle(target)); }, true);
            if (!graph.Compile(error)) throw std::runtime_error(error);
            RHIRecordedBatchDesc batchDesc{};
            batchDesc.frameId = repeat + 1;
            batchDesc.backendGeneration = GetRHISubmissionThread().GetOwnerGeneration(&resources);
            batchDesc.lifetimeToken = std::make_shared<unsigned>(repeat);
            RHIRecordedBatch batch;
            RHISubmissionTicket ticket;
            if (!graph.RecordParallel(pool, 4, batchDesc, batch, error) ||
                !GetRHISubmissionThread().EnqueueRecordedBatch(&resources, resources, std::move(batch), ticket, error) ||
                !resources.EndFrame(error) || !GetRHISubmissionThread().Wait(ticket, error))
                throw std::runtime_error(error);
            std::vector<VulkanCaptureGpuProfiler::Timing> timings;
            double span{}, busy{};
            if (!profiler.Collect(timings, span, busy, error)) throw std::runtime_error(error);
            if (timings.size() != 5 || profiler.SliceCount() != 5 ||
                !std::isfinite(span) || !std::isfinite(busy) || span < busy || busy <= 0)
                throw std::runtime_error("Incomplete timestamp coverage");
            RHIReadbackImage image;
            if (!resources.MapReadback(readback, image, error)) throw std::runtime_error(error);
            const float expected[]{.75f, .5f, .25f, 1.f};
            if (image.width != 32 || image.height != 32 || image.format != RHIFormat::RGBA8Unorm)
                throw std::runtime_error("Readback shape mismatch");
            for (unsigned channel = 0; channel < 4; ++channel)
                if (std::abs(image.At(16, 16, channel) - expected[channel]) > 1.5f / 255.f)
                    throw std::runtime_error("Parallel clear/readback mismatch");
            std::cout << "BASE0_VULKAN_TIMING_SUBMISSION_OK repeat=" << repeat
                      << " slices=5 spanMs=" << span << " busyMs=" << busy << '\n';
        }
        resources.WaitForGpu();
        resources.ReleaseReadback(readback);
        pool.Shutdown();
        std::string validation;
        if (resources.DrainDebugMessages(validation)) throw std::runtime_error(validation);
        if (resources.GetUnimplementedCount() || resources.GetEncoderUnimplementedCount() ||
            pool.GetEncoderUnimplementedCount()) throw std::runtime_error("Encoder call omitted");
        resources.Shutdown();
        std::cout << "BASE0_VULKAN_TIMING_OK validation=0 repeats=3\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "BASE0_VULKAN_TIMING_FAILED " << error.what() << '\n';
        return 1;
    }
}
