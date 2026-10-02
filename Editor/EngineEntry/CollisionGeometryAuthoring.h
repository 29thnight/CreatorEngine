#pragma once

#include "../../Engine/SceneRuntime/CollisionGeometryCodec.h"
#include "../../Engine/SceneRuntime/CollisionGeometryIO.h"
#include <Windows.h>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <limits>

namespace Editor
{
inline ce::physics::result<ce::physics::CollisionGeometrySource> ReadCollisionGeometrySource(
    const std::filesystem::path& path)
{
    using namespace ce::physics;
    try
    {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        const auto size = input ? input.tellg() : std::streampos{-1};
        if (size <= 0 || size > static_cast<std::streamoff>(CollisionGeometryCodec::max_bytes))
            return std::unexpected(error{error_code::invalid_argument, 0, "Invalid geometry source file size"});

        std::vector<std::byte> bytes(static_cast<std::size_t>(size));
        input.seekg(0);
        if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
            return std::unexpected(error{error_code::invalid_argument, 0, "Geometry source read failed"});

        return CollisionGeometryCodec::Decode(bytes);
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(error{error_code::out_of_memory, 0, "Geometry source allocation failed"});
    }
}

// Text interchange is an Editor input only. Native runtime input remains CECG.
// convex: point-count, xyz...; mesh: point-count, xyz..., triangle-count, indices...;
// heightfield: rows, columns, signed-16 samples... (whitespace separated).
inline ce::physics::result<ce::physics::CollisionGeometrySource> ParseCollisionGeometry(
    std::string_view text, std::string_view kind, ce::physics::geometry_asset_key key)
{
    using namespace ce::physics;
    auto remaining = text;
    const auto next = [&]<class T>(T& value) {
        const auto first = remaining.find_first_not_of(" \t\r\n");
        if (first == std::string_view::npos)
            return false;

        remaining.remove_prefix(first);
        const auto end = remaining.find_first_of(" \t\r\n");
        const auto token = remaining.substr(0, end);
        const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value);
        remaining.remove_prefix(token.size());
        return parsed.ec == std::errc{} && parsed.ptr == token.data() + token.size();
    };
    const auto invalid = [] {
        return std::unexpected(error{error_code::invalid_argument, 0, "Invalid collision geometry text input"});
    };

    if (text.size() > CollisionGeometryCodec::max_bytes)
        return invalid();

    try
    {
        CollisionGeometrySource source{key, convex_source{}};
        if (kind == "heightfield")
        {
            heightfield_source input;
            if (!next(input.rows) || !next(input.columns) || input.rows < 2 || input.columns < 2 ||
                std::uint64_t(input.rows) * input.columns > 16 * 1024 * 1024)
                return invalid();

            input.heights.resize(std::size_t(input.rows) * input.columns);
            if (!std::ranges::all_of(input.heights, [&](auto& height) { return next(height); }))
                return invalid();

            source.form = std::move(input);
        }
        else if (kind == "convex" || kind == "mesh")
        {
            std::uint32_t count = 0;
            if (!next(count) || count < (kind == "convex" ? 4u : 3u) || count > 1024 * 1024)
                return invalid();

            std::vector<math::vector3> points(count);
            if (!std::ranges::all_of(points,
                                     [&](auto& point) { return next(point.x) && next(point.y) && next(point.z); }))
                return invalid();

            if (kind == "convex")
                source.form = convex_source{std::move(points)};
            else
            {
                if (!next(count) || !count || count > 4 * 1024 * 1024)
                    return invalid();

                std::vector<triangle_indices> triangles(count);
                if (!std::ranges::all_of(triangles, [&](auto& triangle) {
                        return next(triangle.a) && next(triangle.b) && next(triangle.c);
                    }))
                    return invalid();

                source.form = triangle_mesh_source{std::move(points), std::move(triangles)};
            }
        }
        else
            return invalid();

        if (remaining.find_first_not_of(" \t\r\n") != std::string_view::npos)
            return invalid();

        return ValidateCollisionGeometrySource(source).transform([&] { return std::move(source); });
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(error{error_code::out_of_memory, 0, "Geometry input allocation failed"});
    }
}

