#pragma once

#include "../../Utility_Framework/Foundation/StandardLibrary.h"

namespace AssetDepot
{
    // A lookup token, not ownership of a mount or its artifact backing.
    struct [[reflgen::runtime]] AssetMountId final
    {
        std::uint64_t value{};

        [[nodiscard]] bool IsValid() const noexcept { return value != 0u; }
        friend auto operator<=>(const AssetMountId&, const AssetMountId&) noexcept = default;
    };
}
