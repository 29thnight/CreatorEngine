#pragma once
#include "ModelCpuGeometry.h"
#include "ModelMeshDescriptor.h"

namespace assets
{
struct ModelCpuGeometryPayload final
{
    ModelGeometryKey key;
    cpu_geometry::Geometry geometry;
    bool Matches(const ModelMeshDescriptor& descriptor) const
    {
        return key == descriptor.geometryKey && geometry.positions.size() == descriptor.vertexCount &&
            geometry.indices.size() == descriptor.indexCount;
    }
};
inline own::shared_owner<const ModelCpuGeometryPayload> LoadModelCpuGeometry(
    const ModelMeshDescriptor& descriptor, std::string& error)
{
    try
    {
        if (!descriptor.streamMetadata || !descriptor.streamMetadata->cpuReader)
            throw std::runtime_error("Mesh requires a current exact CPU geometry artifact; recook it");
        auto result = own::make_shared<ModelCpuGeometryPayload>(); result->key = descriptor.geometryKey;
        const auto bytes = descriptor.streamMetadata->cpuReader();
        if (!cpu_geometry::Decode(bytes, result->geometry, error) || !result->Matches(descriptor))
            throw std::runtime_error("Exact CPU geometry differs from its descriptor: " + error);
        return result;
    }
    catch (const std::exception& failure) { error = failure.what(); return {}; }
}
}
