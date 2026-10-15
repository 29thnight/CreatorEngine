#pragma once

#include "IRenderDeviceServices.h"

#include <cstring>
#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

struct RHIModelMeshletUpload
{
    std::vector<std::byte> bytes;
    uint32_t meshletCount{};
    uint32_t profileVersion{};
};

// Canonical METEOR GPU serialization. Cluster/primitive IDs come only from cooked
// payloads. Chunk-local vertex remaps are paired with original geometry IDs.
inline bool PackMeteorMeshletGpuPayload(const experiment::MeshletPayload& cooked,
    uint64_t vertexCount, uint32_t vertexStride, uint32_t vertexAttributeMask,
    uint32_t sourceLodIndex, uint32_t sourcePrimitiveCount, uint32_t sourceVertexCount,
    std::span<const uint32_t> sourceVertices, RHIModelMeshletUpload& result, std::string& error,
    const experiment::MeshClusterHierarchy* hierarchy = nullptr)
{
    result = {};
    const auto* payload = &cooked;
    if (!payload->HasMeshlets() || payload->settings.profileVersion != experiment::kMeshletProfileVersion ||
        payload->settings.builderVersion != experiment::kMeshletBuilderVersion ||
        payload->settings.meshoptimizerVersion != experiment::kMeshletMeshoptimizerVersion ||
        payload->lod0.firstMeshlet != 0 || payload->lod0.meshletCount != payload->descriptors.size())
    {
        error = "METEOR requires current cooked meshlets; force reimport/recook.";
        return false;
    }
    if (hierarchy && !hierarchy->groupCount && (!hierarchy->groups.empty() || hierarchy->rootGroup!=UINT32_MAX)) {
        error="METEOR empty spatial DAG has stale metadata."; return false;
    }
    if (hierarchy && hierarchy->groupCount) {
        if (hierarchy->groupCount != hierarchy->groups.size() || hierarchy->groupCount > experiment::kMeshClusterMaxGroups ||
            hierarchy->rootGroup + 1 != hierarchy->groupCount) {
            error = "METEOR GPU spatial DAG has invalid count/root."; return false;
        }
        for (uint32_t i=0;i<hierarchy->groupCount;++i) {
            const auto& g=hierarchy->groups[i];
            if (!g.meshletCount || !std::isfinite(g.geometricError) || g.geometricError<0 ||
                !std::isfinite(g.sphere[0]) || !std::isfinite(g.sphere[1]) || !std::isfinite(g.sphere[2]) ||
                !std::isfinite(g.sphere[3]) || g.sphere[3]<0 || g.lodIndex>experiment::kMeshLodMaxLevels ||
                (i==hierarchy->rootGroup ? g.parentGroup!=UINT32_MAX : (g.parentGroup<=i || g.parentGroup>=hierarchy->groupCount)) ||
                (g.childGroup!=UINT32_MAX && (g.childGroup>=i || g.secondChild>=i || g.childGroup==g.secondChild))) {
                error = "METEOR GPU spatial DAG edges/bounds/errors are invalid."; return false;
            }
            if (g.childGroup==UINT32_MAX ? (g.secondChild!=UINT32_MAX || g.lodIndex!=0 || g.geometricError!=0) :
                (hierarchy->groups[g.childGroup].parentGroup!=i || hierarchy->groups[g.secondChild].parentGroup!=i ||
                 g.geometricError<hierarchy->groups[g.childGroup].geometricError || g.geometricError<hierarchy->groups[g.secondChild].geometricError)) {
                error="METEOR GPU spatial DAG ownership/error is invalid."; return false;
            }
        }
        for (const auto& d:payload->descriptors) {
            if (d.reserved>=hierarchy->groupCount) { error="METEOR cluster has no spatial group."; return false; }
            const auto& g=hierarchy->groups[d.reserved];
            if (d.clusterId<g.firstMeshlet || uint64_t(d.clusterId)>=uint64_t(g.firstMeshlet)+g.meshletCount) {
                error="METEOR cluster belongs to another spatial group."; return false;
            }
        }
    }
    uint64_t vertexEnd{}, triangleEnd{}, primitiveEnd{};
    uint32_t cluster = payload->descriptors.front().clusterId;
    uint32_t previousPrimitive = payload->primitiveRemap.empty() ? UINT32_MAX : payload->primitiveRemap.front();
    for (const auto& descriptor : payload->descriptors)
    {
        if (!descriptor.vertexCount || descriptor.vertexCount > experiment::kMeshletMaxVertices ||
            !descriptor.triangleCount || descriptor.triangleCount > experiment::kMeshletMaxTriangles ||
            descriptor.clusterId != cluster++ || descriptor.vertexOffset != vertexEnd ||
            descriptor.triangleOffset != triangleEnd || descriptor.primitiveOffset != primitiveEnd ||
            vertexEnd + descriptor.vertexCount > payload->vertexRemap.size() ||
            triangleEnd + descriptor.triangleCount * 3 > payload->triangleIndices.size() ||
            primitiveEnd + descriptor.triangleCount > payload->primitiveRemap.size())
        { error = "METEOR GPU meshlet descriptor range/identity is invalid."; return false; }
        for (uint32_t i = 0; i < descriptor.triangleCount * 3; ++i)
            if (payload->triangleIndices[triangleEnd + i] >= descriptor.vertexCount)
            { error = "METEOR GPU micro-triangle index is out of range."; return false; }
        for (uint32_t i = 0; i < descriptor.triangleCount; ++i)
            if (payload->primitiveRemap[primitiveEnd + i] != previousPrimitive++ || previousPrimitive > sourcePrimitiveCount)
            { error = "METEOR GPU source primitive identity is invalid."; return false; }
        vertexEnd += descriptor.vertexCount;
        triangleEnd += descriptor.triangleCount * 3;
        primitiveEnd += descriptor.triangleCount;
    }
    if (vertexEnd != payload->vertexRemap.size() || triangleEnd != payload->triangleIndices.size() ||
        primitiveEnd != payload->primitiveRemap.size() || sourceLodIndex > kRHIMaxCoarseMeshLods ||
        !sourceVertexCount || !sourcePrimitiveCount || (!sourceVertices.empty() && sourceVertices.size() != vertexCount))
    { error = "METEOR GPU geometry extent/LOD is invalid."; return false; }
    constexpr uint64_t maximum = (std::numeric_limits<uint32_t>::max)();
    if (vertexCount > maximum || payload->descriptors.size() > maximum ||
        payload->vertexRemap.size() > maximum || payload->triangleIndices.size() > maximum ||
        payload->primitiveRemap.size() > maximum)
    {
        error = "Meshlet GPU streams exceed 32-bit addressing.";
        return false;
    }
    RHIMeshletBufferHeader header;
    header.profileVersion = payload->settings.profileVersion;
    header.meshletCount = static_cast<uint32_t>(payload->descriptors.size());
    header.descriptorStride = sizeof(experiment::MeshletDescriptor);
    header.vertexRemapCount = static_cast<uint32_t>(payload->vertexRemap.size());
    header.triangleByteCount = static_cast<uint32_t>(payload->triangleIndices.size());
    header.primitiveRemapCount = static_cast<uint32_t>(payload->primitiveRemap.size());
    header.vertexCount = static_cast<uint32_t>(vertexCount);
    header.vertexStride = vertexStride;
    header.vertexAttributeMask = vertexAttributeMask;
    header.sourceLodIndex = sourceLodIndex;
    header.sourceVertexCount = sourceVertexCount;
    header.sourcePrimitiveCount = sourcePrimitiveCount;
    header.sourceVertexRemapCount = header.vertexRemapCount;
    uint64_t bytes = sizeof(header);
    const auto section = [&bytes](uint64_t count, uint64_t stride, uint32_t& offset)
    {
        constexpr uint64_t limit = (std::numeric_limits<uint32_t>::max)();
        if (count > limit / stride)
        {
            return false;
        }
        const uint64_t size = count * stride;
        const uint64_t aligned = (bytes + 15u) & ~uint64_t{15u};
        if (aligned > limit || size > limit - aligned)
        {
            return false;
        }
        offset = static_cast<uint32_t>(aligned);
        bytes = aligned + size;
        return true;
    };
    if (!section(header.meshletCount, header.descriptorStride, header.descriptorOffset) ||
        !section(header.vertexRemapCount, sizeof(uint32_t), header.vertexRemapOffset) ||
        !section(header.triangleByteCount, sizeof(uint8_t), header.triangleOffset) ||
        !section(header.primitiveRemapCount, sizeof(uint32_t), header.primitiveRemapOffset) ||
        !section(header.sourceVertexRemapCount, sizeof(uint32_t), header.sourceVertexRemapOffset) ||
        (hierarchy && hierarchy->groupCount && !section(hierarchy->GpuBytes(), 1, header.hierarchyOffset)) ||
        bytes > maximum - 15u)
    {
        error = "Meshlet GPU buffer size overflows 32-bit offsets.";
        return false;
    }
    // ByteAddressBuffer loads fetch whole words, including the final packed
    // triangle's last byte; zero padding makes that access remain in bounds.
    bytes = (bytes + 15u) & ~uint64_t{15u};
    try
    {
        RHIModelMeshletUpload candidate;
        candidate.bytes.resize(static_cast<size_t>(bytes));
        std::memcpy(candidate.bytes.data(), &header, sizeof(header));
        const auto copy = [&candidate](uint32_t offset, const void* source, size_t size)
        {
            if (size)
            {
                std::memcpy(candidate.bytes.data() + offset, source, size);
            }
        };
        if (header.hierarchyOffset) {
            const uint32_t dagHeader[4]{hierarchy->groupCount,hierarchy->rootGroup,0,0};
            copy(header.hierarchyOffset,dagHeader,sizeof(dagHeader));
            copy(header.hierarchyOffset+16,hierarchy->groups.data(),hierarchy->groups.size()*sizeof(experiment::MeshClusterGroup));
        }
        copy(header.descriptorOffset, payload->descriptors.data(),
            payload->descriptors.size() * sizeof(experiment::MeshletDescriptor));
        copy(header.vertexRemapOffset, payload->vertexRemap.data(), payload->vertexRemap.size() * sizeof(uint32_t));
        copy(header.triangleOffset, payload->triangleIndices.data(), payload->triangleIndices.size());
        copy(header.primitiveRemapOffset, payload->primitiveRemap.data(), payload->primitiveRemap.size() * sizeof(uint32_t));
        for (uint32_t i = 0; i < header.vertexRemapCount; ++i)
        {
            const auto local = payload->vertexRemap[i];
            if (local >= vertexCount || (!sourceVertices.empty() && local >= sourceVertices.size()))
            {
                error = "METEOR GPU meshlet remap exceeds its sealed vertices.";
                return false;
            }
            const uint32_t original = sourceVertices.empty() ? local : sourceVertices[local];
            if (original >= sourceVertexCount) { error = "METEOR source vertex identity is out of range."; return false; }
            std::memcpy(candidate.bytes.data() + header.sourceVertexRemapOffset + i * sizeof(uint32_t), &original, sizeof(original));
        }
        candidate.meshletCount = header.meshletCount;
        candidate.profileVersion = header.profileVersion;
        result = std::move(candidate);
        return true;
    }
    catch (const std::bad_alloc&)
    {
        error = "Meshlet GPU upload staging allocation failed.";
    }
    catch (const std::length_error&)
    {
        error = "Meshlet GPU upload staging exceeds addressable memory.";
    }
    return false;
}

inline bool BuildRHIModelMeshletUpload(const RHIModelMeshView& view,
    RHIModelMeshletUpload& result, std::string& error)
{
    const auto* payload = view.Meshlets();
    if (!view.IsComplete() || !payload)
    {
        result = {};
        error = "METEOR model meshlet upload requires validated cooked geometry; force reimport/recook.";
        return false;
    }
    return PackMeteorMeshletGpuPayload(*payload, view.vertexBytes / view.vertexStride,
        view.vertexStride, view.vertexAttributeMask, view.sourceLodIndex,
        view.indexCount / 3, static_cast<uint32_t>(view.vertexBytes / view.vertexStride), {}, result, error);
}
