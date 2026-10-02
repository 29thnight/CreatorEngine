#pragma once

#include "EditorAssetDatabase.h"
#include "EditorModelCollisionAsset.h"
#include "ModelCollisionGeometry.h"
#include "Scene.h"
#include "DataSystem.h"
#include "PathFinder.h"
#include "AuthoringParsedDocument.h"

namespace Editor
{
inline ce::physics::result<ce::physics::geometry_asset_key> PublishModelCollisionGeometry(
    Scene& scene, const file::path& root, Uuid::Uuid16 model, Uuid::Uuid16 mesh,
    const ce::physics::triangle_mesh_source& triangles)
{
    using namespace ce::physics;
    ce::profile_scope scope{ce::marker<"Physics.ModelCollisionPublish">()};

    try
    {
        if (!PathFinder::IsAssetAuthoringEnabled() || model.IsNil() || mesh.IsNil())
            return std::unexpected(
                error{error_code::wrong_phase, 0, "Model collision publication requires Editor authoring"});

        auto prepared = PrepareModelCollisionAsset(root, model, mesh, triangles);
        if (!prepared)
            return std::unexpected(prepared.error());

        const auto& source = prepared->source;
        const auto& destination = prepared->destination;
        const bool existing = prepared->existing;

        auto published = scene.PublishCollisionGeometry(source, [&](std::span<const std::byte>) {
            return existing || EditorAssetDatabase::Get().CreateCollisionGeometry(destination, source);
        });
        if (!published)
            return std::unexpected(published.error());

        DataSystems->ApplyAssetChange(
            {RuntimeAssetChangeKind::CatalogUpsert, RuntimeAssetType::Auto, FileGuid(source.key.asset), destination});
        return source.key;
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(error{error_code::out_of_memory, 0, "Model collision publication allocation failed"});
    }
    catch (...)
    {
        return std::unexpected(error{error_code::invalid_argument, 0, "Model collision publication failed"});
    }
}
} // namespace Editor
