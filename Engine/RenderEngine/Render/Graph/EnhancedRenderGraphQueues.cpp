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
    diagnostic.requestedExecutionMode = GetQueueExecutionMode();
    diagnostic.measurementDomain = GetMeasurementDomain();
    cpu.diagnostic = &diagnostic.execution;
    QueueSchedule plan;
    if (!BuildQueueSchedule({true, compute != nullptr, false, compute != nullptr}, hints,
        minimumGpuNanoseconds, plan, outError))
    {
        return false;
    }
    diagnostic.effectiveExecutionMode = plan.usesCompute ? 2u : 1u;
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
        std::vector<bool> retainedGraphicsTextures(m_resources.size(), false);
        for (size_t index = 0; index < m_resources.size(); ++index)
        {
            const auto& resource = m_resources[index];
            if (resource.used && !resource.IsBuffer())
            {
                retainedGraphicsTextures[index] = m_deviceServices->DescribeTexture(resource.handle).
                    retainsExplicitStateOnGraphicsQueue;
            }
        }
        const auto rejectComputeUses = [&](const std::vector<RGPassUsage>& usages)
        {
            for (const auto& usage : usages)
            {
                if (usage.handle.IsValid() && usage.handle.index < retainedGraphicsTextures.size())
                {
                    retainedGraphicsTextures[usage.handle.index] = false;
                }
            }
        };
        for (const auto& entry : plan.entries)
        {
            if (entry.queue == RHIQueueKind::Compute)
            {
                const auto& pass = m_passes[entry.pass];
                rejectComputeUses(pass.usages);
                for (const auto& phase : pass.phases)
                {
                    rejectComputeUses(phase.usages);
                }
            }
        }
        PlanBarriers(&batchEnds, &prologue, &epilogue, &retainedGraphicsTextures);
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
        std::shared_ptr<IRHIQueueRecording> targets;
    };
    std::vector<Recording> recordings(output.plannedBatches);
    std::vector<RHITimelinePoint> points(output.plannedBatches);
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
    };
    const auto recordBoundary = [&](RHIEncoder& encoder, const PhaseBarrierPlan& boundary, const char* name)
    {
        auto* profiler = graphics.profiler ? graphics.profiler : m_profiler;
        // Separate scopes include boundary work in graph elapsed GPU time without
        // fabricating graphics activity across queue waits. Invalid identities
        // keep these diagnostic scopes out of measured pass scheduling costs.
        const auto slot = profiler ? profiler->BeginPass(encoder, name, {}) : IRHIGpuProfiler::kInvalidSlot;
        PassTimer timer{profiler, encoder, slot};
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
    const auto recordPass = [&](RHIEncoder& encoder, QueueEndpoint& endpoint,
        uint16_t passIndex, uint32_t slice, uint32_t sliceCount)
    {
        const auto& pass = m_passes[passIndex];
        auto* profiler = endpoint.profiler ? endpoint.profiler : m_profiler;
        const auto timerSlot = profiler ? profiler->BeginPass(encoder, pass.name,
            TimingIdentity(passIndex)) : IRHIGpuProfiler::kInvalidSlot;
        PassTimer timer{profiler, encoder, timerSlot};
        ExecuteContext context{this, &encoder};
        if (slice == 0)
        {
            RecordPassBarriers(encoder, pass);
        }
        RecordPassBody(context, pass, slice, sliceCount);
        if (slice + 1 == sliceCount)
        {
            RecordPassFinalBarriers(encoder, pass);
        }
    };
    uint32_t totalRecordCost = 0;
    bool recordingSideEffect = false;
    for (const auto& entry : plan.entries)
    {
        totalRecordCost += m_passes[entry.pass].recordCost;
        recordingSideEffect = recordingSideEffect || m_passes[entry.pass].recordingSideEffect;
    }
    m_stats.totalRecordCost = totalRecordCost;
    m_stats.parallelDeclined = recordingSideEffect ||
        (totalRecordCost != 0 && totalRecordCost < m_parallelCostThreshold);
    const uint32_t workers = m_stats.parallelDeclined ? 1u : (std::max)(1u, recorder.GetWorkerCount());
    for (uint32_t index = 0; index < output.plannedBatches; ++index)
    {
        recordings[index].endpoint = diagnostic.batches[index].queue == RHIQueueKind::Compute ? compute : &graphics;
    }
    try
    {
        if (recorder.GetWorkerCount() == 0)
        {
            m_stats.recordWorkers = 1;
            m_stats.recordedLists = output.plannedBatches;
            m_stats.recordUnits = static_cast<uint32_t>(plan.entries.size());
            m_stats.recordingWaveCount = 1;
            // Existing serial adapters retain their Record contract.
            for (uint32_t index = 0; index < output.plannedBatches; ++index)
            {
                auto& recording = recordings[index];
                if (!recorder.Record(recording.endpoint->queue, [&](RHIEncoder& encoder)
                {
                    if (index == 0)
                    {
                        recordBoundary(encoder, prologue, "RenderGraph.QueuePrologue");
                    }
                    else if (index + 1 == output.plannedBatches)
                    {
                        recordBoundary(encoder, epilogue, "RenderGraph.QueueEpilogue");
                    }
                    for (const auto pass : diagnostic.batches[index].passes)
                    {
                        recordPass(encoder, *recording.endpoint, pass, 0, 1);
                    }
                }, lease, recording.batch, outError))
                {
                    return false;
                }
            }
        }
        else
        {
            if (thread_pool::is_worker_thread())
            {
                outError = "Queue recording cannot join nested engine jobs.";
                return false;
            }
            struct Unit
            {
                uint32_t batch, target, wave;
                uint16_t pass;
                uint32_t slice, sliceCount;
            };
            struct Target
            {
                uint32_t batch, local;
                std::vector<size_t> units;
                size_t cursor{0};
            };
            std::vector<Unit> units;
            std::vector<Target> targets;
            for (uint32_t batch = 0; batch < output.plannedBatches; ++batch)
            {
                const size_t begin = units.size();
                if (diagnostic.batches[batch].passes.empty())
                {
                    units.push_back({batch, 0, 0, UINT16_MAX, 0, 1});
                }
                for (const auto passIndex : diagnostic.batches[batch].passes)
                {
                    const auto& pass = m_passes[passIndex];
                    const uint32_t slices = pass.splitExecute && !pass.recordingSideEffect
                        ? (std::max)(1u, (std::min)(pass.maxSlices, workers)) : 1u;
                    for (uint32_t slice = 0; slice < slices; ++slice)
                    {
                        units.push_back({batch, 0, 0, passIndex, slice, slices});
                    }
                }
                const size_t unitCount = units.size() - begin;
                const uint32_t targetCount = (std::min)(workers, static_cast<uint32_t>(unitCount));
                const uint32_t firstTarget = static_cast<uint32_t>(targets.size());
                for (uint32_t target = 0; target < targetCount; ++target)
                {
                    targets.push_back({batch, target, {}});
                }
                for (size_t index = begin; index < units.size(); ++index)
                {
                    const uint32_t target = firstTarget + static_cast<uint32_t>((index - begin) * targetCount / unitCount);
                    units[index].target = target;
                    targets[target].units.push_back(index);
                }
                auto& recording = recordings[batch];
                if (!recorder.BeginRecording(recording.endpoint->queue, targetCount, lease, recording.targets, outError))
                {
                    return false;
                }
                if (!recording.targets)
                {
                    outError = "Queue recorder returned no command targets.";
                    return false;
                }
            }
            m_stats.recordUnits = static_cast<uint32_t>(units.size()) - 2u;
            m_stats.recordWorkers = (std::min)(workers, (std::max)(1u, m_stats.recordUnits));
            m_stats.recordedLists = static_cast<uint32_t>(targets.size());
            // RG4's contiguous targets preserve list submission order. Dependency
            // waves additionally preserve callback-produced CPU data across jobs.
            std::vector<std::vector<uint16_t>> predecessors(m_passes.size());
            for (const auto& edge : m_versionEdges)
            {
                predecessors[edge.consumer].push_back(static_cast<uint16_t>(edge.producer));
            }
            std::vector<uint32_t> passWave(m_passes.size(), 0), targetWave(targets.size(), 0);
            uint32_t waveCount = 1;
            for (size_t begin = 0; begin < units.size();)
            {
                size_t end = begin + 1;
                while (end < units.size() && units[end].batch == units[begin].batch && units[end].pass == units[begin].pass)
                {
                    ++end;
                }
                const uint16_t pass = units[begin].pass;
                uint32_t wave = 0;
                if (pass != UINT16_MAX)
                {
                    for (const auto producer : predecessors[pass])
                    {
                        wave = (std::max)(wave, passWave[producer] + 1);
                    }
                }
                for (size_t index = begin; index < end; ++index)
                {
                    wave = (std::max)(wave, targetWave[units[index].target]);
                }
                for (size_t index = begin; index < end; ++index)
                {
                    units[index].wave = wave;
                    targetWave[units[index].target] = wave;
                }
                if (pass != UINT16_MAX)
                {
                    passWave[pass] = wave;
                }
                waveCount = (std::max)(waveCount, wave + 1);
                begin = end;
            }
            std::vector<std::vector<uint32_t>> waveTargets(waveCount);
            for (uint32_t target = 0; target < targets.size(); ++target)
            {
                uint32_t previous = UINT32_MAX;
                for (const auto index : targets[target].units)
                {
                    const auto& unit = units[index];
                    if (unit.wave != previous)
                    {
                        waveTargets[unit.wave].push_back(target);
                        previous = unit.wave;
                    }
                }
            }
            const auto recordTarget = [&](uint32_t targetIndex, uint32_t wave)
            {
                auto& target = targets[targetIndex];
                auto& recording = recordings[target.batch];
                while (target.cursor < target.units.size())
                {
                    const auto& unit = units[target.units[target.cursor]];
                    if (unit.wave != wave)
                    {
                        break;
                    }
                    ++target.cursor;
                    auto& encoder = recording.targets->AcquireEncoder(target.local);
                    if (unit.pass == UINT16_MAX)
                    {
                        recordBoundary(encoder, unit.batch == 0 ? prologue : epilogue,
                            unit.batch == 0 ? "RenderGraph.QueuePrologue" : "RenderGraph.QueueEpilogue");
                    }
                    else
                    {
                        recordPass(encoder, *recording.endpoint, unit.pass, unit.slice, unit.sliceCount);
                    }
                }
            };
            const auto recordSerial = [&]
            {
                for (const auto& unit : units)
                {
                    recordTarget(unit.target, unit.wave);
                }
            };
            m_stats.recordingWaveCount = workers == 1 ? 1u : waveCount;
            if (recordingSideEffect)
            {
                // Explicit CPU side effects keep the entire graph unsplit on
                // its owner, before any queue submission begins.
                recordSerial();
            }
            else if (workers == 1)
            {
                recorder.RunParallel([&](uint32_t) { recordSerial(); }, 1);
            }
            for (uint32_t wave = 0; workers > 1 && !recordingSideEffect && wave < waveCount;)
            {
                if (waveTargets[wave].size() == 1)
                {
                    const uint32_t begin = wave++;
                    while (wave < waveCount && waveTargets[wave].size() == 1)
                    {
                        ++wave;
                    }
                    const uint32_t end = wave;
                    recorder.RunParallel([&](uint32_t)
                    {
                        for (uint32_t run = begin; run < end; ++run)
                        {
                            recordTarget(waveTargets[run].front(), run);
                        }
                    }, 1);
                }
                else
                {
                    const auto& active = waveTargets[wave];
                    const uint32_t jobs = (std::min)(workers, static_cast<uint32_t>(active.size()));
                    recorder.RunParallel([&](uint32_t job)
                    {
                        for (size_t index = job; index < active.size(); index += jobs)
                        {
                            recordTarget(active[index], wave);
                        }
                    }, jobs);
                    ++wave;
                }
            }
            for (auto& recording : recordings)
            {
                if (!recording.targets->Finish(recording.batch, outError))
                {
                    return false;
                }
                recording.targets.reset();
            }
        }
    }
    catch (const std::exception& exception)
    {
        outError = std::string("Queue recording job failed: ") + exception.what();
        return false;
    }
    catch (...)
    {
        outError = "Queue recording job failed with an unknown exception.";
        return false;
    }
    for (const auto& recording : recordings)
    {
        if (!recording.batch || recording.batch->GetQueueIdentity() != recording.endpoint->queue->GetIdentity())
        {
            outError = "Recorder returned a foreign batch";
            return false;
        }
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
        if (!recorder.ExecuteSubmission([&](std::string& error)
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
                        !endpoint.queue->Wait(points[wait.producerBatch], error))
                    {
                        return false;
                    }
                    wait.submitted = true;
                    ++output.submittedWaits;
                    ++nextWait;
                }
                output.submissionAttempted = true;
                if (!endpoint.queue->Submit(recording.batch, endpoint.nextSignal, points[index], error))
                {
                    return false;
                }
                ++endpoint.nextSignal;
                diagnostic.batches[index].submitted = true;
                ++output.submittedBatches;
                output.computeBatches += diagnostic.batches[index].queue == RHIQueueKind::Compute ? 1u : 0u;
            }
            return true;
        }, outError))
        {
            return failSubmission();
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
