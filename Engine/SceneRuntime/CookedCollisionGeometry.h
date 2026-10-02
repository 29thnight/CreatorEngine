#pragma once

#include "CollisionGeometryCodec.h"
#include "../Physics/PhysicsScene.h"
#include <algorithm>

namespace ce::physics
{
struct cooked_geometry_record
{
    geometry_asset_key key;
    geometry_kind kind;
    std::uint64_t source_hash = 0;
    std::vector<std::byte> payload;
};

// CEPG v1 bundles immutable revisions under one CEMF asset UUID.
// Platform/cook-policy and SDK version are exact-match contracts.
class CookedCollisionGeometry final
{
  public:
    static constexpr std::size_t max_bytes = 256 * 1024 * 1024;
    static constexpr std::uint32_t format_version = 1;

    static std::uint64_t Hash(std::span<const std::byte> bytes)
    {
        std::uint64_t hash = 14695981039346656037ull;
        for (auto byte : bytes)
            hash = (hash ^ std::to_integer<unsigned>(byte)) * 1099511628211ull;
        return hash;
    }

    static result<cooked_geometry_record> Cook(PhysicsScene& cooker, const CollisionGeometrySource& source)
    {
        auto encoded = CollisionGeometryCodec::Encode(source);
        if (!encoded)
            return std::unexpected(encoded.error());

        return std::visit(
            [&](const auto& input) -> result<cooked_geometry_record> {
                using T = std::remove_cvref_t<decltype(input)>;
                PhysicsScene::geometry_cook_input request;
                geometry_kind kind;
                if constexpr (std::same_as<T, convex_source>)
                {
                    request = PhysicsScene::convex_cook_input{input.points};
                    kind = geometry_kind::convex;
                }
                else if constexpr (std::same_as<T, triangle_mesh_source>)
                {
                    request = PhysicsScene::triangle_cook_input{input.points, input.triangles};
                    kind = geometry_kind::triangle_mesh;
                }
                else
                {
                    request = heightfield_desc{input.rows, input.columns, input.heights};
                    kind = geometry_kind::heightfield;
                }
                return cooker.cook_geometry_blob(request).transform([&](auto&& payload) {
                    return cooked_geometry_record{source.key, kind, Hash(*encoded), std::move(payload)};
                });
            },
            source.form);
    }

    static result<std::vector<std::byte>> Encode(std::span<const cooked_geometry_record> records)
    {
        try
        {
            if (records.empty() || records.size() > 1024)
                return Invalid();

            std::size_t size = 44;
            std::uint64_t previous = 0;
            for (const auto& record : records)
            {
                if (record.key.asset.IsNil() || record.key.asset != records.front().key.asset ||
                    record.key.revision <= previous || record.payload.empty() || !record.source_hash ||
                    static_cast<unsigned>(record.kind) > 2 || record.payload.size() > max_bytes - 24 ||
                    size > max_bytes - 24 - record.payload.size())
                    return Invalid();

                previous = record.key.revision;
                size += 24 + record.payload.size();
            }

            std::vector<std::byte> bytes;
            bytes.reserve(size);
            Put(bytes, 0x47504543, 4); // CEPG
            Put(bytes, format_version, 4);
            Put(bytes, PhysicsScene::sdk_version(), 4);
            Put(bytes, 0x01000801, 4); // policy 1, Windows x64, little endian
            for (auto byte : records.front().key.asset.data)
                Put(bytes, byte, 1);
            Put(bytes, records.size(), 4);
            for (const auto& record : records)
            {
                Put(bytes, record.key.revision, 8);
                Put(bytes, static_cast<unsigned>(record.kind), 4);
                Put(bytes, record.source_hash, 8);
                Put(bytes, record.payload.size(), 4);
                bytes.insert(bytes.end(), record.payload.begin(), record.payload.end());
            }
            Put(bytes, Hash(bytes), 8);
            return bytes;
        }
        catch (const std::bad_alloc&)
        {
            return std::unexpected(error{error_code::out_of_memory, 0, "CEPG allocation failed"});
        }
    }

    static result<cooked_geometry_record> Decode(std::span<const std::byte> bytes, geometry_asset_key expected)
    {
        try
        {
            if (bytes.size() < 68 || bytes.size() > max_bytes || expected.asset.IsNil() || !expected.revision)
                return Invalid();

            std::size_t end = bytes.size() - 8;
            std::size_t checksumCursor = end;
            if (Get(bytes, checksumCursor, 8) != Hash(bytes.first(end)))
                return Invalid();

            std::size_t cursor = 0;
            if (Get(bytes, cursor, 4) != 0x47504543 || Get(bytes, cursor, 4) != format_version ||
                Get(bytes, cursor, 4) != PhysicsScene::sdk_version() || Get(bytes, cursor, 4) != 0x01000801)
                return Invalid();

            Uuid::Uuid16 id;
            for (auto& byte : id.data)
                byte = static_cast<std::uint8_t>(Get(bytes, cursor, 1));
            if (id != expected.asset)
                return Invalid();

            const auto count = Get(bytes, cursor, 4);
            if (!count || count > 1024)
                return Invalid();

            std::uint64_t previous = 0, sourceHash = 0;
            geometry_kind selectedKind{};
            std::span<const std::byte> selected;
            for (std::uint64_t index = 0; index < count; ++index)
            {
                if (cursor > end || end - cursor < 24)
                    return Invalid();

                const auto revision = Get(bytes, cursor, 8);
                const auto kind = Get(bytes, cursor, 4);
                const auto hash = Get(bytes, cursor, 8);
                const auto length = Get(bytes, cursor, 4);
                if (revision <= previous || kind > 2 || !hash || !length || length > end - cursor)
                    return Invalid();

                previous = revision;
                if (revision == expected.revision)
                {
                    selectedKind = static_cast<geometry_kind>(kind);
                    sourceHash = hash;
                    selected = bytes.subspan(cursor, static_cast<std::size_t>(length));
                }
                cursor += static_cast<std::size_t>(length);
            }
            if (cursor != end || selected.empty())
                return Invalid();

            return cooked_geometry_record{expected, selectedKind, sourceHash, {selected.begin(), selected.end()}};
        }
        catch (const std::bad_alloc&)
        {
            return std::unexpected(error{error_code::out_of_memory, 0, "CEPG decode allocation failed"});
        }
    }

  private:
    static std::unexpected<error> Invalid()
    {
        return std::unexpected(
            error{error_code::invalid_argument, 0, "Invalid or incompatible cooked geometry artifact"});
    }
    static void Put(std::vector<std::byte>& bytes, std::uint64_t value, unsigned count)
    {
        for (unsigned index = 0; index < count; ++index)
            bytes.push_back(std::byte((value >> (index * 8)) & 255));
    }
    static std::uint64_t Get(std::span<const std::byte> bytes, std::size_t& cursor, unsigned count)
    {
        std::uint64_t value = 0;
        for (unsigned index = 0; index < count; ++index)
            value |= std::uint64_t(std::to_integer<unsigned>(bytes[cursor++])) << (index * 8);
        return value;
    }
};
} // namespace ce::physics
