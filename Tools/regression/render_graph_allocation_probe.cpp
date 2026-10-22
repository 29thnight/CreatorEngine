#include "fixtures/render-graph-allocation-counters.h"
#include "fixtures/render-graph-portable-services.h"
#include "fixtures/render-graph-portable-queues.h"
#include "Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h"
#include <array>
#include <chrono>
#include <iostream>

namespace
{
    using Graph = EnhancedRenderGraph;
    using ce::regression::AllocationCounts;
    using ce::regression::ScopedAllocationMeasurement;
    constexpr uint32_t kWarmups = 8;
    const uint32_t kIterations = []
    {
        const auto* value = std::getenv("CE_GRAPH_ALLOCATION_ITERATIONS");
        const auto iterations = value ? std::strtoul(value, nullptr, 10) : 512ul;
        if (!iterations || iterations > 100000)
        {
            throw std::invalid_argument("CE_GRAPH_ALLOCATION_ITERATIONS must be in [1, 100000]");
        }
        return static_cast<uint32_t>(iterations);
    }();
    constexpr uint32_t kResources = 24;

    void Require(bool condition, const std::string& error)
    {
        if (!condition)
        {
            throw std::runtime_error(error);
        }
    }

    struct Measurement
    {
        AllocationCounts allocations;
        uint64_t nanoseconds{};
        uint32_t iterations{};

