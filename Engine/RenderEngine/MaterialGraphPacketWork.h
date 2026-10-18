#pragma once
#include "MaterialGraphTransientBufferPool.h"

namespace material_graph
{
// A recording owns all packet storage and selectors. The GPU counts and packs
// work; the CPU supplies only conservative capacities and immutable PSO buckets.
struct PacketDispatchWork final
{
    static constexpr uint32_t ScanWidth = 256;
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
    struct BucketRange { uint32_t first{}, count{}; };
    IRenderDeviceServices* device{};
    std::shared_ptr<TransientBufferPool> pool;
    RHIBufferHandle arguments, ranges, packets, scratch;
    RHIBindingTable outputs;
    RHIBufferSlice constants, plans;
    Constants values;
    std::vector<ScanLevel> levels;
    std::vector<Slice> slices;
    std::vector<BucketRange> buckets;
    uint32_t scratchWords{}, argumentWords{};
    mutable RGHandle graphArguments, graphRanges, graphPackets, graphScratch;
    mutable const EnhancedRenderGraph* graphOwner{};
    mutable uint64_t graphEpoch{};

    ~PacketDispatchWork()
    {
        if (device)
            for (const auto buffer : {arguments, ranges, packets, scratch})
                ReleaseSceneBuffer(*device, pool, buffer);
    }
    bool Prepare(IRenderDeviceServices& services, std::shared_ptr<TransientBufferPool> buffers,
                 uint32_t inputCount, uint32_t capacity, std::span<const uint32_t> bucketCapacities,
                 uint32_t width, uint32_t height, std::string_view name, std::string& error)
    {
        device = &services;
        pool = std::move(buffers);
        const auto caps = device->GetMeshShaderCapabilities();
        const auto dispatchWidth = static_cast<uint32_t>(std::min<uint64_t>(caps.maxGroupCountX,
                                                                          caps.maxTotalGroupCount));
        const auto rows = dispatchWidth ? std::min<uint64_t>({caps.maxGroupCountY,
            caps.maxTotalGroupCount / dispatchWidth, UINT32_MAX / dispatchWidth}) : 0;
        const auto sliceCapacity = static_cast<uint32_t>(dispatchWidth * rows);
        if (!capacity || bucketCapacities.empty() || bucketCapacities.size() > UINT32_MAX ||
            !caps.meshIndirect || !sliceCapacity || !caps.maxGroupCountZ)
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
        uint64_t words = values.bucketCount;
        uint32_t count = values.bucketCount, source = 0;
        for (;;)
        {
            const auto groups = (uint64_t(count) + ScanWidth - 1) / ScanWidth;
            if (words + count + groups > UINT32_MAX)
            {
                error = "METEOR packet scan exceeds its address range.";
                return false;
            }
            ScanLevel level;
            level.count = count;
            level.source = source;
            level.prefix = static_cast<uint32_t>(words);
            words += count;
            level.sums = static_cast<uint32_t>(words);
            words += groups;
            levels.push_back(level);
            if (groups == 1)
                break;
            source = level.sums;
            count = static_cast<uint32_t>(groups);
        }
        scratchWords = static_cast<uint32_t>(words);
        values.scanBase = levels.front().prefix;
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
            if (!level.scanConstants.IsValid() ||
                (index + 1 < levels.size() && !level.addConstants.IsValid()))
            {
                error = "METEOR packet scan constants upload failed.";
                return false;
            }
        }
        std::vector<std::array<uint32_t, 4>> planValues;
        planValues.reserve(bucketCapacities.size());
        buckets.reserve(bucketCapacities.size());
        for (uint32_t bucket = 0; bucket < values.bucketCount; ++bucket)
        {
            const auto capacityForBucket = std::min(capacity, bucketCapacities[bucket]);
            const auto sliceCount = static_cast<uint32_t>(std::max<uint64_t>(1,
                (uint64_t(capacityForBucket) + sliceCapacity - 1) / sliceCapacity));
            if (uint64_t(slices.size()) + sliceCount > UINT32_MAX / 3)
            {
                error = "METEOR packet slice arguments exceed their address range.";
                return false;
            }
            const auto first = static_cast<uint32_t>(slices.size());
            buckets.push_back({first, sliceCount});
            planValues.push_back({first, sliceCount, capacityForBucket, 0});
            for (uint32_t slice = 0; slice < sliceCount; ++slice)
            {
                const std::array<uint32_t, 4> selector{bucket, dispatchWidth, slice * sliceCapacity, 0};
                const auto uploaded = device->UploadConstants(selector.data(), sizeof(selector));
                if (!uploaded.IsValid())
                {
                    error = "METEOR packet selector upload failed.";
                    return false;
                }
                slices.push_back({uint64_t(first + slice) * 12, uploaded});
            }
        }
        argumentWords = static_cast<uint32_t>(slices.size() * 3);
        plans = device->AllocateUpload(planValues.size() * sizeof(planValues.front()), 16);
        if (!plans.IsWritable())
        {
            error = "METEOR packet bucket plan upload failed.";
            return false;
        }
        std::memcpy(plans.cpuAddress, planValues.data(), planValues.size() * sizeof(planValues.front()));
        const std::wstring label(name.begin(), name.end());
        const auto create = [&](RHIBufferHandle& buffer, uint64_t bytes, bool indirect) {
            RHIBufferDesc desc;
            desc.bytes = bytes;
            desc.allowUnorderedAccess = true;
            desc.allowIndirectArguments = indirect;
            desc.debugName = label.c_str();
            return CreateSceneBuffer(*device, pool.get(), desc, buffer, error);
        };
        if (!create(arguments, uint64_t(argumentWords) * 4, true) ||
            !create(ranges, uint64_t(values.bucketCount) * 16, false) ||
            !create(packets, uint64_t(capacity) * 8, false) ||
            !create(scratch, uint64_t(scratchWords) * 4, false))
            return false;
        const RHIBindingDesc views[]{RHIBindingDesc::UavBuffer(arguments, argumentWords, 4),
            RHIBindingDesc::UavBuffer(ranges, values.bucketCount, 16),
            RHIBindingDesc::UavBuffer(packets, capacity, 8),
            RHIBindingDesc::UavBuffer(scratch, scratchWords, 4)};
        outputs = device->CreateBindings(views);
        if (!outputs.IsValid())
            error = "METEOR packet output bindings failed.";
        return outputs.IsValid();
    }
    static void DispatchLinear(RHIEncoder& encoder, uint32_t items, uint32_t threads)
    {
        const auto groups = (uint64_t(items) + threads - 1) / threads;
        encoder.Dispatch(static_cast<uint32_t>(std::min<uint64_t>(groups, 65535)),
                         static_cast<uint32_t>((groups + 65534) / 65535), 1);
    }
    template<class Owner, class InputUses, class BindInputs>
    void Declare(EnhancedRenderGraph& graph, std::shared_ptr<const Owner> owner,
                 const std::array<std::shared_ptr<const LX::Runtime::ComputeGeneration>, 6>& pipelines,
                 std::string name, bool tileWork, InputUses inputUses, BindInputs bindInputs) const
    {
        if (graphOwner == &graph && graphEpoch == graph.ResourceEpoch())
            return;
        if (graphOwner || graph.GetSchedulingMode() != RGSchedulingMode::ExplicitVersioned)
            throw std::runtime_error("METEOR packet work requires one ExplicitVersioned recording.");
        graphOwner = &graph;
        graphEpoch = graph.ResourceEpoch();
        const auto import = [&](RHIBufferHandle buffer, const char* suffix) {
            const auto handle = graph.ImportBuffer(buffer, RHIResourceState::Common, (name + suffix).c_str());
            graph.RequireImportedFinalState(handle, RHIResourceState::Common);
            return handle;
        };
        graphArguments = import(arguments, ".Arguments");
        graphRanges = import(ranges, ".Ranges");
        graphPackets = import(packets, ".Packets");
        graphScratch = import(scratch, ".Scan");
        const auto add = [&](uint32_t stage, RHIBufferSlice stageConstants, uint32_t items,
                             const char* suffix) {
            std::vector<EnhancedRenderGraph::RGPassUsage> uses;
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
                inputUses(uses);
            const auto pipeline = pipelines[stage];
            const auto pass = graph.AddPass((name + suffix).c_str(), std::move(uses),
                [this, owner, pipeline, bindInputs, stage, stageConstants, items, tileWork](const auto& execution) {
                    auto& encoder = *execution.encoder;
                    encoder.SetPipeline(RHIBindPoint::Compute, pipeline->GetHandle());
                    encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, stageConstants);
                    bindInputs(encoder, execution, stage);
                    encoder.SetBindings(RHIBindPoint::Compute, 4, outputs);
                    if (tileWork && (stage == 1 || stage == 5))
                        encoder.Dispatch((values.width + 7) / 8, (values.height + 7) / 8, 1);
                    else
                        DispatchLinear(encoder, items, stage == 2 ? ScanWidth :
                                                       (!tileWork && stage == 5 ? 1 : 64));
                });
            graph.DeclareRecordingIndependent(pass);
        };
        add(0, constants, values.bucketCount, ".Reset");
        add(1, constants, values.inputCount, ".Count");
        for (const auto& level : levels)
            add(2, level.scanConstants, level.count, ".Scan");
        for (size_t index = levels.size(); index-- > 1;)
            add(3, levels[index - 1].addConstants, levels[index - 1].count, ".AddOffsets");
        add(4, constants, values.bucketCount, ".Arguments");
        add(5, constants, values.inputCount, ".Scatter");
    }
    void AddRead(std::vector<EnhancedRenderGraph::RGPassUsage>& uses) const
    {
        uses.push_back({graphPackets, RHIResourceState::ShaderResource, RGAccessMode::Read});
        uses.push_back({graphRanges, RHIResourceState::ShaderResource, RGAccessMode::Read});
        uses.push_back({graphArguments, RHIResourceState::IndirectArgument, RGAccessMode::Read});
    }
    void Draw(RHIEncoder& encoder, size_t bucket, uint32_t selectorRoot) const
    {
        const auto range = buckets.at(bucket);
        for (uint32_t index = range.first; index < range.first + range.count; ++index)
        {
            const auto& slice = slices[index];
            encoder.SetConstantBuffer(RHIBindPoint::Graphics, selectorRoot, slice.selector);
            if (!encoder.DispatchMeshIndirect(arguments, slice.argumentOffset))
                throw std::runtime_error("METEOR packet mesh dispatch failed.");
        }
    }
};
} // namespace material_graph
