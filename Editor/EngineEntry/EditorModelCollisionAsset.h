#pragma once

#include "CollisionGeometryAuthoring.h"
#include "ModelCollisionGeometry.h"
#include "TypeTrait.h"
#include "AuthoringParsedDocument.h"

namespace Editor
{
struct ModelCollisionAsset final
{
    std::filesystem::path destination;
    ce::physics::CollisionGeometrySource source;
    bool existing;
};

// Host-owned source preparation. Filename hashes are never trusted without a full content comparison.
inline ce::physics::result<ModelCollisionAsset> PrepareModelCollisionAsset(
    const std::filesystem::path& root, Uuid::Uuid16 model, Uuid::Uuid16 mesh,
    const ce::physics::triangle_mesh_source& triangles)
{
    using namespace ce::physics;
    namespace file = std::filesystem;

    try
    {
        if (model.IsNil() || mesh.IsNil())
            return std::unexpected(
                error{error_code::invalid_argument, 0, "Model geometry requires model and mesh identity"});

        const auto directory = root / "CollisionGeometry";
        file::create_directories(directory);
        const auto relative = file::canonical(directory).lexically_relative(file::canonical(root));
        if (relative.empty() || relative.is_absolute() || *relative.begin() == "..")
            return std::unexpected(error{error_code::invalid_argument, 0, "Model geometry directory escaped project"});

        const auto destination = directory / ("Model_" + Uuid::ToString(model) + "_" + Uuid::ToString(mesh) + "_" +
                                              std::to_string(ModelCollisionMeshHash(triangles)) + ".cegeometry");
        const auto meta = file::path(destination.string() + ".meta");
        CollisionGeometrySource source{{FileGuid::CreateRandomV4().m_guid, 1}, triangles};
        const bool existing = file::exists(destination);

        if (existing)
        {
            auto stored = ReadCollisionGeometrySource(destination);
            if (!stored || !FileGuid(stored->key.asset).IsRandomV4() || stored->key.revision != 1)
                return std::unexpected(error{error_code::invalid_argument, 0, "Invalid generated model geometry"});

            std::string failure;
            auto metadata = Authoring::ParsedDocument::ParseFile(meta.string(), failure);
            if (!metadata || !metadata.Root()["guid"].IsScalar() ||
                metadata.Root()["guid"].AsString() != Uuid::ToString(stored->key.asset) ||
                !metadata.Root()["geometryRevision"].IsScalar() ||
                metadata.Root()["geometryRevision"].AsString() != "1")
                return std::unexpected(
                    error{error_code::invalid_argument, 0, "Generated model geometry metadata mismatch"});

            source.key = stored->key;
            auto expected = CollisionGeometryCodec::Encode(source);
            auto actual = CollisionGeometryCodec::Encode(*stored);
            if (!expected || !actual || *expected != *actual)
                return std::unexpected(
                    error{error_code::invalid_argument, 0, "Generated model geometry content conflict"});
        }

        if (auto valid = ValidateCollisionGeometrySource(source); !valid)
            return std::unexpected(valid.error());

        return ModelCollisionAsset{destination, std::move(source), existing};
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(
            error{error_code::out_of_memory, 0, "Model collision asset preparation allocation failed"});
    }
    catch (...)
    {
        return std::unexpected(error{error_code::invalid_argument, 0, "Model collision asset preparation failed"});
    }
}
} // namespace Editor