        template<typename F>
        void Record(F&& function)
        {
            const auto begin = std::chrono::steady_clock::now();
            {
                ScopedAllocationMeasurement scope{allocations};
                function();
            }
            nanoseconds += static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - begin).count());
            ++iterations;
        }
    };

    class Services final : public ce::regression::PortableRenderServices
    {
      public:
        bool SupportsConcurrentTextureShaderReads() const override { return true; }
        bool SupportsTransientAliasing() const override { return true; }
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

    class BoundaryRecorder final : public IRHIQueueRecorder
    {
      public:
        explicit BoundaryRecorder(Services& device) : native(device) {}
        ce::regression::PortableQueueRecorder native;
        IRenderDeviceServices& DeviceServices() const override { return native.DeviceServices(); }
        bool Record(const own::shared_owner<IRHICommandQueue>& queue, const std::function<void(RHIEncoder&)>& commands,
                    ce::lifetime_token owner, ce::pooled_owner<IRHIQueueCommandBatch>& batch,
                    std::string& error) override
        {
            // Fixture encoder/batch allocation is not production graph allocation.
            // The caller-created std::function and graph lease remain measured.
            struct Pause
            {
                AllocationCounts* prior{std::exchange(ce::regression::activeAllocationCounts, nullptr)};
                ~Pause() { ce::regression::activeAllocationCounts = prior; }
            } pause;
            return native.Record(queue, commands, std::move(owner), batch, error);
        }
    };

    void Print(const char* name, const Measurement& measurement)
    {
        std::cout << "{\"name\":\"" << name << "\",\"iterations\":" << measurement.iterations
                  << ",\"standard_calls\":" << measurement.allocations.standardCalls
                  << ",\"standard_bytes\":" << measurement.allocations.standardBytes
                  << ",\"mimalloc_calls\":" << measurement.allocations.mimallocCalls
                  << ",\"mimalloc_bytes\":" << measurement.allocations.mimallocBytes
                  << ",\"cpu_nanoseconds\":" << measurement.nanoseconds << '}';
    }

    void MeasureFrames(bool ranges, bool aliasing, Measurement& declarations, Measurement& compilation)
    {
        Services services;
        services.subresourceCapabilities = {true, true, true};
        services.textures.reserve(128);
        services.buffers.reserve(128);
        RGTransientPool pool;
        Graph graph{services, RGSchedulingMode::ExplicitVersioned};
        graph.SetTransientPool(&pool);
        graph.SetTransientAliasing(aliasing);
        std::array<RGTextureDesc, kResources> descriptors;
        std::array<std::string, kResources> passNames;
        for (uint32_t i = 0; i < kResources; ++i)
        {
            descriptors[i].width = descriptors[i].height = 64;
            descriptors[i].arraySize = ranges ? 8 : 1;
            descriptors[i].allowUnorderedAccess = true;
            descriptors[i].name = "Texture " + std::to_string(i);
            passNames[i] = "Pass " + std::to_string(i);
        }
        std::string error;
        const auto declare = [&]
        {
            graph.Reset();
            graph.BeginProgramBinding();
            std::array<RGHandle, kResources> handles;
            for (uint32_t i = 0; i < kResources; ++i)
            {
                handles[i] = graph.Write(graph.CreateTexture(descriptors[i]));
            }
            const RHITextureSubresourceRange range =
                ranges ? RHITextureSubresourceRange{0, 1, 0, 1} : RHITextureSubresourceRange{};
            for (uint32_t i = 0; i < kResources; ++i)
            {
                std::array<Graph::RGPassUsage, 2> usages;
                usages[0] = {handles[i], RHIResourceState::UnorderedAccess, RGAccessMode::Write, range};
                if (i)
                {
                    usages[1] = {handles[i - 1], RHIResourceState::ShaderResource, RGAccessMode::Read, range};
                }
                graph.AddPass(
                    passNames[i], std::span{usages.data(), i ? 2u : 1u}, [](const auto&) {}, i == kResources - 1);
            }
            Require(graph.FinishProgramBinding(error), error);
        };
        const auto compile = [&] { Require(graph.Compile(error), error); };
        size_t warmedNativeCreates{};
        for (uint32_t frame = 0; frame < kWarmups + kIterations; ++frame)
        {
            if (frame < kWarmups)
            {
                declare();
                compile();
                warmedNativeCreates = services.texturesCreated + services.buffersCreated;
            }
            else
            {
                declarations.Record(declare);
                compilation.Record(compile);
                const auto stats = graph.GetStats();
                Require(stats.structuralScheduleReused && stats.programBindingReused,
                        "Warm measurement must reuse the actual immutable graph program");
                Require(services.texturesCreated + services.buffersCreated == warmedNativeCreates,
                        "Native fixture growth must stay outside warmed graph allocation measurement");
                if (aliasing)
                {
                    Require(stats.aliasResourceReuses > 1, "Warm alias measurement must reuse real placed groups");
                }
            }
            services.encoder.Reset();
            Require(graph.Execute(error), error);
        }
    }

    void MeasureQueuePlanning(Measurement& threshold, Measurement& overlap)
    {
        Services services;
        Graph graph{services, RGSchedulingMode::ExplicitVersioned};
        RHITextureDesc descriptor{};
        descriptor.width = descriptor.height = 64;
        descriptor.format = RHIFormat::RGBA8Unorm;
        RHITextureHandle native;
        std::string error;
        Require(services.CreateTexture(descriptor, native, error), error);
        const auto input = graph.ImportTexture(native, RHIResourceState::Common, "Immutable");
        std::vector<Graph::QueueHint> hints;
        hints.reserve(32);
        for (uint32_t i = 0; i < 32; ++i)
        {
            const auto pass = graph.AddPass(
                "Read", {{input, RHIResourceState::ShaderResource, RGAccessMode::Read}}, [](const auto&) {}, true);
            if (i % 2)
            {
                graph.DeclareComputeCompatible(pass);
            }
            hints.push_back({pass, i % 2 != 0, 1000000});
        }
        Require(graph.Compile(error), error);
        Graph::QueueSchedule schedule;
        for (const auto placement : {RGQueuePlacement::Threshold, RGQueuePlacement::Overlap})
        {
            graph.SetQueueCostModel({placement});
            const auto plan = [&]
            { Require(graph.BuildQueueSchedule({true, true, false, true}, hints, 1, schedule, error), error); };
            auto& measurement = placement == RGQueuePlacement::Threshold ? threshold : overlap;
            for (uint32_t iteration = 0; iteration < kWarmups + kIterations; ++iteration)
            {
                if (iteration < kWarmups)
                {
                    plan();
                }
                else
                {
                    measurement.Record(plan);
                    Require(schedule.usesCompute, "Measured queue fixture must use both queues");
                }
            }
        }
    }

    void MeasureSubmission(bool asynchronous, Measurement& measurement)
    {
        using ce::regression::PortableQueue;
        Services services;
        auto graph = own::make_shared<Graph>(services, RGSchedulingMode::ExplicitVersioned);
        auto graphics = own::make_shared<PortableQueue>(RHIQueueIdentity{55, 1, RHIQueueKind::Graphics});
        auto compute = own::make_shared<PortableQueue>(RHIQueueIdentity{55, 2, RHIQueueKind::Compute});
        Graph::QueueEndpoint graphicsEndpoint{graphics}, computeEndpoint{compute};
        BoundaryRecorder recorder{services};
        const auto owner = own::make_shared<int>(1);
        RHITextureDesc descriptor{};
        descriptor.width = descriptor.height = 64;
        descriptor.format = RHIFormat::RGBA8Unorm;
        RHITextureHandle native;
        std::string error;
        Require(services.CreateTexture(descriptor, native, error), error);
        std::vector<Graph::QueueHint> hints;
        hints.reserve(32);
        Graph::QueueExecution execution;
        for (uint32_t frame = 0; frame < kWarmups + kIterations; ++frame)
        {
            graph->Reset();
            graph->BeginProgramBinding();
            hints.clear();
            const auto input = graph->ImportTexture(native, RHIResourceState::Common, "Immutable");
            for (uint32_t i = 0; i < 32; ++i)
            {
                const auto pass = graph->AddPass(
                    "Read", {{input, RHIResourceState::ShaderResource, RGAccessMode::Read}}, [](const auto&) {}, true);
                if (i % 2)
                {
                    graph->DeclareComputeCompatible(pass);
                }
                hints.push_back({pass, i % 2 != 0, 1000000});
            }
            Require(graph->FinishProgramBinding(error) && graph->Compile(error), error);
            const auto submit = [&]
            {
                Require(graph->SubmitQueues(graph, recorder, graphicsEndpoint,
                                            asynchronous ? &computeEndpoint : nullptr, hints, asynchronous ? 1 : 0,
                                            owner, execution, error),
                        error);
            };
            if (frame < kWarmups)
            {
                submit();
            }
            else
            {
                measurement.Record(submit);
                Require(graph->GetStats().programBindingReused, "Submission measurement must reuse its program");
            }
            Require(execution.completed && execution.retirement.IsValid(), "Submission must retain native completion");
            graphics->Complete();
            compute->Complete();
            Require(execution.retirement.IsComplete(), "Both mock queues must retire before graph reuse");
            recorder.native.recorded.clear();
            graphics->waits.clear();
            compute->waits.clear();
        }
    }
} // namespace

