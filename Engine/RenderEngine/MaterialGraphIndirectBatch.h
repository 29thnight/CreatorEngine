#pragma once
#include "../Utility_Framework/Ownership.h"
#include "RenderContainerReusePolicy.h"
#include "MaterialGraphPacketGroups.h"

#include "GpuGeometryVisibility.h"
#include "MaterialGraphTransientBufferPool.h"
#include "MaterialGraphPacketWork.h"
#include "../Utility_Framework/StandardLibrary.h"

namespace material_graph
{
    // Immutable command templates belong to the sealed frame. Only GPU visibility
    // writes dispatch dimensions. Raster consumers submit one command range per PSO.
    struct IndirectMaterialBatch
    {
        using PacketDraw = ScenePacketRecord;
        static_assert(sizeof(PacketDraw) == 16);
        struct Bucket
        {
            std::size_t representative{};
            std::vector<std::uint32_t> roots;
            std::uint32_t offset{}, count{};
        };
        IRenderDeviceServices* device{};
        own::shared_owner<TransientBufferPool> pool;
        own::shared_owner<const GpuGeometryVisibility::Frame> visibility;
        own::shared_owner<const LX::Runtime::ComputeGeneration> pipeline;
        std::array<own::shared_owner<const LX::Runtime::ComputeGeneration>, 6> packetPipelines;
        own::unique_owner<PacketDispatchWork> packetWork;
        // Physical buckets outlive the active span, preserving every roots buffer.
        std::vector<Bucket> m_bucketStorage;
        std::span<Bucket> buckets;
        std::vector<uint32_t> m_capacities;
        bool m_packetMode{};

        Bucket& AppendBucket()
        {
            const auto index = buckets.size();
            if (index == m_bucketStorage.size())
            {
                m_bucketStorage.emplace_back();
            }
            buckets = std::span(m_bucketStorage).first(index + 1);
            auto& bucket = buckets.back();
            bucket.representative = 0;
            bucket.offset = bucket.count = 0;
            bucket.roots.clear();
            return bucket;
        }
        void ReserveBuckets(size_t count)
        {
            const auto active = buckets.size();
            m_bucketStorage.reserve(count);
            buckets = std::span(m_bucketStorage).first(active);
        }
        RHIBufferSlice templates, commands, constants;
        RHIBufferHandle output;
        RHIBindingTable outputTable;
        mutable RGHandle graphOutput;
        mutable const EnhancedRenderGraph* graphOwner{};
        mutable std::uint64_t graphEpoch{};
        std::uint32_t commandCount{};
        std::string name{"METEOR.Scene.Commands"};

        ~IndirectMaterialBatch() { ResetForReuse(); }
        void ResetForReuse()
        {
            if (device && !m_packetMode)
            {
                ReleaseSceneBuffer(*device, pool, output);
            }
            if (packetWork)
            {
                packetWork->ResetForReuse();
            }
            for (auto& bucket : buckets)
            {
                bucket.roots.clear();
            }
            buckets = {};
            m_capacities.clear();
            visibility.reset();
            pipeline.reset();
            packetPipelines = {};
            pool.reset();
            device = nullptr;
            templates = commands = constants = {};
            output = {};
            outputTable = {};
            graphOutput = {};
            graphOwner = nullptr;
            graphEpoch = 0;
            commandCount = 0;
            m_packetMode = false;
        }

