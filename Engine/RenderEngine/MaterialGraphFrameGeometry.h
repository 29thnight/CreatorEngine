#pragma once
#include "MaterialGraphGeometryContract.h"
#include "MaterialGraphVisibilityContract.h"
#include "MaterialGraphMeshSurface.h"
#include <cstring>
#include <stdexcept>

namespace material_graph
{
// Immutable frame-local addressing. Index zero is background. A nonzero index
// resolves to the exact posed stream and cooked topology retained by this frame.
// Visibility = {frame primitive address, standard-Z float bits}.
// Record reserved[1..2] is its half-open primitive-slot range; no pointer IDs.
// Original primitive/cluster/LOD/vertex IDs are read with LXLoadGeometryPrimitive.
struct SceneFrameGeometry final
{
    struct alignas(16) Record {
        SceneGeometryIdentity identity;
        uint32_t chunkIndex{}, owner{}, meshletCount{}, vertexCount{};
    };
    static_assert(sizeof(Record) == 80);
    static_assert(offsetof(Record, owner) == 68);
    IRenderDeviceServices* device{};
    std::vector<Record> records{Record{}};
    std::vector<std::shared_ptr<const MeshSurfaceBatch>> geometry{nullptr};
    uint32_t nextPrimitiveAddress{1};
    uint32_t FindRecord(uint32_t address) const {
        if (!address || address == UINT32_MAX) return 0;
        size_t first = 1, last = records.size();
        while (first < last) {
            const auto middle = first + (last - first) / 2;
            const auto& range = records[middle].identity.reserved;
            if (address < range[1]) last = middle;
            else if (address >= range[2]) first = middle + 1;
            else return (address - range[1]) % 128 < 126 ? static_cast<uint32_t>(middle) : 0;
        }
        return 0;
    }
    RHIBufferSlice staging;
    RHIBufferHandle buffer;
    mutable RGHandle graphTable;
    mutable const EnhancedRenderGraph* graphOwner{};
    mutable uint64_t graphEpoch{};
    ~SceneFrameGeometry() { if (device && buffer.IsValid()) device->ReleaseBuffer(buffer); }

    bool Upload(std::string& error) {
        if (records.size() != geometry.size()) { error = "METEOR geometry table ownership mismatch."; return false; }
        const auto bytes = records.size() * sizeof(Record);
        staging = device->AllocateUpload({bytes, RHIUploadUsage::BufferCopy, 16});
        if (!staging.IsWritable()) { error = "METEOR frame geometry table staging failed."; return false; }
        std::memcpy(staging.cpuAddress, records.data(), bytes);
        RHIBufferDesc desc; desc.bytes = bytes; desc.debugName = L"METEOR.Scene.Geometry.Table";
        return device->CreateBuffer(desc, buffer, error);
    }
    void Declare(EnhancedRenderGraph& graph, std::shared_ptr<const SceneFrameGeometry> owner) const {
        if (graphOwner == &graph && graphEpoch == graph.ResourceEpoch()) return;
        if (graphOwner) throw std::runtime_error("METEOR geometry table cannot cross frame graphs.");
        graphOwner = &graph; graphEpoch = graph.ResourceEpoch();
        graphTable = graph.ImportBuffer(buffer, RHIResourceState::Common, "METEOR.Scene.Geometry.Table");
        const bool explicitAccess = graph.GetSchedulingMode() != RGSchedulingMode::DeclarationOrder;
        if (graph.GetSchedulingMode() == RGSchedulingMode::ExplicitVersioned) graphTable = graph.Write(graphTable);
        graph.AddPass("METEOR.Scene.Geometry.Table.Upload",
            {{graphTable, RHIResourceState::CopyDest, explicitAccess ? RGAccessMode::Write : RGAccessMode::LegacyState}},
            [owner](const auto& execution) {
                auto destination = RHIBufferSlice::Whole(owner->buffer);
                destination.size = owner->staging.size;
                if (!execution.encoder->CopyBuffer(destination, owner->staging))
                    throw std::runtime_error("METEOR geometry table copy failed.");
            });
    }
    RGHandle GraphTable(const EnhancedRenderGraph& graph) const {
        if (graphOwner != &graph || graphEpoch != graph.ResourceEpoch())
            throw std::runtime_error("METEOR geometry table requires its explicit producer.");
        return graphTable;
    }
};
}
