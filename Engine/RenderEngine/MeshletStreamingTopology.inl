#include "../Utility_Framework/Foundation/EngineOrderedMap.h"
#include "../Utility_Framework/Foundation/EngineOrderedSet.h"
// Included inside material_graph after RHIModelMeshletUpload.h.
struct SparseClusterPayload
{
    struct Packet
    {
        uint32_t offset{}, priority{};
        own::shared_owner<const ce::dynamic_array<std::byte>> bytes;
    };
    uint64_t virtualBytes{};
    ce::dynamic_array<Packet> packets;
};

inline void PackStreamedMeshletGpuPayload(RHIModelMeshletUpload& result, const MeshSurfaceInput& input)
{
    using assets::quantized::Require;
    const auto& payload = input.Meshlets();
    Require(input.StreamedTopology() && !payload.descriptors.empty() && payload.vertexRemap.empty() &&
                payload.triangleIndices.empty() && payload.primitiveRemap.empty(),
            "Thin GPU topology contract");
    RHIMeshletBufferHeader header;
    header.schemaVersion = 6;
    header.profileVersion = payload.settings.profileVersion;
    header.meshletCount = static_cast<uint32_t>(payload.descriptors.size());
    header.descriptorOffset = sizeof(header);
    header.descriptorStride = sizeof(experiment::MeshletDescriptor);
    header.vertexCount = input.Count();
    header.vertexStride = input.Geometry().vertexStride;
    header.vertexAttributeMask = input.Geometry().vertexAttributeMask;
    header.sourceLodIndex = input.Geometry().sourceLodIndex;
    header.sourceVertexCount = input.SourceVertexCount();
    header.sourcePrimitiveCount = input.SourcePrimitiveCount();
    uint64_t size = sizeof(header) + payload.descriptors.size() * sizeof(experiment::MeshletDescriptor);
    if (input.Hierarchy().groupCount)
    {
        header.hierarchyOffset = static_cast<uint32_t>(size);
        size += input.Hierarchy().GpuBytes();
    }
    Require(size <= assets::quantized::MaximumBytes, "Thin GPU metadata budget");
    result.bytes.resize((size + 15) & ~uint64_t(15));
    std::memcpy(result.bytes.data(), &header, sizeof(header));
    std::memcpy(result.bytes.data() + header.descriptorOffset, payload.descriptors.data(),
                payload.descriptors.size() * sizeof(experiment::MeshletDescriptor));
    if (header.hierarchyOffset)
    {
        const uint32_t dag[4]{input.Hierarchy().groupCount, input.Hierarchy().rootGroup, 0, 0};
        std::memcpy(result.bytes.data() + header.hierarchyOffset, dag, sizeof(dag));
        std::memcpy(result.bytes.data() + header.hierarchyOffset + 16, input.Hierarchy().groups.data(),
                    input.Hierarchy().groups.size() * sizeof(experiment::MeshClusterGroup));
    }
    result.meshletCount = header.meshletCount;
    result.profileVersion = header.profileVersion;
}

