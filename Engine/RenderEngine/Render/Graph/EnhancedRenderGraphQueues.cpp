#include "EnhancedRenderGraph.h"
#include <algorithm>
#include <chrono>
#include <stdexcept>

void EnhancedRenderGraph::RequireQueueIdle() const
{
    if (!m_queueLease.expired())
    {
        throw std::logic_error("Graph is retained by queue recordings; collect completed batches before mutation");
    }
}

bool EnhancedRenderGraph::SubmitQueues(const std::shared_ptr<EnhancedRenderGraph>& self,
    IRHIQueueRecorder& recorder, QueueEndpoint& graphics, QueueEndpoint* compute,
    const std::vector<QueueHint>& hints, uint64_t minimumGpuNanoseconds,
    std::shared_ptr<const void> owner, QueueExecution& output, std::string& outError)
{
    output = {};
    outError.clear();
    if (!self || self.get() != this || !owner || !m_queueLease.expired() || m_queueExecutionAttempted ||
        m_statesCommitted || m_recordedRecording != 0 || m_preparedPool || m_aliasing || (m_profiler && compute && (!graphics.profiler || !compute->profiler)) ||
        m_scheduling != RGSchedulingMode::ExplicitVersioned || &recorder.DeviceServices() != m_deviceServices ||
        !graphics.queue || graphics.queue->GetIdentity().kind != RHIQueueKind::Graphics)
    {
        outError = "Queue execution requires a fresh owned versioned graph, matching services, pinned storage and nonaliased resources";
        return false;
    }
    if (compute && ((graphics.profiler != nullptr) != (compute->profiler != nullptr) ||
        (graphics.profiler && graphics.profiler == compute->profiler)))
    {
        outError = "Measured multi-queue execution requires distinct queue-local profilers";
        return false;
    }
    const auto graphicsId = graphics.queue->GetIdentity();
    if (!graphicsId.IsValid() || (compute && (!compute->queue || !compute->queue->GetIdentity().IsValid() ||
        compute->queue->GetIdentity().kind != RHIQueueKind::Compute ||
        compute->queue->GetIdentity().deviceGeneration != graphicsId.deviceGeneration ||
        compute->queue->GetIdentity().queueId == graphicsId.queueId)))
    {
        outError = "Queue endpoints must belong to the same device incarnation";
        return false;
    }
    QueueSchedule plan;
    if (!BuildQueueSchedule({true, compute != nullptr, false, compute != nullptr}, hints,
        minimumGpuNanoseconds, plan, outError))
    {
        return false;
    }
    // A queue's wait on a newer producer point covers all older points from
    // that producer queue. Keep the earliest required frontier, then split
    // batches at its producer/consumer so independent work is not held back.
    std::vector<size_t> positions(m_passes.size(), 0);
    for (size_t index = 0; index < plan.entries.size(); ++index)
    {
        positions[plan.entries[index].pass] = index + 1;
    }
    size_t graphicsFrontier = 0, computeFrontier = 0;
    std::vector<QueueSchedule::Wait> waits;
    std::vector<bool> incoming(m_passes.size(), false), outgoing(m_passes.size(), false);
    for (const auto& entry : plan.entries)
    {
        const QueueSchedule::Wait* newest = nullptr;
        for (const auto& wait : plan.waits)
        {
            if (wait.consumer == entry.pass && (!newest || positions[wait.producer] > positions[newest->producer]))
            {
                newest = &wait;
            }
        }
        auto& frontier = entry.queue == RHIQueueKind::Compute ? computeFrontier : graphicsFrontier;
        if (newest && positions[newest->producer] > frontier)
        {
            frontier = positions[newest->producer];
            waits.push_back(*newest);
            incoming[newest->consumer] = true;
            outgoing[newest->producer] = true;
        }
    }
    plan.waits = std::move(waits);
    const auto count = plan.entries.size() + 2;
    const auto validCounter = [&](const QueueEndpoint& endpoint)
    {
        return endpoint.nextSignal > 0 && endpoint.nextSignal < UINT64_MAX - count;
    };
    if (!validCounter(graphics) || (compute && !validCounter(*compute)))
    {
        outError = "Queue signal range is exhausted";
        return false;
    }
    // Destruction order matters: graph releases its handles before external storage.
    struct Lease
    {
        std::shared_ptr<const void> owner;
        std::shared_ptr<EnhancedRenderGraph> graph;
    };
    auto lease = std::make_shared<Lease>(Lease{std::move(owner), self});
    m_queueLease = lease;
    struct Recording
    {
        QueueEndpoint* endpoint;
        std::shared_ptr<IRHIQueueCommandBatch> batch;
        std::vector<uint16_t> passes;
    };
    std::vector<Recording> recordings;
    const auto recordingStart = std::chrono::steady_clock::now();
    recordings.reserve(count);
    const auto record = [&](QueueEndpoint& endpoint, const std::function<void(RHIEncoder&)>& commands)
    {
        Recording recording{&endpoint, {}};
        if (!recorder.Record(endpoint.queue, commands, lease, recording.batch, outError))
        {
            return false;
        }
        if (!recording.batch || recording.batch->GetQueueIdentity() != endpoint.queue->GetIdentity())
        {
            outError = "Recorder returned a foreign batch";
            return false;
        }
        recordings.push_back(std::move(recording));
        return true;
    };
    std::vector<bool> batchEnds(m_passes.size(), false);
    for (size_t index = 0; index < plan.entries.size(); ++index)
    {
        batchEnds[plan.entries[index].pass] = index + 1 == plan.entries.size() ||
            plan.entries[index + 1].queue != plan.entries[index].queue ||
            outgoing[plan.entries[index].pass] || incoming[plan.entries[index + 1].pass];
    }
    // Specialization consumes this compiled graph even on recording rejection.
    // Reset is required before retry: Execute must never use a queue-specialized
    // pass plan without its prologue and epilogue.
    m_queueExecutionAttempted = true;
    PhaseBarrierPlan prologue, epilogue;
    if (plan.usesCompute)
    {
        PlanBarriers(&batchEnds, &prologue, &epilogue);
    }
    output.plannedBarriers = m_stats.barriersEmitted;
    const auto recordBoundary = [&](RHIEncoder& encoder, const PhaseBarrierPlan& plan)
    {
        RHIBarrierBatch barriers{};
        barriers.textureTransitions = plan.transitions;
        barriers.bufferTransitions = plan.bufferTransitions;
        if (!barriers.IsEmpty())
        {
            encoder.ResourceBarriers(barriers);
        }
    };
    if (!record(graphics, [&](RHIEncoder& encoder) { recordBoundary(encoder, prologue); }))
    {
        return false;
    }
    for (size_t begin = 0; begin < plan.entries.size();)
    {
        size_t end = begin + 1;
        while (end < plan.entries.size() && !batchEnds[plan.entries[end - 1].pass])
        {
            ++end;
        }
        auto& endpoint = plan.entries[begin].queue == RHIQueueKind::Compute ? *compute : graphics;
        if (!record(endpoint, [&](RHIEncoder& encoder)
        {
            for (size_t index = begin; index < end; ++index)
            {
                const auto& entry = plan.entries[index];
                const auto& pass = m_passes[entry.pass];
                auto* profiler = endpoint.profiler ? endpoint.profiler : m_profiler;
                const auto timerSlot = profiler ? profiler->BeginPass(encoder, pass.name) : IRHIGpuProfiler::kInvalidSlot;
                struct PassTimer
                {
                    IRHIGpuProfiler* profiler;
                    RHIEncoder& encoder;
                    uint32_t slot;
                    ~PassTimer()
                    {
                        if (profiler)
                        {
                            profiler->EndPass(encoder, slot);
                        }
                    }
                } timer{profiler, encoder, timerSlot};
                ExecuteContext context{this, &encoder};
                RecordPassBarriers(encoder, pass);
                RecordPassBody(context, pass, 0, 1);
                RecordPassFinalBarriers(encoder, pass);
            }
        }))
        {
            return false;
        }
        for (size_t index = begin; index < end; ++index)
        {
            recordings.back().passes.push_back(plan.entries[index].pass);
        }
        begin = end;
    }
    if (!record(graphics, [&](RHIEncoder& encoder) { recordBoundary(encoder, epilogue); }))
    {
        return false;
    }
    // No callbacks or allocation of recordings after submission begins.
    std::vector<RHITimelinePoint> points(m_passes.size());
    RHITimelinePoint prefix, lastCompute;
    output.recordingMilliseconds = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - recordingStart).count();
    const auto failSubmission = [&]
    {
        output.recoveryRequired = true;
        // A partially executed graph must never return resources with planned final
        // states to a reusable pool. Q0 retains submitted/quarantined batches.
        m_transientPool = nullptr;
        return false;
    };
    const auto submit = [&](Recording& recording, RHITimelinePoint& point)
    {
        m_queueExecutionAttempted = true;
        output.submissionAttempted = true;
        auto& endpoint = *recording.endpoint;
        if (!endpoint.queue->Submit(recording.batch, endpoint.nextSignal, point, outError))
        {
            return false;
        }
        ++endpoint.nextSignal;
        ++output.submittedBatches;
        if (endpoint.queue->GetIdentity().kind == RHIQueueKind::Compute)
        {
            ++output.computeBatches;
        }
        return true;
    };
    try
    {
        if (!submit(recordings.front(), prefix))
        {
            return failSubmission();
        }
        uint64_t graphicsWaitedCompute = 0;
        uint64_t computeWaitedGraphics = 0;
        for (size_t index = 1; index + 1 < recordings.size(); ++index)
        {
            auto& recording = recordings[index];
            const bool onCompute = recording.endpoint == compute;
            RHITimelinePoint waitPoint;
            if (onCompute && !lastCompute.IsValid())
            {
                waitPoint = prefix;
            }
            // One wait on the newest producer value subsumes older values from
            // that queue, including duplicate resource edges and earlier batches.
            for (const auto& wait : plan.waits)
            {
                if (std::find(recording.passes.begin(), recording.passes.end(), wait.consumer) != recording.passes.end() &&
                    points[wait.producer].value > waitPoint.value)
                {
                    waitPoint = points[wait.producer];
                }
            }
            auto& waited = onCompute ? computeWaitedGraphics : graphicsWaitedCompute;
            if (waitPoint.IsValid() && waitPoint.value > waited)
            {
                if (!recording.endpoint->queue->Wait(waitPoint, outError))
                {
                    return failSubmission();
                }
                waited = waitPoint.value;
            }
            RHITimelinePoint completion;
            if (!submit(recording, completion))
            {
                return failSubmission();
            }
            for (const auto pass : recording.passes)
            {
                points[pass] = completion;
            }
            if (onCompute)
            {
                lastCompute = completion;
            }
        }
        if (lastCompute.IsValid() && lastCompute.value > graphicsWaitedCompute &&
            !graphics.queue->Wait(lastCompute, outError))
        {
            return failSubmission();
        }
        if (!submit(recordings.back(), output.completion))
        {
            return failSubmission();
        }
        for (const auto& resource : m_resources)
        {
            if (resource.used && resource.writeback)
            {
                *resource.writeback = resource.state;
            }
        }
    }
    catch (const std::exception& exception)
    {
        outError = exception.what();
        return failSubmission();
    }
    catch (...)
    {
        outError = "Queue submission threw an unknown exception";
        return failSubmission();
    }
    m_statesCommitted = true;
    return true;
}
