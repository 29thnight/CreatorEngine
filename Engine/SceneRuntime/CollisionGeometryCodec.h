#pragma once

#include "CollisionGeometrySource.h"
#include <bit>
#include <new>

namespace ce::physics
{
// CECG v1: little endian, UUID/revision, tagged cook inputs, checksum.
// SDK ABI, pointers and cooked SDK blobs never enter this portable source record.
class CollisionGeometryCodec final
{
  public:
    static constexpr std::size_t max_bytes = 64 * 1024 * 1024;

    static result<std::vector<std::byte>> Encode(const CollisionGeometrySource& source)
    {
        if (auto valid = ValidateCollisionGeometrySource(source); !valid)
            return std::unexpected(valid.error());

        try
        {
            std::vector<std::byte> bytes;
            const auto payloadSize = std::visit(
                [](const auto& input) -> std::size_t {
                    using T = std::remove_cvref_t<decltype(input)>;
                    if constexpr (std::same_as<T, heightfield_source>)
                        return 52 + input.heights.size() * 2;
                    else if constexpr (std::same_as<T, triangle_mesh_source>)
                        return 48 + input.points.size() * 12 + input.triangles.size() * 12;
                    else
                        return 44 + input.points.size() * 12;
                },
                source.form);
            bytes.reserve(payloadSize);
            Put(bytes, 0x47434543, 4); // CECG
            Put(bytes, 1, 4);
            for (const auto byte : source.key.asset.data)
                Put(bytes, byte, 1);
            Put(bytes, source.key.revision, 8);
            Put(bytes, source.form.index(), 4);
            std::visit(
                [&](const auto& input) {
                    using T = std::remove_cvref_t<decltype(input)>;
                    if constexpr (std::same_as<T, heightfield_source>)
                    {
                        Put(bytes, input.rows, 4);
                        Put(bytes, input.columns, 4);
                        for (const auto height : input.heights)
                            Put(bytes, std::bit_cast<std::uint16_t>(height), 2);
                    }
                    else
                    {
                        Put(bytes, input.points.size(), 4);
                        if constexpr (std::same_as<T, triangle_mesh_source>)
                            Put(bytes, input.triangles.size(), 4);
                        for (const auto& point : input.points)
                            for (const auto component : math::components(point))
                                Put(bytes, std::bit_cast<std::uint32_t>(component), 4);
                        if constexpr (std::same_as<T, triangle_mesh_source>)
                            for (const auto& triangle : input.triangles)
                            {
                                Put(bytes, triangle.a, 4);
                                Put(bytes, triangle.b, 4);
                                Put(bytes, triangle.c, 4);
                            }
                    }
                },
                source.form);

            if (bytes.size() > max_bytes - 8)
                return std::unexpected(
                    error{error_code::capacity_exceeded, 0, "Collision geometry source exceeds size limit"});

            Put(bytes, Checksum(bytes), 8);
            return bytes;
        }
        catch (const std::bad_alloc&)
        {
            return std::unexpected(
                error{error_code::out_of_memory, 0, "Collision geometry encoding allocation failed"});
        }
    }

