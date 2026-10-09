#pragma once

#include "MaterialGraphSceneInput.h"
#include "Render/Graph/ShadowCasterBounds.h"

#include <array>
#include <cmath>

namespace material_graph
{
    // Same conservative projection derivative bound as the native meshlet LOD
    // selector. Authored simplification error is a quality metric, not Hausdorff.
    inline uint32_t SelectSceneGeometryLod(std::span<const experiment::MeshLodLevel> levels,
        const math::matrix4x4& world, const shadow_math::Sphere& sphere, const SceneInputView& view, float maxPixelError)
    {
        if (levels.empty() ||
            !std::isfinite(maxPixelError) || maxPixelError <= 0 ||
            !shadow_math::FiniteAffine(world))
        {
            return 0;
        }
        if (!std::isfinite(sphere.radius) || sphere.radius <= 0)
        {
            return 0;
        }
        const auto projection = view.camera.view * view.camera.projection;
        std::array<double, 4> center{}, radius{}, rounding{}, norm{};
        const double position[]{sphere.center.x, sphere.center.y, sphere.center.z, 1};
        for (unsigned column = 0; column < 4; ++column)
        {
            double magnitude{};
            for (unsigned row = 0; row < 4; ++row)
            {
                center[column] += position[row] * projection.m[row][column];
                magnitude += (std::abs(position[row]) + (row < 3 ? sphere.radius : 0)) *
                    std::abs(projection.m[row][column]);
                if (row < 3)
                {
                    norm[column] += double(projection.m[row][column]) * projection.m[row][column];
                }
            }
            norm[column] = std::sqrt(norm[column]) * 1.000002;
            radius[column] = sphere.radius * norm[column];
            rounding[column] = std::max(0.000001, magnitude * 0.000002);
        }
        const double lowW = center[3] - radius[3] - rounding[3];
        const double lowZ = center[2] - radius[2] - rounding[2];
        if (!std::isfinite(lowW) || !std::isfinite(lowZ) || lowW <= 0.000001 || lowZ <= 0)
        {
            return 0;
        }
        double scaleSquared{};
        for (unsigned row = 0; row < 3; ++row)
        {
            for (unsigned column = 0; column < 3; ++column)
            {
                scaleSquared += double(world.m[row][column]) * world.m[row][column];
            }
        }
        const double maxW = std::abs(center[3]) + radius[3] + rounding[3];
        const auto derivative = [&](unsigned column, uint32_t extent) {
            return (norm[column] * maxW + norm[3] *
                (std::abs(center[column]) + radius[column] + rounding[column])) / (lowW * lowW) * extent * 0.5;
        };
        const double pixels = std::hypot(derivative(0, view.width), derivative(1, view.height)) *
            std::sqrt(scaleSquared) * 1.000004;
        if (!std::isfinite(pixels) || pixels <= 0)
        {
            return 0;
        }
        uint32_t selected{};
        for (uint32_t level = 0; level < levels.size(); ++level)
        {
            const auto& lod = levels[level];
            if (!std::isfinite(lod.geometricError) || lod.geometricError < 0)
            {
                return 0;
            }
            if (!lod.indices.empty() && lod.indices.size() % 3 == 0 &&
                lod.geometricError * pixels <= maxPixelError)
            {
                selected = level + 1;
            }
        }
        return selected;
    }

    inline uint32_t SelectSceneGeometryLod(const EnhancedDrawItem& draw, const SceneInputView& view,
        float maxPixelError)
    {
        const auto* source = draw.modelMeshView.SourceMesh();
        if (!source || draw.boneCount)
        {
            return 0; // No simplification error bound for a deforming palette.
        }
        return SelectSceneGeometryLod(source->coarseLods.levels, draw.worldMatrix,
            shadow_math::WorldBounds(draw), view, maxPixelError);
    }
}
