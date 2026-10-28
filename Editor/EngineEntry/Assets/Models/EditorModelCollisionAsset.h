#pragma once
#include "../../../../Engine/Containers/ContainerAPI.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../../Tools/build/ModuleAdapters/Creator.Core.Uuid.h"
#else
import Creator.Core.Uuid;
#endif

#include "CollisionGeometryAuthoring.h"
#include "ModelCollisionGeometry.h"
#include "../../../../Engine/Utility_Framework/Reflection/TypeTrait.h"
#include "../../../../Engine/Utility_Framework/Authoring/AuthoringParsedDocument.h"

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
    const std::filesystem::path& root, uuid::uuid16 model, uuid::uuid16 mesh,
    const ce::physics::triangle_mesh_source& triangles)
{
    using namespace ce::physics;
    namespace file = std::filesystem;

    try
    {
        if (model.is_nil() || mesh.is_nil())
            return std::unexpected(
                error{error_code::invalid_argument, 0, "Model geometry requires model and mesh identity"});

        const auto directory = root / "CollisionGeometry";
        std::filesystem::create_directories(directory);
        const auto relative = std::filesystem::canonical(directory).lexically_relative(std::filesystem::canonical(root));
        if (relative.empty() || relative.is_absolute() || *relative.begin() == "..")
            return std::unexpected(error{error_code::invalid_argument, 0, "Model geometry directory escaped project"});

        const auto destination = directory / ("Model_" + uuid::to_string(model) + "_" + uuid::to_string(mesh) + "_" +
                                              std::to_string(ModelCollisionMeshHash(triangles)) + ".cegeometry");
        const auto meta = std::filesystem::path(destination.string() + ".meta");
        CollisionGeometrySource source{{FileGuid::CreateRandomV4().m_guid, 1}, triangles};
        const bool existing = std::filesystem::exists(destination);

        if (existing)
        {
            auto stored = ReadCollisionGeometrySource(destination);
            if (!stored || !FileGuid(stored->key.asset).IsRandomV4() || stored->key.revision != 1)
                return std::unexpected(error{error_code::invalid_argument, 0, "Invalid generated model geometry"});

            std::string failure;
            auto metadata = Authoring::ParsedDocument::ParseFilePath(meta, failure);
            if (!metadata || !metadata.Root()["guid"].IsScalar() ||
                metadata.Root()["guid"].AsString() != uuid::to_string(stored->key.asset) ||
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