        bool UploadPackets(std::span<const PacketDraw> draws, std::string& error,
                           const ScenePacketGroups* groups = nullptr)
        {
            if (!device || !visibility || draws.empty() || draws.size() > UINT32_MAX || buckets.empty() ||
                std::ranges::any_of(packetPipelines, [](const auto& item) { return !item; }))
            {
                error = "METEOR mesh packet inputs are incomplete.";
                return false;
            }
            uint64_t capacity{};
            std::span<const uint32_t> capacities;
            if (groups)
            {
                if (!groups->CapacityValid() || groups->Templates().size() != draws.size() ||
                    groups->Capacities().size() != buckets.size())
                {
                    error = "METEOR cached packet capacity does not match its exact group plan.";
                    return false;
                }
                capacity = groups->Capacity();
                capacities = groups->Capacities();
            }
            else
            {
                auto& checkedCapacities = m_capacities;
                checkedCapacities.assign(buckets.size(), 0);

                for (const auto& draw : draws)
                {
                    if (!draw.record || draw.bucket >= buckets.size() ||
                        uint64_t(checkedCapacities[draw.bucket]) + draw.capacity > UINT32_MAX ||
                        (capacity += draw.capacity) > UINT32_MAX)
                    {
                        error = "METEOR mesh packet capacities exceed their address range.";
                        return false;
                    }
                    checkedCapacities[draw.bucket] += draw.capacity;
                }
                capacities = m_capacities;
            }
            commandCount = static_cast<uint32_t>(draws.size());
            commands = device->AllocateUpload(draws.size_bytes(), 16);
            if (!commands.IsWritable())
            {
                error = "METEOR mesh packet input upload failed.";
                return false;
            }
            std::memcpy(commands.cpuAddress, draws.data(), draws.size_bytes());
            if (!packetWork)
            {
                packetWork = own::make_unique<PacketDispatchWork>();
            }
            m_packetMode = true;
            if (!packetWork->Prepare(*device, pool, commandCount, static_cast<uint32_t>(capacity), capacities, 0, 0,
                                     name, error))
            {
                return false;
            }
            output = packetWork->arguments;
            return true;
        }
        bool PacketMode() const { return m_packetMode; }
        RHIBufferHandle PacketBuffer() const { return PacketMode() ? packetWork->packets : RHIBufferHandle{}; }
        RHIBufferHandle RangeBuffer() const { return PacketMode() ? packetWork->ranges : RHIBufferHandle{}; }
        uint32_t PacketCapacity() const { return PacketMode() ? packetWork->values.packetCapacity : 0; }

        bool Upload(std::span<const std::uint32_t> words, std::span<const std::array<std::uint32_t, 4>> records,
                    std::uint32_t outputBytes, std::string& error)
        {
            // The native indirect signature consumes ascending, unique root slots.
            // Reject malformed templates in Prepare, before any graph work is recorded.
            for (const auto& bucket : buckets)
            {
                uint32_t previous{};
                bool first = true;
                for (const auto root : bucket.roots)
                {
                    const auto slot = root & 0x7fffffffu;
                    if (!first && slot <= previous)
                    {
                        error = "METEOR indirect roots must be strictly increasing.";
                        return false;
                    }
                    previous = slot;
                    first = false;
                }
            }
            commandCount = static_cast<std::uint32_t>(records.size());
            if (!commandCount)
            {
                return true;
            }
            const auto upload = [&](const void* data, std::size_t bytes)
            {
                auto slice = device->AllocateUpload(bytes, 16);
                if (slice.IsWritable())
                {
                    std::memcpy(slice.cpuAddress, data, bytes);
                }
                return slice;
            };
            templates = upload(words.data(), words.size_bytes());
            commands = upload(records.data(), records.size_bytes());
            const std::array<std::uint32_t, 16> values{commandCount};
            constants = device->UploadConstants(values.data(), sizeof(values));
            RHIBufferDesc desc;
            desc.bytes = outputBytes;
            desc.allowUnorderedAccess = true;
            desc.allowIndirectArguments = true;
            const std::wstring debugName(name.begin(), name.end());
            desc.debugName = debugName.c_str();
            if (!templates.IsValid() || !commands.IsValid() || !constants.IsValid() ||
                !CreateSceneBuffer(*device, pool.operator->(), desc, output, error))
            {
                return false;
            }
            const auto binding = RHIBindingDesc::UavBuffer(output, outputBytes / 4, 4);
            outputTable = device->CreateBindings({&binding, 1});
            return outputTable.IsValid();
        }

