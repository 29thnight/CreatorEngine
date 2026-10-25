#if defined(CE_CONTRACT_STANDALONE)
#include "../../../Tools/build/ModuleAdapters/Creator.Core.Paths.h"
#else
import Creator.Core.Paths;
#endif
#pragma once
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../Tools/build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif

#include "ProjectLayerSettingsIO.h"
#include "LegacyProjectLayerImport.h"
#include "Interfaces/AssetAuthoringPort.h"
#include "../../../Engine/Utility_Framework/Foundation/StandardLibrary.h"

namespace Editor
{
inline bool SaveProjectLayerSettings(const project_layer_snapshot& snapshot)
{
    return bool(ProjectLayerSettingsIO::Write(snapshot, [](std::span<const std::byte> bytes) {
        UncatalogedAuthoringRequest request;
        request.destinationPath = ce::paths::project_setting_path(ProjectLayerSettingsIO::filename);
        request.payload.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        return AssetAuthoringPort::WriteLayerSettings(request);
    }));
}

// Only Editor imports legacy YAML. Player's loader accepts CLYR exclusively.
inline own::shared_owner<ProjectLayerSettings> LoadProjectLayerSettings()
{
    const auto path = ce::paths::project_setting_path(ProjectLayerSettingsIO::filename);
    auto state = own::make_shared<ProjectLayerSettings>();
    if (std::filesystem::exists(path))
    {
        auto loaded = ProjectLayerSettingsIO::Read(path);
        if (!loaded || !state->Restore(*loaded))
            throw std::runtime_error("Invalid project layer settings: " + path.string());

        return state;
    }

    const auto migration = ImportLegacyProjectLayers(ce::paths::project_setting_path(""));
    if (!migration || !state->Restore(*migration) || !SaveProjectLayerSettings(*state->Snapshot()))
        throw std::runtime_error("Project layer migration/publication failed");

    return state;
}
} // namespace Editor
