#pragma once

#include "CollisionGeometryCodec.h"
#include <filesystem>
#include <fstream>

namespace ce::physics
{
class CollisionGeometryIO final
{
  public:
    static constexpr const char* extension = ".cegeometry";

    static std::filesystem::path RevisionPath(const std::filesystem::path& assetRoot, geometry_asset_key key)
    {
        return assetRoot / "Derived" / "CollisionGeometry" / Uuid::ToString(key.asset) /
               (std::to_string(key.revision) + extension);
    }

    static result<CollisionGeometrySource> Read(const std::filesystem::path& path, geometry_asset_key expected)
    {
        try
        {
            if (path.extension() != extension)
                return std::unexpected(
                    error{error_code::invalid_argument, 0, "Expected native collision geometry asset"});

            std::ifstream input(path, std::ios::binary | std::ios::ate);
            const auto size = input ? input.tellg() : std::streampos{-1};
            if (size <= 0 || size > static_cast<std::streamoff>(CollisionGeometryCodec::max_bytes))
                return std::unexpected(
                    error{error_code::invalid_argument, 0, "Collision geometry file missing or too large"});

            std::vector<std::byte> bytes(static_cast<std::size_t>(size));
            input.seekg(0);
            if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
                return std::unexpected(error{error_code::invalid_argument, 0, "Collision geometry file read failed"});

            return CollisionGeometryCodec::Decode(bytes).and_then(
                [&](auto&& source) -> result<CollisionGeometrySource> {
                    if (source.key != expected)
                        return std::unexpected(error{error_code::invalid_argument, 0,
                                                     "Collision geometry file identity/revision mismatch"});
                    return std::move(source);
                });
        }
        catch (const std::bad_alloc&)
        {
            return std::unexpected(error{error_code::out_of_memory, 0, "Collision geometry file allocation failed"});
        }
        catch (const std::filesystem::filesystem_error&)
        {
            return std::unexpected(error{error_code::invalid_argument, 0, "Collision geometry path failed"});
        }
    }
};
} // namespace ce::physics
