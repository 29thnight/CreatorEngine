#pragma once
#include "MaterialGraphIndirectBatch.h"
#include "MaterialGraphFrameGeometry.h"

namespace material_graph
{
// Prepared immutable root templates plus GPU-owned compact tile ranges. The
// producer is declared after HW visibility; consumers never insert passes.
struct SceneMaterialWork final
{
    using Bucket = IndirectMaterialBatch::Bucket;
    IRenderDeviceServices* device{};
    std::shared_ptr<const SceneFrameGeometry> geometry;
    std::array<std::shared_ptr<const LX::Runtime::ComputeGeneration>, 4> pipelines;
    std::vector<Bucket> buckets;
    RHIBufferSlice templates, records, constants;
    RHIBufferHandle work, arguments;
    RHIBindingTable outputs;
    uint32_t width{}, height{}, recordCount{}, commandCount{};
    uint64_t workBytes{};
    uint32_t commandBytes{};
    mutable RGHandle graphWork, graphArguments;
    mutable const EnhancedRenderGraph* graphOwner{};
    mutable uint64_t graphEpoch{};
    ~SceneMaterialWork() {
        if (device && work.IsValid()) device->ReleaseBuffer(work);
        if (device && arguments.IsValid()) device->ReleaseBuffer(arguments);
    }
    bool Upload(std::span<const uint32_t> words, std::span<const std::array<uint32_t,4>> commands,
        uint32_t argumentBytes, std::string& error) {
        commandCount = static_cast<uint32_t>(commands.size());
        commandBytes = argumentBytes;
        const auto upload = [&](const void* data, size_t bytes) {
            auto slice = device->AllocateUpload(bytes, 16);
            if (slice.IsWritable()) std::memcpy(slice.cpuAddress, data, bytes);
            return slice;
        };
        templates = upload(words.data(), words.size_bytes());
        records = upload(commands.data(), commands.size_bytes());
        const std::array<uint32_t,4> values{width,height,recordCount,commandCount};
        constants = device->UploadConstants(values.data(), sizeof(values));
        workBytes = (4ull + 3ull * recordCount + uint64_t(width) * height) * 4;
        if (!width || !height || !recordCount || workBytes / 4 > UINT32_MAX || !commandCount) {
            error = "METEOR material tile capacity is invalid."; return false;
        }
        RHIBufferDesc desc; desc.bytes = workBytes; desc.allowUnorderedAccess = true;
        desc.debugName = L"METEOR.Scene.Material.Tiles";
        if (!templates.IsValid() || !records.IsValid() || !constants.IsValid() || !device->CreateBuffer(desc, work, error)) return false;
        desc.bytes = argumentBytes; desc.allowIndirectArguments = true;
        desc.debugName = L"METEOR.Scene.Material.Arguments";
        if (!device->CreateBuffer(desc, arguments, error)) return false;
        const RHIBindingDesc views[]{RHIBindingDesc::UavBuffer(work, static_cast<uint32_t>(workBytes / 4), 4),
            RHIBindingDesc::UavBuffer(arguments, argumentBytes / 4, 4)};
        outputs = device->CreateBindings(views);
        return outputs.IsValid();
    }
    void Declare(EnhancedRenderGraph& graph, RGHandle visibility, std::shared_ptr<const SceneMaterialWork> owner) const {
        if (graphOwner == &graph && graphEpoch == graph.ResourceEpoch()) return;
        if (graphOwner) throw std::runtime_error("METEOR material work cannot cross frame graphs.");
        graphOwner = &graph; graphEpoch = graph.ResourceEpoch();
        graphWork = graph.ImportBuffer(work, RHIResourceState::Common, "METEOR.Scene.Material.Tiles");
        graphArguments = graph.ImportBuffer(arguments, RHIResourceState::Common, "METEOR.Scene.Material.Arguments");
        graph.RequireImportedFinalState(graphWork, RHIResourceState::Common);
        graph.RequireImportedFinalState(graphArguments, RHIResourceState::Common);
        const bool versioned = graph.GetSchedulingMode() == RGSchedulingMode::ExplicitVersioned;
        const bool explicitAccess = graph.GetSchedulingMode() != RGSchedulingMode::DeclarationOrder;
        constexpr const char* names[]{"METEOR.Scene.Material.Reset", "METEOR.Scene.Material.CountTiles",
            "METEOR.Scene.Material.BuildRanges", "METEOR.Scene.Material.ScatterTiles"};
        for (unsigned stage = 0; stage < 4; ++stage) {
            if (versioned) graphWork = stage == 0 ? graph.Write(graphWork) : graph.Modify(graphWork);
            std::vector<EnhancedRenderGraph::RGPassUsage> uses{{graphWork, RHIResourceState::UnorderedAccess,
                !explicitAccess ? RGAccessMode::LegacyState : stage == 0 ? RGAccessMode::Write : RGAccessMode::ReadWrite}};
            if (stage == 1 || stage == 3) {
                uses.push_back({visibility, RHIResourceState::ShaderResource,
                    explicitAccess ? RGAccessMode::Read : RGAccessMode::LegacyState});
                uses.push_back({geometry->GraphTable(graph), RHIResourceState::ShaderResource,
                    explicitAccess ? RGAccessMode::Read : RGAccessMode::LegacyState});
            }
            if (stage == 2) {
                if (versioned) graphArguments = graph.Write(graphArguments);
                uses.push_back({graphArguments, RHIResourceState::UnorderedAccess,
                    explicitAccess ? RGAccessMode::Write : RGAccessMode::LegacyState});
            }
            graph.AddPass(names[stage], uses, [owner,visibility,stage](const auto& execution) {
                auto& encoder = *execution.encoder;
                encoder.SetPipeline(RHIBindPoint::Compute, owner->pipelines[stage]->GetHandle());
                encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, owner->constants);
                if (stage == 1 || stage == 3) {
                    const auto desc = RHIBindingDesc::Srv2D(execution.ResolveHandle(visibility), RHIFormat::RG32Uint);
                    const auto view = owner->device->CreateBindings({&desc,1});
                    if (!view.IsValid()) throw std::runtime_error("METEOR material tile visibility binding failed.");
                    encoder.SetBindings(RHIBindPoint::Compute, 1, view);
                    encoder.SetRootBuffer(RHIBindPoint::Compute, 5, RHIBufferSlice::Whole(owner->geometry->buffer));
                }
                encoder.SetRootBuffer(RHIBindPoint::Compute, 2, owner->records);
                encoder.SetRootBuffer(RHIBindPoint::Compute, 3, owner->templates);
                encoder.SetBindings(RHIBindPoint::Compute, 4, owner->outputs);
                if (stage == 0) encoder.Dispatch((owner->recordCount + 63) / 64, 1, 1);
                else if (stage == 2) encoder.Dispatch(1,1,1);
                else encoder.Dispatch((owner->width + 7) / 8, (owner->height + 7) / 8, 1);
            });
        }
    }
    void AddRead(EnhancedRenderGraph& graph, std::vector<EnhancedRenderGraph::RGPassUsage>& uses) const {
        if (graphOwner != &graph || graphEpoch != graph.ResourceEpoch())
            throw std::runtime_error("METEOR material consumer requires explicit tile work producer.");
        const auto read = graph.GetSchedulingMode() == RGSchedulingMode::DeclarationOrder ? RGAccessMode::LegacyState : RGAccessMode::Read;
        uses.push_back({graphWork, RHIResourceState::ShaderResource, read});
        uses.push_back({graphArguments, RHIResourceState::IndirectArgument, read});
    }
    void Draw(RHIEncoder& encoder, size_t bucket) const {
        const auto& range = buckets.at(bucket);
        if (!encoder.DispatchMeshIndirectBatch(arguments, range.offset, range.count, range.roots))
            throw std::runtime_error("METEOR material tile submission failed.");
    }
};
}
