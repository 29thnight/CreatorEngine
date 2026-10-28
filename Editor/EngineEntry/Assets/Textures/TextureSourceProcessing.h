#include "../../../../Engine/Containers/ContainerAPI.h"
#pragma once

#include "../../../../Engine/Utility_Framework/Foundation/StandardLibrary.h"

namespace Authoring
{
    // Stateless process-lifetime callbacks. Only Editor and offline tools link
    // this implementation; removing a callback never invalidates SDK owners.
    [[nodiscard]] bool CookTerrainSource(const std::filesystem::path& source,
        const std::filesystem::path& terrainRoot, ce::dynamic_array<std::byte>& artifact, std::string& failure);

    void InstallTextureSourceProcessing() noexcept;
    void UninstallTextureSourceProcessing() noexcept;
}
