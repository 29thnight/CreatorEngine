#pragma once
#include <array>
#include <cmath>
#include <compare>
#include <cstdint>

namespace assets
{
    // Per texture reference, independent of image/cache identity. glTF order:
    // offset + rotation * scale * selected UV. Missing data means UV0/identity.
    struct TextureCoordinates final
    {
        std::uint32_t set{};
        std::array<float, 2> offset{};
        std::array<float, 2> scale{1.f, 1.f};
        float rotation{};
        bool IsValid() const noexcept
        {
            return set <= 1 && std::isfinite(offset[0]) && std::isfinite(offset[1])
                && std::isfinite(scale[0]) && std::isfinite(scale[1]) && std::isfinite(rotation);
        }
        friend auto operator<=>(const TextureCoordinates&, const TextureCoordinates&) = default;
    };
}
