#pragma once

#include "MaterialGraphGeometryContract.h"
#include "MaterialGraphMeshSurface.h"
#include "MaterialGraphRenderBindings.h"
#include "MaterialGraphResidentBuffer.h"

namespace material_graph
{
// Frame record IDs share the visibility geometry table's 1-based namespace.
// Resource indices are recording-local descriptor indices, never GPU addresses.
struct SceneGpuData final
{
    static constexpr uint32_t kGeometryPageSize = 64;
    struct alignas(16) Record
    {
        SceneGeometryIdentity identity;
        std::array<math::vector4, 3> worldColumns, previousWorldColumns;
        // current vertex, previous vertex, topology page slot, vertex count
        std::array<uint32_t, 4> geometry{};
        // uniform byte offset, descriptor byte offset, owner, coverage flags
        std::array<uint32_t, 4> material{};
        // cutoff float bits, visibility record, chunk, source meshlet capacity
        std::array<uint32_t, 4> surface{};
        // temporal valid, distinct previous pose, material bucket, reserved
        std::array<uint32_t, 4> history{0, 0, UINT32_MAX, 0};
    };
    static_assert(sizeof(Record) == 224);
    static_assert(offsetof(Record, geometry) == 160);
    static_assert(offsetof(Record, material) == 176);
    static_assert(offsetof(Record, surface) == 192);
    static_assert(offsetof(Record, history) == 208);

    struct Page
    {
        struct Vertex
        {
            RHIBufferHandle buffer;
            uint32_t elements{};
        };
        std::vector<Vertex> vertices;
        std::vector<RHIBufferSlice> topology;
        std::vector<std::shared_ptr<const MeshSurfaceBatch>> owners;
        RHIBindingTable vertexTable, topologyTable;
    };
    std::vector<Record> records{Record{}};
    std::vector<uint32_t> recordPages{UINT32_MAX};
    std::vector<Page> pages;
    // Offset zero is a valid all-zero default, including material-less passes.
    std::vector<std::byte> materialBytes = std::vector<std::byte>(512);
    std::map<const RenderBindings*, std::pair<uint32_t, uint32_t>> materialOffsets;
    std::shared_ptr<const ResidentBufferFrame> recordBuffer, materialBuffer;
    RHIBufferSlice viewConstants;
    std::array<RHIBufferSlice, 3> shadowConstants;

    bool AppendMaterial(const RenderBindings& bindings, std::pair<uint32_t, uint32_t>& offsets,
                        std::string& error)
    {
        if (const auto found = materialOffsets.find(&bindings); found != materialOffsets.end())
        {
            offsets = found->second;
            return true;
        }
        const auto& uniforms = bindings.resources.uniforms;
        const auto paddedUniforms = (uniforms.size() + 15u) & ~size_t{15u};
        constexpr size_t descriptorBytes = 128 * sizeof(uint32_t);
        if (paddedUniforms > UINT32_MAX - descriptorBytes ||
            materialBytes.size() > UINT32_MAX - paddedUniforms - descriptorBytes)
        {
            error = "METEOR scene material table exceeds its 32-bit byte addressing contract.";
            return false;
        }
        offsets.first = static_cast<uint32_t>(materialBytes.size());
        offsets.second = offsets.first + static_cast<uint32_t>(paddedUniforms);
        materialBytes.resize(materialBytes.size() + paddedUniforms + descriptorBytes);
        if (!uniforms.empty())
            std::memcpy(materialBytes.data() + offsets.first, uniforms.data(), uniforms.size());
        std::memcpy(materialBytes.data() + offsets.second, bindings.descriptorIndexValues.data(), descriptorBytes);
        materialOffsets.emplace(&bindings, offsets);
        return true;
    }

