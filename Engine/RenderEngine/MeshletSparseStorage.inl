#include "../Utility_Framework/Foundation/EngineOrderedMap.h"
// Included inside material_graph. One stable virtual buffer per sealed chunk.
struct MeshletSparseStorage
{
    IRenderDeviceServices& device;
    own::shared_owner<RHISharedSparsePagePool> pool;
    own::shared_owner<RHISharedSparsePagePool::Allocation> allocation;
    RHIBufferHandle buffer;
    uint64_t virtualBytes{}, revision{}, serial{}, committedSerial{};
    ce::engine_ordered_map<uint32_t, own::shared_owner<const ce::dynamic_array<std::byte>>> committedPackets;
    ce::dynamic_array<std::byte> committedMetadata;
    ce::engine_ordered_map<uint32_t, uint32_t> committedTiles;
    explicit MeshletSparseStorage(IRenderDeviceServices& owner)
        : device(owner), pool(RHISharedSparsePagePool::ForDevice(owner))
    {
    }
    uint64_t Revision() const { return allocation ? pool->Revision(allocation) : 0; }
    void SetEvictable(bool value)
    {
        if (allocation)
        {
            pool->SetEvictable(allocation, value);
        }
    }
    static ce::engine_ordered_map<uint32_t, uint32_t> WantedTiles(const SparseClusterPayload& payload, uint64_t metadataBytes)
    {
        ce::engine_ordered_map<uint32_t, uint32_t> wanted;
        const auto metadataTiles = (metadataBytes + RHISparseTileBytes - 1) / RHISparseTileBytes;
        for (uint32_t tile = 0; tile < metadataTiles; ++tile)
        {
            wanted[tile] = 2;
        }
        for (const auto& packet : payload.packets)
        {
            const auto end = (packet.offset + packet.bytes->size() + RHISparseTileBytes - 1) / RHISparseTileBytes;
            for (uint32_t tile = packet.offset / RHISparseTileBytes; tile < end; ++tile)
            {
                wanted[tile] = (std::max)(wanted[tile], packet.priority);
            }
        }
        return wanted;
    }
    template<typename Rebuild>
    bool PrepareResidentCut(SparseClusterPayload& payload, ce::dynamic_array<std::byte>& metadata,
                            own::shared_owner<const assets::cluster::ResidentSet>& cut,
                            std::span<const experiment::MeshClusterGroup> groups, uint64_t& admission, bool& limited,
                            Rebuild&& rebuild, std::string& error)
    {
        return pool->WithAdmissionBudget(
            allocation,
            [&](uint64_t available)
            {
                admission = available;
                if (!groups.empty() && groups.size() != cut->groups.size())
                {
                    error = "Sparse residency hierarchy group count mismatch";
                    return false;
                }
                const auto requested = cut;
                while (WantedTiles(payload, metadata.size()).size() > available)
                {
                    uint32_t nextLod = UINT32_MAX;
                    for (uint32_t group = 0; group < groups.size(); ++group)
                    {
                        if (cut->groups[group] && groups[group].parentGroup != UINT32_MAX)
                        {
                            nextLod = (std::min)(nextLod, groups[group].lodIndex);
                        }
                    }
                    if (nextLod == UINT32_MAX)
                    {
                        error = "Shared sparse budget cannot fit metadata and minimum root LOD";
                        return false;
                    }
                    cut = assets::cluster::CoarsenResidentCut(*requested, groups, nextLod + 1);
                    limited = true;
                    if (!rebuild(*cut))
                    {
                        return false;
                    }
                }
                return Prepare(payload, metadata.size(), cut->residentLimit, error);
            }, WantedTiles(payload, metadata.size()).size());
    }
    bool Prepare(const SparseClusterPayload& payload, uint64_t metadataBytes, uint64_t, std::string& error)
    {
        SetEvictable(false);
        const auto wanted = WantedTiles(payload, metadataBytes);
        if (!allocation)
        {
            allocation = pool->Create(payload.virtualBytes, error);
            if (!allocation)
            {
                return false;
            }
            buffer = allocation->buffer;
            virtualBytes = payload.virtualBytes;
        }
        if (virtualBytes != payload.virtualBytes)
        {
            error = "Sparse sealed virtual layout changed";
            return false;
        }
        const auto validate = [&]
        {
            const auto intact = [&](uint64_t offset, uint64_t size)
            {
                for (uint32_t tile = static_cast<uint32_t>(offset / RHISparseTileBytes);
                     tile < (offset + size + RHISparseTileBytes - 1) / RHISparseTileBytes; ++tile)
                {
                    const auto prior = committedTiles.find(tile);
                    const auto current = allocation->tiles.find(tile);
                    if (prior == committedTiles.end() || current == allocation->tiles.end() ||
                        prior->second != current->second)
                    {
                        return false;
                    }
                }
                return true;
            };
            ce::erase_if(committedPackets, [&](const auto& item) { return !intact(item.first, item.second->size()); });
            if (!intact(0, committedMetadata.size()))
            {
                committedMetadata.clear();
            }
        };
        // Check before remapping too: an evicted tile may return at the same
        // physical index after another asset has overwritten its contents.
        validate();
        if (!pool->Prepare(allocation, wanted, error))
        {
            return false;
        }
        validate();
        revision = Revision();
        ++serial;
        return true;
    }
    bool NeedsPacket(const SparseClusterPayload::Packet& packet) const
    {
        const auto found = committedPackets.find(packet.offset);
        return found == committedPackets.end() || found->second.operator->() != packet.bytes.operator->();
    }
    void Commit(uint64_t uploadSerial, uint64_t uploadRevision, const SparseClusterPayload& payload,
                const ce::dynamic_array<std::byte>& metadata)
    {
        // Only the latest completed upload can prove the current mapping contents.
        if (uploadSerial == serial && uploadSerial >= committedSerial && uploadRevision == Revision())
        {
            committedSerial = uploadSerial;
            committedMetadata = metadata;
            committedTiles = allocation->tiles;
            for (const auto& packet : payload.packets)
            {
                committedPackets[packet.offset] = packet.bytes;
            }
        }
    }
};
