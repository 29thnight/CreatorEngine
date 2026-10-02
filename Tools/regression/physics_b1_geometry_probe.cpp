#include "../../Engine/SceneRuntime/CollisionGeometryLibrary.h"
#include "../../Engine/SceneRuntime/CollisionGeometryIO.h"
#include "../../Engine/SceneRuntime/PhysicsShapeDefinition.h"
#include "../../Engine/SceneRuntime/ScenePhysicsSimulation.h"
#if !CE_SHIPPING
#include "../../Engine/EngineDiagnostics/ProfileCaptureFile.h"
#endif
#include <array>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <stdexcept>

using namespace ce::physics;

namespace
{
int checks = 0;

void check(bool value, const char* label)
{
    ++checks;
    if (!value)
    {
        std::cerr << "geometry check failed: " << label << '\n';
        std::exit(1);
    }
}

CollisionGeometrySource convex(std::uint64_t revision = 1)
{
    return {{Uuid::Parse("550e8400-e29b-41d4-a716-446655440000"), revision},
            convex_source{
                {{-1, -1, -1}, {1, -1, -1}, {-1, 1, -1}, {1, 1, -1}, {-1, -1, 1}, {1, -1, 1}, {-1, 1, 1}, {1, 1, 1}}}};
}

CollisionGeometrySource mesh()
{
    return {{Uuid::Parse("550e8400-e29b-41d4-a716-446655440001"), 1},
            triangle_mesh_source{{{0, 0, 0}, {0, 0, 2}, {2, 0, 0}, {2, 0, 2}}, {{0, 1, 2}, {2, 1, 3}}}};
}

CollisionGeometrySource heightfield()
{
    return {{Uuid::Parse("550e8400-e29b-41d4-a716-446655440002"), 1}, heightfield_source{2, 2, {0, 1, 2, 0}}};
}

void seal(std::vector<std::byte>& bytes)
{
    std::uint64_t checksum = 14695981039346656037ull;
    for (const auto byte : std::span(bytes).first(bytes.size() - 8))
        checksum = (checksum ^ std::to_integer<unsigned>(byte)) * 1099511628211ull;
    for (std::size_t i = 0; i < 8; ++i)
        bytes[bytes.size() - 8 + i] = std::byte((checksum >> (i * 8)) & 255);
}

void codec_and_io(const std::filesystem::path& path)
{
    for (const auto& source : {convex(), mesh(), heightfield()})
    {
        const auto encoded = CollisionGeometryCodec::Encode(source);
        check(bool(encoded), "source encoded");
        const auto decoded = CollisionGeometryCodec::Decode(*encoded);
        const auto encodedAgain =
            decoded.and_then([](const auto& value) { return CollisionGeometryCodec::Encode(value); });
        check(encodedAgain && *encodedAgain == *encoded, "canonical source roundtrip");
        for (std::size_t offset = 0; offset < encoded->size(); ++offset)
        {
            auto corrupt = *encoded;
            corrupt[offset] ^= std::byte{1};
            check(!CollisionGeometryCodec::Decode(corrupt), "every source byte protected by checksum");
            check(!CollisionGeometryCodec::Decode(std::span(*encoded).first(offset)),
                  "every truncated source rejected");
        }
        auto trailing = *encoded;
        trailing.push_back(std::byte{});
        check(!CollisionGeometryCodec::Decode(trailing), "trailing byte rejected");
        seal(trailing);
        check(!CollisionGeometryCodec::Decode(trailing), "valid checksum cannot hide trailing payload");
        auto badCount = *encoded;
        for (std::size_t i = 36; i < 40; ++i)
            badCount[i] = std::byte{255};
        seal(badCount);
        check(!CollisionGeometryCodec::Decode(badCount),
              "oversized count rejected before allocation with valid checksum");
        auto badVersion = *encoded;
        badVersion[4] = std::byte{2};
        seal(badVersion);
        check(!CollisionGeometryCodec::Decode(badVersion), "unsupported version rejected with valid checksum");
    }

    auto invalid = convex();
    invalid.key.asset = {};
    check(!CollisionGeometryCodec::Encode(invalid), "nil UUID rejected");
    invalid = mesh();
    std::get<triangle_mesh_source>(invalid.form).triangles[0].a = 999;
    check(!CollisionGeometryCodec::Encode(invalid), "bad mesh indices rejected");
    invalid = heightfield();
    std::get<heightfield_source>(invalid.form).rows = UINT32_MAX;
    check(!CollisionGeometryCodec::Encode(invalid), "overflowing heightfield dimensions rejected");

    const auto source = convex();
    const auto bytes = *CollisionGeometryCodec::Encode(source);
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        check(output.good(), "test host publishes native source");
    }
    check(bool(CollisionGeometryIO::Read(path, source.key)), "native source reader roundtrip");
    check(!CollisionGeometryIO::Read(path, convex(2).key), "file revision mismatch rejected");
    check(!CollisionGeometryIO::Read(path, mesh().key), "file UUID mismatch rejected");
    check(!CollisionGeometryIO::Read(path.string() + ".wrong", source.key), "wrong extension rejected");
}

