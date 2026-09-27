#pragma once

#include <mathematics/matrix4x4.hpp>

#include <cstddef>
#include <cstring>
#include <type_traits>

// Three transposed float4 rows retain every affine bone coefficient. Slang's
// structured-buffer matrix layout is column-major, so the shader transposes
// these rows back before applying the existing row-vector skinning math.
struct PackedBoneMatrix final
{
    float rows[3][4]{};

    [[nodiscard]] static PackedBoneMatrix From(const math::matrix4x4& matrix) noexcept
    {
        const auto transposed = math::transpose(matrix);
        PackedBoneMatrix packed;
        std::memcpy(packed.rows, transposed.m, sizeof(packed.rows));
        return packed;
    }

    [[nodiscard]] static PackedBoneMatrix Identity() noexcept
    {
        return From(math::matrix4x4::identity());
    }
};

static_assert(sizeof(PackedBoneMatrix) == sizeof(float) * 12);
static_assert(std::is_trivially_copyable_v<PackedBoneMatrix>);
