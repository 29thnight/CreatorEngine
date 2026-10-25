#pragma once
#if defined(CE_CONTRACT_STANDALONE)
#include "../../Tools/build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif
#include "MaterialGraphGeometryContract.h"
#include "MaterialGraphVisibilityContract.h"
#include "MaterialGraphMeshSurface.h"
#include "MaterialGraphTransientBufferPool.h"
#include "MaterialGraphResidentBuffer.h"
#include <stddef.h>
#include <stdint.h>
#include "../Utility_Framework/Foundation/StandardLibrary.h"

namespace material_graph
{
// Immutable frame-local addressing. Index zero is background. A nonzero index
// resolves to the exact posed stream and cooked topology retained by this frame.
// Visibility = {frame primitive address, standard-Z float bits}.
// Record reserved[1..2] is its half-open primitive-slot range; no pointer IDs.
// Original primitive/cluster/LOD/vertex IDs are read with LXLoadGeometryPrimitive.
struct SceneFrameGeometry final
{
    struct alignas(16) Record
    {
        SceneGeometryIdentity identity;
        uint32_t chunkIndex{}, owner{}, meshletCount{}, vertexCount{};
        // Object-to-world columns of the row-vector transform: world = dot(float4(p,1), column).
        math::vector4 worldColumns[3];
        void SetWorld(const math::matrix4x4& world)
        {
            for (unsigned column = 0; column < 3; ++column)
                worldColumns[column] =
                    math::vector4(world.m[0][column], world.m[1][column], world.m[2][column], world.m[3][column]);
        }
    };
    static_assert(sizeof(Record) == 128);
    static_assert(offsetof(Record, owner) == 68);
    IRenderDeviceServices* device{};
    own::shared_owner<TransientBufferPool> pool;
    std::vector<Record> records{Record{}};
    std::vector<own::shared_owner<const MeshSurfaceBatch>> geometry{nullptr};
    uint32_t nextPrimitiveAddress{1};
    uint32_t FindRecord(uint32_t address) const
    {
        if (!address || address == UINT32_MAX)
            return 0;
        size_t first = 1, last = records.size();
        while (first < last)
        {
            const auto middle = first + (last - first) / 2;
            const auto& range = records[middle].identity.reserved;
            if (address < range[1])
                last = middle;
            else if (address >= range[2])
                first = middle + 1;
            else
                return (address - range[1]) % 128 < 126 ? static_cast<uint32_t>(middle) : 0;
        }
        return 0;
    }
    RHIBufferSlice staging;
    RHIBufferHandle buffer;
    own::shared_owner<const ResidentBufferFrame> resident;
    mutable RGHandle graphTable;
    mutable const EnhancedRenderGraph* graphOwner{};
    mutable uint64_t graphEpoch{};
    ~SceneFrameGeometry()
    {
        if (device && !resident)
            ReleaseSceneBuffer(*device, pool, buffer);
    }

    bool Upload(std::string& error, ResidentBufferCache* cache = nullptr, ResidentBufferCache::Key key = {})
    {
        if (records.size() != geometry.size())
        {
            error = "METEOR geometry table ownership mismatch.";
            return false;
        }
        const auto bytes = records.size() * sizeof(Record);
        if (cache)
        {
            if (!cache->Prepare(key, std::as_bytes(std::span{records}), sizeof(Record),
                                 "METEOR.Scene.Geometry.Table", resident, error))
                return false;
            buffer = resident->Buffer();
            return true;
        }
        staging = device->AllocateUpload({bytes, RHIUploadUsage::BufferCopy, 16});
        if (!staging.IsWritable())
        {
            error = "METEOR frame geometry table staging failed.";
            return false;
        }
        std::memcpy(staging.cpuAddress, records.data(), bytes);
        RHIBufferDesc desc;
        desc.bytes = bytes;
        desc.debugName = L"METEOR.Scene.Geometry.Table";
        return CreateSceneBuffer(*device, pool.operator->(), desc, buffer, error);
    }
    void Declare(EnhancedRenderGraph& graph, own::shared_owner<const SceneFrameGeometry> owner) const
    {
        if (graphOwner == graph.DeclarationGraph() && graphEpoch == graph.DeclarationEpoch())
        {
            return;
        }
        if (graphOwner)
            throw std::runtime_error("METEOR geometry table cannot cross frame graphs.");
        graphOwner = graph.DeclarationGraph();
        graphEpoch = graph.DeclarationEpoch();
        if (resident)
        {
            resident->Declare(graph, resident);
            graphTable = resident->GraphBuffer(graph);
            return;
        }
        graphTable = graph.ImportBuffer(buffer, RHIResourceState::Common, "METEOR.Scene.Geometry.Table");
        if (graph.GetSchedulingMode() != RGSchedulingMode::ExplicitVersioned)
            throw std::runtime_error("METEOR geometry table requires an ExplicitVersioned graph.");
        graphTable = graph.Write(graphTable);
        graph.AddPass("METEOR.Scene.Geometry.Table.Upload",
                      {{graphTable, RHIResourceState::CopyDest, RGAccessMode::Write}}, [owner](const auto& execution) {
                          auto destination = RHIBufferSlice::Whole(owner->buffer);
                          destination.size = owner->staging.size;
                          if (!execution.encoder->CopyBuffer(destination, owner->staging))
                              throw std::runtime_error("METEOR geometry table copy failed.");
                      });
    }
    RGHandle GraphTable(const EnhancedRenderGraph& graph) const
    {
        if (graphOwner != graph.DeclarationGraph() || graphEpoch != graph.DeclarationEpoch())
        {
            throw std::runtime_error("METEOR geometry table requires its explicit producer.");
        }
        return graphTable;
    }
};
} // namespace material_graph