void cache_transactions()
{
    CollisionGeometryLibrary library;
    int reads = 0;
    const auto resolve = [&](geometry_asset_key key) {
        return library.Resolve(key, [&](auto) -> result<CollisionGeometrySource> {
            ++reads;
            return convex();
        });
    };
    const auto old = resolve(convex().key);
    check(bool(old), "first source cooked");
    check(resolve(convex().key).value() == *old && reads == 1 && library.Stats()->cooks == 1,
          "multiple consumers share one cook");

    auto changed = convex();
    std::get<convex_source>(changed.form).points[0].x = -2;
    const auto collision = library.Publish(changed);
    check(!collision && resolve(convex().key).value() == *old,
          "immutable revision content collision preserves old asset");
    bool writerCalled = false;
    const auto failedWrite = library.Publish(convex(2), [&](auto) {
        writerCalled = true;
        return false;
    });
    check(!failedWrite && writerCalled && library.Stats()->assets == 1 && resolve(convex().key).value() == *old,
          "failed host publication preserves complete cache");

    auto badCook = convex(2);
    std::get<convex_source>(badCook.form).points.assign(8, math::vector3{});
    writerCalled = false;
    const auto failedCook = library.Publish(badCook, [&](auto) {
        writerCalled = true;
        return true;
    });
    check(!failedCook && !writerCalled && library.Stats()->assets == 1 && resolve(convex().key).value() == *old,
          "SDK cook failure cannot publish or retire old revision");
    check(bool(library.Publish(convex(2),
                               [&](auto) {
                                   const auto nested = library.Publish(convex(3));
                                   return !nested && nested.error().code == error_code::wrong_phase;
                               })) &&
              library.Stats()->assets == 2,
          "reentrant writer cannot overwrite outer cache transaction");
    check(bool(library.Publish(convex(3))) && library.Stats()->assets == 3,
          "publication guard released after callback");

    check(!library.Publish(convex(), [](auto) { return false; }) && library.Stats()->assets == 3,
          "existing revision host failure preserves cache");
    check(!library.Publish(convex(4), [](auto) -> bool { throw std::runtime_error("injected writer failure"); }) &&
              library.Stats()->assets == 3 && bool(library.Publish(convex(4))),
          "throwing host preserves cache and releases transaction guard");

    const auto mismatch = library.Resolve(mesh().key, [](auto) -> result<CollisionGeometrySource> { return convex(); });
    check(!mismatch && library.Stats()->assets == 4, "loader identity mismatch cannot mutate cache");
    bool rejected = false;
    std::jthread worker([&] {
        rejected = !library.Publish(convex(5)) && !library.Stats() &&
                   !library.Resolve(convex().key, [](auto) -> result<CollisionGeometrySource> { return convex(); });
    });
    worker.join();
    check(rejected, "foreign owner rejects load cook publication and statistics");
}