class CollisionGeometryRevisionEdit final
{
  public:
    CollisionGeometryRevisionEdit(ce::physics::CollisionGeometrySource before,
                                  ce::physics::CollisionGeometrySource after)
        : m_before(std::move(before)), m_after(std::move(after))
    {
    }

    template<class Publisher>
    auto Undo(Publisher&& publisher) const
    {
        return std::invoke(std::forward<Publisher>(publisher), m_after, m_before);
    }

    template<class Publisher>
    auto Redo(Publisher&& publisher) const
    {
        return std::invoke(std::forward<Publisher>(publisher), m_before, m_after);
    }

  private:
    ce::physics::CollisionGeometrySource m_before, m_after;
};

class CollisionGeometryAuthoring final
{
  public:
    static bool Move(const std::filesystem::path& source, const std::filesystem::path& destination)
    {
        return ::MoveFileExW(source.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH) != 0;
    }

    static ce::physics::result<std::uint64_t> NextRevision(const std::filesystem::path& root,
                                                           ce::physics::geometry_asset_key current)
    {
        using namespace ce::physics;
        try
        {
            auto maximum = current.revision;
            const auto directory = CollisionGeometryIO::RevisionPath(root, current).parent_path();
            if (std::filesystem::exists(directory))
                for (const auto& file : std::filesystem::directory_iterator(directory))
                {
                    if (file.path().extension() != ".cegeometry")
                        continue;

                    const auto source = ReadCollisionGeometrySource(file.path());
                    if (!source || source->key.asset != current.asset ||
                        file.path().stem().string() != std::to_string(source->key.revision))
                        return std::unexpected(
                            error{error_code::invalid_argument, 0, "Corrupt geometry revision archive"});

                    maximum = (std::max)(maximum, source->key.revision);
                }

            if (maximum == (std::numeric_limits<std::uint64_t>::max)())
                return std::unexpected(error{error_code::invalid_argument, 0, "Geometry revision exhausted"});

            return maximum + 1;
        }
        catch (...)
        {
            return std::unexpected(error{error_code::invalid_argument, 0, "Cannot read geometry revision archive"});
        }
    }

