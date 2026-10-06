#pragma once
#include "ShadowMath.h"
#include "EnhancedDrawIdentity.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace shadow_math
{
    inline bool FiniteAffine(const math::matrix4x4& matrix)
    {
        for (const auto& row : matrix.m)
        {
            for (float value : row)
            {
                if (!std::isfinite(value))
                {
                    return false;
                }
            }
        }
        return matrix.m[0][3] == 0.f && matrix.m[1][3] == 0.f &&
            matrix.m[2][3] == 0.f && matrix.m[3][3] == 1.f;
    }

    inline bool FinitePose(const EnhancedDrawItem& draw)
    {
        if (!FiniteAffine(draw.worldMatrix) || draw.boneCount > 256 ||
            (draw.boneCount != 0 && !draw.bonePalette))
        {
            return false;
        }
        for (uint32_t i = 0; i < draw.boneCount; ++i)
        {
            if (!FiniteAffine(draw.bonePalette[i]))
            {
                return false;
            }
        }
        return true;
    }

    namespace bounds_detail
    {
        using Vector = std::array<double, 3>;
        using Matrix = std::array<std::array<double, 4>, 4>;

        inline Matrix Widen(const math::matrix4x4& source)
        {
            Matrix result{};
            for (unsigned row = 0; row < 4; ++row)
            {
                for (unsigned column = 0; column < 4; ++column)
                {
                    result[row][column] = source.m[row][column];
                }
            }
            return result;
        }

        inline Matrix Compose(const Matrix& left, const Matrix& right)
        {
            Matrix result{};
            for (unsigned row = 0; row < 4; ++row)
            {
                for (unsigned column = 0; column < 4; ++column)
                {
                    for (unsigned component = 0; component < 4; ++component)
                    {
                        result[row][column] += left[row][component] * right[component][column];
                    }
                }
            }
            return result;
        }

        inline Vector Transform(const Vector& point, const Matrix& matrix)
        {
            Vector result{};
            for (unsigned column = 0; column < 3; ++column)
            {
                result[column] = matrix[3][column];
                for (unsigned row = 0; row < 3; ++row)
                {
                    result[column] += point[row] * matrix[row][column];
                }
            }
            return result;
        }

        inline Vector Magnitude(const Vector& point, double radius, const Matrix& matrix)
        {
            Vector result{};
            for (unsigned column = 0; column < 3; ++column)
            {
                result[column] = std::abs(matrix[3][column]);
                for (unsigned row = 0; row < 3; ++row)
                {
                    result[column] += (std::abs(point[row]) + radius) * std::abs(matrix[row][column]);
                }
            }
            return result;
        }

        inline double Scale(const Matrix& matrix)
        {
            double squared = 0;
            for (unsigned row = 0; row < 3; ++row)
            {
                for (unsigned column = 0; column < 3; ++column)
                {
                    squared += matrix[row][column] * matrix[row][column];
                }
            }
            return std::sqrt(squared);
        }

        inline double Distance(const Vector& left, const Vector& right)
        {
            return std::hypot(left[0] - right[0], left[1] - right[1], left[2] - right[2]);
        }
    }

    inline Sphere WorldBounds(const EnhancedDrawItem& draw)
    {
        using namespace bounds_detail;
        // Invalid transforms/poses cannot support rejection, on CPU or GPU.
        if (!FinitePose(draw))
        {
            return {};
        }
        const auto authoredCenter = draw.boundRadius > 0.f || !draw.mesh
            ? draw.boundCenter : draw.mesh->GetBoundingSphere().center;
        Vector localCenter{authoredCenter.x, authoredCenter.y, authoredCenter.z};
        double localRadius = enhanced_draw::BoundRadius(draw);
        if (!(localRadius > 0.0) && draw.modelMeshView.IsComplete() && draw.modelMeshView.vertexStride >= 12)
        {
            // Isolated typed fixtures may omit cached bounds. Derive them in
            // double so large finite endpoints cannot overflow before validation.
            const auto& mesh = draw.modelMeshView;
            Vector minimum{}, maximum{};
            for (uint64_t offset = 0; offset < mesh.vertexBytes; offset += mesh.vertexStride)
            {
                float position[3];
                std::memcpy(position, static_cast<const std::byte*>(mesh.vertexData) + offset, sizeof(position));
                for (unsigned axis = 0; axis < 3; ++axis)
                {
                    if (!std::isfinite(position[axis]))
                    {
                        return {};
                    }
                    if (offset == 0)
                    {
                        minimum[axis] = maximum[axis] = position[axis];
                    }
                    minimum[axis] = (std::min)(minimum[axis], double(position[axis]));
                    maximum[axis] = (std::max)(maximum[axis], double(position[axis]));
                }
            }
            for (unsigned axis = 0; axis < 3; ++axis)
            {
                localCenter[axis] = (minimum[axis] + maximum[axis]) * 0.5;
            }
            localRadius = Distance(minimum, maximum) * 0.5;
        }
        if (!(localRadius > 0.0) || !std::isfinite(localRadius) ||
            !std::isfinite(localCenter[0]) || !std::isfinite(localCenter[1]) || !std::isfinite(localCenter[2]))
        {
            return {};
        }

        const Matrix world = Widen(draw.worldMatrix);
        const Vector center = Transform(localCenter, world);
        double radius = localRadius * Scale(world);
        Vector arithmeticError = Magnitude(localCenter, localRadius, world);
        for (double& component : arithmeticError)
        {
            component *= 0.000002;
        }
        if (draw.boneCount != 0)
        {
            // Only callers that have proved nonnegative normalized weights may
            // use this skin sphere for rejection. Unproven skin must stay unknown.
            // Compose the ORIGINAL float matrices in double, not a float matrix
            // product: that product can overflow although sequential GPU math does not.
            Vector deformedMagnitude{std::abs(localCenter[0]) + localRadius,
                std::abs(localCenter[1]) + localRadius, std::abs(localCenter[2]) + localRadius};
            for (uint32_t bone = 0; bone < draw.boneCount; ++bone)
            {
                const Matrix transform = Widen(draw.bonePalette[bone]);
                const Matrix composed = Compose(transform, world);
                const Vector boneCenter = Transform(Transform(localCenter, transform), world);
                const double extent = Distance(center, boneCenter) + localRadius * Scale(composed);
                if (!std::isfinite(extent))
                {
                    return {};
                }
                radius = (std::max)(radius, extent);
                const Vector magnitude = Magnitude(localCenter, localRadius, transform);
                for (unsigned axis = 0; axis < 3; ++axis)
                {
                    deformedMagnitude[axis] = (std::max)(deformedMagnitude[axis], magnitude[axis]);
                }
            }
            // Shader skin blends four float matrices, transforms the point,
            // discards blended W, then applies world. Bound both rounding stages
            // with uncancelled magnitudes, including the admitted weight-sum error.
            constexpr double weightTolerance = 0.00002;
            const Vector origin{world[3][0], world[3][1], world[3][2]};
            radius = radius * (1.0 + weightTolerance) + Distance(center, origin) * weightTolerance;
            for (unsigned column = 0; column < 3; ++column)
            {
                double propagatedSkinError = 0;
                double worldMagnitude = std::abs(world[3][column]);
                for (unsigned row = 0; row < 3; ++row)
                {
                    const double skinError = deformedMagnitude[row] * 0.000008;
                    const double coefficient = std::abs(world[row][column]);
                    propagatedSkinError += skinError * coefficient;
                    worldMagnitude += (deformedMagnitude[row] * (1.0 + weightTolerance) + skinError) * coefficient;
                }
                arithmeticError[column] = propagatedSkinError + worldMagnitude * 0.000002;
            }
        }

        Sphere result;
        Vector roundedCenter{};
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            if (!std::isfinite(center[axis]) || std::abs(center[axis]) > (std::numeric_limits<float>::max)())
            {
                return {};
            }
            roundedCenter[axis] = static_cast<float>(center[axis]);
        }
        result.center = {static_cast<float>(roundedCenter[0]), static_cast<float>(roundedCenter[1]),
            static_cast<float>(roundedCenter[2])};
        // Covers center conversion, shader float arithmetic and radius/scale
        // rounding. Nonrepresentable bounds retain geometry rather than reject it.
        radius = radius * 1.000002 + Distance(center, roundedCenter) +
            std::hypot(arithmeticError[0], arithmeticError[1], arithmeticError[2]);
        if (!std::isfinite(radius) || radius > (std::numeric_limits<float>::max)())
        {
            return {};
        }
        result.radius = std::nextafter(static_cast<float>(radius), (std::numeric_limits<float>::infinity)());
        if (!std::isfinite(result.radius))
        {
            return {};
        }
        return result;
    }
}
