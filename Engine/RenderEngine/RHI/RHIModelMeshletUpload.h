#pragma once

#include "IRenderDeviceServices.h"

#include <cstring>
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

// Only a view tied to the exact immutable, read-validated asset generation can
// expose meshlets. The codec has verified its SHA-256 against these final bytes;
// do not re-import/rebuild meshlets or hash the same immutable mesh every frame.
// A missing payload is normal. A packing/allocation failure rejects only the
// optional mesh route and leaves the caller's indexed upload intact.
inline bool BuildRHIModelMeshletUpload(const RHIModelMeshView& view,
    RHIModelMeshletUpload& result, std::string& error)
{
    result = {};
    error.clear();
    const auto* payload = view.Meshlets();
    if (!payload)
    {
        return true;
    }
    if (!view.IsComplete() || payload->settings.profileVersion != experiment::kMeshletProfileVersion ||
        payload->settings.builderVersion != experiment::kMeshletBuilderVersion ||
        payload->settings.meshoptimizerVersion != experiment::kMeshletMeshoptimizerVersion ||
        payload->lod0.firstMeshlet != 0 || payload->lod0.meshletCount != payload->descriptors.size())
    {
        error = "Meshlet upload has unsupported profile or generation metadata.";
        return false;
    }
    constexpr uint64_t maximum = (std::numeric_limits<uint32_t>::max)();
    const uint64_t vertexCount = view.vertexBytes / view.vertexStride;
    if (view.vertexBytes > maximum || vertexCount > maximum || payload->descriptors.size() > maximum ||
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
    header.vertexStride = view.vertexStride;
    header.vertexAttributeMask = view.vertexAttributeMask;
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
        copy(header.descriptorOffset, payload->descriptors.data(),
            payload->descriptors.size() * sizeof(experiment::MeshletDescriptor));
        copy(header.vertexRemapOffset, payload->vertexRemap.data(), payload->vertexRemap.size() * sizeof(uint32_t));
        copy(header.triangleOffset, payload->triangleIndices.data(), payload->triangleIndices.size());
        copy(header.primitiveRemapOffset, payload->primitiveRemap.data(), payload->primitiveRemap.size() * sizeof(uint32_t));
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
