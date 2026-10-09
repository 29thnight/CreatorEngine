#pragma once
#include "Render/Graph/EnhancedRenderGraph.h"
#include "RHI/DX12/DX12DeviceResources.h"
#include <stdexcept>
#include <algorithm>

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
        outLog = "RG8_QUEUE_SCHEDULE_OK checks=" + std::to_string(checks) + " scope=compiled-plan nativeExecution=false\n";
        return true;
    }
    catch (const std::exception& exception)
    {
        outLog = std::string("RG8_QUEUE_SCHEDULE_FAILED: ") + exception.what() + "\n";
        return false;
    }
}
