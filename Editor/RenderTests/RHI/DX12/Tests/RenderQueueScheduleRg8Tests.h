#include "../../../../../Engine/Containers/ContainerAPI.h"
#include "../../../../../Engine/RenderEngine/RenderAllocator.h"
#pragma once
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../../../Tools/build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif
#include "Render/Graph/EnhancedRenderGraph.h"
#include "RHI/DX12/DX12DeviceResources.h"
#include "Render/Graph/EnhancedGpuMeasurementHistory.h"
#include "../../../../../Engine/Utility_Framework/Foundation/StandardLibrary.h"

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
        const RHIQueueCapabilities caps{true, true, true, true};
        EnhancedRenderGraph graph(device, RGSchedulingMode::ExplicitVersioned);
        EnhancedRenderGraph::QueueSchedule plan;
        require(!graph.BuildQueueSchedule(caps, {}, true, plan, error) && plan.entries.empty(), "Uncompiled graph admitted.");
        uint32_t callbacks = 0;
        const auto body = [&](const EnhancedRenderGraph::ExecuteContext&) { ++callbacks; };
        const auto resource = graph.CreateBuffer({256, true, false, "shared"});
        const auto version = graph.Write(resource);
        const auto producer = graph.AddPass("producer", {{version, RHIResourceState::UnorderedAccess, RGAccessMode::Write}}, body);
        const auto compute = graph.AddPass("authored-async", {{version, RHIResourceState::ShaderResource, RGAccessMode::Read}}, body, true);
        const auto graphics = graph.AddPass("graphics-reader", {{version, RHIResourceState::CopySource, RGAccessMode::Read}}, body, true);
        const auto compatible = graph.AddPass("compatible-only", {}, body, true);
        const auto culled = graph.AddPass("culled-async", {}, body);
        graph.DeclareAsyncCompute(compute);
        graph.DeclareAsyncCompute(culled);
        graph.DeclareComputeCompatible(compatible);
        require(graph.Compile(error), error);
        graph.SetQueueExecutionMode(2);
        require(graph.SelectExecutionQueues(caps, error), error);
        require(graph.GetQueueDiagnostics().effectiveExecutionMode == 2 &&
            graph.GetQueueDiagnostics().schedule.usesCompute && !graph.GetQueueDiagnostics().specialized,
            "Explicit async graph did not select owned queues before recording.");
        graph.SetQueueExecutionMode(1);
        require(graph.SelectExecutionQueues(caps, error) && graph.GetQueueDiagnostics().effectiveExecutionMode == 0 &&
            !graph.GetQueueDiagnostics().schedule.usesCompute, "Graphics-only mode did not reuse primary execution.");
        graph.SetQueueExecutionMode(0);
        require(graph.SelectExecutionQueues(caps, error) && graph.GetQueueDiagnostics().requestedExecutionMode == 0 &&
            graph.GetQueueDiagnostics().effectiveExecutionMode == 0, "OFF did not retain primary execution.");
        const auto order = graph.GetExecuteOrder();
        const auto sameOrder = [&]()
        {
            return plan.entries.size() == order.size() && std::equal(plan.entries.begin(), plan.entries.end(), order.begin(),
                [](const auto& entry, auto pass) { return entry.pass == pass; });
        };
        const auto onCompute = [&](RGPassId id)
        {
            return std::any_of(plan.entries.begin(), plan.entries.end(), [&](const auto& entry)
            {
                return entry.pass == id.index && entry.queue == RHIQueueKind::Compute;
            });
        };
        const auto hasWait = [&](RGPassId from, RGPassId to)
        {
            return std::any_of(plan.waits.begin(), plan.waits.end(), [&](const auto& wait)
            {
                return wait.producer == from.index && wait.consumer == to.index && wait.resource == resource.index;
            });
        };
        require(graph.BuildQueueSchedule(caps, {}, true, plan, error), error);
        require(plan.usesCompute && onCompute(compute), "Explicit async request requires no timing history.");
        require(!onCompute(compatible), "Compatibility alone requested async execution.");
        require(!onCompute(culled) && plan.entries.size() == 4, "Culled async pass was scheduled.");
        require(sameOrder(), "Authored placement changed compiled order.");
        require(hasWait(producer, compute), "RAW fork wait missing.");
        require(hasWait(compute, graphics), "Physical read ownership join missing.");
        require(plan.waits.size() == 2, "Duplicate queue waits.");
        require(plan.eligibleComputePasses == 1 && plan.fallbackReason == RGQueueFallbackReason::None, "Authored pass count mismatch.");
        require(plan.placementTrials == 0 && plan.simulationMilliseconds == 0 && plan.predictedNanoseconds == 0 &&
            plan.predictedSerialNanoseconds == 0 && !plan.predictionComplete, "Cost search or prediction survived.");
        const auto expectedWaits = plan.waits;
        EnhancedRenderGraph::DiagnosticSnapshot snapshot;
        require(graph.CaptureDiagnosticSnapshot(snapshot) && snapshot.passes[compute.index].asyncCompute &&
            snapshot.passes[compatible.index].computeCompatible && !snapshot.passes[compatible.index].asyncCompute,
            "Viewer snapshot lost author intent.");
        // Contradictory compatibility hints and saturated timings cannot change authored placement.
        ce::dynamic_array<EnhancedRenderGraph::QueueHint> hints{{compute, false, 0, true}, {compatible, true, UINT64_MAX, true}};
        require(graph.BuildQueueSchedule(caps, hints, true, plan, error), error);
        require(onCompute(compute) && !onCompute(compatible) && sameOrder(), "Timing or compatibility hint overrode author intent.");
        require(plan.waits.size() == expectedWaits.size() && plan.measuredPasses == 2, "Diagnostic samples changed waits.");
        require(graph.BuildQueueSchedule(caps, hints, false, plan, error) && !plan.usesCompute && plan.waits.empty() && sameOrder() &&
            plan.fallbackReason == RGQueueFallbackReason::Disabled, "Disabled async did not preserve graphics execution.");
        auto unsupported = caps;
        unsupported.compute = false;
        require(graph.BuildQueueSchedule(unsupported, {}, true, plan, error) && !plan.usesCompute && plan.waits.empty() &&
            plan.fallbackReason == RGQueueFallbackReason::Unsupported, "Missing compute queue fallback failed.");
        unsupported = caps;
        unsupported.crossQueueTimeline = false;
        require(graph.BuildQueueSchedule(unsupported, {}, true, plan, error) && !plan.usesCompute &&
            plan.fallbackReason == RGQueueFallbackReason::Unsupported, "Missing cross-queue timeline fallback failed.");
        require(!graph.BuildQueueSchedule(caps, {{compute, true, 1}, {compute, true, 2}}, true, plan, error) &&
            plan.entries.empty(), "Duplicate hint accepted.");
        require(!graph.BuildQueueSchedule(caps, {{RGPassId{}, true, 1}}, true, plan, error), "Invalid hint accepted.");
        bool rejected = false;
        try
        {
            graph.DeclareAsyncCompute(compatible);
        }
        catch (const std::logic_error&)
        {
            rejected = true;
        }
        require(rejected, "Compiled graph author intent was mutable.");
        require(callbacks == 0, "Planning invoked pass callbacks.");
        require(device.BeginFrame(error), error);
        graph.SetTransientAliasing(true);
        require(graph.Compile(error), error);
        require(graph.BuildQueueSchedule(caps, {}, true, plan, error) && plan.usesCompute,
            "DAG-proven alias lifetime rejected authored queue placement.");
        device.AbortFrame();
        graph.Reset();
        require(!graph.BuildQueueSchedule(caps, {}, true, plan, error) && plan.entries.empty(), "Reset retained a schedule.");
        graph.SetTransientAliasing(false);
        require(!graph.SelectExecutionQueues(caps, error), "Uncompiled graph selected an execution path.");
        const auto plain = graph.AddPass("compatible-only", {}, body, true);
        graph.DeclareComputeCompatible(plain);
        require(graph.Compile(error), error);
        require(graph.BuildQueueSchedule(caps, {{plain, true, UINT64_MAX}}, true, plan, error) && !plan.usesCompute &&
            plan.fallbackReason == RGQueueFallbackReason::NoAsyncPasses, "No explicit request did not report no-async-passes.");
        graph.SetQueueExecutionMode(2);
        require(graph.SelectExecutionQueues(caps, error) && graph.GetQueueDiagnostics().effectiveExecutionMode == 0 &&
            graph.GetQueueDiagnostics().schedule.fallbackReason == RGQueueFallbackReason::NoAsyncPasses,
            "Async mode without an authored pass did not select primary graphics.");
        EnhancedRenderGraph illegal(device, RGSchedulingMode::ExplicitVersioned);
        RGTextureDesc textureDesc{};
        textureDesc.width = textureDesc.height = 8;
        textureDesc.allowRenderTarget = true;
        const auto texture = illegal.Write(illegal.CreateTexture(textureDesc));
        const auto raster = illegal.AddPass("illegal-async-raster", {{texture, RHIResourceState::RenderTarget, RGAccessMode::Write}}, body, true);
        illegal.DeclareAsyncCompute(raster);
        require(illegal.Compile(error), error);
        require(illegal.BuildQueueSchedule(caps, {}, true, plan, error) && !plan.usesCompute &&
            plan.rejectedStatePasses == 1 && plan.fallbackReason == RGQueueFallbackReason::UnsupportedState,
            "Graphics-only resource state entered compute queue.");
        // Structural template matching includes author intent, including reused pass slots.
        EnhancedRenderGraph first(device, RGSchedulingMode::ExplicitVersioned);
        const auto firstPass = first.AddPass("same-name", {}, body, true);
        first.DeclareComputeCompatible(firstPass);
        require(first.Compile(error), error);
        EnhancedRenderGraph second(device, RGSchedulingMode::ExplicitVersioned);
        second.InheritStructuralSchedule(first);
        const auto secondPass = second.AddPass("same-name", {}, body, true);
        second.DeclareAsyncCompute(secondPass);
        require(second.Compile(error), error);
        require(second.BuildQueueSchedule(caps, {}, true, plan, error) && onCompute(secondPass), "Inherited template lost async intent.");
        second.Reset();
        second.AddPass("same-name", {}, body, true);
        require(second.Compile(error), error);
        require(second.BuildQueueSchedule(caps, {}, true, plan, error) && !plan.usesCompute, "Reused pass retained async intent.");
        require(callbacks == 0, "Plan and template probes invoked callbacks.");
        struct TimingFixture
        {
            own::unique_owner<EnhancedRenderGraph> graph;
            ce::dynamic_array<GpuPassTimingIdentity> identities;
            ce::dynamic_array<EnhancedRenderGraph::QueueHint> hints;
        };
        const auto buildTiming = [&](uint64_t firstBytes, const std::string& literalName, bool swapUsages,
            uint32_t repeats, RGMeasurementDomain domain = RGMeasurementDomain::Normal)
        {
            TimingFixture result;
            result.graph = own::make_unique<EnhancedRenderGraph>(device, RGSchedulingMode::ExplicitVersioned);
            auto& g = *result.graph;
            g.SetMeasurementDomain(domain);
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
                original.identities.front().graphSignature.operator->() != rebuilt.identities.front().graphSignature.operator->(),
                "Structurally identical rebuilt graphs lost stable timing identity.");
            auto captured = buildTiming(256, "duplicate (x2)", false, 2, RGMeasurementDomain::Capture);
            require(captured.graph->GetMeasurementDomain() == RGMeasurementDomain::Capture &&
                original.graph->GetMeasurementDomain() == RGMeasurementDomain::Normal &&
                captured.identities.front() != original.identities.front(),
                "Normal and capture timing identities shared a measurement signature.");
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
        {
            EnhancedGpuMeasurementHistory history;
            const auto signature = own::make_shared<const std::string>("same-structural-signature");
            const auto token = [](uint64_t submission, uint64_t view, uint64_t frame, uint64_t tick)
            {
                GpuFrameToken result{};
                result.ringSlot = 0;
                result.submissionId = submission;
                result.renderViewId = view;
                result.engineFrameId = frame;
                result.cpuSubmitTick = tick;
                return result;
            };
            const auto normalToken = token(10, 7, 20, 1000);
            require(history.Store(RGMeasurementDomain::Normal, {signature, {uint64_t{0}, std::nullopt}, normalToken}),
                "Normal timing sample was rejected.");
            const auto* normal = history.Find(RGMeasurementDomain::Normal, 7);
            require(normal && normal->nanoseconds[0].has_value() && *normal->nanoseconds[0] == 0 &&
                !normal->nanoseconds[1].has_value() && !history.Find(RGMeasurementDomain::Capture, 7),
                "Measured zero, missing samples or capture-domain isolation were lost.");
            require(history.Store(RGMeasurementDomain::Capture,
                {signature, {uint64_t{900}}, token(11, 7, 20, 1010)}) &&
                *history.Find(RGMeasurementDomain::Normal, 7)->nanoseconds[0] == 0,
                "Successful capture replaced a structurally identical normal sample.");
            history.Discard(RGMeasurementDomain::Capture, token(13, 7, 21, 1020));
            require(own::identity(history.Find(RGMeasurementDomain::Normal, 7)->graphSignature) == own::identity(signature) &&
                !history.Find(RGMeasurementDomain::Capture, 7)->graphSignature,
                "Failed capture tombstoned normal history or omitted its own watermark.");
            require(!history.Store(RGMeasurementDomain::Capture,
                {signature, {uint64_t{800}}, token(12, 7, 20, 1015)}) &&
                !history.Find(RGMeasurementDomain::Capture, 7)->graphSignature,
                "Late successful capture resurrected an invalidated capture sample.");
            history.Discard(RGMeasurementDomain::Normal, token(9, 7, 19, 990));
            require(own::identity(history.Find(RGMeasurementDomain::Normal, 7)->graphSignature) == own::identity(signature) &&
                history.Find(RGMeasurementDomain::Normal, 7)->token.submissionId == 10,
                "Late failure invalidated a newer normal sample.");
            const auto current = token(14, 7, 21, 1050);
            require(history.FindFresh(RGMeasurementDomain::Normal, current, 1000) &&
                !history.FindFresh(RGMeasurementDomain::Capture, current, 1000),
                "Capture lookup consumed a normal signature or failed watermark.");
            require(!history.FindFresh(RGMeasurementDomain::Normal, normalToken, 1000) &&
                !history.FindFresh(RGMeasurementDomain::Normal, token(14, 7, 29, 1050), 1000) &&
                !history.FindFresh(RGMeasurementDomain::Normal, token(14, 7, 21, 1501), 1000) &&
                !history.FindFresh(RGMeasurementDomain::Normal, current, 0),
                "Stale frame/time or unknown CPU-clock history was treated as warm cost evidence.");
            bool capturesAccepted = true;
            for (uint64_t view = 20; view < 29; ++view)
            {
                capturesAccepted = history.Store(RGMeasurementDomain::Capture,
                    {signature, {view}, token(20 + view, view, 21, 2000 + view)}) && capturesAccepted;
            }
            require(capturesAccepted && !history.Find(RGMeasurementDomain::Capture, 20) &&
                history.Find(RGMeasurementDomain::Capture, 28), "Capture domain did not enforce its own bounded eviction.");
            require(history.Find(RGMeasurementDomain::Normal, 7)->token.submissionId == 10 &&
                history.FindFresh(RGMeasurementDomain::Normal, current, 1000),
                "Capture eviction removed the warm normal sample.");
            const GpuFrameToken invalid{};
            history.Discard(RGMeasurementDomain::Normal, invalid);
            require(!history.Store(RGMeasurementDomain::Normal, {signature, {uint64_t{7}}, invalid}) &&
                history.Find(RGMeasurementDomain::Normal, 7)->token.submissionId == 10,
                "Unknown/invalid submission modified normal history.");
            history.Clear();
            require(!history.Find(RGMeasurementDomain::Normal, 7) &&
                !history.Find(RGMeasurementDomain::Capture, 28), "History reset retained a measurement domain.");
            const auto alternate = own::make_shared<const std::string>("alternate-history-role");
            require(history.Store(RGMeasurementDomain::Normal, {signature, {uint64_t{7}}, token(30,7,30,2000)}) &&
                history.Store(RGMeasurementDomain::Normal, {alternate, {uint64_t{9}}, token(31,7,31,2001)}) &&
                history.FindFresh(RGMeasurementDomain::Normal, token(32,7,32,2010),1000,signature) &&
                history.FindFresh(RGMeasurementDomain::Normal, token(32,7,32,2010),1000,alternate),
                "Alternating graph declarations lost warm costs.");
            require(history.Store(RGMeasurementDomain::Normal, {signature, {uint64_t{11}}, token(32,7,32,2002)}) &&
                history.FindFresh(RGMeasurementDomain::Normal, token(33,7,33,2010),1000,signature)->nanoseconds[0] == 11 &&
                history.FindFresh(RGMeasurementDomain::Normal, token(33,7,33,2010),1000,alternate)->nanoseconds[0] == 9,
                "Updating one declaration overwrote another declaration's costs.");
            require(!history.FindFresh(RGMeasurementDomain::Capture, token(33,7,33,2010),1000,signature) &&
                !history.FindFresh(RGMeasurementDomain::Normal, token(42,7,42,2010),1000,signature) &&
                !history.FindFresh(RGMeasurementDomain::Normal, token(33,7,33,2010),1000,
                    own::make_shared<const std::string>("unknown")), "Variant lookup bypassed domain, age or signature guards.");
            for(uint64_t i=0;i<5;++i)
            {
                history.Store(RGMeasurementDomain::Normal, {own::make_shared<const std::string>("variant-"+std::to_string(i)),
                    {i},token(40+i,7,40+i,2100+i)});
            }
            require(!history.FindFresh(RGMeasurementDomain::Normal,token(45,7,45,2110),1000,
                    own::make_shared<const std::string>("variant-0")) &&
                history.FindFresh(RGMeasurementDomain::Normal,token(45,7,45,2110),1000,
                    own::make_shared<const std::string>("variant-1")), "Per-view signature cache was unbounded or evicted too early.");
            history.Discard(RGMeasurementDomain::Normal,token(46,7,46,2200));
            require(!history.Store(RGMeasurementDomain::Normal,{alternate,{uint64_t{9}},token(45,7,45,2110)}) &&
                !history.FindFresh(RGMeasurementDomain::Normal,token(47,7,47,2210),1000,
                    own::make_shared<const std::string>("variant-4")), "Failure watermark restored an older graph variant.");
            require(history.Store(RGMeasurementDomain::Normal,{signature,{uint64_t{12}},token(47,7,47,2210)}) &&
                history.FindFresh(RGMeasurementDomain::Normal,token(48,7,48,2220),1000,signature) &&
                !history.FindFresh(RGMeasurementDomain::Normal,token(48,7,48,2220),1000,alternate),
                "Recovery resurrected pre-failure variants.");
        }
        std::string validation;
        require(device.DrainDebugMessages(validation) == 0, validation);
        constexpr uint32_t kExpectedChecks = 82;
        if (checks != kExpectedChecks)
        {
            throw std::runtime_error("Queue schedule check count actual=" + std::to_string(checks));
        }
        outLog = "RG8_QUEUE_SCHEDULE_OK schema=5 checks=" + std::to_string(checks) +
            " scope=compiled-plan nativeExecution=false authoredAsync=true declarationOrder=true readRead=true fallbackReasons=true timingIndependent=true structuralIntent=true primaryGraphics=true\n";
        return true;
    }
    catch (const std::exception& exception)
    {
        outLog = std::string("RG8_QUEUE_SCHEDULE_FAILED: ") + exception.what() + "\n";
        return false;
    }
}
