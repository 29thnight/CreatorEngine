#pragma once

#include "../../Engine/SceneRuntime/ProjectLayerSettingsIO.h"
#include "../../Engine/Utility_Framework/AuthoringParsedDocument.h"

namespace Editor
{
// One-time authoring conversion, shared by Editor bootstrap and the offline migration tool.
inline ce::layers::result<project_layer_snapshot> ImportLegacyProjectLayers(const std::filesystem::path& directory)
{
    using namespace ce::layers;
    try
    {
        ProjectLayerSettings defaults;
        auto migration = *defaults.Snapshot();
        const auto tags_path = directory / "TagManager.asset";
        std::vector<std::string> names{"Default", "TransparentFX", "Ignore RayCast", "Water", "UI"};
        std::string parse_error;
        if (std::filesystem::exists(tags_path))
        {
            const auto document = Authoring::ParsedDocument::ParseFile(tags_path.string(), parse_error);
            if (!document || !document.Root()["layers"].IsSequence())
                return std::unexpected(error::invalid_definition);

            names.clear();
            for (const auto node : document.Root()["layers"])
                names.push_back(node.AsStringChecked());
        }

        const auto imported = LayerCatalog::ImportLegacy(names);
        if (!imported)
            return std::unexpected(imported.error());

        migration.catalog = *imported;
        const auto matrix_path = directory / "CollisionMatrix.asset";
        if (std::filesystem::exists(matrix_path))
        {
            const auto document = Authoring::ParsedDocument::ParseFile(matrix_path.string(), parse_error);
            if (!document || !document.Root().IsSequence() ||
                (document.Root().Size() != names.size() && document.Root().Size() != 32))
                return std::unexpected(error::invalid_definition);

            const auto extent = document.Root().Size();

            std::size_t row = 0;
            for (const auto values : document.Root())
            {
                if (!values.IsSequence() || values.Size() != extent)
                    return std::unexpected(error::invalid_definition);

                std::size_t column = 0;
                for (const auto value : values)
                    migration.policy.matrix[row * 32 + column++] = value.As<bool>();

                ++row;
            }
        }

        if (!ce::physics::PhysicsCollisionPolicy::Validate(migration.policy))
            return std::unexpected(error::invalid_definition);

        return migration;
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(error::out_of_memory);
    }
    catch (const std::exception&)
    {
        return std::unexpected(error::invalid_definition);
    }
}
} // namespace Editor