int main()
{
    try
    {
        Measurement plainDeclare, plainCompile, rangeDeclare, rangeCompile, aliasDeclare, aliasCompile;
        Measurement threshold, overlap, graphicsSubmit, computeSubmit;
        MeasureFrames(false, false, plainDeclare, plainCompile);
        MeasureFrames(true, false, rangeDeclare, rangeCompile);
        MeasureFrames(false, true, aliasDeclare, aliasCompile);
        MeasureQueuePlanning(threshold, overlap);
        MeasureSubmission(false, graphicsSubmit);
        MeasureSubmission(true, computeSubmit);
        std::cout << "{\"warmups\":" << kWarmups << ",\"resources_per_frame\":" << kResources
                  << ",\"scope\":\"production CPU graph, one producer, native services mocked; timing is descriptive\","
                     "\"measurements\":[";
        Print("plain_stream_declare", plainDeclare);
        std::cout << ',';
        Print("plain_compile", plainCompile);
        std::cout << ',';
        Print("range_stream_declare", rangeDeclare);
        std::cout << ',';
        Print("range_compile", rangeCompile);
        std::cout << ',';
        Print("alias_stream_declare", aliasDeclare);
        std::cout << ',';
        Print("alias_compile", aliasCompile);
        std::cout << ',';
        Print("queue_threshold", threshold);
        std::cout << ',';
        Print("queue_overlap", overlap);
        std::cout << ',';
        Print("queue_submit_graphics_excluding_native_mock", graphicsSubmit);
        std::cout << ',';
        Print("queue_submit_compute_excluding_native_mock", computeSubmit);
        std::cout << "]}\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
