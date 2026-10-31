#if defined(CE_CONTRACT_STANDALONE)
#include "../build/ModuleAdapters/Creator.Core.LifetimeToken.h"
#else
import Creator.Core.LifetimeToken;
#endif
#if defined(CE_CONTRACT_STANDALONE)
#include "../build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif
#include "fixtures/render-graph-portable-services.h"
#include "fixtures/render-graph-portable-queues.h"
#include "Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h"
#include <iostream>

namespace
{
    void Require(bool condition, const std::string& message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    using Graph = EnhancedRenderGraph;
    class QueueServices final : public ce::regression::PortableRenderServices
    {
      public:
        bool sharedReads{true};
        bool aliasing{false};
        bool SupportsConcurrentTextureShaderReads() const override { return sharedReads; }
        bool SupportsTransientAliasing() const override { return aliasing; }
        bool DescribeTransientAllocation(const RHITransientResourceDesc& description,
                                         RHITransientAllocationInfo& result, std::string&) const override
        {
            result = {description.buffer ? description.bufferDesc.bytes
                                         : uint64_t(description.textureDesc.width) * description.textureDesc.height * 4,
                      65536, description.buffer ? 1u : 2u};
            return true;
        }
        bool CreateTransientHeap(const RHITransientAllocationInfo&, own::shared_owner<RHITransientHeap>& heap,
                                 std::string&) override
        {
            heap = own::make_shared<RHITransientHeap>();
            return true;
        }
        bool CreatePlacedTransient(const RHITransientResourceDesc& description, RHITransientHeap&,
                                   RHITextureHandle& texture, RHIBufferHandle& buffer, std::string& error) override
        {
            return description.buffer ? CreateBuffer(description.bufferDesc, buffer, error)
                                      : CreateTexture(description.textureDesc, texture, error);
        }
    };
    struct Owner
    {
        int* destroyed;
        ~Owner() { ++*destroyed; }
    };
    RGHandle Declare(Graph& graph, int& callbackValue, int value, uint32_t cost = 7, bool changed = false)
    {
        auto output = graph.Write(graph.CreateBuffer({64, true, false, "Output"}));
        graph.AddSplitPass(
            "Write", {{output, RHIResourceState::UnorderedAccess, RGAccessMode::Write}},
            [&, value](const auto&, uint32_t, uint32_t) { callbackValue = value; }, 4, true, cost);
        if (changed)
        {
            graph.AddPass("New feature", {}, [](const auto&) {}, true);
        }
        return output;
    }

    void CheckDeclarationBufferLifetime()
    {
        ce::regression::PortableRenderServices services;
        Graph graph{services, RGSchedulingMode::ExplicitVersioned};
        std::string error;
        for (int frame = 0; frame < 3; ++frame)
        {
            graph.Reset();
            auto values = graph.CreateDeclarationBuffer<RGHandle>(2);
            values.push_back(graph.Write(graph.CreateBuffer({64, true, false, "Payload"})));
            const auto payload = std::span<const RGHandle>{values.data(), values.size()};
            graph.AddPass("Producer", {{payload[0], RHIResourceState::UnorderedAccess, RGAccessMode::Write}},
                          [](const auto&) {});
            // Packet batches may append after existing declarations. Opening a
            // batch must not rewind buffers or usages that still belong to it.
            graph.BeginDeclarationPackets();
            bool observed = false;
            graph.AddPass(
                "Packet consumer with an owned non-SSO diagnostic name",
                {{payload[0], RHIResourceState::ShaderResource, RGAccessMode::Read}},
                [&, payload](const auto& context)
                {
                    Require(payload[0].epoch == graph.ResourceEpoch() &&
                                context.graph->ResolveBufferHandle(payload[0]).IsValid(),
                            "Declaration buffers remain valid through execution");
                    observed = true;
                },
                true);
            graph.FinishDeclarationPackets();
            Require(graph.Compile(error) && graph.Execute(error) && observed, error);
        }
    }

    void CheckQueuePlanReset()
    {
        ce::regression::PortableRenderServices services;
        Graph graph{services, RGSchedulingMode::ExplicitVersioned};
        const auto pass = graph.AddPass("Live", {}, [](const auto&) {}, true);
        std::string error;
        Require(graph.Compile(error), error);
        Graph::QueueSchedule schedule;
        Require(graph.BuildQueueSchedule({true, false, false, false}, {}, 0, schedule, error), error);
        Require(schedule.entries.size() == 1 && schedule.compileGeneration != 0,
                "A valid plan populates its reusable output");
        const auto capacity = schedule.entries.capacity();
        Require(!graph.BuildQueueSchedule({true, false, false, false}, {{pass}, {pass}}, 0, schedule, error),
                "Duplicate queue hints remain invalid");
        Require(schedule.entries.empty() && schedule.waits.empty() && schedule.compileGeneration == 0 &&
                    schedule.predictedSerialNanoseconds == 0 && !schedule.predictionComplete &&
                    schedule.entries.capacity() == capacity,
                "A rejected plan clears old values and preserves only caller-owned capacity");
    }

    void CheckInlineRecordingEntryGuards()
    {
        using namespace ce::regression;
        class PreparationPool final : public IRHIParallelCommandPool
        {
          public:
            uint32_t preparations{};
            bool IsInitialized() const override { return true; }
            uint32_t GetWorkerCount() const override { return 1; }
            bool Prepare(std::string& error) override
            {
                ++preparations;
                error.clear();
                return true;
            }
            void BeginFrame(uint32_t) override { Unexpected(); }
            bool OpenWorker(uint32_t, std::string&) override { Unexpected(); }
            RHIEncoder& AcquireEncoder(uint32_t) override { Unexpected(); }
            bool CloseAll(std::string&) override { Unexpected(); }
            uint32_t DrainEncoderDrops(std::string&) override { Unexpected(); }
            bool HasRecorded(uint32_t) const override { return false; }

          protected:
            uint32_t GetCurrentFrameSlot() const override { return 0; }
            bool PrepareRecordedCommands(uint32_t, RHICompletionPoint&, std::string&) override { Unexpected(); }
            bool SubmitRecordedCommands(uint32_t, std::span<const uint32_t>, RHICompletionPoint,
                                        std::string&) override
            {
                Unexpected();
            }

          private:
            [[noreturn]] static void Unexpected()
            {
                throw std::logic_error("Entry guard fixture must not touch native command targets");
            }
        } pool;
        PortableRenderServices services;
        Graph preparation{services, RGSchedulingMode::ExplicitVersioned};
        std::string error;
        Require(!RHIInlineCommandRecordingScope::IsActive(), "The fixture starts outside inline recording");
        // This CPU fixture enters the production scope directly. Actual helper
        // callbacks and scheduler admission are covered by the jobs regression.
        {
            const RHIInlineCommandRecordingScope scope;
            Require(!preparation.PrepareParallel(pool, error) && !error.empty() && pool.preparations == 0,
                    "Inline recording rejects preparation before opening a native resource boundary");
        }
        Require(!RHIInlineCommandRecordingScope::IsActive() && preparation.PrepareParallel(pool, error) &&
                    pool.preparations == 1,
                "Rejected nested preparation leaves the graph ready for its owner after scope exit");
        preparation.Reset();

        auto graph = own::make_shared<Graph>(services, RGSchedulingMode::ExplicitVersioned);
        uint32_t calls{};
        graph->AddPass("Entry guard", {}, [&](const auto&) { ++calls; }, true);
        Require(graph->Compile(error), error);
        graph->SetQueueExecutionMode(2);
        Require(graph->SelectExecutionQueues({true, true, false, true}, error), error);
        const auto compileGeneration = graph->GetQueueDiagnostics().schedule.compileGeneration;
        Require(compileGeneration != 0 && graph->GetQueueDiagnostics().schedule.entries.size() == 1,
                "Nested submission starts with an existing compiled queue plan");
        auto queue = own::make_shared<PortableQueue>(RHIQueueIdentity{59, 1, RHIQueueKind::Graphics});
        Graph::QueueEndpoint endpoint{queue, 8};
        PortableQueueRecorder recorder{services};
        Graph::QueueExecution output;
        Require(queue->Signal(7, output.completion, error) && output.retirement.Add(output.completion), error);
        output.plannedBatches = 17;
        output.submittedBatches = 11;
        output.totalMilliseconds = 123.0;
        output.failure.message = "outer submission";
        {
            const RHIInlineCommandRecordingScope scope;
            Require(!graph->SubmitQueues(graph, recorder, endpoint, nullptr, {}, false, own::make_shared<int>(1),
                                         output, error) && !error.empty(),
                    "Inline recording rejects nested queue submission");
        }
        Require(output.plannedBatches == 17 && output.submittedBatches == 11 && output.totalMilliseconds == 123.0 &&
                    output.failure.message == "outer submission" && output.completion.value == 7 &&
                    output.retirement.Size() == 1,
                "Nested submission preserves the outer output and its completion ownership");
        Require(graph->GetQueueDiagnostics().schedule.compileGeneration == compileGeneration &&
                    graph->GetQueueDiagnostics().schedule.entries.size() == 1 &&
                    graph->GetQueueDiagnostics().requestedExecutionMode == 2 && endpoint.nextSignal == 8 &&
                    recorder.recorded.empty() && recorder.preparations == 0 && queue->submissions == 0 &&
                    queue->waits.empty() && calls == 0,
                "Nested submission cannot reset a queue plan, run callbacks or consume a signal");
        Graph::QueueExecution accepted;
        Require(!RHIInlineCommandRecordingScope::IsActive() &&
                    graph->SubmitQueues(graph, recorder, endpoint, nullptr, {}, false, own::make_shared<int>(2),
                                        accepted, error) && calls == 1,
                "Rejected nested submission leaves the compiled graph usable after scope exit");
        queue->Complete();
        graph->Reset();
    }

    void CheckOwnedSubmissionScratch()
    {
        using namespace ce::regression;
        // Exercise owned-target construction and arena-backed recording arrays.
        // A declared CPU side effect keeps callbacks on this fixture's producer;
        // this checks storage lifetime, not real parallel worker scheduling.
        class Recorder final : public IRHIQueueRecorder
        {
          public:
            struct Batch final : IRHIQueueCommandBatch
            {
                RHIQueueIdentity identity;
                ce::lifetime_token owner;
                std::vector<PortableRenderEncoder> encoders;
                RHIQueueIdentity GetQueueIdentity() const override { return identity; }
                uint64_t GetRecordingId() const override { return 1; }
            };
            struct Targets final : IRHIQueueRecording
            {
                own::shared_owner<Batch> batch;
                RHIEncoder& AcquireEncoder(uint32_t target) override { return batch->encoders.at(target); }
                bool Finish(ce::pooled_owner<IRHIQueueCommandBatch>& output, std::string&) override
                {
                    output = std::move(batch);
                    return true;
                }
            };
            PortableRenderServices& services;
            explicit Recorder(PortableRenderServices& device) : services(device) {}
            IRenderDeviceServices& DeviceServices() const override { return services; }
            uint32_t GetWorkerCount() const override { return 4; }
            bool Record(const own::shared_owner<IRHICommandQueue>&, const ce::owned_func<void(RHIEncoder&)>&,
                        ce::lifetime_token, ce::pooled_owner<IRHIQueueCommandBatch>&, std::string&) override
            {
                throw std::logic_error("Owned fixture must use independent recording targets");
            }
            bool BeginRecording(const own::shared_owner<IRHICommandQueue>& queue, uint32_t count,
                                ce::lifetime_token owner, ce::pooled_owner<IRHIQueueRecording>& output,
                                std::string&) override
            {
                auto targets = own::make_shared<Targets>();
                targets->batch = own::make_shared<Batch>();
                targets->batch->identity = queue->GetIdentity();
                targets->batch->owner = std::move(owner);
                targets->batch->encoders.resize(count);
                output = std::move(targets);
                return true;
            }
        };
        PortableRenderServices services;
        auto graph = own::make_shared<Graph>(services, RGSchedulingMode::ExplicitVersioned);
        auto queue = own::make_shared<PortableQueue>(RHIQueueIdentity{58, 1, RHIQueueKind::Graphics});
        Graph::QueueEndpoint endpoint{queue};
        Recorder recorder{services};
        std::string error;
        for (const uint32_t passCount : {32u, 3u, 48u, 32u})
        {
            graph->Reset();
            uint32_t calls = 0;
            for (uint32_t index = 0; index < passCount; ++index)
            {
                const auto pass = graph->AddPass(
                    "Owned scratch", {},
                    [&](const auto& context)
                    {
                        ++calls;
                        context.encoder->Dispatch(1, 1, 1);
                    },
                    true);
                if (index == 0)
                {
                    graph->DeclareRecordingSideEffect(pass);
                }
            }
            Require(graph->Compile(error), error);
            Graph::QueueExecution execution;
            Require(graph->SubmitQueues(graph, recorder, endpoint, nullptr, {}, 0, own::make_shared<int>(1), execution,
                                        error),
                    error);
            Require(calls == passCount && graph->GetQueueDiagnostics().batches.size() == 3 &&
                        graph->GetQueueDiagnostics().batches[1].passes.size() == passCount,
                    "Owned recording and retained diagnostics adapt across graph shape changes");
            Require(queue->GetPendingBatchCount() == 3 && !execution.retirement.IsComplete(),
                    "Native batches retain their independent owners after submission scratch is destroyed");
            queue->Complete();
            Require(execution.retirement.IsComplete(), "Collected native batches release the graph lease");
        }
    }

    void CheckTransientMips()
    {
        ce::regression::PortableRenderServices services;
        services.subresourceCapabilities = {true, true, true};
        RGTransientPool pool;
        Graph graph{services, RGSchedulingMode::ExplicitVersioned};
        graph.SetTransientPool(&pool);
        RGTextureDesc desc{};
        desc.width = desc.height = 16;
        desc.arraySize = 2;
        desc.allowRenderTarget = true;
        desc.name = "mip pool";
        std::string error;
        RHITextureHandle firstNative;
        own::shared_owner<const Graph::GraphProgram> previous;
        for (const uint32_t mips : {1u, 2u, 1u})
        {
            graph.Reset();
            if (previous)
            {
                graph.InheritProgram(previous);
                graph.BeginProgramBinding();
            }
            desc.mipLevels = mips;
            const auto texture = graph.Write(graph.CreateTexture(desc));
            graph.AddPass("last mip", {{texture, RHIResourceState::RenderTarget, RGAccessMode::Write,
                          {mips - 1, 1, 1, 1}}}, [](const auto&) {}, true);
            if (previous)
            {
                Require(graph.FinishProgramBinding(error), error);
            }
            Require(graph.Compile(error), error);
            RGResourceDescription description;
            Require(graph.DescribeResource(texture, description) && description.mipLevels == mips,
                    "Logical resource description preserves transient mip count");
            const auto native = Graph::ExecuteContext{&graph}.ResolveHandle(texture);
            Require(services.DescribeTexture(native).mipLevels == mips,
                    "Native transient storage preserves declared mip count");
            if (!previous)
            {
                firstNative = native;
            }
            else
            {
                Require(!graph.GetStats().programBindingReused, "Mip changes invalidate retained graph program");
                Require((native == firstNative) == (mips == 1), "Pool key separates mip counts and reuses exact match");
            }
            previous = graph.SnapshotProgram();
            Require(graph.Execute(error), error);
        }
        graph.Reset();
        desc.mipLevels = 6;
        Require(!graph.CreateTexture(desc).IsValid() && !graph.Compile(error) &&
                error.find("invalid mip count") != std::string::npos, "Mip chain cannot exceed dimensions");
        graph.Reset();
        desc.mipLevels = 0;
        Require(!graph.CreateTexture(desc).IsValid() && !graph.Compile(error), "Zero mip count is rejected");
    }

    void CheckTransientMipProgramInvalidation()
    {
        ce::regression::PortableRenderServices services;
        services.subresourceCapabilities = {true, true, true};
        Graph graph{services, RGSchedulingMode::ExplicitVersioned};
        RGTextureDesc desc{};
        desc.width = desc.height = 16;
        desc.arraySize = 2;
        desc.allowRenderTarget = true;
        desc.name = "fixed-range mip program";
        std::string error;
        uint32_t previousMips{};
        own::shared_owner<const Graph::GraphProgram> previous;
        for (const uint32_t mips : {1u, 2u, 1u, 1u})
        {
            graph.Reset();
            if (previous)
            {
                graph.InheritProgram(previous);
                graph.BeginProgramBinding();
            }
            desc.mipLevels = mips;
            const auto texture = graph.Write(graph.CreateTexture(desc));
            // Only the resource schema changes; pass ranges and recording cost
            // must not independently invalidate the retained program.
            graph.AddPass("fixed mip", {{texture, RHIResourceState::RenderTarget, RGAccessMode::Write,
                                         {0, 1, 1, 1}}}, [](const auto&) {}, true);
            if (previous)
            {
                Require(graph.FinishProgramBinding(error), error);
            }
            Require(graph.Compile(error), error);
            if (previous)
            {
                Require(graph.GetStats().programBindingReused == (mips == previousMips),
                        "Mip-only changes invalidate retained programs while identical schemas reuse them");
            }
            const auto program = graph.SnapshotProgram();
            Graph::FrameBindings bindings;
            bindings.owner = own::make_shared<int>(1);
            bindings.resources.resize(1);
            bindings.passes.resize(1);
            bindings.passes[0].execute = [](const auto&) {};
            Graph rebound{services, RGSchedulingMode::ExplicitVersioned};
            Require(rebound.BindProgram(program, bindings, error) && rebound.Compile(error), error);
            RGResourceDescription description;
            const auto reboundTexture =
                rebound.RebindProgramHandle(program->ResourceHandle(texture.index, texture.version));
            Require(rebound.DescribeResource(reboundTexture, description) &&
                        description.mipLevels == mips,
                    "Published programs preserve the current mip schema for direct frame bindings");
            previous = program;
            previousMips = mips;
        }
    }

    void CheckTransientMipInitializationAfterReuse()
    {
        ce::regression::PortableRenderServices services;
        services.subresourceCapabilities = {true, true, true};
        const auto declare = [](Graph& graph, uint32_t mips)
        {
            RGTextureDesc desc{};
            desc.width = desc.height = 16;
            desc.mipLevels = mips;
            desc.allowRenderTarget = true;
            desc.name = "partially initialized mip program";
            const auto texture = graph.Write(graph.CreateTexture(desc));
            graph.AddPass("mip zero writer", {{texture, RHIResourceState::RenderTarget, RGAccessMode::Write,
                                              {0, 1, 0, 1}}}, [](const auto&) {});
            graph.AddPass("whole texture reader", {{texture, RHIResourceState::ShaderResource, RGAccessMode::Read}},
                          [](const auto&) {}, true);
        };
        Graph retained{services, RGSchedulingMode::ExplicitVersioned};
        std::string error;
        declare(retained, 1);
        Require(retained.Compile(error), error);
        const auto program = retained.SnapshotProgram();
        retained.Reset();
        retained.InheritProgram(program);
        retained.BeginProgramBinding();
        declare(retained, 2);
        Require(retained.FinishProgramBinding(error), error);

        Graph fresh{services, RGSchedulingMode::ExplicitVersioned};
        declare(fresh, 2);
        std::string freshError;
        Require(!fresh.Compile(freshError) && freshError.find("uninitialized subresource") != std::string::npos,
                "A fresh graph rejects whole-texture reads of an unwritten mip");
        Require(!retained.Compile(error) && error == freshError,
                "A retained program must reject the same uninitialized mip read as a fresh graph");

        retained.Reset();
        retained.BeginProgramBinding();
        declare(retained, 1);
        Require(retained.FinishProgramBinding(error) && retained.Compile(error), error);
        Require(retained.GetStats().programBindingReused,
                "A rejected mip expansion preserves the last valid program for Reset recovery");
    }

    void CheckRg9SplitAndRecovery()
    {
        class SplitServices final : public ce::regression::PortableRenderServices
        {
          public:
            bool enabled{true};
            bool SupportsSplitTransitions() const override { return enabled; }
        } services;
        services.subresourceCapabilities = {true, true, true};
        std::string error;
        for (const bool narrow : {false, true})
        {
            for (const bool enabled : {false, true})
            {
                services.enabled = enabled;
                Graph graph{services, RGSchedulingMode::ExplicitVersioned};
                RGTextureDesc desc{};
                desc.width = desc.height = 8;
                desc.arraySize = 2;
                desc.allowRenderTarget = true;
                auto texture = graph.Write(graph.CreateTexture(desc));
                auto buffer = graph.Write(graph.CreateBuffer({256, true, false, "split buffer"}));
                auto independent = graph.Write(graph.CreateBuffer({256, true, false, "independent"}));
                const RHITextureSubresourceRange range = narrow ? RHITextureSubresourceRange{0, 1, 0, 1}
                                                               : RHITextureSubresourceRange{};
                const Graph::RGPassUsage textureWrite{texture, RHIResourceState::RenderTarget, RGAccessMode::Write, range};
                const Graph::RGPassUsage textureRead{texture, RHIResourceState::CopySource, RGAccessMode::Read, range};
                const Graph::RGPassUsage bufferWrite{buffer, RHIResourceState::UnorderedAccess, RGAccessMode::Write};
                const Graph::RGPassUsage bufferRead{buffer, RHIResourceState::CopySource, RGAccessMode::Read};
                const Graph::RGPassUsage independentWrite{independent, RHIResourceState::UnorderedAccess, RGAccessMode::Write};
                graph.AddRepeatedPass("split phases", {textureWrite, bufferWrite, independentWrite},
                    {{"write", {textureWrite, bufferWrite}}, {"independent", {independentWrite}}, {"read", {textureRead, bufferRead}}}, 3,
                    [&](const auto&, uint32_t, uint32_t phase)
                    {
                        if (!enabled)
                        {
                            return;
                        }
                        const auto contains = [&](const auto& transitions, RHITransitionPhase expected)
                        {
                            return std::ranges::any_of(transitions, [&](const auto& transition)
                            { return transition.phase == expected; });
                        };
                        if (phase == 1)
                        {
                            Require(contains(services.encoder.textureTransitions, RHITransitionPhase::Begin) &&
                                    contains(services.encoder.bufferTransitions, RHITransitionPhase::Begin),
                                    "Split begins precede independent phase");
                        }
                        if (phase == 2)
                        {
                            Require(contains(services.encoder.textureTransitions, RHITransitionPhase::End) &&
                                    contains(services.encoder.bufferTransitions, RHITransitionPhase::End),
                                    "Split ends precede consumer phase");
                        }
                    }, true);
                Require(graph.Compile(error), error);
                Graph::DiagnosticSnapshot snapshot;
                Require(graph.CaptureDiagnosticSnapshot(snapshot), "RG9 snapshot");
                const auto& phases = snapshot.passes.front().phases;
                for (const bool first : {true, false})
                {
                    const auto& begin = first ? phases[1].firstBarriers : phases[1].repeatBarriers;
                    const auto& end = first ? phases[2].firstBarriers : phases[2].repeatBarriers;
                    Require(std::ranges::count_if(begin, [](const auto& t) { return t.phase == RHITransitionPhase::Begin; }) == (enabled ? 2 : 0) && end.size() == 2, "Split/full fallback count");
                    for (const auto& value : end)
                    {
                        Require(value.phase == (enabled ? RHITransitionPhase::End : RHITransitionPhase::Full),
                                "Backend fallback emits complete consumer transition");
                        if (enabled)
                        {
                            const auto match = std::ranges::find_if(begin, [&](const auto& other)
                            { return other.resource == value.resource; });
                            Require(match != begin.end() && match->phase == RHITransitionPhase::Begin &&
                                    match->before == value.before && match->after == value.after &&
                                    match->textureRange == value.textureRange && match->bufferRange == value.bufferRange,
                                    "Split pair resource, state and range match exactly");
                        }
                    }
                }
                services.encoder.Reset();
                Require(graph.Execute(error), error);
                const auto splitCount = [](const auto& transitions)
                { return std::ranges::count_if(transitions, [](const auto& t) { return t.phase == RHITransitionPhase::Begin; }); };
                Require(splitCount(services.encoder.textureTransitions) == (enabled ? 3 : 0) &&
                        splitCount(services.encoder.bufferTransitions) == (enabled ? 3 : 0),
                        "Repeated execution emits one pair per iteration");
                graph.Reset();
                Require(!graph.CaptureDiagnosticSnapshot(snapshot), "Reset invalidates snapshot capture");
            }
        }
        services.enabled = true;
        Graph adjacent{services, RGSchedulingMode::ExplicitVersioned};
        auto buffer = adjacent.Write(adjacent.CreateBuffer({64, true, false, "adjacent"}));
        const Graph::RGPassUsage write{buffer, RHIResourceState::UnorderedAccess, RGAccessMode::Write};
        const Graph::RGPassUsage read{buffer, RHIResourceState::CopySource, RGAccessMode::Read};
        adjacent.AddRepeatedPass("no independent interval", {write},
            {{"write", {write}}, {"last write", {write}}, {"read", {read}}}, 2,
            [](const auto&, uint32_t, uint32_t) {}, true);
        Require(adjacent.Compile(error), error);
        Graph::DiagnosticSnapshot adjacentSnapshot;
        Require(adjacent.CaptureDiagnosticSnapshot(adjacentSnapshot), "Adjacent phases snapshot");
        for (const auto& phase : adjacentSnapshot.passes[0].phases)
        {
            Require(std::ranges::all_of(phase.firstBarriers, [](const auto& t)
                    { return t.phase == RHITransitionPhase::Full; }) &&
                    std::ranges::all_of(phase.repeatBarriers, [](const auto& t)
                    { return t.phase == RHITransitionPhase::Full; }),
                    "Intervening access prevents splitting, preserving UAV barriers");
        }
        Graph invalid{services, RGSchedulingMode::ExplicitVersioned};
        RGTextureDesc desc{};
        desc.width = desc.height = 8;
        auto target = invalid.Write(invalid.CreateTexture(desc));
        invalid.AddPass("bad range", {{target, RHIResourceState::CopyDest, RGAccessMode::Write, {0, 0, 0, 1}}},
                        [](const auto&) {}, true);
        Require(!invalid.Compile(error), "Zero range rejects compile");
        invalid.Reset();
        target = invalid.Write(invalid.CreateTexture(desc));
        invalid.AddPass("recovered", {{target, RHIResourceState::CopyDest, RGAccessMode::Write}}, [](const auto&) {}, true);
        Require(invalid.Compile(error) && invalid.Execute(error), "Failed range compilation recovers after Reset");
    }

    void CheckSubresources(ce::regression::PortableRenderServices& services)
    {
        services.subresourceCapabilities = {true, true, true};
        std::string error;
        Graph graph{services, RGSchedulingMode::ExplicitVersioned};
        RGTextureDesc descriptor{};
        descriptor.width = descriptor.height = 16;
        descriptor.arraySize = 2;
        descriptor.allowUnorderedAccess = true;
        descriptor.name = "Layers";
        auto first = graph.Write(graph.CreateTexture(descriptor));
        auto second = graph.Write(first);
        const RHITextureSubresourceRange layer0{0, 1, 0, 1};
        const RHITextureSubresourceRange layer1{0, 1, 1, 1};
        auto producer =
            graph.AddPass("Layer zero", {{first, RHIResourceState::UnorderedAccess, RGAccessMode::Write, layer0}},
                          [](const auto&) {});
        auto discarded = graph.AddPass("Discarded layer one",
                                       {{second, RHIResourceState::UnorderedAccess, RGAccessMode::Write, layer1}},
                                       [](const auto&) {});
        auto consumer = graph.AddPass(
            "Inherited layer zero", {{second, RHIResourceState::ShaderResource, RGAccessMode::Read, layer0}},
            [](const auto&) {}, true);
        Require(graph.Compile(error), error);
        Require(!graph.IsPassCulled(producer) && graph.IsPassCulled(discarded),
                "Partial versions inherit untouched data and cull unrelated writes");
        Graph::DiagnosticSnapshot snapshot;
        Require(graph.CaptureDiagnosticSnapshot(snapshot), "Subresource diagnostic snapshot");
        Require(std::ranges::any_of(snapshot.versionEdges, [&](const auto& edge)
                                    { return edge.producer == producer.index && edge.consumer == consumer.index; }),
                "Read of an inherited range depends on its real reaching writer");
        Require(snapshot.passes[producer.index].usages[0].textureRange == layer0 &&
                    std::ranges::any_of(snapshot.passes[producer.index].barriers,
                                        [](const auto& barrier) { return barrier.textureRange.layerCount == 1; }),
                "Diagnostic snapshots preserve declared and lowered subresource ranges");
        services.encoder.Reset();
        Require(graph.Execute(error), error);
        Require(
            !services.encoder.textureTransitions.empty() &&
                std::ranges::all_of(services.encoder.textureTransitions, [](const auto& transition)
                                    { return transition.range.firstLayer == 0 && transition.range.layerCount == 1; }),
            "Only accessed array layer transitions, then converges before returning storage");
        graph.Reset();
        first = graph.Write(graph.CreateTexture(descriptor));
        graph.AddPass("Partial initialization",
                      {{first, RHIResourceState::UnorderedAccess, RGAccessMode::Write, layer0}}, [](const auto&) {});
        graph.AddPass(
            "Uninitialized read", {{first, RHIResourceState::ShaderResource, RGAccessMode::Read, layer1}},
            [](const auto&) {}, true);
        Require(!graph.Compile(error) && error.find("uninitialized subresource") != std::string::npos,
                "Read of untouched transient storage is rejected");

        graph.Reset();
        first = graph.Write(graph.CreateTexture(descriptor));
        second = graph.Write(first);
        producer = graph.AddPass(
            "Independent first", {{first, RHIResourceState::UnorderedAccess, RGAccessMode::Write, layer0}},
            [](const auto&) {}, true);
        consumer = graph.AddPass(
            "Independent second", {{second, RHIResourceState::UnorderedAccess, RGAccessMode::Write, layer1}},
            [](const auto&) {}, true);
        Require(graph.Compile(error) && graph.CaptureDiagnosticSnapshot(snapshot), error);
        Require(snapshot.versionEdges.empty(), "Nonoverlapping version writes have no WAW edge");

        Graph buffers{services, RGSchedulingMode::ExplicitVersioned};
        RHIBufferHandle native;
        RHIBufferDesc buffer{};
        buffer.bytes = 64;
        buffer.allowUnorderedAccess = true;
        Require(services.CreateBuffer(buffer, native, error), error);
        const auto imported = buffers.ImportBuffer(native, RHIResourceState::Common, "Ranges");
        const auto modified = buffers.Write(imported);
        buffers.AddPass(
            "Distinct buffer states",
            {{imported, RHIResourceState::ShaderResource, RGAccessMode::Read, {}, {0, 32}},
             {modified, RHIResourceState::UnorderedAccess, RGAccessMode::Write, {}, {32, 32}}},
            [](const auto&) {}, true);
        services.subresourceCapabilities.bufferRanges = false;
        Require(!buffers.Compile(error) && error.find("state granularity") != std::string::npos,
                "Whole-buffer legacy backends reject simultaneous conflicting range states");
        services.subresourceCapabilities.bufferRanges = true;
        Require(buffers.Compile(error), error);
        services.encoder.Reset();
        Require(buffers.Execute(error), error);
        Require(std::ranges::any_of(services.encoder.bufferTransitions, [](const auto& transition)
                                    { return transition.range.offset == 32 && transition.range.size == 32; }),
                "Range-capable backends receive precise byte transitions");

        Graph depth{services, RGSchedulingMode::ExplicitVersioned};
        RHITextureDesc depthDescriptor{};
        depthDescriptor.width = depthDescriptor.height = 16;
        depthDescriptor.format = RHIFormat::D24UnormS8Uint;
        RHITextureHandle depthNative;
        Require(services.CreateTexture(depthDescriptor, depthNative, error), error);
        const auto depthInput = depth.ImportTexture(depthNative, RHIResourceState::Common, "Depth stencil");
        depth.AddPass(
            "Separate depth and stencil",
            {{depthInput, RHIResourceState::ShaderResource, RGAccessMode::Read, {0, 1, 0, 1, RHITextureAspect::Depth}},
             {depthInput, RHIResourceState::CopySource, RGAccessMode::Read, {0, 1, 0, 1, RHITextureAspect::Stencil}}},
            [](const auto&) {}, true);
        services.subresourceCapabilities.separateDepthStencil = false;
        Require(!depth.Compile(error) && error.find("state granularity") != std::string::npos,
                "Combined depth/stencil backends reject incompatible simultaneous aspect states");
        services.subresourceCapabilities.separateDepthStencil = true;
        Require(depth.Compile(error), error);
        services.encoder.Reset();
        Require(depth.Execute(error), error);
        Require(std::ranges::any_of(services.encoder.textureTransitions, [](const auto& transition)
                                    { return transition.range.aspect == RHITextureAspect::Stencil; }),
                "Separate aspect support emits precise stencil transitions");

        Graph volume{services, RGSchedulingMode::ExplicitVersioned};
        RHITextureDesc volumeDescriptor{};
        volumeDescriptor.dim = RHITextureDesc::Dim::Texture3D;
        volumeDescriptor.width = volumeDescriptor.height = 16;
        volumeDescriptor.depthOrArraySize = 8;
        volumeDescriptor.mipLevels = 4;
        volumeDescriptor.format = RHIFormat::RGBA8Unorm;
        RHITextureHandle volumeNative;
        Require(services.CreateTexture(volumeDescriptor, volumeNative, error), error);
        auto volumeInput = volume.ImportTexture(volumeNative, RHIResourceState::Common, "Volume");
        volume.AddPass(
            "Volume mip", {{volumeInput, RHIResourceState::ShaderResource, RGAccessMode::Read, {1, 1, 0, 1}}},
            [](const auto&) {}, true);
        Require(volume.Compile(error), error);
        services.encoder.Reset();
        Require(volume.Execute(error) &&
                    std::ranges::all_of(services.encoder.textureTransitions, [](const auto& transition)
                                        { return transition.range.firstMip == 1 && transition.range.layerCount == 1; }),
                "Volume depth is not interpreted as array subresources");
        volume.Reset();
        volumeInput = volume.ImportTexture(volumeNative, RHIResourceState::Common, "Volume");
        volume.AddPass(
            "Invalid volume layer", {{volumeInput, RHIResourceState::ShaderResource, RGAccessMode::Read, {1, 1, 1, 1}}},
            [](const auto&) {}, true);
        Require(!volume.Compile(error) && error.find("Invalid subresource range") != std::string::npos,
                "A 3D texture cannot declare an array-layer range");

        Graph bounded{services, RGSchedulingMode::ExplicitVersioned};
        RHITextureDesc largeDescriptor{};
        largeDescriptor.width = largeDescriptor.height = 32768;
        largeDescriptor.mipLevels = 16;
        largeDescriptor.depthOrArraySize = 8192;
        largeDescriptor.format = RHIFormat::RGBA8Unorm;
        RHITextureHandle largeNative;
        Require(services.CreateTexture(largeDescriptor, largeNative, error), error);
        const auto largeInput = bounded.ImportTexture(largeNative, RHIResourceState::Common, "Partition bound");
        std::vector<Graph::RGPassUsage> narrow;
        for (uint32_t layer = 0; layer < 4097; ++layer)
        {
            narrow.push_back(
                {largeInput, RHIResourceState::ShaderResource, RGAccessMode::Read, {layer % 16, 1, layer, 1}});
        }
        bounded.AddPass("Excessive partition", narrow, [](const auto&) {}, true);
        Require(!bounded.Compile(error) && error.find("partition exceeds") != std::string::npos,
                "Pathological Cartesian subresource partitions fail before unbounded compiler allocation");
    }

    void CheckAuthoredProgram()
    {
        ce::regression::PortableRenderServices services;
        Graph graph{services, RGSchedulingMode::ExplicitVersioned};
        std::string error;
        auto pass = graph.AddPass("Authored async", {}, [](const auto&) {}, true);
        graph.DeclareAsyncCompute(pass);
        Require(graph.Compile(error), error);
        const auto program = graph.SnapshotProgram();
        Graph::FrameBindings bindings;
        bindings.owner = own::make_shared<int>(1);
        bindings.passes.resize(1);
        bindings.passes[0].execute = [](const auto&) {};
        Graph direct{services, RGSchedulingMode::ExplicitVersioned};
        Require(direct.BindProgram(program, bindings, error) && direct.Compile(error), error);
        direct.SetQueueExecutionMode(2);
        Require(direct.SelectExecutionQueues({true, true, false, true}, error), error);
        Require(direct.GetQueueDiagnostics().schedule.usesCompute,
                "Direct program binding preserves authored async without timing history");
        graph.Reset();
        pass = graph.AddPass("Authored async", {}, [](const auto&) {}, true);
        graph.DeclareComputeCompatible(pass);
        Require(graph.Compile(error), error);
        Require(!graph.GetStats().programBindingReused,
                "Changing authored async intent rejects structural reuse");
        graph.SetQueueExecutionMode(2);
        Require(graph.SelectExecutionQueues({true, true, false, true}, error), error);
        Require(!graph.GetQueueDiagnostics().schedule.usesCompute,
                "Compute compatibility alone does not select async");
    }

    void CheckQueues()
    {
        using namespace ce::regression;
        QueueServices services;
        auto graphics = own::make_shared<PortableQueue>(RHIQueueIdentity{41, 1, RHIQueueKind::Graphics});
        auto compute = own::make_shared<PortableQueue>(RHIQueueIdentity{41, 2, RHIQueueKind::Compute});
        Graph::QueueEndpoint graphicsEndpoint{graphics}, computeEndpoint{compute};
        PortableQueueRecorder recorder{services};
        auto graph = own::make_shared<Graph>(services, RGSchedulingMode::ExplicitVersioned);
        std::string error;
        RHITextureDesc texture{};
        texture.width = texture.height = 16;
        texture.format = RHIFormat::RGBA8Unorm;
        RHITextureHandle native;
        Require(services.CreateTexture(texture, native, error), error);
        const auto input = graph->ImportTexture(native, RHIResourceState::RenderTarget, "Immutable environment");
        const auto graphicsPass = graph->AddPass(
            "Graphics read", {{input, RHIResourceState::ShaderResource, RGAccessMode::Read}}, [](const auto&) {}, true);
        const auto computePass = graph->AddPass(
            "Compute read", {{input, RHIResourceState::ShaderResource, RGAccessMode::Read}}, [](const auto&) {}, true);
        graph->DeclareAsyncCompute(computePass);
        Require(graph->Compile(error), error);
        const ce::dynamic_array<Graph::QueueHint> hints{{graphicsPass, false, 1000000}, {computePass, true, 1000000}};
        Graph::QueueSchedule schedule;
        Require(graph->BuildQueueSchedule({true, true, false, true}, hints, 1, schedule, error), error);
        Require(schedule.usesCompute && schedule.waits.empty(), "Supported immutable readers fan out across queues");
        services.sharedReads = false;
        Require(graph->BuildQueueSchedule({true, true, false, true}, hints, 1, schedule, error), error);
        Require(!schedule.waits.empty(), "Unsupported shared state keeps conservative queue ownership dependencies");
        services.sharedReads = true;
        Graph::QueueExecution execution;
        Require(graph->SubmitQueues(graph, recorder, graphicsEndpoint, &computeEndpoint, hints, 1,
                                    own::make_shared<int>(1), execution, error),
                error);
        Require(execution.retirement.IsValid() && execution.retirement.Size() == 2 &&
                    !execution.retirement.IsComplete(),
                "Accepted execution publishes both terminal queue requirements");
        size_t sharedTransitions = 0;
        for (const auto& record : recorder.recorded)
        {
            for (const auto& transition : record->encoder.textureTransitions)
            {
                if (transition.texture == native)
                {
                    Require(record->queue.kind == RHIQueueKind::Graphics,
                            "Compute reader never transitions a graphics shared state");
                    Require(transition.before == RHIResourceState::SharedShaderResource ||
                                transition.after == RHIResourceState::SharedShaderResource,
                            "Only prologue establishment and epilogue restoration touch a shared resource");
                    ++sharedTransitions;
                }
            }
        }
        Require(sharedTransitions == 2, "Stable read state has one fanout and one fanin transition");
        bool resetRejected = false;
        try
        {
            graph->Reset();
        }
        catch (const std::logic_error&)
        {
            resetRejected = true;
        }
        Require(resetRejected, "Submitted graph cannot release resources before queue collection");
        bool declarationBufferRejected = false;
        try
        {
            graph->CreateDeclarationBuffer<RGHandle>(1);
        }
        catch (const std::logic_error&)
        {
            declarationBufferRejected = true;
        }
        Require(declarationBufferRejected, "In-flight graph cannot mutate declaration arena storage");
        graphics->Complete();
        Require(!execution.retirement.IsComplete(), "Graphics completion alone does not retire independent compute");
        compute->Complete();
        Require(execution.retirement.IsComplete(), "All queue terminal requirements retire together");
        graph->Reset();

        services.aliasing = true;
        graph->SetTransientAliasing(true);
        const auto first = graph->Write(graph->CreateBuffer({128, true, false, "First occupant"}));
        const auto bridge = graph->Write(graph->CreateBuffer({32, true, false, "Dependency bridge"}));
        const auto next = graph->Write(graph->CreateBuffer({128, true, false, "Next occupant"}));
        const auto a =
            graph->AddPass("A", {{first, RHIResourceState::UnorderedAccess, RGAccessMode::Write}}, [](const auto&) {});
        const auto b = graph->AddPass("B",
                                      {{first, RHIResourceState::ShaderResource, RGAccessMode::Read},
                                       {bridge, RHIResourceState::UnorderedAccess, RGAccessMode::Write}},
                                      [](const auto&) {});
        const auto c = graph->AddPass("C",
                                      {{bridge, RHIResourceState::ShaderResource, RGAccessMode::Read},
                                       {next, RHIResourceState::UnorderedAccess, RGAccessMode::Write}},
                                      [](const auto&) {});
        const auto d = graph->AddPass(
            "D", {{next, RHIResourceState::ShaderResource, RGAccessMode::Read}}, [](const auto&) {}, true);
        graph->DeclareAsyncCompute(a);
        graph->DeclareAsyncCompute(c);
        Require(graph->Compile(error), error);
        Graph::DiagnosticSnapshot snapshot;
        Require(graph->CaptureDiagnosticSnapshot(snapshot) && snapshot.resources[0].aliasGroup != UINT32_MAX &&
                    snapshot.resources[0].aliasGroup == snapshot.resources[2].aliasGroup,
                "All-use dependency proof permits safe placed allocation reuse");
        const auto firstNative = graph->ResolveBufferHandle(first), nextNative = graph->ResolveBufferHandle(next);
        recorder.recorded.clear();
        const ce::dynamic_array<Graph::QueueHint> aliasHints{
            {a, true, 1000000}, {b, false, 1000000}, {c, true, 1000000}, {d, false, 1000000}};
        Require(graph->SubmitQueues(graph, recorder, graphicsEndpoint, &computeEndpoint, aliasHints, 1,
                                    own::make_shared<int>(2), execution, error),
                error);
        Require(execution.computeBatches != 0, "Aliased graph supports actual cross-queue submission");
        for (const auto boundary : {size_t{0}, recorder.recorded.size() - 1})
        {
            for (const auto& transition : recorder.recorded[boundary]->encoder.bufferTransitions)
            {
                Require(transition.buffer != firstNative && transition.buffer != nextNative,
                        "Prologue/epilogue cannot transition inactive placed objects");
            }
        }
        graphics->Complete();
        compute->Complete();
        graph->Reset();
        const auto independentA = graph->Write(graph->CreateBuffer({64, true, false, "Independent A"}));
        const auto independentB = graph->Write(graph->CreateBuffer({64, true, false, "Independent B"}));
        graph->AddPass(
            "Independent A", {{independentA, RHIResourceState::UnorderedAccess, RGAccessMode::Write}},
            [](const auto&) {}, true);
        graph->AddPass(
            "Independent B", {{independentB, RHIResourceState::UnorderedAccess, RGAccessMode::Write}},
            [](const auto&) {}, true);
        Require(graph->Compile(error) && graph->CaptureDiagnosticSnapshot(snapshot), error);
        Require(snapshot.resources[0].aliasGroup == UINT32_MAX && snapshot.resources[1].aliasGroup == UINT32_MAX,
                "Linear topological intervals alone cannot authorize aliasing concurrent work");
    }

    void CheckPartialSubmission()
    {
        using namespace ce::regression;
        QueueServices services;
        auto graphics = own::make_shared<PortableQueue>(RHIQueueIdentity{42, 1, RHIQueueKind::Graphics});
        auto compute = own::make_shared<PortableQueue>(RHIQueueIdentity{42, 2, RHIQueueKind::Compute});
        // Accept the graphics prologue and compute work, reject the final join.
        graphics->rejectAfter = 1;
        Graph::QueueEndpoint graphicsEndpoint{graphics}, computeEndpoint{compute};
        PortableQueueRecorder recorder{services};
        auto graph = own::make_shared<Graph>(services, RGSchedulingMode::ExplicitVersioned);
        const auto pass = graph->AddPass("Accepted compute", {}, [](const auto&) {}, true);
        graph->DeclareAsyncCompute(pass);
        std::string error;
        Require(graph->Compile(error), error);
        int ownerDestroyed = 0;
        Graph::QueueExecution execution;
        Require(!graph->SubmitQueues(graph, recorder, graphicsEndpoint, &computeEndpoint, {{pass, true, 1000000}}, 1,
                                     own::make_shared<Owner>(&ownerDestroyed), execution, error),
                "Injected epilogue rejection must fail the execution");
        Require(execution.submittedBatches == 2 && execution.retirement.Size() == 2 && execution.retirement.IsValid() &&
                    execution.recoveryRequired && !execution.completed && !execution.completion.IsValid(),
                "Partial submission preserves accepted queue frontiers without publishing a whole-frame completion");
        Require(ownerDestroyed == 0, "Accepted batches keep external storage alive after submission failure");
        bool rejected = false;
        try
        {
            graph->Reset();
        }
        catch (const std::logic_error&)
        {
            rejected = true;
        }
        Require(rejected, "Partial submission cannot release pending resources");
        graphics->Complete();
        Require(ownerDestroyed == 0 && !execution.retirement.IsComplete(),
                "A surviving compute batch continues to pin its graph owner");
        compute->Complete();
        Require(ownerDestroyed == 1 && execution.retirement.IsComplete(),
                "All accepted queues must complete before failed-execution ownership retires");
        Require(!graph->Compile(error), "A partially submitted execution cannot be recompiled before Reset");
        graph->Reset();
    }

    void CheckPreparationFailure()
    {
        using namespace ce::regression;
        QueueServices services;
        auto graphics = own::make_shared<PortableQueue>(RHIQueueIdentity{43, 1, RHIQueueKind::Graphics});
        auto compute = own::make_shared<PortableQueue>(RHIQueueIdentity{43, 2, RHIQueueKind::Compute});
        Graph::QueueEndpoint graphicsEndpoint{graphics}, computeEndpoint{compute};
        PortableQueueRecorder recorder{services};
        recorder.rejectPreparationAt = 1;
        auto graph = own::make_shared<Graph>(services, RGSchedulingMode::ExplicitVersioned);
        const auto pass = graph->AddPass("Pre-submit translation", {}, [](const auto&) {}, true);
        graph->DeclareAsyncCompute(pass);
        std::string error;
        Require(graph->Compile(error), error);
        int ownerDestroyed = 0;
        Graph::QueueExecution execution;
        Require(!graph->SubmitQueues(graph, recorder, graphicsEndpoint, &computeEndpoint, {{pass, true, 1000000}}, 1,
                                     own::make_shared<Owner>(&ownerDestroyed), execution, error),
                "Preparation rejection fails before submission begins");
        Require(recorder.preparations == 2 && graphics->submissions == 0 && compute->submissions == 0 &&
                    graphics->waits.empty() && compute->waits.empty() && !execution.submissionAttempted &&
                    !execution.recoveryRequired && !execution.retirement.IsValid(),
                "Every batch translation completes before the first native wait, signal or submit");
        Require(execution.failure.stage == RHICommandFailureStage::Translation && !execution.failure.message.empty() &&
                    ownerDestroyed == 1,
                "Pre-native failure publishes translation diagnostics and releases unsubmitted ownership");
        graph->Reset();
    }

    void CheckExternalPresentation()
    {
        using namespace ce::regression;
        QueueServices services;
        services.retainTextureStates = false;
        services.subresourceCapabilities = {true, true, true};
        RHITextureDesc descriptor{};
        descriptor.width = descriptor.height = 16;
        descriptor.format = RHIFormat::RGBA8Unorm;
        RHITextureHandle native;
        std::string error;
        Require(services.CreateTexture(descriptor, native, error), error);
        Graph invalid{services, RGSchedulingMode::ExplicitVersioned};
        const auto unreadable = invalid.ImportTexture(native, RHIResourceState::ExternalShared, "External display");
        invalid.AddPass(
            "Discarded external input", {{unreadable, RHIResourceState::ShaderResource, RGAccessMode::Read}},
            [](const auto&) {}, true);
        Require(!invalid.Compile(error) && error.find("fully overwritten") != std::string::npos,
                "External reacquisition cannot read contents discarded by the native ownership acquire");

        for (const bool explicitRange : {false, true})
        {
            auto graphics = own::make_shared<PortableQueue>(RHIQueueIdentity{44, 1, RHIQueueKind::Graphics});
            auto compute = own::make_shared<PortableQueue>(RHIQueueIdentity{44, 2, RHIQueueKind::Compute});
            Graph::QueueEndpoint graphicsEndpoint{graphics}, computeEndpoint{compute};
            PortableQueueRecorder recorder{services};
            auto graph = own::make_shared<Graph>(services, RGSchedulingMode::ExplicitVersioned);
            const auto output = graph->Write(graph->ImportTexture(native, RHIResourceState::ExternalShared, "Display"));
            const auto range = explicitRange ? RHITextureSubresourceRange{0, 1, 0, 1} : RHITextureSubresourceRange{};
            const auto present =
                graph->AddPass("Full display copy", {{output, RHIResourceState::CopyDest, RGAccessMode::Write, range}},
                               [](const auto&) {});
            const auto work = graph->AddPass("Independent compute", {}, [](const auto&) {}, true);
            graph->DeclareAsyncCompute(present);
            graph->DeclareAsyncCompute(work);
            graph->RequireImportedFinalState(output, RHIResourceState::ExternalShared);
            Require(graph->Compile(error), error);
            Graph::QueueExecution execution;
            Require(graph->SubmitQueues(graph, recorder, graphicsEndpoint, &computeEndpoint,
                                        {{present, true, 1000000}, {work, true, 1000000}}, 1, own::make_shared<int>(1),
                                        execution, error),
                    error);
            Require(execution.computeBatches == 1, "External ownership operations stay on graphics");
            size_t transitions = 0;
            for (const auto& recorded : recorder.recorded)
            {
                for (const auto& transition : recorded->encoder.textureTransitions)
                {
                    if (transition.texture != native)
                    {
                        continue;
                    }
                    ++transitions;
                    Require(recorded->queue.kind == RHIQueueKind::Graphics &&
                                ((transition.before == RHIResourceState::ExternalShared &&
                                  transition.after == RHIResourceState::CopyDest) ||
                                 (transition.before == RHIResourceState::CopyDest &&
                                  transition.after == RHIResourceState::ExternalShared)),
                            "External acquire/release never passes through the generic COMMON queue handoff");
                }
            }
            Require(transitions == 2, "Whole and range planners preserve direct external acquire/copy/release");
            graphics->Complete();
            compute->Complete();
            graph->Reset();
        }
    }
} // namespace

int main()
{
    try
    {
        ce::regression::PortableRenderServices services;
        Graph graph{services, RGSchedulingMode::ExplicitVersioned};
        std::string error;
        int callbackValue = 0;
        graph.BeginProgramBinding();
        const auto firstHandle = Declare(graph, callbackValue, 1);
        Require(graph.FinishProgramBinding(error), error);
        Require(graph.Compile(error), error);
        const auto program = graph.SnapshotProgram();
        Require(bool(program) && program->ResourceCount() == 1 && program->PassCount() == 1,
                "Compiled graph publishes logical slots");
        Require(!graph.GetStats().programBindingReused, "Cold declaration cannot report a program hit");
        Require(graph.Execute(error) && callbackValue == 1, "Initial callback executes");

        Graph otherView{services, RGSchedulingMode::ExplicitVersioned};
        otherView.InheritProgram(program);
        otherView.BeginProgramBinding();
        const auto otherHandle = Declare(otherView, callbackValue, 10);
        Require(otherView.FinishProgramBinding(error) && otherView.Compile(error), error);
        Require(otherView.GetStats().programBindingReused &&
                    otherView.SnapshotProgram().operator->() == program.operator->() &&
                    otherHandle.epoch != firstHandle.epoch,
                "New per-view executions inherit one immutable program with distinct live epochs");
        Require(otherView.Execute(error) && callbackValue == 10, "Inherited program invokes this view's callback");
        otherView.Reset();

        graph.Reset();
        Require(!graph.ResolveBufferHandle(firstHandle).IsValid(), "Old epochs remain invalid");
        graph.BeginProgramBinding();
        const auto secondHandle = Declare(graph, callbackValue, 2, 90);
        Require(graph.FinishProgramBinding(error), error);
        Require(graph.Compile(error), error);
        Require(graph.SnapshotProgram().operator->() == program.operator->(),
                "Changing recording cost preserves immutable program identity");
        Require(graph.GetStats().programBindingReused && graph.GetStats().programUsageCopiesAvoided == 1,
                "Streamed binding reuses retained usages and the compiled DAG");
        Require(graph.Execute(error) && callbackValue == 2, "Current frame replaces callback captures");
        Require(graph.RebindProgramHandle(program->ResourceHandle(0, 0)).epoch == secondHandle.epoch,
                "Logical resource handles bind to the current epoch");

        graph.Reset();
        graph.BeginProgramBinding();
        Declare(graph, callbackValue, 3, 90, true);
        Require(graph.FinishProgramBinding(error) && graph.Compile(error), error);
        Require(!graph.GetStats().programBindingReused && graph.SnapshotProgram()->PassCount() == 2,
                "Topology changes compile a complete same-frame fallback");
        Require(graph.Execute(error) && callbackValue == 3, "Fallback preserves current callback captures");

        Graph direct{services, RGSchedulingMode::ExplicitVersioned};
        Graph::FrameBindings bindings;
        int destroyed = 0;
        bindings.owner = own::make_shared<Owner>(&destroyed);
        bindings.resources.resize(program->ResourceCount());
        bindings.passes.resize(program->PassCount());
        const auto logical = program->ResourceHandle(0, 0);
        bindings.passes[0].splitExecute = [&, logical](const auto& context, uint32_t, uint32_t)
        {
            Require(context.graph->ResolveBufferHandle(context.graph->RebindProgramHandle(logical)).IsValid(),
                    "Direct callbacks resolve current physical resources");
            callbackValue = 4;
        };
        bindings.passes[0].recordCost = 8;
        Require(direct.BindProgram(program, bindings, error) && direct.Compile(error), error);
        Require(direct.GetStats().programBindingReused, "Direct binding executes the admitted program");
        bindings.owner.reset();
        Require(destroyed == 0, "Frame execution pins the binding owner");
        Require(direct.Execute(error) && callbackValue == 4, "Direct binding executes new callbacks");
        direct.Reset();
        Require(destroyed == 1, "Reset releases frame ownership after resources and callbacks");
        Require(!direct.RebindProgramHandle(logical).IsValid(), "Reset revokes bound logical handles");
        Require(!direct.BindProgram(program, bindings, error), "Missing frame ownership is rejected");
        bindings.owner = own::make_shared<int>(1);
        bindings.passes[0].splitExecute = {};
        Require(!direct.BindProgram(program, bindings, error), "Missing callback rejected before publication");

        Graph imported{services, RGSchedulingMode::ExplicitVersioned};
        RHITextureHandle firstNative, secondNative;
        RHITextureDesc texture{};
        texture.width = texture.height = 16;
        texture.format = RHIFormat::RGBA8Unorm;
        Require(services.CreateTexture(texture, firstNative, error) &&
                    services.CreateTexture(texture, secondNative, error),
                error);
        auto source = imported.ImportTexture(firstNative, RHIResourceState::CopyDest, "Source");
        imported.AddPass(
            "Read", {{source, RHIResourceState::ShaderResource, RGAccessMode::Read}}, [](const auto&) {}, true);
        Require(imported.Compile(error), error);
        auto importedProgram = imported.SnapshotProgram();
        imported.Reset();
        Graph::FrameBindings importedBindings;
        importedBindings.owner = own::make_shared<int>(2);
        importedBindings.resources.resize(1);
        importedBindings.resources[0].texture = secondNative;
        importedBindings.resources[0].initialState = RHIResourceState::RenderTarget;
        importedBindings.passes.resize(1);
        importedBindings.passes[0].execute = [](const auto&) {};
        Require(imported.BindProgram(importedProgram, importedBindings, error) && imported.Compile(error), error);
        services.encoder.Reset();
        Require(imported.Execute(error), error);
        Require(services.encoder.textureTransitions.size() == 1 &&
                    services.encoder.textureTransitions[0].texture == secondNative &&
                    services.encoder.textureTransitions[0].before == RHIResourceState::RenderTarget,
                "Barrier lowering binds current native IDs and initial states");
        imported.Reset();
        services.ReleaseTexture(secondNative);
        Require(!imported.BindProgram(importedProgram, importedBindings, error), "Stale native import is rejected");
        Graph stale{services, RGSchedulingMode::ExplicitVersioned};
        const auto staleInput = stale.ImportTexture(secondNative, RHIResourceState::Common, "Released storage");
        stale.AddPass(
            "Read released", {{staleInput, RHIResourceState::ShaderResource, RGAccessMode::Read}}, [](const auto&) {},
            true);
        Require(!stale.Compile(error) && error.find("stale native import") != std::string::npos,
                "A cold fallback cannot publish a program for stale native storage");
        texture.width = 32;
        Require(services.CreateTexture(texture, secondNative, error), error);
        importedBindings.resources[0].texture = secondNative;
        Require(!imported.BindProgram(importedProgram, importedBindings, error),
                "Direct binding rejects changed native resource schema");
        imported.BeginProgramBinding();
        source = imported.ImportTexture(secondNative, RHIResourceState::RenderTarget, "Source");
        imported.AddPass(
            "Read", {{source, RHIResourceState::ShaderResource, RGAccessMode::Read}}, [](const auto&) {}, true);
        Require(imported.FinishProgramBinding(error) && imported.Compile(error), error);
        Require(!imported.GetStats().programBindingReused &&
                    imported.SnapshotProgram().operator->() != importedProgram.operator->(),
                "Streamed binding recompiles changed native resource schema in the same frame");
        CheckDeclarationBufferLifetime();
        CheckQueuePlanReset();
        CheckInlineRecordingEntryGuards();
        CheckOwnedSubmissionScratch();
        CheckSubresources(services);
        CheckRg9SplitAndRecovery();
        CheckTransientMips();
        CheckTransientMipProgramInvalidation();
        CheckTransientMipInitializationAfterReuse();
        std::cout << "RG9_CPU_OK texture/buffer whole/range split/full adjacent/UAV mip/program reset/recovery\n";
        CheckAuthoredProgram();
        CheckQueues();
        CheckPartialSubmission();
        CheckPreparationFailure();
        CheckExternalPresentation();
        std::cout << "program bindings, ownership, inline recording entry guards, partial-range hazards/barriers, "
                     "shared reads and happens-before async aliasing passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
