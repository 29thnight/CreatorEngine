#include "../../Engine/SceneRuntime/CollisionGeometryLibrary.h"
#include "../../Engine/RenderEngine/Experiment/Cooked/CookedAssetCatalog.h"
#include "../../Engine/RenderEngine/Experiment/Cooked/PakAudioClipByteSource.h"
#include "../AssetCooker/CollisionGeometryCookProducer.h"
#include <fstream>
#include <iostream>
#include <source_location>

int main(int argc, char** argv)
{
    using namespace ce::physics;
    namespace ck = experiment::cooked;
    std::size_t checks = 0;
    const auto check = [&](bool value, std::source_location location = std::source_location::current()) {
        ++checks;
        if (!value)
            throw std::runtime_error("Cooked check " + std::to_string(checks) + " at line " +
                                     std::to_string(location.line()));
    };
    try
    {
        if (argc != 2 && argc != 3)
            return 2;
        const std::filesystem::path root(argv[1]);
        const auto uuid = Uuid::Parse("91ea9b44-13a6-4aec-99ec-0963eef7eaa1");
        auto cooker = PhysicsScene::create();
        check(bool(cooker));
        // Small physical objects must keep their authored metre units while cooking.
        for (const float scale : {0.01f, 0.1f, 1.f})
        {
            const std::array<math::vector3, 4> points{{{0,0,0}, {scale,0,0}, {0,scale,0}, {0,0,scale}}};
            check(bool((*cooker)->cook_convex(points)));
            auto blob = (*cooker)->cook_geometry_blob(PhysicsScene::convex_cook_input{points});
            check(bool(blob));
            check(bool((*cooker)->load_geometry_blob(geometry_kind::convex, *blob)));
        }
        const std::array<math::vector3, 4> line{{{0,0,0}, {.01f,0,0}, {.02f,0,0}, {.03f,0,0}}};
        check(!(*cooker)->cook_convex(line));
        check(!(*cooker)->cook_geometry_blob(PhysicsScene::convex_cook_input{line}));

        if (argc == 3)
        {
            std::ifstream input(argv[2]);
            std::size_t count = 0;
            check(bool(input >> count) && count >= 4 && count <= 1024*1024);
            std::vector<math::vector3> points(count);
            for (auto& point : points) if (!(input >> point.x >> point.y >> point.z))
                throw std::runtime_error("Incomplete real-asset convex input");
            check(bool((*cooker)->cook_convex(points)));
            auto blob = (*cooker)->cook_geometry_blob(PhysicsScene::convex_cook_input{points});
            check(bool(blob));
            check(bool((*cooker)->load_geometry_blob(geometry_kind::convex, *blob)));
        }

        std::array<CollisionGeometrySource, 3> sources{
            CollisionGeometrySource{{uuid, 1}, convex_source{{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}}}},
            CollisionGeometrySource{{uuid, 2}, triangle_mesh_source{{{0, 0, 0}, {0, 0, 1}, {1, 0, 0}}, {{0, 1, 2}}}},
            CollisionGeometrySource{{uuid, 3}, heightfield_source{2, 2, {0, 0, 0, 0}}}};
        std::vector<cooked_geometry_record> records;
        for (const auto& source : sources)
        {
            auto record = CookedCollisionGeometry::Cook(**cooker, source);
            check(bool(record));
            records.push_back(std::move(*record));
        }
        auto bundle = CookedCollisionGeometry::Encode(records);
        check(bool(bundle));
        cooker->reset();
        for (std::size_t index = 0; index < bundle->size(); ++index)
        {
            auto corrupt = *bundle;
            corrupt[index] ^= std::byte{1};
            check(!CookedCollisionGeometry::Decode(corrupt, {uuid, 1}));
            check(!CookedCollisionGeometry::Decode(std::span(*bundle).first(index), {uuid, 1}));
        }
        check(!CookedCollisionGeometry::Decode(*bundle, {uuid, 4}));
        auto incompatible = *bundle;
        incompatible[8] ^= std::byte{1};
        auto checksum = CookedCollisionGeometry::Hash(std::span(incompatible).first(incompatible.size() - 8));
        for (unsigned i = 0; i < 8; ++i)
            incompatible[incompatible.size() - 8 + i] = std::byte((checksum >> (i * 8)) & 255);
        check(!CookedCollisionGeometry::Decode(incompatible, {uuid, 1}));

        for (const auto offset : {12u, 32u, 36u, 44u, 48u, 56u})
        {
            auto malformed = *bundle;
            const unsigned width = offset == 36u || offset == 48u ? 8 : 4;
            for (unsigned index = 0; index < width; ++index)
                malformed[offset + index] = std::byte{};
            if (offset == 44u)
                malformed[offset] = std::byte{3}; // Unknown geometry kind.
            const auto digest = CookedCollisionGeometry::Hash(std::span(malformed).first(malformed.size() - 8));
            for (unsigned index = 0; index < 8; ++index)
                malformed[malformed.size() - 8 + index] = std::byte((digest >> (index * 8)) & 255);
            check(!CookedCollisionGeometry::Decode(malformed, {uuid, 1}));
        }

        const auto sourceRoot = root / "authoring";
        const auto sourcePath = sourceRoot / "shape.cegeometry";
        const auto writeSource = [&](const auto& source, const auto& target) {
            auto encoded = CollisionGeometryCodec::Encode(source);
            check(bool(encoded));
            std::filesystem::create_directories(target.parent_path());
            std::ofstream file(target, std::ios::binary);
            file.write(reinterpret_cast<const char*>(encoded->data()), encoded->size());
            check(bool(file));
        };
        writeSource(sources.back(), sourcePath);
        {
            std::ofstream meta(sourcePath.string() + ".meta");
            meta << "guid: " << Uuid::ToString(uuid) << "\ngeometryRevision: 3\n";
        }
        for (const auto& source : sources)
            writeSource(source, CollisionGeometryIO::RevisionPath(sourceRoot, source.key));
        auto product = geometry_cook::Build(sourceRoot, sourcePath);
        check(product && product->revisions == std::vector<std::uint64_t>{1, 2, 3});
        // A separately cooked SDK blob is not the identity contract. Verify the
        // producer's revision/source identity, then import its actual output below.
        for (const auto& original : records)
        {
            auto produced = CookedCollisionGeometry::Decode(product->bytes, original.key);
            check(produced && produced->kind == original.kind);
            check(produced && produced->source_hash == original.source_hash);
            check(produced && !produced->payload.empty());
        }
        *bundle = product->bytes;
        {
            std::ofstream meta(sourcePath.string() + ".meta");
            meta << "guid: 11111111-1111-4111-8111-111111111111\n";
        }
        check(!geometry_cook::Build(sourceRoot, sourcePath));
        {
            std::ofstream meta(sourcePath.string() + ".meta");
            meta << "guid: " << Uuid::ToString(uuid) << "\n";
        }
        {
            std::ofstream bad(CollisionGeometryIO::RevisionPath(sourceRoot, sources.front().key), std::ios::binary);
            bad << "corrupt";
        }
        check(!geometry_cook::Build(sourceRoot, sourcePath));
        std::filesystem::remove_all(sourceRoot);

        const std::string path = "Derived/CollisionGeometry/91/" + Uuid::ToString(uuid) + ".cepg";
        ck::CookedAssetManifest manifest;
        ck::CookedAssetManifestEntry entry;
        entry.assetId = {uuid};
        entry.kind = ck::CookedAssetKind::CollisionGeometry;
        entry.formatVersion = 1;
        entry.byteSize = bundle->size();
        entry.artifactPath = path;
        std::string failure;
        check(ck::ComputeSha256(*bundle, entry.contentSha256, failure));
        manifest.entries.push_back(entry);
        const auto encodedManifest = ck::WriteAssetManifest(manifest);
        check(encodedManifest.Succeeded());
        std::vector<ck::AssetManifestIssue> issues;
        const auto catalog = ck::CookedAssetCatalog::Load(encodedManifest.bytes, root, issues);
        check(issues.empty() && catalog.Size() == 1);
        std::filesystem::create_directories((root / path).parent_path());
        {
            std::ofstream file(root / path, std::ios::binary);
            file.write(reinterpret_cast<const char*>(bundle->data()), bundle->size());
        }
        ck::LooseArtifactByteSource loose(root);
        std::vector<std::byte> loaded;
        check(catalog.ReadCollisionGeometry({uuid}, loose, loaded, failure) && loaded == *bundle);

        {
            auto corrupt = *bundle;
            corrupt[60] ^= std::byte{1};
            std::ofstream file(root / path, std::ios::binary);
            file.write(reinterpret_cast<const char*>(corrupt.data()), corrupt.size());
        }
        check(!catalog.ReadCollisionGeometry({uuid}, loose, loaded, failure) && loaded.empty());
        {
            std::ofstream file(root / path, std::ios::binary);
            file.write(reinterpret_cast<const char*>(bundle->data()), bundle->size());
        }

        const auto pakPath = root / "geometry.pak";
        {
            Pak::BuildOptions options;
            options.chunkSize = 97;
            Pak::Builder builder(pakPath, options);
            builder.addFile("Assets/" + path, root / path);
            builder.finish();
        }
        auto archive = std::make_shared<Pak::Archive>(pakPath);
        ck::PakAudioClipByteSource pak(archive);
        check(catalog.ReadCollisionGeometry({uuid}, pak, loaded, failure) && loaded == *bundle);
        std::filesystem::remove(root / path); // The mounted path has no loose artifact/source fallback.
        bool gpu = false;
        for (auto execution : {execution_preference::cpu, execution_preference::prefer_gpu})
        {
            scene_config config;
            config.execution = execution;
            config.workers = 2;
            auto simulation = PhysicsScene::create(config);
            check(bool(simulation));
            gpu = gpu || (*simulation)->status().backend == execution_backend::gpu;
            CollisionGeometryLibrary library;
            for (const auto& source : sources)
            {
                const auto resolve = [&](auto key) -> result<std::vector<std::byte>> {
                    std::vector<std::byte> bytes;
                    if (!catalog.ReadCollisionGeometry({key.asset}, pak, bytes, failure))
                        return std::unexpected(error{error_code::invalid_argument, 0, "CEMF read failed"});
                    return bytes;
                };
                auto asset = library.ResolveCooked(source.key, resolve);
                check(bool(asset));
                check(library.ResolveCooked(source.key, resolve).value() == *asset);
                ShapeInstance shape;
                shape.id = shape_id{1};
                shape.form = cooked_geometry{*asset};
                body_desc body;
                body.shapes = std::span(&shape, 1);
                auto handle = (*simulation)->create_body(body);
                check(bool(handle));
                std::array<query_hit, 4> hits;
                const auto ray = (*simulation)->raycast({.2f, 4, .2f}, {0, -1, 0}, 10, hits);
                check(ray && ray->written == 1);
                check(bool((*simulation)->destroy_body(*handle)));
            }
            const auto before = library.Stats().value();
            check(!library.ResolveCooked({uuid, 4}, [](auto) -> result<std::vector<std::byte>> {
                return std::unexpected(error{error_code::invalid_argument, 0, "Missing artifact"});
            }));
            const auto after = library.Stats().value();
            check(after.assets == before.assets && after.imports == before.imports);
            check(library.Stats()->cooks == 0 && library.Stats()->imports == 3);
        }
        std::cout << "{\"result\":\"PHYSICS_COOKED_GEOMETRY_OK\",\"checks\":" << checks
                  << ",\"gpu_verified\":" << (gpu ? "true" : "false") << "}\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
