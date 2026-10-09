#pragma once
#include "Render/Graph/EnhancedRenderGraph.h"
#include "RHI/DX12/DX12DeviceResources.h"
#include <stdexcept>
#include <algorithm>
#include <memory>
#include <vector>
#include <optional>

bool DX12Test::RunQueueScheduleTest(std::string& outLog)
{
    uint32_t checks = 0;
    const auto require = [&](bool accepted, const std::string& message)
    {
        ++checks;
        if (!accepted)
        {
            throw std::runtime_error(message);
        }
    };
    try
    {
        DX12DeviceResources device;
        std::string error;
        require(device.Initialize(32, 32, error), error);
        EnhancedRenderGraph graph(device, RGSchedulingMode::ExplicitVersioned);
        EnhancedRenderGraph::QueueSchedule plan;
        const RHIQueueCapabilities caps{true, true, true, true};
        require(!graph.BuildQueueSchedule(caps, {}, 100, plan, error) && plan.entries.empty(), "Uncompiled graph admitted.");
        auto buffer = graph.CreateBuffer({256, true, false, "shared"});
        auto written = graph.Write(buffer);
        uint32_t callbacks = 0;
        auto body = [&](const EnhancedRenderGraph::ExecuteContext&) { ++callbacks; };
        auto producer = graph.AddPass("producer", {{written, RHIResourceState::UnorderedAccess, RGAccessMode::Write}}, body);
        auto compute = graph.AddPass("compute-reader", {{written, RHIResourceState::ShaderResource, RGAccessMode::Read}}, body, true);
        auto reader = graph.AddPass("graphics-reader", {{written, RHIResourceState::CopySource, RGAccessMode::Read}}, body, true);
        auto independent = graph.AddPass("independent", {}, body, true);
        auto culled = graph.AddPass("culled", {}, body);
        require(graph.Compile(error), error);
        std::vector<EnhancedRenderGraph::QueueHint> hints{{compute, true, 100}, {culled, true, 1000}};
        require(graph.BuildQueueSchedule(caps, hints, 100, plan, error), error);
        require(plan.entries.size() == 4 && plan.usesCompute && plan.compileGeneration != 0, "Live pass plan mismatch.");
        require(callbacks == 0, "Planning executed a callback.");
        require(plan.entries[1].pass == compute.index && plan.entries[1].queue == RHIQueueKind::Compute, "Eligible compute pass misplaced.");
        const auto hasWait = [&](RGPassId from, RGPassId to)
        {
            return std::any_of(plan.waits.begin(), plan.waits.end(), [&](const auto& wait)
            {
                return wait.producer == from.index && wait.consumer == to.index && wait.resource == buffer.index;
            });
        };
        require(hasWait(producer, compute), "RAW queue wait missing.");
        require(hasWait(compute, reader), "Read/read physical ownership wait missing.");
        require(plan.waits.size() == 2, "Duplicate or unnecessary queue wait.");
        require(plan.entries.back().pass == independent.index, "Independent graphics pass removed.");
        auto unsupported = caps;
        unsupported.compute = false;
        require(graph.BuildQueueSchedule(unsupported, hints, 100, plan, error) && !plan.usesCompute && plan.waits.empty(), "Compute fallback failed.");
        unsupported = caps;
        unsupported.crossQueueTimeline = false;
        require(graph.BuildQueueSchedule(unsupported, hints, 100, plan, error) && !plan.usesCompute, "Timeline fallback failed.");
        require(graph.BuildQueueSchedule(caps, hints, 101, plan, error) && !plan.usesCompute, "Below-cost pass promoted.");
        require(graph.BuildQueueSchedule(caps, hints, 0, plan, error) && !plan.usesCompute, "Unmeasured threshold admitted.");
        hints[0].computeCompatible = false;
        require(graph.BuildQueueSchedule(caps, hints, 100, plan, error) && !plan.usesCompute, "Incompatible callback promoted.");
        hints[0].computeCompatible = true;
        hints.push_back(hints.front());
        require(!graph.BuildQueueSchedule(caps, hints, 100, plan, error) && plan.entries.empty(), "Duplicate hint admitted or stale output retained.");
        hints = {{{RGPassId::kInvalid}, true, 100}};
        require(!graph.BuildQueueSchedule(caps, hints, 100, plan, error), "Invalid pass admitted.");
        unsupported.graphics = false;
        require(!graph.BuildQueueSchedule(unsupported, {}, 100, plan, error), "Missing graphics capability admitted.");
        EnhancedRenderGraph legacy(device);
        auto legacyPass = legacy.AddPass("legacy", {}, body, true);
        require(legacy.Compile(error) && legacy.BuildQueueSchedule(caps, {{legacyPass, true, 100}}, 100, plan, error) && !plan.usesCompute, "Legacy ordering was moved to compute.");
        if (!device.BeginFrame(error))
        {
            throw std::runtime_error(error);
        }
        EnhancedRenderGraph aliased(device, RGSchedulingMode::ExplicitVersioned);
        aliased.SetTransientAliasing(true);
        auto aliasedPass = aliased.AddPass("alias-fallback", {}, body, true);
        require(aliased.Compile(error) && aliased.BuildQueueSchedule(caps, {{aliasedPass, true, 100}}, 100, plan, error) && !plan.usesCompute, "Single-order alias lifetime was overlapped.");
        device.AbortFrame();
        require(graph.BuildQueueSchedule(caps, {}, 100, plan, error), error);
        require(plan.entries.size() == graph.GetExecuteOrder().size() && std::equal(plan.entries.begin(), plan.entries.end(), graph.GetExecuteOrder().begin(), [](const auto& entry, auto pass)
        {
            return entry.pass == pass && entry.queue == RHIQueueKind::Graphics;
        }), "Fallback changed compiled DAG order.");
        graph.Reset();
        require(!graph.BuildQueueSchedule(caps, {}, 100, plan, error) && plan.entries.empty() && plan.compileGeneration == 0, "Reset graph retained a valid queue plan.");
        // Overlap placement on an SSAO-shaped graph. Costs (ns) are chosen so the
        // expected spans can be derived by hand with a 20 us handoff:
        // serial 400+500+200+200+300 = 1600 us. With shadows behind the compute
        // chain: GBuffer 0-500, Shadow 500-900, AO 520-920 (compute), Deferred
        // waits 940 -> 1240 us. Without shadows nothing overlaps (1240 > 1200).
        struct OverlapGraph
        {
            std::unique_ptr<EnhancedRenderGraph> graph;
            RGPassId shadow, gbuffer, raw, filter, deferred;
            uint32_t depthResource{0}, filteredResource{0};
            std::vector<EnhancedRenderGraph::QueueHint> hints;
        };
        const auto buildOverlap = [&](bool withShadow, bool declare, const EnhancedRenderGraph::QueueCostModel* model,
            RGOrderPolicy orderPolicy = RGOrderPolicy::DependencyOrder)
        {
            OverlapGraph result;
            result.graph = std::make_unique<EnhancedRenderGraph>(device, RGSchedulingMode::ExplicitVersioned,
                orderPolicy);
            auto& g = *result.graph;
            if (model)
            {
                g.SetQueueCostModel(*model);
            }
            const auto depthBuffer = g.CreateBuffer({256, true, false, "depth"});
            const auto rawBuffer = g.CreateBuffer({256, true, false, "ao-raw"});
            const auto filteredBuffer = g.CreateBuffer({256, true, false, "ao-filtered"});
            const auto colorBuffer = g.CreateBuffer({256, true, false, "color"});
            result.depthResource = depthBuffer.index;
            result.filteredResource = filteredBuffer.index;
            const auto depth = g.Write(depthBuffer);
            const auto raw = g.Write(rawBuffer);
            const auto filtered = g.Write(filteredBuffer);
            const auto color = g.Write(colorBuffer);
            RGHandle shadowMap{};
            if (withShadow)
            {
                // Authored first: the scheduler must move it behind GBuffer to overlap AO.
                shadowMap = g.Write(g.CreateBuffer({256, true, false, "shadow"}));
                result.shadow = g.AddPass("shadow", {{shadowMap, RHIResourceState::UnorderedAccess, RGAccessMode::Write}}, body);
            }
            result.gbuffer = g.AddPass("gbuffer", {{depth, RHIResourceState::UnorderedAccess, RGAccessMode::Write}}, body);
            result.raw = g.AddPass("ao-raw", {
                {depth, RHIResourceState::ShaderResource, RGAccessMode::Read},
                {raw, RHIResourceState::UnorderedAccess, RGAccessMode::Write}}, body);
            result.filter = g.AddPass("ao-filter", {
                {raw, RHIResourceState::ShaderResource, RGAccessMode::Read},
                {filtered, RHIResourceState::UnorderedAccess, RGAccessMode::Write}}, body);
            std::vector<EnhancedRenderGraph::RGPassUsage> deferredUsages{
                {depth, RHIResourceState::ShaderResource, RGAccessMode::Read},
                {filtered, RHIResourceState::ShaderResource, RGAccessMode::Read},
                {color, RHIResourceState::UnorderedAccess, RGAccessMode::Write}};
            if (withShadow)
            {
                deferredUsages.push_back({shadowMap, RHIResourceState::ShaderResource, RGAccessMode::Read});
            }
            result.deferred = g.AddPass("deferred", deferredUsages, body, true);
            if (declare)
            {
                g.DeclareComputeCompatible(result.raw);
                g.DeclareComputeCompatible(result.filter);
            }
            require(g.Compile(error), error);
            if (withShadow)
            {
                result.hints.push_back({result.shadow, false, 400'000});
            }
            result.hints.push_back({result.gbuffer, false, 500'000});
            result.hints.push_back({result.raw, true, 200'000});
            result.hints.push_back({result.filter, true, 200'000});
            result.hints.push_back({result.deferred, false, 300'000});
            return result;
        };
        const auto positionOf = [](const EnhancedRenderGraph::QueueSchedule& schedule, RGPassId pass)
        {
            for (size_t index = 0; index < schedule.entries.size(); ++index)
            {
                if (schedule.entries[index].pass == pass.index)
                {
                    return index;
                }
            }
            return schedule.entries.size();
        };
        const auto queueOf = [&](const EnhancedRenderGraph::QueueSchedule& schedule, RGPassId pass)
        {
            return schedule.entries[positionOf(schedule, pass)].queue;
        };
        const auto keepsCompiledOrder = [](const EnhancedRenderGraph::QueueSchedule& schedule, const EnhancedRenderGraph& g)
        {
            return schedule.entries.size() == g.GetExecuteOrder().size() &&
                std::equal(schedule.entries.begin(), schedule.entries.end(), g.GetExecuteOrder().begin(),
                    [](const auto& entry, auto pass) { return entry.pass == pass && entry.queue == RHIQueueKind::Graphics; });
        };
        EnhancedRenderGraph::QueueCostModel overlapModel;
        overlapModel.placement = RGQueuePlacement::Overlap;
        overlapModel.handoffNanoseconds = 20'000;
        overlapModel.minimumGainNanoseconds = 50'000;
        const uint32_t callbacksBeforeOverlap = callbacks;
        {
            auto subject = buildOverlap(true, true, &overlapModel);
            require(subject.graph->GetExecuteOrder().front() == subject.shadow.index, "Fixture compiled order changed.");
            require(subject.graph->BuildQueueSchedule(caps, subject.hints, 1000, plan, error), error);
            require(plan.usesCompute && plan.predictedSerialNanoseconds == 1'600'000 &&
                plan.predictedNanoseconds == 1'240'000, "Overlap prediction mismatch.");
            require(plan.entries.front().pass == subject.gbuffer.index &&
                positionOf(plan, subject.shadow) < positionOf(plan, subject.deferred),
                "Compute ancestor was not hoisted ahead of independent graphics work.");
            require(queueOf(plan, subject.raw) == RHIQueueKind::Compute &&
                queueOf(plan, subject.filter) == RHIQueueKind::Compute &&
                queueOf(plan, subject.shadow) == RHIQueueKind::Graphics &&
                queueOf(plan, subject.deferred) == RHIQueueKind::Graphics, "Overlap queue placement mismatch.");
            require(positionOf(plan, subject.gbuffer) < positionOf(plan, subject.raw) &&
                positionOf(plan, subject.raw) < positionOf(plan, subject.deferred),
                "Physical resource use order changed.");
            const auto waits = [&](RGPassId from, RGPassId to, uint32_t resource)
            {
                return std::any_of(plan.waits.begin(), plan.waits.end(), [&](const auto& wait)
                {
                    return wait.producer == from.index && wait.consumer == to.index && wait.resource == resource;
                });
            };
            require(waits(subject.gbuffer, subject.raw, subject.depthResource) &&
                waits(subject.filter, subject.deferred, subject.filteredResource) &&
                std::all_of(plan.waits.begin(), plan.waits.end(), [&](const auto& wait)
                {
                    return positionOf(plan, RGPassId{wait.producer}) < positionOf(plan, RGPassId{wait.consumer});
                }), "Cross-queue waits missing or not topological.");
            require(waits(subject.raw, subject.deferred, subject.depthResource),
                "Same-state read/read physical ownership wait missing.");
            EnhancedRenderGraph::QueueSchedule repeated;
            require(subject.graph->BuildQueueSchedule(caps, subject.hints, 1000, repeated, error) &&
                repeated.compileGeneration == plan.compileGeneration &&
                repeated.predictedNanoseconds == plan.predictedNanoseconds &&
                repeated.entries.size() == plan.entries.size() && repeated.waits.size() == plan.waits.size() &&
                std::equal(repeated.entries.begin(), repeated.entries.end(), plan.entries.begin(),
                    [](const auto& a, const auto& b) { return a.pass == b.pass && a.queue == b.queue; }) &&
                std::equal(repeated.waits.begin(), repeated.waits.end(), plan.waits.begin(),
                    [](const auto& a, const auto& b)
                    {
                        return a.producer == b.producer && a.consumer == b.consumer && a.resource == b.resource;
                    }), "Repeated overlap planning changed the schedule.");
            // Threshold placement on the same graph moves AO without anything to overlap.
            EnhancedRenderGraph::QueueCostModel thresholdModel = overlapModel;
            thresholdModel.placement = RGQueuePlacement::Threshold;
            subject.graph->SetQueueCostModel(thresholdModel);
            require(subject.graph->BuildQueueSchedule(caps, subject.hints, 1000, plan, error), error);
            require(plan.usesCompute && plan.entries.front().pass == subject.shadow.index &&
                plan.predictedNanoseconds == 1'640'000 && plan.predictedNanoseconds > plan.predictedSerialNanoseconds,
                "Threshold placement prediction mismatch.");
        }
        {
            auto subject = buildOverlap(false, true, &overlapModel);
            require(subject.graph->BuildQueueSchedule(caps, subject.hints, 1000, plan, error), error);
            require(!plan.usesCompute && plan.waits.empty() && keepsCompiledOrder(plan, *subject.graph) &&
                plan.predictedNanoseconds == plan.predictedSerialNanoseconds,
                "Compute chain without independent graphics work was moved.");
        }
        {
            auto strict = overlapModel;
            strict.minimumGainNanoseconds = 400'000;
            auto subject = buildOverlap(true, true, &strict);
            require(subject.graph->BuildQueueSchedule(caps, subject.hints, 1000, plan, error), error);
            require(!plan.usesCompute && keepsCompiledOrder(plan, *subject.graph), "Gain below minimum admitted.");
        }
        {
            auto expensive = overlapModel;
            expensive.handoffNanoseconds = 1'000'000;
            auto subject = buildOverlap(true, true, &expensive);
            require(subject.graph->BuildQueueSchedule(caps, subject.hints, 1000, plan, error), error);
            require(!plan.usesCompute && keepsCompiledOrder(plan, *subject.graph), "Handoff cost was ignored.");
        }
        {
            auto subject = buildOverlap(true, false, &overlapModel);
            require(subject.graph->BuildQueueSchedule(caps, subject.hints, 1000, plan, error), error);
            require(!plan.usesCompute && keepsCompiledOrder(plan, *subject.graph), "Undeclared pass moved by hint alone.");
            auto unsupportedOverlap = caps;
            unsupportedOverlap.compute = false;
            auto declared = buildOverlap(true, true, &overlapModel);
            require(declared.graph->BuildQueueSchedule(unsupportedOverlap, declared.hints, 1000, plan, error) &&
                !plan.usesCompute && keepsCompiledOrder(plan, *declared.graph), "Overlap ignored missing compute queue.");
        }
        {
            auto subject = buildOverlap(true, true, &overlapModel, RGOrderPolicy::PreserveDeclarationOrder);
            require(subject.graph->BuildQueueSchedule(caps, subject.hints, 1000, plan, error), error);
            require(!plan.usesCompute && plan.waits.empty() && keepsCompiledOrder(plan, *subject.graph) &&
                plan.predictedNanoseconds == plan.predictedSerialNanoseconds,
                "Preserved declaration order admitted a non-beneficial compute move.");
        }
        {
            auto subject = buildOverlap(true, true, &overlapModel);
            auto noTimeline = caps;
            noTimeline.crossQueueTimeline = false;
            require(subject.graph->BuildQueueSchedule(noTimeline, subject.hints, 1000, plan, error) &&
                !plan.usesCompute && plan.waits.empty() && keepsCompiledOrder(plan, *subject.graph),
                "Overlap ignored missing cross-queue timeline.");
            require(subject.graph->BuildQueueSchedule(caps, subject.hints, 0, plan, error) &&
                !plan.usesCompute && plan.waits.empty() && keepsCompiledOrder(plan, *subject.graph),
                "Overlap accepted an unmeasured threshold.");
        }
        {
            auto subject = buildOverlap(true, true, &overlapModel);
            const auto completeHints = subject.hints;
            subject.hints.erase(subject.hints.begin());
            require(subject.graph->BuildQueueSchedule(caps, subject.hints, 1000, plan, error) &&
                !plan.predictionComplete && !plan.usesCompute && keepsCompiledOrder(plan, *subject.graph),
                "A missing independent-work sample was treated as an overlap benefit.");
            subject.hints = completeHints;
            subject.hints.front().measuredGpuNanoseconds = 0;
            subject.hints.front().hasGpuMeasurement = false;
            require(subject.graph->BuildQueueSchedule(caps, subject.hints, 1000, plan, error) &&
                !plan.predictionComplete && !plan.usesCompute && keepsCompiledOrder(plan, *subject.graph),
                "An unknown zero-valued sample was treated as an overlap benefit.");
            subject.hints = completeHints;
            subject.hints.back().measuredGpuNanoseconds = 0;
            require(subject.graph->BuildQueueSchedule(caps, subject.hints, 1000, plan, error) &&
                plan.predictionComplete && plan.usesCompute && plan.predictedSerialNanoseconds == 1'300'000 &&
                plan.predictedNanoseconds == 940'000,
                "A measured zero-length pass forced fallback despite known independent GPU work.");
            subject.hints = completeHints;
            for (auto& hint : subject.hints)
            {
                hint.measuredGpuNanoseconds = UINT64_MAX / 2;
            }
            require(subject.graph->BuildQueueSchedule(caps, subject.hints, 1000, plan, error) &&
                plan.predictedSerialNanoseconds == UINT64_MAX && plan.predictedNanoseconds == UINT64_MAX &&
                !plan.predictionComplete && !plan.usesCompute && keepsCompiledOrder(plan, *subject.graph),
                "Saturated pass costs wrapped into a false overlap benefit.");
            subject.hints = completeHints;
            auto saturatedHandoff = overlapModel;
            saturatedHandoff.handoffNanoseconds = UINT64_MAX;
            subject.graph->SetQueueCostModel(saturatedHandoff);
            require(subject.graph->BuildQueueSchedule(caps, subject.hints, 1000, plan, error) &&
                plan.predictionComplete && !plan.predictionCalibrated && !plan.usesCompute &&
                plan.predictedNanoseconds == plan.predictedSerialNanoseconds && keepsCompiledOrder(plan, *subject.graph),
                "Saturated handoff arithmetic wrapped into a false overlap benefit.");
        }
        {
            EnhancedRenderGraph prefix(device, RGSchedulingMode::ExplicitVersioned);
            auto prefixModel = overlapModel;
            prefixModel.handoffNanoseconds = 75'000;
            prefixModel.minimumGainNanoseconds = 0;
            prefix.SetQueueCostModel(prefixModel);
            const auto graphicsPass = prefix.AddPass("prefix-graphics", {}, body, true);
            const auto computePass = prefix.AddPass("prefix-compute", {}, body, true);
            prefix.DeclareComputeCompatible(computePass);
            require(prefix.Compile(error), error);
            require(prefix.BuildQueueSchedule(caps, {{graphicsPass, false, 100'000}, {computePass, true, 100'000}},
                1000, plan, error) && plan.predictionComplete && !plan.usesCompute && plan.waits.empty() &&
                plan.predictedSerialNanoseconds == 200'000 && plan.predictedNanoseconds == 200'000,
                "Independent compute omitted the mandatory prologue wait or final graphics join.");
        }
        {
            EnhancedRenderGraph uncompiled(device, RGSchedulingMode::ExplicitVersioned);
            uint32_t measurements = 0;
            require(uncompiled.MeasuredQueueHints([&](const GpuPassTimingIdentity&)
            {
                ++measurements;
                return uint64_t{100};
            }).empty() && measurements == 0, "Uncompiled graph exposed a timing identity.");
        }
        struct TimingFixture
        {
            std::unique_ptr<EnhancedRenderGraph> graph;
            std::vector<GpuPassTimingIdentity> identities;
            std::vector<EnhancedRenderGraph::QueueHint> hints;
        };
        const auto buildTiming = [&](uint64_t firstBytes, const std::string& literalName, bool swapUsages,
            uint32_t repeats)
        {
            TimingFixture result;
            result.graph = std::make_unique<EnhancedRenderGraph>(device, RGSchedulingMode::ExplicitVersioned);
            auto& g = *result.graph;
            const auto first = g.Write(g.CreateBuffer({firstBytes, true, false, "timing-first"}));
            const auto second = g.Write(g.CreateBuffer({512, true, false, "timing-second"}));
            const auto repeated = g.Write(g.CreateBuffer({256, true, false, "timing-repeated"}));
            const auto firstPass = g.AddPass("duplicate",
                {{swapUsages ? second : first, RHIResourceState::UnorderedAccess, RGAccessMode::Write}}, body, true);
            const auto secondPass = g.AddPass("duplicate",
                {{swapUsages ? first : second, RHIResourceState::UnorderedAccess, RGAccessMode::Write}}, body, true);
            g.AddRepeatedPass(literalName,
                {{repeated, RHIResourceState::UnorderedAccess, RGAccessMode::Write}},
                {{"write", {{repeated, RHIResourceState::UnorderedAccess, RGAccessMode::Write}}}}, repeats,
                [&](const auto&, uint32_t, uint32_t) { ++callbacks; }, true);
            g.DeclareComputeCompatible(secondPass);
            require(g.Compile(error), error);
            result.hints = g.MeasuredQueueHints([&](const GpuPassTimingIdentity& identity)
            {
                result.identities.push_back(identity);
                return uint64_t{100} * (identity.passIndex + 1);
            });
            require(result.identities.size() == 3 && result.hints.size() == 3 &&
                result.identities[0].IsValid() && result.identities[1].IsValid() && result.identities[2].IsValid() &&
                result.identities[0].passIndex == firstPass.index &&
                result.identities[1].passIndex == secondPass.index &&
                result.identities[0] != result.identities[1] && result.identities[1] != result.identities[2] &&
                result.hints[0].measuredGpuNanoseconds == 100 && result.hints[1].measuredGpuNanoseconds == 200 &&
                result.hints[2].measuredGpuNanoseconds == 300 && !result.hints[0].computeCompatible &&
                result.hints[1].computeCompatible, "Duplicate display names collapsed pass timing slots.");
            return result;
        };
        {
            auto original = buildTiming(256, "duplicate (x2)", false, 2);
            auto rebuilt = buildTiming(256, "duplicate (x2)", false, 2);
            require(original.identities == rebuilt.identities &&
                original.identities.front().graphSignature.get() != rebuilt.identities.front().graphSignature.get(),
                "Structurally identical rebuilt graphs lost stable timing identity.");
            auto resized = buildTiming(1024, "duplicate (x2)", false, 2);
            auto renamed = buildTiming(256, "duplicate", false, 2);
            auto reordered = buildTiming(256, "duplicate (x2)", true, 2);
            auto repeated = buildTiming(256, "duplicate (x2)", false, 3);
            require(original.identities.front() != resized.identities.front() &&
                original.identities.front() != renamed.identities.front() &&
                original.identities.front() != reordered.identities.front() &&
                original.identities.front() != repeated.identities.front(),
                "Incompatible resource/name/order/repeat signatures reused stale timing samples.");
            const auto zeroHints = original.graph->MeasuredQueueHints(
                [&](const GpuPassTimingIdentity& identity) -> std::optional<uint64_t>
                {
                    if (identity.passIndex == original.identities.front().passIndex)
                    {
                        return uint64_t{0};
                    }
                    return std::nullopt;
                });
            require(zeroHints.size() == 3 && zeroHints[0].hasGpuMeasurement &&
                zeroHints[0].measuredGpuNanoseconds == 0 && !zeroHints[1].hasGpuMeasurement &&
                !zeroHints[2].hasGpuMeasurement,
                "Timing lookup conflated a measured zero-length scope with missing samples.");
            original.graph->Reset();
            require(original.identities.front().IsValid() && original.identities == rebuilt.identities &&
                original.graph->MeasuredQueueHints([](const GpuPassTimingIdentity&) { return uint64_t{100}; }).empty(),
                "Reset invalidated retained timing identity or exposed stale live hints.");
        }
        require(callbacks == callbacksBeforeOverlap, "Overlap planning executed a callback.");
        constexpr uint32_t kExpectedChecks = 82;
        if (checks != kExpectedChecks)
        {
            throw std::runtime_error("Queue schedule acceptance check count changed.");
        }
        outLog = "RG8_QUEUE_SCHEDULE_OK schema=2 checks=" + std::to_string(checks) +
            " scope=compiled-plan nativeExecution=false overlap=true declarationOrder=true readRead=true modelGuards=true timingIdentity=true\n";
        return true;
    }
    catch (const std::exception& exception)
    {
        outLog = std::string("RG8_QUEUE_SCHEDULE_FAILED: ") + exception.what() + "\n";
        return false;
    }
}