std::array<PhysicsShapeDefinition, 2> authoring()
{
    std::array<PhysicsShapeDefinition, 2> definitions;
    for (auto& shape : definitions)
    {
        shape.kind = PhysicsShapeKind::convex;
        shape.geometryAsset = Uuid::ToString(convex().key.asset);
        shape.geometryRevision = 1;
    }
    definitions[0].shapeId = 101;
    definitions[1].shapeId = 102;
    definitions[1].localPosition = {3, 0, 0};
    return definitions;
}

bool exercise(execution_preference execution)
{
    ScenePhysicsSimulation session;
    body_definition definition;
    definition.properties.kind = body_kind::static_body;
    definition.properties.initial_pose.position.y = 2;
    std::shared_ptr<const CollisionGeometry> shared;
    {
        CollisionGeometryLibrary library;
        auto source = authoring();
        int resolves = 0;
        const auto resolver = [&](geometry_asset_key key) {
            ++resolves;
            return library.Resolve(key, [](auto) -> result<CollisionGeometrySource> { return convex(); });
        };
        check(bool(ValidatePhysicsShapes(source, {1, 1, 1}, {}, body_kind::static_body)),
              "cook reference validates without SDK lookup");
        auto duplicate = source;
        duplicate[1].shapeId = duplicate[0].shapeId;
        check(!BuildPhysicsShapes(duplicate, {1, 1, 1}, {}, body_kind::static_body, resolver) && resolves == 0,
              "complete compound preflight before first cook");
        auto missing = source;
        missing[1].geometryAsset = "invalid-uuid";
        check(!BuildPhysicsShapes(missing, {1, 1, 1}, {}, body_kind::static_body, resolver) && resolves == 0,
              "last malformed reference prevents all source reads");
        auto built = BuildPhysicsShapes(source, {1, 1, 1}, {}, body_kind::static_body, resolver);
        check(built && library.Stats()->cooks == 1 && resolves == 2, "compound shares source and SDK geometry");
        shared = std::get<cooked_geometry>((*built)[0].form).asset;
        check(shared == std::get<cooked_geometry>((*built)[1].form).asset,
              "both shape instances retain same immutable asset");
        definition.shapes = std::move(*built);

        source[0].kind = PhysicsShapeKind::triangle_mesh;
        source[0].geometryAsset = Uuid::ToString(mesh().key.asset);
        auto loadedMesh = library.Publish(mesh());
        check(bool(loadedMesh), "triangle mesh source cooked");
        auto wrong = BuildPhysicsShapes(std::span(source).subspan(1), {1, 1, 1}, {}, body_kind::static_body,
                                        [&](auto) { return loadedMesh; });
        check(!wrong, "geometry kind mismatch rejected");
        check(!BuildPhysicsShapes(std::span(source).first(1), {1, 1, 1}, {}, body_kind::dynamic, resolver),
              "moving triangle mesh rejected before resolver");
        source[0].sensor = true;
        check(!BuildPhysicsShapes(std::span(source).first(1), {1, 1, 1}, {}, body_kind::static_body, resolver),
              "triangle mesh sensor rejected");

        source[0].sensor = false;
        source[0].localPosition = {10, -2, 0};
        source[0].shapeId = 201;
        built = BuildPhysicsShapes(std::span(source).first(1), {1, 1, 1}, {}, body_kind::static_body, [&](auto key) {
            return library.Resolve(key, [](auto) -> result<CollisionGeometrySource> { return mesh(); });
        });
        check(bool(built), "mesh authoring resolved");
        definition.shapes.push_back(std::move(built->front()));

        source[0].kind = PhysicsShapeKind::heightfield;
        source[0].geometryAsset = Uuid::ToString(heightfield().key.asset);
        source[0].geometryScale = {1, .1f, 1};
        source[0].localPosition = {20, -2, 0};
        source[0].shapeId = 301;
        built = BuildPhysicsShapes(std::span(source).first(1), {1, 1, 1}, {}, body_kind::static_body, [&](auto key) {
            return library.Resolve(key, [](auto) -> result<CollisionGeometrySource> { return heightfield(); });
        });
        check(bool(built), "heightfield authoring resolved");
        definition.shapes.push_back(std::move(built->front()));
    } // Shared shapes outlive the Scene cache and every temporary cooking scene.

    body_definition moving;
    moving.properties.kind = body_kind::dynamic;
    moving.properties.initial_pose.position = {100, 10, 0};
    moving.shapes.assign(definition.shapes.begin(), definition.shapes.begin() + 2);
    const auto movingBinding = session.Register(std::move(moving), true);
    check(bool(movingBinding), "dynamic compound shares cooked convex assets");
    const auto binding = session.Register(std::move(definition), true);
    check(bool(binding), "body retains all shared cooked assets after cache destruction");
    scene_config config;
    config.workers = 2;
    config.execution = execution;
    check(bool(session.Start(config)), "shared cooked compound Play starts");
    const bool gpu = session.Runtime()->status().backend == execution_backend::gpu;
    shared.reset();
    std::array<query_hit, 4> hits;
    for (const auto& [origin, id] :
         std::array{std::pair{math::vector3{0, 8, 0}, shape_id{101}}, std::pair{math::vector3{3, 8, 0}, shape_id{102}},
                    std::pair{math::vector3{10.5f, 8, .5f}, shape_id{201}},
                    std::pair{math::vector3{20.5f, 8, .5f}, shape_id{301}}})
    {
        const auto ray = session.Runtime()->raycast(origin, {0, -1, 0}, 20, hits);
        check(ray && ray->written == 1 && hits[0].shape == id && hits[0].body == session.Handle(*binding),
              "actual SDK query resolves cooked shape after cooking/cache owners are gone");
    }
    check(bool(session.Advance(ScenePhysicsSimulation::fixed_seconds)) &&
              session.Read(*movingBinding)->transform.position.y < 10,
          "actual solver advances shared cooked dynamic compound");
    check(bool(session.Stop()) && bool(session.Start(config)), "shared asset survives Stop and next Play");
    check(bool(session.Stop()), "shared cooked teardown");
    return gpu;
}
} // namespace

