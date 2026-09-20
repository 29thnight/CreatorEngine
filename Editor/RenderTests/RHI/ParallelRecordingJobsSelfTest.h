#pragma once
#include "RHI/RHIParallelCommandPool.h"
#include "RHI/RHISubmissionThread.h"
#include "Render/Graph/EnhancedRenderGraph.h"

#include <array>
#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>

// Both native pools exercise the same execution and aborted-recording contract.
// The injected scheduler is test-owned; the product pools use the engine scheduler.
template<class Resources>
bool VerifyParallelRecordingJobs(IRHIParallelCommandPool& pool, Resources& resources,
    job_scheduler& scheduler, std::string& outLog)
{
    const auto require = [](bool condition, const std::string& message)
    {
        if (!condition) throw std::runtime_error(message);
    };
    try
    {
        std::atomic<uint32_t> calls{ 0 };
        bool rejected = false;
        try { pool.RunParallel([&](uint32_t) { ++calls; }, 4); }
        catch (const std::runtime_error&) { rejected = true; }
        require(rejected && 0 == calls, "stopped scheduler executed recording inline");
        pool.RunParallel([&](uint32_t) { ++calls; }, 0);
        require(0 == calls, "empty recording ran a callback");

        for (const uint32_t physicalWorkers : { 1u, 4u })
        {
            scheduler.start(physicalWorkers);
            for (const uint32_t requested : { 1u, 4u, 99u })
            {
                std::array<std::atomic<uint32_t>, 4> visits{};
                std::atomic<uint32_t> invalid{ 0 };
                pool.RunParallel([&](uint32_t lane)
                {
                    if (!thread_pool::is_worker_thread() || lane >= visits.size()) ++invalid;
                    else ++visits[lane];
                }, requested);
                require(0 == invalid, "recording used the caller or an invalid lane");
                for (uint32_t lane = 0; lane < visits.size(); ++lane)
                    require(visits[lane] == (lane < (std::min)(requested, 4u) ? 1u : 0u),
                        "recording lane was omitted or executed twice");
            }
            calls = 0;
            bool propagated = false;
            try
            {
                pool.RunParallel([&](uint32_t lane)
                {
                    if (0 != lane) std::this_thread::sleep_for(std::chrono::milliseconds(3));
                    ++calls;
                    if (0 == lane) throw std::runtime_error("recording failure probe");
                }, 4);
            }
            catch (const std::runtime_error& e)
            {
                propagated = std::string(e.what()) == "recording failure probe";
            }
            require(propagated && 4 == calls, "recording error returned before all lanes finished");
            calls = 0;
            bool nestedRejected = false;
            scheduler.submit([&]
            {
                try { pool.RunParallel([&](uint32_t) { ++calls; }, 4); }
                catch (const std::logic_error&) { nestedRejected = true; }
            }).wait();
            require(nestedRejected && 0 == calls, "nested recording submitted work before rejection");
            scheduler.shutdown();
        }

        // A failed/stopped recording must leave no open native target behind.
        // Reuse the same slot and then really submit it; native validation catches
        // Reset/Begin on an unclosed target, while the graph must publish no failed batch.
        std::string error;
        require(resources.BeginFrame(error), error);
        EnhancedRenderGraph graph(static_cast<IRenderDeviceServices&>(resources));
        graph.SetParallelRecordCostThreshold(0);
        bool throwPass = false;
        calls = 0;
        for (uint32_t lane = 0; lane < 4; ++lane)
            graph.AddPass("jobs." + std::to_string(lane), {},
                [&, lane](const EnhancedRenderGraph::ExecuteContext&)
                {
                    ++calls;
                    if (throwPass && 0 == lane) throw std::runtime_error("pass failure probe");
                }, true);
        require(graph.Compile(error), error);
        RHIRecordedBatchDesc desc{};
        desc.backendGeneration = GetRHISubmissionThread().GetOwnerGeneration(&resources);
        RHIRecordedBatch batch;
        pool.BeginFrame(0);
        require(!graph.RecordParallel(pool, 4, desc, batch, error) && !batch.IsValid() &&
            0 == calls && error.find("stopped") != std::string::npos,
            "stopped graph recording did not report failure without a batch");
        scheduler.start(1);
        bool graphNestedRejected = false;
        scheduler.submit([&]
        {
            std::string nestedError;
            RHIRecordedBatch nestedBatch;
            graphNestedRejected = !graph.RecordParallel(pool, 4, desc, nestedBatch, nestedError)
                && !nestedBatch.IsValid() && !nestedError.empty();
        }).wait();
        require(graphNestedRejected && 0 == calls, "graph recording touched native state on a worker");
        pool.BeginFrame(0);
        throwPass = true;
        require(!graph.RecordParallel(pool, 4, desc, batch, error) && !batch.IsValid() &&
            4 == calls && error.find("pass failure probe") != std::string::npos,
            "failed graph recording did not drain all lanes or published a batch");
        pool.BeginFrame(0);
        throwPass = false;
        require(graph.RecordParallel(pool, 4, desc, batch, error) && 8 == calls,
            "recording did not recover after a failed pass");
        RHISubmissionTicket ticket;
        require(GetRHISubmissionThread().EnqueueRecordedBatch(&resources, resources,
            std::move(batch), ticket, error), error);
        require(resources.EndFrame(error), error);
        require(GetRHISubmissionThread().Wait(ticket, error), error);
        resources.WaitForGpu();
        outLog += "[jobs] enkiTS workers=1/4 lanes=1/4/clamped: exactly once, no caller execution, "
            "exception drain, nested/stopped rejection, native reset/recovery passed\n";
        return true;
    }
    catch (const std::exception& e)
    {
        resources.AbortFrame();
        resources.WaitForGpu();
        outLog += "[jobs] failure: " + std::string(e.what()) + "\n";
        return false;
    }
}