    // Expected contents prevent stale edits/Undo from overwriting newer work.
    // Archives are immutable and retained even when live publication fails.
    template<class Mover = decltype(&Move)>
    static ce::physics::result<void> Replace(const std::filesystem::path& root,
                                             const std::filesystem::path& destination,
                                             const ce::physics::CollisionGeometrySource& expected,
                                             const ce::physics::CollisionGeometrySource& replacement,
                                             std::string_view metadata, Mover mover = &Move)
    {
        using namespace ce::physics;
        try
        {
            const auto canonicalRoot = std::filesystem::canonical(root);
            const auto target = std::filesystem::canonical(destination);
            const auto relative = target.lexically_relative(canonicalRoot);
            if (relative.empty() || relative.is_absolute() || *relative.begin() == ".." ||
                target.extension() != ".cegeometry" || expected.key.asset != replacement.key.asset ||
                expected.key.revision == replacement.key.revision || metadata.empty())
                return std::unexpected(error{error_code::invalid_argument, 0, "Invalid geometry replacement"});

            const auto before = CollisionGeometryCodec::Encode(expected);
            const auto after = CollisionGeometryCodec::Encode(replacement);
            const auto current = ReadCollisionGeometrySource(target);
            const auto actual = current ? CollisionGeometryCodec::Encode(*current)
                                        : result<std::vector<std::byte>>{std::unexpected(
                                              error{error_code::invalid_argument, 0, "Missing current geometry"})};
            if (!before || !after || !actual || *before != *actual)
                return std::unexpected(error{error_code::invalid_argument, 0, "Stale geometry edit"});

            const auto write = [](const auto& path, std::span<const std::byte> bytes) {
                std::ofstream output(path, std::ios::binary);
                output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
                output.flush();
                const bool success = output.good();
                output.close();
                return success && !output.fail();
            };
            const auto archive = [&](const auto& source, const auto& bytes) {
                const auto path = CollisionGeometryIO::RevisionPath(canonicalRoot, source.key);
                const auto parent = std::filesystem::weakly_canonical(path.parent_path()).lexically_relative(canonicalRoot);
                if (parent.empty() || parent.is_absolute() || *parent.begin() == "..")
                    return false;

                std::filesystem::create_directories(path.parent_path());

                if (std::filesystem::exists(path))
                {
                    const auto old = ReadCollisionGeometrySource(path);
                    const auto oldBytes = old ? CollisionGeometryCodec::Encode(*old)
                                              : result<std::vector<std::byte>>{std::unexpected(
                                                    error{error_code::invalid_argument, 0, "Corrupt revision"})};
                    return oldBytes && *oldBytes == bytes;
                }

                const auto stage = std::filesystem::path(path.wstring() + L".tmp");
                if (std::filesystem::exists(std::filesystem::symlink_status(stage)))
                    return false;

                const bool success = write(stage, bytes) && Move(stage, path);
                std::error_code ignored;
                std::filesystem::remove(stage, ignored);
                return success;
            };
            if (!archive(expected, *before) || !archive(replacement, *after))
                return std::unexpected(error{error_code::invalid_argument, 0, "Geometry revision preservation failed"});

            const auto meta = std::filesystem::path(target.wstring() + L".meta");
            const auto stage = std::filesystem::path(target.wstring() + L".geometry-update.tmp");
            const auto metaStage = std::filesystem::path(meta.wstring() + L".geometry-update.tmp");
            const auto backup = std::filesystem::path(target.wstring() + L".geometry-backup.tmp");
            const auto metaBackup = std::filesystem::path(meta.wstring() + L".geometry-backup.tmp");
            if (!std::filesystem::is_regular_file(meta))
                return std::unexpected(error{error_code::invalid_argument, 0, "Geometry sidecar missing"});

            for (const auto& path : {stage, metaStage, backup, metaBackup})
                if (std::filesystem::exists(std::filesystem::symlink_status(path)))
                    return std::unexpected(
                        error{error_code::invalid_argument, 0, "Pending geometry transaction requires recovery"});

            struct transaction
            {
                std::filesystem::path target, meta, stage, metaStage, backup, metaBackup;
                bool sourceSaved = false, metaSaved = false, committed = false;
                ~transaction()
                {
                    std::error_code ignored;
                    if (!committed)
                    {
                        if (sourceSaved)
                            ::MoveFileExW(backup.c_str(), target.c_str(),
                                          MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
                        if (metaSaved)
                            ::MoveFileExW(metaBackup.c_str(), meta.c_str(),
                                          MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
                    }
                    else
                    {
                        std::filesystem::remove(backup, ignored);
                        std::filesystem::remove(metaBackup, ignored);
                    }
                    std::filesystem::remove(stage, ignored);
                    std::filesystem::remove(metaStage, ignored);
                }
            } rollback{target, meta, stage, metaStage, backup, metaBackup};

            if (!write(stage, *after) || !write(metaStage, std::as_bytes(std::span{metadata.data(), metadata.size()})))
                return std::unexpected(error{error_code::invalid_argument, 0, "Geometry update staging failed"});

            if (!std::invoke(mover, meta, metaBackup))
                return std::unexpected(error{error_code::invalid_argument, 0, "Geometry sidecar backup failed"});
            rollback.metaSaved = true;

            if (!std::invoke(mover, target, backup))
                return std::unexpected(error{error_code::invalid_argument, 0, "Geometry source backup failed"});
            rollback.sourceSaved = true;

            if (!std::invoke(mover, metaStage, meta) || !std::invoke(mover, stage, target))
                return std::unexpected(
                    error{error_code::invalid_argument, 0, "Geometry replacement publication failed"});

            rollback.committed = true;
            return {};
        }
        catch (const std::bad_alloc&)
        {
            return std::unexpected(error{error_code::out_of_memory, 0, "Geometry revision allocation failed"});
        }
        catch (...)
        {
            return std::unexpected(error{error_code::invalid_argument, 0, "Geometry revision transaction failed"});
        }
    }

    // Caller serializes watcher/catalog access. New assets only: never overwrite
    // an existing source, sidecar, or staging file. Cook must precede this call.
    template<class Mover = decltype(&Move)>
    static ce::physics::result<void> Create(const std::filesystem::path& root, const std::filesystem::path& destination,
                                            const ce::physics::CollisionGeometrySource& source, Mover mover = &Move)
    {
        using namespace ce::physics;
        auto encoded = CollisionGeometryCodec::Encode(source);
        if (!encoded)
            return std::unexpected(encoded.error());

        try
        {
            const auto canonicalRoot = std::filesystem::canonical(root);
            const auto target = std::filesystem::canonical(destination.parent_path()) / destination.filename();
            const auto relative = target.lexically_relative(canonicalRoot);
            if (relative.empty() || relative.is_absolute() || *relative.begin() == ".." ||
                target.extension() != ".cegeometry" || source.key.revision != 1)
                return std::unexpected(error{error_code::invalid_argument, 0, "Invalid new geometry destination"});

            const auto meta = std::filesystem::path(target.wstring() + L".meta");
            const auto stage = std::filesystem::path(target.wstring() + L".geometry.tmp");
            const auto metaStage = std::filesystem::path(meta.wstring() + L".geometry.tmp");
            const auto present = [](const auto& path) {
                return std::filesystem::exists(std::filesystem::symlink_status(path));
            };
            if (present(target) || present(meta) || present(stage) || present(metaStage))
                return std::unexpected(error{error_code::invalid_argument, 0, "Geometry destination already exists"});

            struct rollback
            {
                std::filesystem::path stage, metaStage, meta;
                bool metaPublished = false, committed = false;
                ~rollback()
                {
                    std::error_code ignored;
                    std::filesystem::remove(stage, ignored);
                    std::filesystem::remove(metaStage, ignored);
                    if (metaPublished && !committed)
                        std::filesystem::remove(meta, ignored);
                }
            } cleanup{stage, metaStage, meta};

            const auto write = [](const auto& path, std::span<const std::byte> bytes) {
                std::ofstream output(path, std::ios::binary);
                output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
                output.flush();
                const bool success = output.good();
                output.close();
                return success && !output.fail();
            };
            const auto metadata = "guid: " + Uuid::ToString(source.key.asset) + "\ngeometryRevision: 1\n";
            if (!write(stage, *encoded) ||
                !write(metaStage, std::as_bytes(std::span{metadata.data(), metadata.size()})) ||
                !std::invoke(mover, metaStage, meta))
                return std::unexpected(
                    error{error_code::invalid_argument, 0, "Geometry staging/meta publication failed"});

            cleanup.metaPublished = true;
            if (!std::invoke(mover, stage, target))
                return std::unexpected(error{error_code::invalid_argument, 0, "Geometry source publication failed"});

            cleanup.committed = true;
            return {};
        }
        catch (const std::bad_alloc&)
        {
            return std::unexpected(error{error_code::out_of_memory, 0, "Geometry publication allocation failed"});
        }
        catch (...)
        {
            return std::unexpected(error{error_code::invalid_argument, 0, "Geometry publication failed"});
        }
    }
};
} // namespace Editor
