#pragma once

#include "CookedAssetManifest.h"
#include "../../../Utility_Framework/InputGraph.h"

namespace experiment::cooked
{
    inline constexpr std::uint32_t kInputGraphRepresentation = 1u;
    inline constexpr std::uint32_t kInputGraphArtifactVersion = 1u;
    inline constexpr std::size_t kInputGraphMaxBytes = 16u * 1024u * 1024u;

    [[nodiscard]] Input::GraphID InputGraphIdentity(const AssetId& asset) noexcept;
    // Wire fields are explicitly little-endian; GUID words use canonical UUID byte order.
    // Editor graph/layout, callback names, pointers and native slot indices never enter the artifact.
    [[nodiscard]] bool WriteInputGraphArtifact(const Input::InputGraphProgram& program,
        std::vector<std::byte>& bytes, std::string& failure);
    // Validation/preparation is CPU-only. Failed reads preserve the caller's last good lease.
    [[nodiscard]] bool ReadInputGraphArtifact(std::span<const std::byte> bytes,
        const AssetId& expectedAsset, own::shared_owner<const Input::InputGraphProgram>& result,
        std::string& failure, std::uint64_t generation = 1u);
}
