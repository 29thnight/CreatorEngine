#pragma once

#include "ProjectLayerSettingsIO.h"
#include "LegacyProjectLayerImport.h"
#include "Interfaces/AssetAuthoringPort.h"
#include "PathFinder.h"
#include <stdexcept>

namespace Editor
{
inline bool SaveProjectLayerSettings(const project_layer_snapshot& snapshot)
{
    return bool(ProjectLayerSettingsIO::Write(snapshot, [](std::span<const std::byte> bytes) {
        UncatalogedAuthoringRequest request;
        request.destinationPath = PathFinder::ProjectSettingPath(ProjectLayerSettingsIO::filename);
        request.payload.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        return AssetAuthoringPort::WriteLayerSettings(request);
    }));
}

// Only Editor imports legacy YAML. Player's loader accepts CLYR exclusively.
inline std::shared_ptr<ProjectLayerSettings> LoadProjectLayerSettings()
{
    const auto path = PathFinder::ProjectSettingPath(ProjectLayerSettingsIO::filename);
    auto state = std::make_shared<ProjectLayerSettings>();
    if (std::filesystem::exists(path))
    {
        auto loaded = ProjectLayerSettingsIO::Read(path);
        if (!loaded || !state->Restore(*loaded))
            throw std::runtime_error("Invalid project layer settings: " + path.string());

        return state;
    }

    const auto migration = ImportLegacyProjectLayers(PathFinder::ProjectSettingPath(""));
    if (!migration || !state->Restore(*migration) || !SaveProjectLayerSettings(*state->Snapshot()))
        throw std::runtime_error("Project layer migration/publication failed");

    return state;
}
} // namespace Editor
