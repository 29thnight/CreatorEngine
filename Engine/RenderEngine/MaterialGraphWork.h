#pragma once
#include "MaterialGraphIndirectBatch.h"
#include "MaterialGraphFrameGeometry.h"
#include "MaterialGraphResidentBuffer.h"

namespace material_graph
{
// Visibility produces one packet for each unique (record, 8x8 tile) pair.
// Packets are bucketed on the GPU; no per-record indirect root template exists.
struct SceneMaterialWork final
{
    using Bucket = IndirectMaterialBatch::Bucket;
    IRenderDeviceServices* device{};
    std::shared_ptr<TransientBufferPool> pool;
    std::shared_ptr<const SceneFrameGeometry> geometry;
    std::shared_ptr<const ResidentBufferFrame> sceneRecordsOwner;
    std::array<std::shared_ptr<const LX::Runtime::ComputeGeneration>, 6> pipelines;
    std::vector<Bucket> buckets;
    RHIBufferSlice records;
    RHIBufferHandle work, ranges, arguments;
    uint32_t width{}, height{}, recordCount{}, commandCount{};
    uint64_t workBytes{};
    std::unique_ptr<PacketDispatchWork> packetWork;

    bool UploadPackets(RHIBufferSlice sceneRecords, uint32_t bucketCount, std::string& error)
    {
        const auto pixelCapacity = uint64_t(width) * height;
        if (!device || !width || !height || !recordCount || !bucketCount || buckets.size() != bucketCount ||
            pixelCapacity > UINT32_MAX || !sceneRecords.IsValid() || sceneRecords.size < uint64_t(recordCount) * 224 ||
            !sceneRecordsOwner || sceneRecordsOwner->Buffer() != sceneRecords.buffer ||
            std::ranges::any_of(pipelines, [](const auto& pipeline) { return !pipeline; }))
        {
            error = "METEOR material packet inputs are incomplete or exceed their address range.";
            return false;
        }
        records = sceneRecords;
        commandCount = bucketCount;
        const auto tileCount = ((uint64_t(width) + 7) / 8) * ((uint64_t(height) + 7) / 8);
        std::vector<uint32_t> capacities;
        capacities.reserve(bucketCount);
        uint64_t capacity{};
        for (const auto& bucket : buckets)
        {
            const auto bucketCapacity = std::min<uint64_t>(pixelCapacity,
                tileCount * (bucket.count ? bucket.count : recordCount));
            capacities.push_back(static_cast<uint32_t>(bucketCapacity));
            capacity = std::min(pixelCapacity, capacity + bucketCapacity);
        }
        // Few-material views reserve records*tiles rather than one packet per
        // viewport pixel; the pixel bound still caps heavily overlapping views.
        packetWork = std::make_unique<PacketDispatchWork>();
        if (!packetWork->Prepare(*device, pool, recordCount, static_cast<uint32_t>(capacity), capacities,
                                 width, height, "METEOR.Scene.Material", error))
            return false;
        work = packetWork->packets;
        ranges = packetWork->ranges;
        arguments = packetWork->arguments;
        workBytes = capacity * 8;
        return true;
    }
    uint32_t PacketCapacity() const { return packetWork ? packetWork->values.packetCapacity : 0; }
    void Declare(EnhancedRenderGraph& graph, RGHandle visibility, std::shared_ptr<const SceneMaterialWork> owner) const
    {
        packetWork->Declare(graph, owner, pipelines, "METEOR.Scene.Material", true,
            [&graph, owner, visibility](auto& uses) {
                uses.push_back({visibility, RHIResourceState::ShaderResource, RGAccessMode::Read});
                uses.push_back({owner->geometry->GraphTable(graph), RHIResourceState::ShaderResource,
                                RGAccessMode::Read});
                owner->sceneRecordsOwner->AddRead(graph, uses);
            },
            [owner, visibility](auto& encoder, const auto& execution, uint32_t stage) {
                if (stage == 1 || stage == 5)
                {
                    const auto desc = RHIBindingDesc::Srv2D(execution.ResolveHandle(visibility), RHIFormat::RG32Uint);
                    const auto view = owner->device->CreateBindings({&desc, 1});
                    if (!view.IsValid())
                        throw std::runtime_error("METEOR material packet visibility binding failed.");
                    encoder.SetBindings(RHIBindPoint::Compute, 1, view);
                    encoder.SetRootBuffer(RHIBindPoint::Compute, 2, owner->records);
                    encoder.SetRootBuffer(RHIBindPoint::Compute, 3, RHIBufferSlice::Whole(owner->geometry->buffer));
                }
                if (stage == 4)
                    encoder.SetRootBuffer(RHIBindPoint::Compute, 5, owner->packetWork->plans);
            });
    }
    std::array<RGHandle, 2> GraphInputs(const EnhancedRenderGraph& graph) const
    {
        if (!packetWork || packetWork->graphOwner != &graph || packetWork->graphEpoch != graph.ResourceEpoch())
            throw std::runtime_error("METEOR material consumer requires its explicit packet producer.");
        return {packetWork->graphPackets, packetWork->graphArguments};
    }
    void ValidateInputs(const EnhancedRenderGraph& graph, const std::array<RGHandle, 2>& inputs) const
    {
        const auto expected = GraphInputs(graph);
        for (size_t index = 0; index < inputs.size(); ++index)
            if (inputs[index].index != expected[index].index || inputs[index].version != expected[index].version ||
                inputs[index].kind != expected[index].kind || inputs[index].epoch != expected[index].epoch)
                throw std::runtime_error("METEOR material packet input is not its published resource version.");
    }
    void AddRead(EnhancedRenderGraph& graph, std::vector<EnhancedRenderGraph::RGPassUsage>& uses,
                 const std::array<RGHandle, 2>& inputs) const
    {
        ValidateInputs(graph, inputs);
        packetWork->AddRead(uses);
    }
    void Draw(RHIEncoder& encoder, size_t bucket, uint32_t selectorRoot) const
    {
        packetWork->Draw(encoder, bucket, selectorRoot);
    }
};
} // namespace material_graph
