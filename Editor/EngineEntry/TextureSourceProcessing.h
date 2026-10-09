#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace Authoring
{
    // Stateless process-lifetime callbacks. Only Editor and offline tools link
    // this implementation; removing a callback never invalidates SDK owners.
    [[nodiscard]] bool CookTerrainSource(const std::filesystem::path& source,
        const std::filesystem::path& terrainRoot, std::vector<std::byte>& artifact, std::string& failure);

    void InstallTextureSourceProcessing() noexcept;
    void UninstallTextureSourceProcessing() noexcept;
}
