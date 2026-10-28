#include "../Containers/ContainerAPI.h"
#pragma once
#if defined(CE_CONTRACT_STANDALONE)
#include "../../Tools/build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif
#include "MaterialGraphTransientBufferPool.h"
#include "MaterialGraphPacketPlan.h"

namespace material_graph
{
    // A recording owns all packet storage and selectors. The GPU counts and packs
    // work; the CPU supplies only conservative capacities and immutable PSO buckets.
    struct PacketDispatchWork final
    {
        static constexpr uint32_t ScanWidth = PacketDispatchPlan::ScanWidth;
        struct Constants
        {
            uint32_t inputCount{}, bucketCount{}, packetCapacity{}, dispatchWidth{};
            uint32_t scanCount{}, scanSource{}, scanDestination{}, scanSums{};
            uint32_t scanParent{}, scanBase{}, width{}, height{};
            uint32_t sliceCapacity{}, padding[3]{};
        };
        static_assert(sizeof(Constants) == 64);
        struct ScanLevel
        {
            uint32_t count{}, source{}, prefix{}, sums{};
            RHIBufferSlice scanConstants, addConstants;
        };
        struct Slice
        {
            uint64_t argumentOffset{};
            RHIBufferSlice selector;
        };
        own::shared_owner<const PacketDispatchPlan> plan;
        IRenderDeviceServices* device{};
        own::shared_owner<TransientBufferPool> pool;
        RHIBufferHandle arguments, ranges, packets, scratch;
        RHIBindingTable outputs;
        RHIBufferSlice constants, plans;
        Constants values;
        ce::dynamic_array<ScanLevel> levels;
        ce::dynamic_array<Slice> slices;
        uint32_t scratchWords{}, argumentWords{};
        mutable RGHandle graphArguments, graphRanges, graphPackets, graphScratch;
        mutable const EnhancedRenderGraph* graphOwner{};
        mutable uint64_t graphEpoch{};

