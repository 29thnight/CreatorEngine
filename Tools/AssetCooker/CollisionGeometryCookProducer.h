#pragma once

#include "../../Engine/SceneRuntime/CookedCollisionGeometry.h"
#include "../../Engine/SceneRuntime/CollisionGeometryIO.h"
#include "../../Engine/RenderEngine/Experiment/Cooked/CookSupport.h"
#include "../../Engine/RenderEngine/Experiment/Cooked/CookedAssetManifest.h"
#include <flat_map>
#include <charconv>
#include <fstream>

namespace geometry_cook
{
struct Product
{
    experiment::cooked::CookedAssetManifestEntry entry;
    std::vector<std::byte> bytes;
    std::vector<std::uint64_t> revisions;
};

inline ce::physics::result<Product> Build(const std::filesystem::path& root, const std::filesystem::path& sourcePath)
{
    using namespace ce::physics;
    namespace ck = experiment::cooked;
    try
    {
        experiment::AssetId id;
        std::string failure;
        if (!ck::ReadMetaAssetId(sourcePath.string() + ".meta", id, failure))
            return std::unexpected(error{error_code::invalid_argument, 0, "Geometry sidecar invalid"});

        std::ifstream input(sourcePath, std::ios::binary | std::ios::ate);
        const auto size = input ? input.tellg() : std::streampos{-1};
        if (size <= 0 || size > static_cast<std::streamoff>(CollisionGeometryCodec::max_bytes))
            return std::unexpected(error{error_code::invalid_argument, 0, "Geometry source size invalid"});

        std::vector<std::byte> bytes(static_cast<std::size_t>(size));
        input.seekg(0);
        if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
            return std::unexpected(error{error_code::invalid_argument, 0, "Geometry source read failed"});

        auto current = CollisionGeometryCodec::Decode(bytes);
        if (!current || current->key.asset != id.value)
            return std::unexpected(error{error_code::invalid_argument, 0, "Geometry source/meta UUID mismatch"});

        std::flat_map<std::uint64_t, CollisionGeometrySource> sources;
        sources.emplace(current->key.revision, *current);
        const auto directory = CollisionGeometryIO::RevisionPath(root, current->key).parent_path();
        if (std::filesystem::exists(directory))
            for (const auto& file : std::filesystem::directory_iterator(directory))
            {
                if (file.path().extension() != ".cegeometry")
                    continue;

                std::uint64_t revision = 0;
                const auto stem = file.path().stem().string();
                const auto parsed = std::from_chars(stem.data(), stem.data() + stem.size(), revision);
                if (parsed.ec != std::errc{} || parsed.ptr != stem.data() + stem.size() || !revision ||
                    sources.size() >= 1024)
                    return std::unexpected(error{error_code::invalid_argument, 0, "Invalid geometry revision archive"});

                auto archived = CollisionGeometryIO::Read(file.path(), {id.value, revision});
                if (!archived)
                    return std::unexpected(archived.error());

                if (const auto found = sources.find(revision); found != sources.end())
                {
                    auto original = CollisionGeometryCodec::Encode(found->second);
                    auto archivedBytes = CollisionGeometryCodec::Encode(*archived);
                    if (!original || !archivedBytes || *original != *archivedBytes)
                        return std::unexpected(
                            error{error_code::invalid_argument, 0, "Geometry revision content conflict"});
                }
                else
                    sources.emplace(revision, std::move(*archived));
            }

        scene_config config;
        config.workers = 1;
        auto cooker = PhysicsScene::create(config);
        if (!cooker)
            return std::unexpected(cooker.error());

        Product product;
        std::vector<cooked_geometry_record> records;
        for (const auto& [revision, source] : sources)
        {
            auto cooked = CookedCollisionGeometry::Cook(**cooker, source);
            if (!cooked)
                return std::unexpected(cooked.error());

            // Verify SDK import while still in unpublished cooker staging.
            if (!(*cooker)->load_geometry_blob(cooked->kind, cooked->payload))
                return std::unexpected(error{error_code::invalid_argument, 0, "Cooked geometry roundtrip failed"});

            product.revisions.push_back(revision);
            records.push_back(std::move(*cooked));
        }
        auto encoded = CookedCollisionGeometry::Encode(records);
        if (!encoded)
            return std::unexpected(encoded.error());

        product.bytes = std::move(*encoded);
        product.entry.assetId = id;
        product.entry.kind = ck::CookedAssetKind::CollisionGeometry;
        product.entry.formatVersion = CookedCollisionGeometry::format_version;
        const auto uuid = Uuid::ToString(id.value);
        product.entry.artifactPath = "Derived/CollisionGeometry/" + uuid.substr(0, 2) + "/" + uuid + ".cepg";
        product.entry.byteSize = product.bytes.size();
        if (!ck::ComputeSha256(product.bytes, product.entry.contentSha256, failure))
            return std::unexpected(error{error_code::invalid_argument, 0, "Cooked geometry hashing failed"});

        return product;
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(
            ce::physics::error{ce::physics::error_code::out_of_memory, 0, "Geometry producer allocation failed"});
    }
    catch (...)
    {
        return std::unexpected(
            ce::physics::error{ce::physics::error_code::invalid_argument, 0, "Geometry producer failed"});
    }
}
} // namespace geometry_cook