int main(int argc, char** argv)
{
    check(argc == 2, "capture/output path provided");
#if !CE_SHIPPING
    auto& profiler = ce::profiler();
    profiler.initialize();
    profiler.register_thread("PhysicsGeometryProbe", ce::track_kind::game_thread);
    profiler.record(1);
#endif
    codec_and_io(std::filesystem::path(argv[1]).replace_extension(".cegeometry"));
    cache_transactions();
    exercise(execution_preference::cpu);
    const bool gpu = exercise(execution_preference::prefer_gpu);
#if !CE_SHIPPING
    profiler.publish_frame(1);
    profiler.pause();
    profiler.wait_until_idle();
    const auto capture = profiler.capture();
    check(capture && capture->complete(), "shared geometry profiling capture complete");
    auto events = capture->frames() |
                  std::views::transform([](const auto& frame) -> const auto& { return frame.events; }) |
                  std::views::join;
    for (const auto name : {"Physics.GeometryResolve", "Physics.GeometryPublish", "Physics.GeometryCook"})
        check(
            std::ranges::any_of(events, [&](const auto& event) { return capture->marker(event.marker).name == name; }),
            "shared geometry hierarchy recorded");
    check(bool(ce::save_capture(*capture, argv[1])), "shared geometry capture saved");
    profiler.unregister_thread();
    profiler.shutdown();
#endif
    std::cout << "{\"result\":\"PHYSICS_B1_GEOMETRY_OK\",\"checks\":" << checks
              << ",\"gpu_verified\":" << (gpu ? "true" : "false") << "}\n";
}
