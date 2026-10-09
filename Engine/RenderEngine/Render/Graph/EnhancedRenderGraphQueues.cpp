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
    // The report survives every return path, including recording and submission
    // rejection. CPU queue-operation tickets are inside submissionMilliseconds;
    // these durations must never be presented as CPU waits for GPU completion.
    struct CpuReport
    {
        QueueExecution& output;
        QueueExecution* diagnostic{nullptr};
        std::chrono::steady_clock::time_point start{std::chrono::steady_clock::now()};
        std::chrono::steady_clock::time_point phaseStart{start};
        enum class Phase { Schedule, Record, Submit } phase{Phase::Schedule};
        void FinishPhase()
        {
            const auto now = std::chrono::steady_clock::now();
            const double elapsed = std::chrono::duration<double, std::milli>(now - phaseStart).count();
            if (phase == Phase::Schedule)
            {
                output.scheduleMilliseconds += elapsed;
            }
            else if (phase == Phase::Record)
            {
                output.recordingMilliseconds += elapsed;
            }
            else
            {
                output.submissionMilliseconds += elapsed;
            }
            phaseStart = now;
        }
        ~CpuReport()
        {
            FinishPhase();
            output.totalMilliseconds = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count();
            if (diagnostic)
            {
                *diagnostic = output;
                diagnostic->completion = {}; // Diagnostic snapshots never retain native fence ownership.
            }
        }
    } cpu{output};
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
    m_queueDiagnostics = {};
    auto& diagnostic = m_queueDiagnostics;
    cpu.diagnostic = &diagnostic.execution;
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
        for (size_t index = 0; index < plan.entries.size(); ++index)
        {
            m_executeOrder[index] = plan.entries[index].pass;
        }
        UpdateResourceUses();
        PlanBarriers(&batchEnds, &prologue, &epilogue);
    }
    diagnostic.specialized = true;
    diagnostic.schedule = plan;
    output.predictedSerialNanoseconds = plan.predictedSerialNanoseconds;
    output.predictedNanoseconds = plan.predictedNanoseconds;
    output.plannedBarriers = m_stats.barriersEmitted;
    output.prologueBarriers = static_cast<uint32_t>(prologue.transitions.size() + prologue.bufferTransitions.size());
    output.epilogueBarriers = static_cast<uint32_t>(epilogue.transitions.size() + epilogue.bufferTransitions.size());
    diagnostic.batches.reserve(count);
    diagnostic.batches.push_back({RHIQueueKind::Graphics, {}, output.prologueBarriers});
    std::vector<uint32_t> passBatch(m_passes.size(), UINT32_MAX);
    for (size_t begin = 0; begin < plan.entries.size();)
    {
        size_t end = begin + 1;
        while (end < plan.entries.size() && !batchEnds[plan.entries[end - 1].pass])
        {
            ++end;
        }
        QueueDiagnostics::Batch batch;
        batch.queue = plan.entries[begin].queue;
        batch.passes.reserve(end - begin);
        for (size_t index = begin; index < end; ++index)
        {
            const auto pass = plan.entries[index].pass;
            passBatch[pass] = static_cast<uint32_t>(diagnostic.batches.size());
            batch.passes.push_back(pass);
            batch.barriers += GetPassBarrierCount(RGPassId{pass});
        }
        output.plannedComputeBatches += batch.queue == RHIQueueKind::Compute ? 1u : 0u;
        diagnostic.batches.push_back(std::move(batch));
        begin = end;
    }
    diagnostic.batches.push_back({RHIQueueKind::Graphics, {}, output.epilogueBarriers});
    output.plannedBatches = static_cast<uint32_t>(diagnostic.batches.size());
    // Build the exact wait list before recording/submission. A batch index plus
    // one is a monotonic frontier on its producer queue; zero means no wait.
    // Both execution and diagnostics use this list, never reconstruct it later.
    uint32_t graphicsWaitedCompute = 0, computeWaitedGraphics = 0, lastCompute = 0;
    for (uint32_t index = 1; index + 1 < diagnostic.batches.size(); ++index)
    {
        const bool onCompute = diagnostic.batches[index].queue == RHIQueueKind::Compute;
        uint32_t producer = onCompute && lastCompute == 0 ? 1u : 0u; // Prologue batch zero.
        for (const auto& wait : plan.waits)
        {
            if (passBatch[wait.consumer] == index)
            {
                producer = (std::max)(producer, passBatch[wait.producer] + 1);
            }
        }
        auto& waited = onCompute ? computeWaitedGraphics : graphicsWaitedCompute;
        if (producer > waited)
        {
            diagnostic.waits.push_back({producer - 1, index});
            waited = producer;
        }
        if (onCompute)
        {
            lastCompute = index + 1;
        }
    }
    if (lastCompute > graphicsWaitedCompute)
    {
        diagnostic.waits.push_back({lastCompute - 1, output.plannedBatches - 1});
    }
    output.plannedWaits = static_cast<uint32_t>(diagnostic.waits.size());

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
    };
    std::vector<Recording> recordings;
    recordings.reserve(output.plannedBatches);
    std::vector<RHITimelinePoint> points(output.plannedBatches);
    const auto recordBoundary = [&](RHIEncoder& encoder, const PhaseBarrierPlan& boundary)
    {
        RHIBarrierBatch barriers{};
        barriers.textureTransitions = boundary.transitions;
        barriers.bufferTransitions = boundary.bufferTransitions;
        if (!barriers.IsEmpty())
        {
            encoder.ResourceBarriers(barriers);
        }
    };
    cpu.FinishPhase();
    cpu.phase = CpuReport::Phase::Record;
    for (uint32_t index = 0; index < output.plannedBatches; ++index)
    {
        const auto& batch = diagnostic.batches[index];
        auto& endpoint = batch.queue == RHIQueueKind::Compute ? *compute : graphics;
        Recording recording{&endpoint, {}};
        if (!recorder.Record(endpoint.queue, [&](RHIEncoder& encoder)
        {
            if (index == 0)
            {
                recordBoundary(encoder, prologue);
            }
            else if (index + 1 == output.plannedBatches)
            {
                recordBoundary(encoder, epilogue);
            }
            for (const auto passIndex : batch.passes)
            {
                const auto& pass = m_passes[passIndex];
                auto* profiler = endpoint.profiler ? endpoint.profiler : m_profiler;
                const auto timerSlot = profiler ? profiler->BeginPass(encoder, pass.name,
                    TimingIdentity(passIndex)) : IRHIGpuProfiler::kInvalidSlot;
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
        }, lease, recording.batch, outError))
        {
            return false;
        }
        if (!recording.batch || recording.batch->GetQueueIdentity() != endpoint.queue->GetIdentity())
        {
            outError = "Recorder returned a foreign batch";
            return false;
        }
        recordings.push_back(std::move(recording));
    }
    // No callbacks or allocation of recordings/diagnostics after submission begins.
    cpu.FinishPhase();
    cpu.phase = CpuReport::Phase::Submit;
    const auto failSubmission = [&]
    {
        output.recoveryRequired = true;
        m_transientPool = nullptr; // Partial execution cannot re-enter the reusable pool.
        return false;
    };
    try
    {
        size_t nextWait = 0;
        for (uint32_t index = 0; index < output.plannedBatches; ++index)
        {
            auto& recording = recordings[index];
            auto& endpoint = *recording.endpoint;
            while (nextWait < diagnostic.waits.size() && diagnostic.waits[nextWait].consumerBatch == index)
            {
                auto& wait = diagnostic.waits[nextWait];
                if (!points[wait.producerBatch].IsValid() ||
                    !endpoint.queue->Wait(points[wait.producerBatch], outError))
                {
                    return failSubmission();
                }
                wait.submitted = true;
                ++output.submittedWaits;
                ++nextWait;
            }
            output.submissionAttempted = true;
            if (!endpoint.queue->Submit(recording.batch, endpoint.nextSignal, points[index], outError))
            {
                return failSubmission();
            }
            ++endpoint.nextSignal;
            diagnostic.batches[index].submitted = true;
            ++output.submittedBatches;
            output.computeBatches += diagnostic.batches[index].queue == RHIQueueKind::Compute ? 1u : 0u;
        }
        output.completion = points.back();
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
    output.completed = true;
    return true;
}