    bool AssignGeometry(uint32_t record, const std::shared_ptr<const MeshSurfaceBatch>& current,
                        const std::shared_ptr<const MeshSurfaceBatch>& previous, std::string& error)
    {
        if (!record || record >= records.size() || recordPages.size() != records.size() || !current || !previous ||
            !current->Buffer().IsValid() || !previous->Buffer().IsValid())
        {
            error = "METEOR scene packet geometry has no exact retained record/stream.";
            return false;
        }
        const auto meshlets = current->Meshlets();
        if (!meshlets.IsValid() || meshlets.offset % 4 || meshlets.size % 4 ||
            meshlets.offset / 4 > UINT32_MAX || meshlets.size / 4 > UINT32_MAX)
        {
            error = "METEOR scene packet topology exceeds raw descriptor addressing.";
            return false;
        }
        const auto findVertex = [](const Page& page, const MeshSurfaceBatch& batch) {
            for (uint32_t i = 0; i < page.vertices.size(); ++i)
                if (page.vertices[i].buffer == batch.Buffer() && page.vertices[i].elements == batch.GeometryViewElements())
                    return i;
            return UINT32_MAX;
        };
        const auto findTopology = [&](const Page& page) {
            for (uint32_t i = 0; i < page.topology.size(); ++i)
            {
                const auto& value = page.topology[i];
                if (value.buffer == meshlets.buffer && value.offset == meshlets.offset && value.size == meshlets.size)
                    return i;
            }
            return UINT32_MAX;
        };
        if (pages.empty()) pages.emplace_back();
        const bool sameStream = current->Buffer() == previous->Buffer() &&
                                current->GeometryViewElements() == previous->GeometryViewElements();
        const auto& last = pages.back();
        const auto newVertices = uint32_t(findVertex(last, *current) == UINT32_MAX) +
                                 uint32_t(!sameStream && findVertex(last, *previous) == UINT32_MAX);
        if (last.vertices.size() + newVertices > kGeometryPageSize ||
            last.topology.size() + uint32_t(findTopology(last) == UINT32_MAX) > kGeometryPageSize)
            pages.emplace_back();
        auto& page = pages.back();
        const auto appendVertex = [&](const std::shared_ptr<const MeshSurfaceBatch>& batch) {
            auto slot = findVertex(page, *batch);
            if (slot == UINT32_MAX)
            {
                slot = static_cast<uint32_t>(page.vertices.size());
                page.vertices.push_back({batch->Buffer(), batch->GeometryViewElements()});
                page.owners.push_back(batch);
            }
            return slot;
        };
        auto& value = records[record];
        value.geometry[0] = appendVertex(current);
        value.geometry[1] = appendVertex(previous);
        auto topologySlot = findTopology(page);
        if (topologySlot == UINT32_MAX)
        {
            topologySlot = static_cast<uint32_t>(page.topology.size());
            page.topology.push_back(meshlets);
            page.owners.push_back(current);
        }
        value.geometry[2] = topologySlot;
        value.geometry[3] = current->Count();
        recordPages[record] = static_cast<uint32_t>(pages.size() - 1);
        return true;
    }

    bool PrepareBindings(IRenderDeviceServices& device, std::string& error)
    {
        for (auto& page : pages)
        {
            std::array<RHIBindingDesc, kGeometryPageSize> vertices, topology;
            for (uint32_t i = 0; i < kGeometryPageSize; ++i)
            {
                // Fully bound arrays avoid requiring partially-bound descriptors.
                // Only record-owned indices can be reached by a packet shader.
                const auto& vertex = page.vertices[i < page.vertices.size() ? i : 0];
                const auto& meshlets = page.topology[i < page.topology.size() ? i : 0];
                vertices[i] = RHIBindingDesc::SrvBuffer(vertex.buffer, vertex.elements, 16);
                topology[i] = RHIBindingDesc::SrvBuffer(meshlets.buffer, static_cast<uint32_t>(meshlets.size / 4),
                                                       0, static_cast<uint32_t>(meshlets.offset / 4));
            }
            page.vertexTable = device.CreateBindings(vertices);
            page.topologyTable = device.CreateBindings(topology);
            if (!page.vertexTable.IsValid() || !page.topologyTable.IsValid())
            {
                error = "METEOR scene packet geometry descriptor page allocation failed.";
                return false;
            }
        }
        return true;
    }

    bool Upload(ResidentBufferCache& cache, uint64_t sceneEpoch, uint64_t viewId, std::string& error)
    {
        return cache.Prepare({sceneEpoch, viewId, 1}, std::as_bytes(std::span{records}), sizeof(Record),
                              "METEOR.Scene.Records", recordBuffer, error) &&
               cache.Prepare({sceneEpoch, viewId, 2}, materialBytes, 16, "METEOR.Scene.Materials", materialBuffer, error);
    }
    bool CreatePacketBindings(IRenderDeviceServices& device, RHIBufferHandle packets, uint32_t packetCapacity,
                              RHIBufferHandle ranges, uint32_t bucketCount, RHIBindingTable& result,
                              std::string& error) const
    {
        const RHIBindingDesc descriptions[]{
            RHIBindingDesc::SrvBuffer(recordBuffer->Buffer(), static_cast<uint32_t>(records.size()), sizeof(Record)),
            RHIBindingDesc::SrvBuffer(packets, packetCapacity, 8),
            RHIBindingDesc::SrvBuffer(materialBuffer->Buffer(), static_cast<uint32_t>(materialBuffer->Size() / 4), 0),
            RHIBindingDesc::SrvBuffer(ranges, bucketCount, 16)};
        result = device.CreateBindings(descriptions);
        if (result.IsValid()) return true;
        error = "METEOR packet scene resource table allocation failed.";
        return false;
    }
    void Declare(EnhancedRenderGraph& graph) const
    {
        recordBuffer->Declare(graph, recordBuffer);
        materialBuffer->Declare(graph, materialBuffer);
    }
    void AddRead(const EnhancedRenderGraph& graph, std::vector<EnhancedRenderGraph::RGPassUsage>& uses) const
    {
        recordBuffer->AddRead(graph, uses);
        materialBuffer->AddRead(graph, uses);
        for (const auto& page : pages)
            for (const auto& owner : page.owners)
            {
                uses.push_back({owner->GraphOutput(graph), RHIResourceState::ShaderResource, RGAccessMode::Read});
                uses.push_back({owner->GraphMeshlets(graph), RHIResourceState::ShaderResource, RGAccessMode::Read});
            }
    }
};
} // namespace material_graph