        void Declare(EnhancedRenderGraph& graph, own::shared_owner<const IndirectMaterialBatch> owner) const
        {
            if (!commandCount)
            {
                return;
            }
            if (PacketMode())
            {
                packetWork->Declare(
                    graph, owner, packetPipelines, name, false,
                    [&graph, owner](auto& uses) { owner->visibility->AddVisibleIdsRead(graph, uses); },
                    [owner](auto& encoder, const auto&, uint32_t stage)
                    {
                        if (stage == 1 || stage == 5)
                        {
                            encoder.SetRootBuffer(RHIBindPoint::Compute, 1, owner->commands);
                            encoder.SetRootBuffer(RHIBindPoint::Compute, 2,
                                                  RHIBufferSlice::Whole(owner->visibility->VisibleIdBuffer()));
                        }
                        if (stage == 4)
                        {
                            encoder.SetRootBuffer(RHIBindPoint::Compute, 3, owner->packetWork->plans);
                        }
                    });
                graphOwner = graph.DeclarationGraph();
                graphEpoch = graph.DeclarationEpoch();
                graphOutput = packetWork->graphArguments;
                return;
            }
            if (graphOwner == graph.DeclarationGraph() && graphEpoch == graph.DeclarationEpoch())
            {
                return;
            }
            if (graphOwner)
            {
                throw std::runtime_error("METEOR indirect batch cannot cross graph recordings.");
            }
            graphOwner = graph.DeclarationGraph();
            graphEpoch = graph.DeclarationEpoch();
            auto uses = graph.CreateDeclarationUsages();
            if (visibility)
            {
                visibility->AddCommandInputRead(graph, uses);
            }
            graphOutput = graph.ImportBuffer(output, RHIResourceState::Common, name.c_str());
            graph.RequireImportedFinalState(graphOutput, RHIResourceState::Common);
            graphOutput = graph.Write(graphOutput);
            uses.push_back({graphOutput, RHIResourceState::UnorderedAccess, RGAccessMode::Write});
            const auto pass = graph.AddPass(
                (name + ".Build").c_str(), std::move(uses),
                [owner](const auto& execution)
                {
                    auto& encoder = *execution.encoder;
                    encoder.SetPipeline(RHIBindPoint::Compute, owner->pipeline->GetHandle());
                    encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, owner->constants);
                    encoder.SetRootBuffer(RHIBindPoint::Compute, 1, owner->commands);
                    encoder.SetRootBuffer(RHIBindPoint::Compute, 2, owner->templates);
                    encoder.SetRootBuffer(RHIBindPoint::Compute, 3,
                                          owner->visibility ? RHIBufferSlice::Whole(owner->visibility->Arguments())
                                                            : owner->templates);
                    encoder.SetBindings(RHIBindPoint::Compute, 4, owner->outputTable);
                    encoder.Dispatch((owner->commandCount + 63) / 64, 1, 1);
                });
            // CPU 콜백은 준비된 불변 입력만 읽는다. visibility 결과의 GPU 의존성은 위 resource read로 유지한다.
            graph.DeclareRecordingIndependent(pass);
        }
        RGHandle GraphOutput(const EnhancedRenderGraph& graph) const
        {
            if (!commandCount)
            {
                return {};
            }
            if (graphOwner != graph.DeclarationGraph() || graphEpoch != graph.DeclarationEpoch())
            {
                throw std::runtime_error("METEOR commands require their explicit producer on this graph.");
            }
            return graphOutput;
        }
        void ValidateInput(const EnhancedRenderGraph& graph, RGHandle input) const
        {
            const auto expected = GraphOutput(graph);
            if (input.IsValid() != expected.IsValid() ||
                (input.IsValid() && (input.index != expected.index || input.version != expected.version ||
                                     input.kind != expected.kind || input.epoch != expected.epoch)))
            {
                throw std::runtime_error("METEOR command input is not the published buffer version.");
            }
        }
        void AddRead(EnhancedRenderGraph& graph, auto& uses, RGHandle input) const
        {
            ValidateInput(graph, input);
            if (PacketMode())
            {
                packetWork->AddRead(uses);
                return;
            }
            if (input.IsValid())
            {
                uses.push_back({input, RHIResourceState::IndirectArgument, RGAccessMode::Read});
            }
        }
        void Draw(RHIEncoder& encoder, std::size_t bucket) const
        {
            if (PacketMode())
            {
                throw std::runtime_error("METEOR packet draw requires its selector root slot.");
            }
            const auto& range = buckets[bucket];
            if (!encoder.DispatchMeshIndirectBatch(output, range.offset, range.count, range.roots))
            {
                throw std::runtime_error("METEOR material bucket indirect submission failed.");
            }
        }
        void Draw(RHIEncoder& encoder, std::size_t bucket, uint32_t selectorRoot) const
        {
            if (PacketMode())
            {
                packetWork->Draw(encoder, bucket, selectorRoot);
            }
            else
            {
                Draw(encoder, bucket);
            }
        }
    };
    // Single preparation owner acquires objects before publishing worker tasks.
    // A retained pool reference is the only reference allowed when recycling.
    class IndirectMaterialBatchPool
    {
      public:
        own::shared_owner<IndirectMaterialBatch> Acquire()
        {
            if (!RenderContainerReuseEnabled())
            {
                return own::make_shared<IndirectMaterialBatch>();
            }
            for (auto& batch : m_batches)
            {
                if (batch.use_count() == 1)
                {
                    batch->ResetForReuse();
                    return batch;
                }
            }
            auto batch = own::make_shared<IndirectMaterialBatch>();
            m_batches.push_back(batch);
            return batch;
        }
        void ClearAfterIdle() { m_batches.clear(); }

      private:
        std::vector<own::shared_owner<IndirectMaterialBatch>> m_batches;
    };
} // namespace material_graph