        ~PacketDispatchWork() { ResetForReuse(); }
        void ResetForReuse()
        {
            if (device)
            {
                for (const auto buffer : {arguments, ranges, packets, scratch})
                {
                    ReleaseSceneBuffer(*device, pool, buffer);
                }
            }
            // Immutable CPU shape survives GPU recording retirement.
            arguments = {};
            ranges = {};
            packets = {};
            scratch = {};
            outputs = {};
            constants = {};
            plans = {};
            values = {};
            levels.clear();
            slices.clear();
            scratchWords = argumentWords = 0;
            graphArguments = graphRanges = graphPackets = graphScratch = {};
            graphOwner = nullptr;
            graphEpoch = 0;
            pool.reset();
            device = nullptr;
        }
        bool Prepare(IRenderDeviceServices& services, own::shared_owner<TransientBufferPool> buffers,
                     uint32_t inputCount, uint32_t capacity, std::span<const uint32_t> bucketCapacities, uint32_t width,
                     uint32_t height, std::string_view name, std::string& error)
        {
            ResetForReuse();
            device = &services;
            pool = std::move(buffers);
            const auto caps = device->GetMeshShaderCapabilities();
            const auto dispatchWidth =
                static_cast<uint32_t>(std::min<uint64_t>(caps.maxGroupCountX, caps.maxTotalGroupCount));
            const auto rows = dispatchWidth
                                  ? std::min<uint64_t>({caps.maxGroupCountY, caps.maxTotalGroupCount / dispatchWidth,
                                                        UINT32_MAX / dispatchWidth})
                                  : 0;
            const auto sliceCapacity = static_cast<uint32_t>(dispatchWidth * rows);
            if (!capacity || bucketCapacities.empty() || bucketCapacities.size() > UINT32_MAX || !caps.meshIndirect ||
                !sliceCapacity || !caps.maxGroupCountZ)
            {
                error = "METEOR packet capacity or mesh dispatch limits are invalid.";
                return false;
            }
            values.inputCount = inputCount;
            values.bucketCount = static_cast<uint32_t>(bucketCapacities.size());
            values.packetCapacity = capacity;
            values.dispatchWidth = dispatchWidth;
            values.width = width;
            values.height = height;
            values.sliceCapacity = sliceCapacity;
            if (!plan || !PacketDispatchPlanCache::Enabled() ||
                !plan->Matches(capacity, dispatchWidth, sliceCapacity, bucketCapacities))
            {
                auto& cache = PacketDispatchPlanCache::Current();
                const auto hits = cache.Hits();
                plan = cache.Acquire(capacity, dispatchWidth, sliceCapacity, bucketCapacities, error,
                                     PacketDispatchPlanCache::Enabled());
                ce::profile_instant(cache.Hits() != hits ? ce::marker<"MaterialPacketPlanReuse">()
                                                         : ce::marker<"MaterialPacketPlanBuild">());
            }
            else
            {
                ce::profile_instant(ce::marker<"MaterialPacketPlanReuse">());
            }
            if (!plan)
            {
                return false;
            }
            scratchWords = plan->scratchWords;
            argumentWords = plan->argumentWords;
            values.scanBase = plan->levels.front().prefix;
            levels.reserve(plan->levels.size());
            for (const auto& level : plan->levels)
            {
                levels.push_back({level.count, level.source, level.prefix, level.sums, {}, {}});
            }
            constants = device->UploadConstants(&values, sizeof(values));
            if (!constants.IsValid())
            {
                error = "METEOR packet constants upload failed.";
                return false;
            }
            for (size_t index = 0; index < levels.size(); ++index)
            {
                auto& level = levels[index];
                auto scan = values;
                scan.scanCount = level.count;
                scan.scanSource = level.source;
                scan.scanDestination = level.prefix;
                scan.scanSums = level.sums;
                level.scanConstants = device->UploadConstants(&scan, sizeof(scan));
                if (index + 1 < levels.size())
                {
                    scan.scanParent = levels[index + 1].prefix;
                    level.addConstants = device->UploadConstants(&scan, sizeof(scan));
                }
                if (!level.scanConstants.IsValid() || (index + 1 < levels.size() && !level.addConstants.IsValid()))
                {
                    error = "METEOR packet scan constants upload failed.";
                    return false;
                }
            }
            slices.reserve(plan->selectors.size());
            for (size_t index = 0; index < plan->selectors.size(); ++index)
            {
                const auto& selector = plan->selectors[index];
                const auto uploaded = device->UploadConstants(selector.data(), sizeof(selector));
                if (!uploaded.IsValid())
                {
                    error = "METEOR packet selector upload failed.";
                    return false;
                }
                slices.push_back({uint64_t(index) * 12, uploaded});
            }
            {
                ce::profile_scope upload{ce::marker<"MaterialScenePacketPlanUpload">()};
                plans = device->AllocateUpload(plan->values.size() * sizeof(plan->values.front()), 16);
                if (!plans.IsWritable())
                {
                    error = "METEOR packet bucket plan upload failed.";
                    return false;
                }
                std::memcpy(plans.cpuAddress, plan->values.data(), plan->values.size() * sizeof(plan->values.front()));
            }
            const std::wstring label(name.begin(), name.end());
            ce::fixed_array<RHIBufferDesc, 4> descriptions{};
            const ce::fixed_array sizes{uint64_t(argumentWords) * 4, uint64_t(values.bucketCount) * 16,
                                   uint64_t(capacity) * 8, uint64_t(scratchWords) * 4};
            for (std::size_t index = 0; index < descriptions.size(); ++index)
            {
                auto& desc = descriptions[index];
                desc.bytes = sizes[index];
                desc.allowUnorderedAccess = true;
                desc.allowIndirectArguments = index == 0;
                desc.debugName = label.c_str();
            }
            ce::fixed_array<RHIBufferHandle, 4> packetBuffers{};
            bool acquired = true;
            if (pool)
            {
                acquired = pool->AcquireBatch(descriptions, packetBuffers, error);
            }
            else
            {
                for (std::size_t index = 0; index < descriptions.size(); ++index)
                {
                    if (!device->CreateBuffer(descriptions[index], packetBuffers[index], error))
                    {
                        acquired = false;
                        break;
                    }
                }
            }
            arguments = packetBuffers[0];
            ranges = packetBuffers[1];
            packets = packetBuffers[2];
            scratch = packetBuffers[3];
            if (!acquired)
            {
                return false;
            }
            const RHIBindingDesc views[]{RHIBindingDesc::UavBuffer(arguments, argumentWords, 4),
                                         RHIBindingDesc::UavBuffer(ranges, values.bucketCount, 16),
                                         RHIBindingDesc::UavBuffer(packets, capacity, 8),
                                         RHIBindingDesc::UavBuffer(scratch, scratchWords, 4)};
            {
                ce::profile_scope bindings{ce::marker<"MaterialScenePacketOutputBindings">()};
                outputs = device->CreateBindings(views);
            }
            if (!outputs.IsValid())
            {
                error = "METEOR packet output bindings failed.";
            }
            return outputs.IsValid();
        }
        static void DispatchLinear(RHIEncoder& encoder, uint32_t items, uint32_t threads)
        {
            const auto groups = (uint64_t(items) + threads - 1) / threads;
            encoder.Dispatch(static_cast<uint32_t>(std::min<uint64_t>(groups, 65535)),
                             static_cast<uint32_t>((groups + 65534) / 65535), 1);
        }
        template<class Owner, class InputUses, class BindInputs>
        void Declare(EnhancedRenderGraph& graph, own::shared_owner<const Owner> owner,
                     const ce::fixed_array<own::shared_owner<const LX::Runtime::ComputeGeneration>, 6>& pipelines,
                     const std::string& name, bool tileWork, InputUses inputUses, BindInputs bindInputs) const
        {
            if (graphOwner == graph.DeclarationGraph() && graphEpoch == graph.DeclarationEpoch())
            {
                return;
            }
            if (graphOwner || graph.GetSchedulingMode() != RGSchedulingMode::ExplicitVersioned)
            {
                throw std::runtime_error("METEOR packet work requires one ExplicitVersioned recording.");
            }
            graphOwner = graph.DeclarationGraph();
            graphEpoch = graph.DeclarationEpoch();
            const auto import = [&](RHIBufferHandle buffer, const char* suffix)
            {
                const auto handle = graph.ImportBuffer(buffer, RHIResourceState::Common, (name + suffix).c_str());
                graph.RequireImportedFinalState(handle, RHIResourceState::Common);
                return handle;
            };
            graphArguments = import(arguments, ".Arguments");
            graphRanges = import(ranges, ".Ranges");
            graphPackets = import(packets, ".Packets");
            graphScratch = import(scratch, ".Scan");
            struct Declaration
            {
                own::shared_owner<const Owner> owner;
                ce::fixed_array<own::shared_owner<const LX::Runtime::ComputeGeneration>, 6> pipelines;
                BindInputs bindInputs;
            };
            const auto* declaration =
                graph.RetainDeclaration<Declaration>(std::move(owner), pipelines, std::move(bindInputs));
            // Input-use callbacks only append reads; declaration never yields or
            // recursively enters this packet stage. The graph copies each prefix.
            auto uses = graph.CreateDeclarationUsages();
            const auto add = [&](uint32_t stage, RHIBufferSlice stageConstants, uint32_t items, const char* suffix)
            {
                if (!uses.empty())
                {
                    uses.clear();
                }
                if (stage <= 3)
                {
                    graphScratch = stage == 0 ? graph.Write(graphScratch) : graph.Modify(graphScratch);
                    uses.push_back({graphScratch, RHIResourceState::UnorderedAccess,
                                    stage == 0 ? RGAccessMode::Write : RGAccessMode::ReadWrite});
                }
                if (stage == 4)
                {
                    uses.push_back({graphScratch, RHIResourceState::UnorderedAccess, RGAccessMode::Read});
                    graphRanges = graph.Write(graphRanges);
                    graphArguments = graph.Write(graphArguments);
                    uses.push_back({graphRanges, RHIResourceState::UnorderedAccess, RGAccessMode::Write});
                    uses.push_back({graphArguments, RHIResourceState::UnorderedAccess, RGAccessMode::Write});
                }
                if (stage == 5)
                {
                    graphRanges = graph.Modify(graphRanges);
                    graphPackets = graph.Write(graphPackets);
                    uses.push_back({graphRanges, RHIResourceState::UnorderedAccess, RGAccessMode::ReadWrite});
                    uses.push_back({graphPackets, RHIResourceState::UnorderedAccess, RGAccessMode::Write});
                }
                if (stage == 1 || stage == 5)
                {
                    inputUses(uses);
                }
                const auto pass = graph.AddPass(
                    (name + suffix).c_str(), std::span{uses},
                    graph.StoreExecute(
                        [this, declaration, stage, stageConstants, items, tileWork](const auto& execution)
                        {
                            auto& encoder = *execution.encoder;
                            encoder.SetPipeline(RHIBindPoint::Compute, declaration->pipelines[stage]->GetHandle());
                            encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, stageConstants);
                            declaration->bindInputs(encoder, execution, stage);
                            encoder.SetBindings(RHIBindPoint::Compute, 4, outputs);
                            if (tileWork && (stage == 1 || stage == 5))
                            {
                                encoder.Dispatch((values.width + 7) / 8, (values.height + 7) / 8, 1);
                            }
                            else
                            {
                                DispatchLinear(encoder, items,
                                               stage == 2 ? ScanWidth : (!tileWork && stage == 5 ? 1 : 64));
                            }
                        }));
                graph.DeclareRecordingIndependent(pass);
            };
            add(0, constants, values.bucketCount, ".Reset");
            add(1, constants, values.inputCount, ".Count");
            for (const auto& level : levels)
            {
                add(2, level.scanConstants, level.count, ".Scan");
            }
            for (size_t index = levels.size(); index-- > 1;)
            {
                add(3, levels[index - 1].addConstants, levels[index - 1].count, ".AddOffsets");
            }
            add(4, constants, values.bucketCount, ".Arguments");
            add(5, constants, values.inputCount, ".Scatter");
        }
        void AddRead(auto& uses) const
        {
            uses.push_back({graphPackets, RHIResourceState::ShaderResource, RGAccessMode::Read});
            uses.push_back({graphRanges, RHIResourceState::ShaderResource, RGAccessMode::Read});
            uses.push_back({graphArguments, RHIResourceState::IndirectArgument, RGAccessMode::Read});
        }
        void Draw(RHIEncoder& encoder, size_t bucket, uint32_t selectorRoot) const
        {
            const auto range = plan->buckets.at(bucket);
            for (uint32_t index = range.first; index < range.first + range.count; ++index)
            {
                const auto& slice = slices[index];
                encoder.SetConstantBuffer(RHIBindPoint::Graphics, selectorRoot, slice.selector);
                if (!encoder.DispatchMeshIndirect(arguments, slice.argumentOffset))
                {
                    throw std::runtime_error("METEOR packet mesh dispatch failed.");
                }
            }
        }
    };
} // namespace material_graph