inline void AppendClusterTopology(RHIModelMeshletUpload& upload, const MeshSurfaceInput& input,
                                  const assets::cluster::ResidentSet& resident, SparseClusterPayload* sparse = nullptr)
{
    using assets::quantized::Put;
    using assets::quantized::Require;
    auto& bytes = upload.bytes;
    RHIMeshletBufferHeader header;
    Require(bytes.size() >= sizeof(header) && input.ClusterArchive(), "Cluster topology contract");
    std::memcpy(&header, bytes.data(), sizeof(header));
    const auto reserve = [&](uint64_t size)
    {
        const auto offset = (bytes.size() + 15u) & ~uint64_t(15u);
        Require(offset + size <= assets::quantized::MaximumBytes, "Resident cluster byte budget");
        bytes.resize(offset + size);
        return static_cast<uint32_t>(offset);
    };
    header.clusterVertexMapOffset = reserve(uint64_t(input.Count()) * 8);
    header.clusterResidentOffset = reserve(uint64_t(input.Meshlets().descriptors.size()) * 4);
    header.clusterGroupResidentOffset = reserve(uint64_t(resident.groups.size()) * 4);
    if (input.StreamedTopology())
    {
        header.clusterReserved[0] = reserve(uint64_t(input.Meshlets().descriptors.size()) * 4);
    }
    header.clusterPositionError = input.ClusterArchive()->grid.positionError;
    // Metadata occupies pinned tiles. Packet addresses depend on the sealed
    // topology alone, so residency changes never relocate a surviving packet.
    uint64_t packetEnd = (bytes.size() + RHISparseTileBytes - 1) & ~(RHISparseTileBytes - 1);
    ce::engine_ordered_map<uint32_t, uint32_t> packetOffsets;
    if (sparse)
    {
        for (const auto& descriptor : input.Meshlets().descriptors)
        {
            if (!packetOffsets.contains(descriptor.clusterId))
            {
                const auto size = input.ClusterArchive()->PacketBytes(descriptor.clusterId);
                Require(packetEnd + size <= UINT32_MAX, "Sparse cluster virtual address budget");
                packetOffsets.emplace(descriptor.clusterId, static_cast<uint32_t>(packetEnd));
                packetEnd = (packetEnd + size + 15) & ~uint64_t(15);
            }
        }
        sparse->virtualBytes = (packetEnd + RHISparseTileBytes - 1) & ~(RHISparseTileBytes - 1);
    }
    ce::engine_ordered_set<uint32_t> emitted;
    for (uint32_t g = 0; g < resident.groups.size(); ++g)
    {
        Put(bytes, header.clusterGroupResidentOffset + g * 4, resident.groups[g]);
    }
    for (uint32_t index = 0; index < input.Meshlets().descriptors.size(); ++index)
    {
        const auto& descriptor = input.Meshlets().descriptors[index];
        Require(descriptor.clusterId < resident.packets.size(), "Cluster archive ordinal");
        const auto& packet = resident.packets[descriptor.clusterId];
        if (!packet)
        {
            continue;
        }
        Put(bytes, header.clusterResidentOffset + index * 4, 1);
        const auto offset = sparse ? packetOffsets.at(descriptor.clusterId) : reserve(packet->size());
        if (sparse)
        {
            if (emitted.insert(descriptor.clusterId).second)
            {
                const bool root = !input.Hierarchy().groupCount ||
                                  input.Hierarchy().groups[descriptor.reserved].parentGroup == UINT32_MAX;
                sparse->packets.push_back({offset, root ? 1u : 0u, packet});
            }
        }
        else
        {
            std::memcpy(bytes.data() + offset, packet->data(), packet->size());
        }
        if (input.StreamedTopology())
        {
            Put(bytes, header.clusterReserved[0] + index * 4, offset);
        }
        for (uint32_t corner = 0; corner < descriptor.vertexCount; ++corner)
        {
            const auto vertex = input.StreamedTopology()
                                    ? descriptor.vertexOffset + corner
                                    : input.Meshlets().vertexRemap[descriptor.vertexOffset + corner];
            Require(vertex < input.Count(), "Resident vertex map");
            const auto map = header.clusterVertexMapOffset + vertex * 8;
            if (assets::quantized::Word(bytes, map) == 0)
            {
                Put(bytes, map, offset + (input.StreamedTopology() ? 32u : 0u));
                Put(bytes, map + 4, corner);
            }
        }
    }
    if (header.hierarchyOffset)
    {
        for (uint32_t group = 0; group < input.Hierarchy().groupCount; ++group)
        {
            const auto address = header.hierarchyOffset + 16 + group * 64;
            const auto bound = std::bit_cast<float>(assets::quantized::Word(bytes, address + 44));
            const auto error = std::bit_cast<float>(assets::quantized::Word(bytes, address + 28));
            const auto margin = header.clusterPositionError * 1.732051f;
            Put(bytes, address + 44, std::bit_cast<uint32_t>(std::nextafter(bound + margin, INFINITY)));
            const bool certifiedSkin = (assets::quantized::Word(bytes, address + 60) & 1u) != 0;
            Put(bytes, address + 28,
                std::bit_cast<uint32_t>(std::nextafter(error + (certifiedSkin ? 2.f : 1.f) * margin, INFINITY)));
            if (certifiedSkin)
            {
                const auto weighted = std::bit_cast<float>(assets::quantized::Word(bytes, address + 52));
                Put(bytes, address + 52, std::bit_cast<uint32_t>(std::nextafter(weighted + 2.f * margin, INFINITY)));
            }
        }
    }
    bytes.resize((bytes.size() + 15u) & ~size_t(15u));
    std::memcpy(bytes.data(), &header, sizeof(header));
}
