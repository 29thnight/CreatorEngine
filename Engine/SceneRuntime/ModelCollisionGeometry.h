#pragma once

#include "CollisionGeometrySource.h"
#include "../RenderEngine/Assets/ModelVertexLayout.h"
#include <bit>
#include <cstring>

namespace ce::physics
{
inline result<triangle_mesh_source> BuildModelCollisionMesh(std::span<const std::byte> vertices,
                                                            std::span<const std::uint32_t> indices, std::uint32_t mask,
                                                            std::uint32_t stride, std::uint64_t layoutHash)
{
    if (!assets::IsSupportedModelVertexLayout(mask) || stride != assets::StrideOf(mask) ||
        layoutHash != assets::VertexLayoutHash(mask) || !stride || vertices.size() % stride != 0 ||
        vertices.size() / stride < 3 || vertices.size() / stride > 1024 * 1024 || indices.empty() ||
        indices.size() % 3 != 0 || indices.size() / 3 > 4 * 1024 * 1024)
        return std::unexpected(error{error_code::invalid_argument, 0, "Invalid model collision mesh layout/count"});

    try
    {
        triangle_mesh_source source;
        source.points.reserve(vertices.size() / stride);
        source.triangles.reserve(indices.size() / 3);
        const auto offset = assets::OffsetOf(mask, assets::VertexAttribute::Position);

        for (std::size_t index = 0; index < vertices.size() / stride; ++index)
        {
            float position[3];
            std::memcpy(position, vertices.data() + index * stride + offset, sizeof(position));
            if (!std::ranges::all_of(position, [](float value) { return std::isfinite(value); }))
                return std::unexpected(error{error_code::invalid_argument, 0, "Nonfinite collision mesh position"});

            source.points.emplace_back(position[0], position[1], position[2]);
        }

        for (std::size_t index = 0; index < indices.size(); index += 3)
        {
            const auto a = indices[index], b = indices[index + 1], c = indices[index + 2];
            if (a >= source.points.size() || b >= source.points.size() || c >= source.points.size() || a == b ||
                b == c || a == c)
                return std::unexpected(error{error_code::invalid_argument, 0, "Invalid collision mesh triangle"});

            source.triangles.push_back({a, b, c});
        }

        return source;
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(error{error_code::out_of_memory, 0, "Model collision mesh allocation failed"});
    }
}

// Filename discriminator only. Reuse must still compare the complete encoded source.
inline std::uint64_t ModelCollisionMeshHash(const triangle_mesh_source& source)
{
    std::uint64_t hash = 14695981039346656037ull;
    const auto append = [&](std::uint64_t value) {
        for (unsigned index = 0; index < 8; ++index)
        {
            hash ^= (value >> (index * 8)) & 255;
            hash *= 1099511628211ull;
        }
    };

    append(source.points.size());
    append(source.triangles.size());

    for (const auto& point : source.points)
        for (float value : math::components(point))
            append(std::bit_cast<std::uint32_t>(value));

    for (const auto& triangle : source.triangles)
    {
        append(triangle.a);
        append(triangle.b);
        append(triangle.c);
    }

    return hash;
}
} // namespace ce::physics
