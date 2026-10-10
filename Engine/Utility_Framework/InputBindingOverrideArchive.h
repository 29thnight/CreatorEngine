#pragma once

#include "InputGraph.h"

namespace Input
{
    inline constexpr std::uint32_t kInputOverrideArchiveVersion = 1u;
    inline constexpr std::size_t kInputOverrideMaxBytes = 4u * 1024u * 1024u;

    // Stable IDs and explicit versions; never resolve an orphan by display name or slot.
    [[nodiscard]] bool WriteInputBindingOverrides(UserID user, const InputGraphProgram& program,
        std::span<const InputBindingOverride> overrides, std::vector<std::byte>& bytes, std::string& failure);
    // Checks user, graph, versions and all overrides against the current definition.
    // Failure preserves the caller's previous overrides and program.
    [[nodiscard]] bool ReadInputBindingOverrides(std::span<const std::byte> bytes, UserID expectedUser,
        const InputGraphProgram& program, std::vector<InputBindingOverride>& overrides, std::string& failure);
}