    static result<CollisionGeometrySource> Decode(std::span<const std::byte> bytes)
    {
        if (bytes.size() < 52 || bytes.size() > max_bytes)
            return Corrupt();

        std::size_t checksumOffset = bytes.size() - 8;
        if (Get(bytes, checksumOffset, 8) != Checksum(bytes.first(bytes.size() - 8)))
            return Corrupt();

        bytes = bytes.first(bytes.size() - 8);
        std::size_t cursor = 0;
        if (Get(bytes, cursor, 4) != 0x47434543 || Get(bytes, cursor, 4) != 1)
            return Corrupt();

        CollisionGeometrySource source;
        for (auto& byte : source.key.asset.data)
            byte = static_cast<std::uint8_t>(Get(bytes, cursor, 1));
        source.key.revision = Get(bytes, cursor, 8);
        const auto kind = Get(bytes, cursor, 4);
        if (kind > 2)
            return Corrupt();

        try
        {
            if (kind == 2)
            {
                heightfield_source input;
                input.rows = static_cast<std::uint32_t>(Get(bytes, cursor, 4));
                input.columns = static_cast<std::uint32_t>(Get(bytes, cursor, 4));
                const auto count = std::uint64_t(input.rows) * input.columns;
                if (input.rows < 2 || input.columns < 2 || count > 16 * 1024 * 1024 ||
                    count * 2 != bytes.size() - cursor)
                    return Corrupt();

                input.heights.reserve(static_cast<std::size_t>(count));
                for (std::uint64_t i = 0; i < count; ++i)
                    input.heights.push_back(
                        std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(Get(bytes, cursor, 2))));
                source.form = std::move(input);
            }
            else
            {
                const auto count = Get(bytes, cursor, 4);
                const auto triangles = kind == 1 ? Get(bytes, cursor, 4) : 0;
                if (count < (kind == 0 ? 4u : 3u) || count > 1024 * 1024 || triangles > 4 * 1024 * 1024 ||
                    (kind == 1 && !triangles) || count * 12 + triangles * 12 != bytes.size() - cursor)
                    return Corrupt();

                std::vector<math::vector3> points;
                points.reserve(static_cast<std::size_t>(count));
                for (std::uint64_t i = 0; i < count; ++i)
                {
                    const auto x = std::bit_cast<float>(static_cast<std::uint32_t>(Get(bytes, cursor, 4)));
                    const auto y = std::bit_cast<float>(static_cast<std::uint32_t>(Get(bytes, cursor, 4)));
                    const auto z = std::bit_cast<float>(static_cast<std::uint32_t>(Get(bytes, cursor, 4)));
                    points.push_back({x, y, z});
                }

                if (kind == 0)
                    source.form = convex_source{std::move(points)};
                else
                {
                    triangle_mesh_source input{std::move(points), {}};
                    input.triangles.reserve(static_cast<std::size_t>(triangles));
                    for (std::uint64_t i = 0; i < triangles; ++i)
                    {
                        const auto a = static_cast<std::uint32_t>(Get(bytes, cursor, 4));
                        const auto b = static_cast<std::uint32_t>(Get(bytes, cursor, 4));
                        const auto c = static_cast<std::uint32_t>(Get(bytes, cursor, 4));
                        input.triangles.push_back({a, b, c});
                    }
                    source.form = std::move(input);
                }
            }

            if (cursor != bytes.size() || !ValidateCollisionGeometrySource(source))
                return Corrupt();
            return source;
        }
        catch (const std::bad_alloc&)
        {
            return std::unexpected(
                error{error_code::out_of_memory, 0, "Collision geometry decoding allocation failed"});
        }
    }

  private:
    static result<CollisionGeometrySource> Corrupt()
    {
        return std::unexpected(error{error_code::invalid_argument, 0, "Corrupt collision geometry source"});
    }

    static void Put(std::vector<std::byte>& output, std::uint64_t value, std::size_t width)
    {
        for (std::size_t i = 0; i < width; ++i)
            output.push_back(std::byte((value >> (i * 8)) & 255));
    }

    static std::uint64_t Get(std::span<const std::byte> input, std::size_t& cursor, std::size_t width)
    {
        // All variable-size reads are bounded against the exact remaining payload before allocation.
        if (cursor > input.size() || width > input.size() - cursor)
            return 0;
        std::uint64_t value = 0;
        for (std::size_t i = 0; i < width; ++i)
            value |= std::uint64_t(std::to_integer<unsigned>(input[cursor++])) << (i * 8);
        return value;
    }

    static std::uint64_t Checksum(std::span<const std::byte> bytes)
    {
        std::uint64_t value = 14695981039346656037ull;
        for (const auto byte : bytes)
            value = (value ^ std::to_integer<unsigned>(byte)) * 1099511628211ull;
        return value;
    }
};
} // namespace ce::physics
