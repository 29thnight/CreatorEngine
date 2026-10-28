#include "../../Containers/ContainerAPI.h"
#pragma once
#include "../../Utility_Framework/Foundation/StandardLibrary.h"

namespace assets
{
    // Per texture reference, independent of image/cache identity. glTF order:
    // offset + rotation * scale * selected UV. Missing data means UV0/identity.
    struct [[reflgen::runtime, reflgen::public_fields, reflgen::public_methods]] TextureCoordinates final
    {
        std::uint32_t set{};
        ce::fixed_array<float, 2> offset{};
        ce::fixed_array<float, 2> scale{1.f, 1.f};
        float rotation{};
        bool IsValid() const noexcept
        {
            return set <= 1 && std::isfinite(offset[0]) && std::isfinite(offset[1])
                && std::isfinite(scale[0]) && std::isfinite(scale[1]) && std::isfinite(rotation);
        }
        friend auto operator<=>(const TextureCoordinates&, const TextureCoordinates&) = default;
    };
}
