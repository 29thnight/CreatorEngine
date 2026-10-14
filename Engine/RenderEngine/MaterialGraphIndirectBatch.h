#pragma once

#include "GpuGeometryVisibility.h"
#include <cstring>

namespace material_graph
{
// Immutable command templates belong to the sealed frame. Only GPU visibility
// writes dispatch dimensions. Raster consumers submit one command range per PSO.
struct IndirectMaterialBatch
{
    struct Bucket
    {
        std::size_t representative{};
        std::vector<std::uint32_t> roots;
        std::uint32_t offset{}, count{};
    };
    IRenderDeviceServices* device{};
    std::shared_ptr<const GpuGeometryVisibility::Frame> visibility;
    std::shared_ptr<const LX::Runtime::ComputeGeneration> pipeline;
    std::vector<Bucket> buckets;
    RHIBufferSlice templates, commands, constants;
    RHIBufferHandle output;
    RHIBindingTable outputTable;
    mutable RGHandle graphOutput;
    mutable const EnhancedRenderGraph* graphOwner{};
    mutable std::uint64_t graphEpoch{};
    std::uint32_t commandCount{};
    std::string name{"METEOR.Scene.Commands"};
    bool indexedReplay{};
    bool indexedVisibility{};

    ~IndirectMaterialBatch() { if (device && output.IsValid()) device->ReleaseBuffer(output); }

    bool Upload(std::span<const std::uint32_t> words, std::span<const std::array<std::uint32_t, 4>> records,
        std::uint32_t outputBytes, std::string& error)
    {
        commandCount = static_cast<std::uint32_t>(records.size());
        if (!commandCount) return true;
        const auto upload = [&](const void* data, std::size_t bytes) {
            auto slice = device->AllocateUpload(bytes, 16);
            if (slice.IsWritable()) std::memcpy(slice.cpuAddress, data, bytes);
            return slice;
        };
        templates = upload(words.data(), words.size_bytes());
        commands = upload(records.data(), records.size_bytes());
        const std::array<std::uint32_t, 4> values{commandCount, indexedVisibility ? 2u : indexedReplay ? 1u : 0u, 0, 0};
        constants = device->UploadConstants(values.data(), sizeof(values));
        RHIBufferDesc desc;
        desc.bytes = outputBytes; desc.allowUnorderedAccess = true; desc.allowIndirectArguments = true;
        const std::wstring debugName(name.begin(), name.end());
        desc.debugName = debugName.c_str();
        if (!templates.IsValid() || !commands.IsValid() || !constants.IsValid() ||
            !device->CreateBuffer(desc, output, error)) return false;
        const auto binding = RHIBindingDesc::UavBuffer(output, outputBytes / 4, 4);
        outputTable = device->CreateBindings({&binding, 1});
        return outputTable.IsValid();
    }

    void Declare(EnhancedRenderGraph& graph, std::shared_ptr<const IndirectMaterialBatch> owner) const
    {
        if (!commandCount) return;
        if (graphOwner == &graph && graphEpoch == graph.ResourceEpoch()) return;
        if (graphOwner) throw std::runtime_error("METEOR indirect batch cannot cross graph recordings.");
        graphOwner = &graph; graphEpoch = graph.ResourceEpoch();
        std::vector<EnhancedRenderGraph::RGPassUsage> uses;
        if (visibility) visibility->AddCommandInputRead(graph, uses);
        graphOutput = graph.ImportBuffer(output, RHIResourceState::Common, name.c_str());
        graph.RequireImportedFinalState(graphOutput, RHIResourceState::Common);
        graphOutput = graph.Write(graphOutput);
        uses.push_back({graphOutput, RHIResourceState::UnorderedAccess, RGAccessMode::Write});
        graph.AddPass((name + ".Build").c_str(), uses, [owner](const auto& execution) {
            auto& encoder = *execution.encoder;
            encoder.SetPipeline(RHIBindPoint::Compute, owner->pipeline->GetHandle());
            encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, owner->constants);
            encoder.SetRootBuffer(RHIBindPoint::Compute, 1, owner->commands);
            encoder.SetRootBuffer(RHIBindPoint::Compute, 2, owner->templates);
            encoder.SetRootBuffer(RHIBindPoint::Compute, 3, owner->visibility ? RHIBufferSlice::Whole(owner->visibility->Arguments()) : owner->templates);
            encoder.SetBindings(RHIBindPoint::Compute, 4, owner->outputTable);
            encoder.Dispatch((owner->commandCount + 63) / 64, 1, 1);
        });
    }
    void AddRead(EnhancedRenderGraph& graph, std::vector<EnhancedRenderGraph::RGPassUsage>& uses) const
    {
        if (commandCount && (graphOwner != &graph || graphEpoch != graph.ResourceEpoch()))
            throw std::runtime_error("METEOR command consumer requires an explicitly declared producer.");
        if (commandCount) uses.push_back({graphOutput, RHIResourceState::IndirectArgument, RGAccessMode::Read});
    }
    void Draw(RHIEncoder& encoder, std::size_t bucket) const
    {
        const auto& range = buckets.at(bucket);
        if (!(indexedReplay ? encoder.DrawIndexedIndirectBatch(output, range.offset, range.count, range.roots)
            : encoder.DispatchMeshIndirectBatch(output, range.offset, range.count, range.roots)))
            throw std::runtime_error("METEOR material bucket indirect submission failed.");
    }
};
}
