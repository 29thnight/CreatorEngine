#include "../VulkanSelfTest.h"
#include "RHI/Vulkan/VulkanCommandBufferPool.h"
#include "RHI/Vulkan/VulkanDeviceResources.h"
#include "RHI/RHISubmissionThread.h"
#include "Render/Graph/EnhancedRenderGraph.h"

#include <cmath>
#include <cstring>
#include <string>

namespace
{
    constexpr uint32_t kParallelWidth = 64;
    constexpr uint32_t kParallelHeight = 64;
    constexpr uint32_t kColorPassCount = 4;

    const float kPassColors[kColorPassCount][4] = {
        { 0.75f, 0.10f, 0.20f, 1.0f },
        { 0.10f, 0.75f, 0.20f, 1.0f },
        { 0.10f, 0.20f, 0.75f, 1.0f },
        { 0.25f, 0.50f, 0.75f, 1.0f },
    };
}

bool RunVulkanParallelRecordingTest(std::string& outLog)
{
    outLog.clear();
    std::string error;
    VulkanDeviceResources resources;
    if (!resources.Initialize(kParallelWidth, kParallelHeight, true, error))
    {
        outLog += "[1/4] Vulkan 초기화 실패: " + error + "\n";
        return false;
    }

    VulkanCommandBufferPool pool;
    if (!pool.Initialize(resources, 4, VulkanDeviceResources::kFrameCount, error))
    {
        outLog += "[1/4] 병렬 command pool 초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }

    RHIReadback readback{};
    if (!resources.CreateReadback(kParallelWidth, kParallelHeight,
        RHIFormat::RGBA8Unorm, 1, readback, error))
    {
        outLog += "[1/4] 리드백 생성 실패: " + error + "\n";
        pool.Shutdown();
        resources.Shutdown();
        return false;
    }
    outLog += "[1/4] Vulkan worker별 command pool/buffer 초기화 통과\n";

    const auto renderOnce = [&](bool parallel, RHIReadbackImage& outImage,
        EnhancedRenderGraph::Stats& outStats) -> bool
    {
        if (!resources.BeginFrame(error)) return false;
        if (parallel) pool.BeginFrame(0);

        EnhancedRenderGraph graph(static_cast<IRenderDeviceServices&>(resources));
        graph.SetParallelRecordCostThreshold(0);

        RGTextureDesc targetDesc{};
        targetDesc.width = kParallelWidth;
        targetDesc.height = kParallelHeight;
        targetDesc.format = RHIFormat::RGBA8Unorm;
        targetDesc.allowRenderTarget = true;
        targetDesc.name = parallel ? "vk.parallel.target" : "vk.sequential.target";
        const RGHandle target = graph.CreateTexture(targetDesc);

        for (uint32_t passIndex = 0; passIndex < kColorPassCount; ++passIndex)
        {
            graph.AddPass("color." + std::to_string(passIndex),
                { { target, RHIResourceState::RenderTarget } },
                [&, passIndex](const EnhancedRenderGraph::ExecuteContext& context)
                {
                    const RHITextureHandle color = context.ResolveHandle(target);
                    const RHIRenderTargetBinding binding =
                        resources.CreateRenderTargets({ &color, 1 });
                    if (!binding.IsValid()) return;
                    context.encoder->BindRenderTargets(binding);
                    context.encoder->ClearRenderTargets(binding, kPassColors[passIndex]);
                }, true);
        }

        graph.AddPass("readback", { { target, RHIResourceState::CopySource } },
            [&](const EnhancedRenderGraph::ExecuteContext& context)
            {
                context.encoder->CopyToReadback(readback, context.ResolveHandle(target));
            }, true);

        if (!graph.Compile(error))
        {
            resources.AbortFrame();
            return false;
        }

        RHIRecordedBatch batch;
        RHISubmissionTicket batchTicket;
        bool batchContract = true;
        bool recorded = false;
        if (parallel)
        {
            RHIRecordedBatchDesc batchDesc{};
            batchDesc.frameId = 17;
            batchDesc.backendGeneration =
                GetRHISubmissionThread().GetOwnerGeneration(&resources);
            batchDesc.displayToken = 29;
            batchDesc.lifetimeToken = std::make_shared<uint64_t>(31);
            recorded = graph.RecordParallel(pool, 4, batchDesc, batch, error);
            batchContract = recorded && batch.IsReadyForSubmit() &&
                17 == batch.GetFrameId() &&
                batchDesc.backendGeneration == batch.GetBackendGeneration() &&
                29 == batch.GetDisplayToken() && 0 == batch.GetFrameSlot() &&
                batch.HasLifetimeToken() && !batch.GetCompletionPoint().IsValid();
            // 제출 직전 current slot을 바꿔도 batch는 기록 당시 slot 0을 써야 한다.
            if (recorded) pool.BeginFrame(1);
            if (recorded) recorded = GetRHISubmissionThread().EnqueueRecordedBatch(
                &resources, resources, std::move(batch), batchTicket, error);
            batchContract = batchContract && recorded;
        }
        else
        {
            recorded = graph.Execute(error);
        }
        if (!recorded)
        {
            resources.AbortFrame();
            return false;
        }
        outStats = graph.GetStats();

        if (!resources.EndFrame(error))
        {
            resources.AbortFrame();
            return false;
        }
        if (parallel && !GetRHISubmissionThread().Wait(batchTicket, error)) return false;
        if (parallel)
        {
            RHIRecordedBatch* const submittedBatch = batchTicket.GetRecordedBatch();
            std::string duplicateError;
            const bool duplicateRejected = nullptr != submittedBatch &&
                !pool.SubmitRecordedBatch(*submittedBatch, duplicateError) &&
                !duplicateError.empty();
            batchContract = batchContract && nullptr != submittedBatch &&
                submittedBatch->IsSubmitted() && duplicateRejected &&
                submittedBatch->GetCompletionPoint().IsValid() &&
                submittedBatch->GetCommandCount() == outStats.recordedLists;
        }
        if (!batchContract)
        {
            error = "RHIRecordedBatch metadata/state/completion 계약 실패";
            return false;
        }
        resources.WaitForGpu();
        const RHILifecycleResult& lifecycle = resources.GetLastLifecycleResult();
        if (!lifecycle.IsClean() ||
            RHILifecycleCommand::OfflineReadbackCapture != lifecycle.command)
        {
            error = "offline lifecycle pending task/batch/retirement가 0이 아니다";
            return false;
        }
        return resources.MapReadback(readback, outImage, error);
    };

    RHIReadbackImage sequential{};
    RHIReadbackImage parallel{};
    EnhancedRenderGraph::Stats sequentialStats{};
    EnhancedRenderGraph::Stats parallelStats{};
    if (!renderOnce(false, sequential, sequentialStats))
    {
        outLog += "[2/4] 순차 기록 실패: " + error + "\n";
        resources.WaitForGpu();
        resources.ReleaseReadback(readback);
        pool.Shutdown();
        resources.Shutdown();
        return false;
    }
    outLog += "[2/4] 순차 RenderGraph 기록·제출 통과\n";

    if (!renderOnce(true, parallel, parallelStats))
    {
        outLog += "[3/4] 병렬 기록 실패: " + error + "\n";
        resources.WaitForGpu();
        resources.ReleaseReadback(readback);
        pool.Shutdown();
        resources.Shutdown();
        return false;
    }
    outLog += "[3/4] 병렬 기록 통과 (worker "
        + std::to_string(parallelStats.recordWorkers) + " · command buffer "
        + std::to_string(parallelStats.recordedLists) + " · unit "
        + std::to_string(parallelStats.recordUnits) + ")\n";

    const bool sameShape = sequential.width == parallel.width &&
        sequential.height == parallel.height && sequential.rowPitch == parallel.rowPitch &&
        sequential.data.size() == parallel.data.size();
    const bool samePixels = sameShape && (sequential.data.empty() ||
        0 == std::memcmp(sequential.data.data(), parallel.data.data(), sequential.data.size()));
    const float expected[4] = { 0.25f, 0.50f, 0.75f, 1.0f };
    bool expectedColor = sameShape;
    for (uint32_t channel = 0; channel < 4 && expectedColor; ++channel)
        expectedColor = std::abs(parallel.At(kParallelWidth / 2,
            kParallelHeight / 2, channel) - expected[channel]) <= (1.5f / 255.0f);

    const bool parallelShape = 4 == parallelStats.recordWorkers &&
        4 == parallelStats.recordedLists && 5 == parallelStats.recordUnits;
    const uint32_t stubs = resources.GetUnimplementedCount() +
        resources.GetEncoderUnimplementedCount() + pool.GetEncoderUnimplementedCount();
    std::string validation;
    const uint32_t problems = resources.DrainDebugMessages(validation);
    bool passed = samePixels && expectedColor && parallelShape &&
        0 == stubs && 0 == problems;

    outLog += "[4/4] 순차/병렬 픽셀 " + std::string(samePixels ? "일치" : "불일치")
        + " · 최종 색 " + (expectedColor ? "통과" : "실패")
        + " · 미구현 " + std::to_string(stubs)
        + " · Vulkan validation " + std::to_string(problems) + "건\n";
    if (!validation.empty()) outLog += validation;

    resources.WaitForGpu();
    resources.ReleaseReadback(readback);
    pool.Shutdown();
    resources.Shutdown();
    const RHISubmissionOwnerStats shutdownOwner =
        GetRHISubmissionThread().GetOwnerStats(&resources);
    const bool shutdownClean = !shutdownOwner.registered &&
        shutdownOwner.IsIdle() &&
        RHILifecycleCommand::BackendShutdown == shutdownOwner.lastCommand;
    outLog += "[lifecycle] shutdown pending task/batch/retirement " +
        std::to_string(shutdownOwner.pendingTasks) + "/" +
        std::to_string(shutdownOwner.pendingBatches) + "/" +
        std::to_string(shutdownOwner.pendingRetirements) +
        (shutdownClean ? " 통과\n" : " 실패\n");
    passed = passed && shutdownClean;
    if (passed) outLog += "Vulkan RenderGraph 병렬 command recording 검증 통과\n";
    return passed;
}
