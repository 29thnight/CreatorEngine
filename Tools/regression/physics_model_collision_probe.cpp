#include "../../Engine/SceneRuntime/ModelCollisionGeometry.h"
#include "../../Editor/EngineEntry/EditorModelCollisionAsset.h"
#include <iostream>
#include <fstream>
#include <limits>
#include <stdexcept>

int main(int argc, char** argv)
{
    int checks = 0;
    const auto check = [&](bool value) {
        ++checks;
        if (!value)
            throw std::runtime_error("Model collision check " + std::to_string(checks));
    };

    try
    {
        const std::uint32_t indices[]{0, 1, 2};
        const float positions[3][3]{{-2, 0, -3}, {4, 0, -3}, {0, 0, 5}};
        std::uint64_t firstHash = 0;

        for (const auto mask : assets::kModelVertexMasks)
        {
            const auto stride = assets::StrideOf(mask);
            const auto layoutHash = assets::VertexLayoutHash(mask);
            std::vector<std::byte> bytes(stride * 3, std::byte{42});

            for (std::size_t index = 0; index < 3; ++index)
                std::memcpy(bytes.data() + index * stride + assets::OffsetOf(mask, assets::VertexAttribute::Position),
                            positions[index], sizeof(positions[index]));

            auto source = ce::physics::BuildModelCollisionMesh(bytes, indices, mask, stride, layoutHash);
            check(bool(source));
            check(source && source->points.size() == 3 && source->triangles.size() == 1);
            check(source && source->points[1] == math::vector3{4, 0, -3} && source->triangles[0].c == 2);
            const auto hash = ce::physics::ModelCollisionMeshHash(*source);
            if (!firstHash)
                firstHash = hash;
            check(hash == firstHash); // Render-only attributes/layout do not change collision identity.
            check(!ce::physics::BuildModelCollisionMesh(bytes, indices, mask, stride + 1, layoutHash));
            check(!ce::physics::BuildModelCollisionMesh(bytes, indices, mask, stride, layoutHash + 1));

            const std::uint32_t invalid[]{0, 1, 3};
            check(!ce::physics::BuildModelCollisionMesh(bytes, invalid, mask, stride, layoutHash));
            const std::uint32_t duplicate[]{0, 0, 2};
            check(!ce::physics::BuildModelCollisionMesh(bytes, duplicate, mask, stride, layoutHash));
            check(!ce::physics::BuildModelCollisionMesh(bytes, std::span{indices, 2}, mask, stride, layoutHash));
            check(!ce::physics::BuildModelCollisionMesh(bytes, {}, mask, stride, layoutHash));

            auto changed = *source;
            changed.points[0].x += 1;
            check(ce::physics::ModelCollisionMeshHash(changed) != hash);
            changed = *source;
            changed.triangles[0] = {2, 1, 0};
            check(ce::physics::ModelCollisionMeshHash(changed) != hash);

            const float bad = std::numeric_limits<float>::infinity();
            std::memcpy(bytes.data(), &bad, sizeof(bad));
            check(!ce::physics::BuildModelCollisionMesh(bytes, indices, mask, stride, layoutHash));
            bytes.pop_back();
            check(!ce::physics::BuildModelCollisionMesh(bytes, indices, mask, stride, layoutHash));
        }

        check(!ce::physics::BuildModelCollisionMesh({}, {}, 0, 0, 0));
        check(argc == 2);
        const auto root = std::filesystem::path(argv[1]) / ("assets-" + std::to_string(GetCurrentProcessId()));
        std::filesystem::create_directories(root);
        const auto model = Uuid::Parse("91ea9b44-13a6-8aec-99ec-0963eef7eaa1");
        const auto mesh = Uuid::Parse("91ea9b44-13a6-8aec-99ec-0963eef7eaa2");
        ce::physics::triangle_mesh_source source{{{-2, 0, -3}, {4, 0, -3}, {0, 0, 5}}, {{0, 1, 2}}};
        auto prepared = Editor::PrepareModelCollisionAsset(root, model, mesh, source);
        check(prepared && !prepared->existing && FileGuid(prepared->source.key.asset).IsRandomV4());
        check(bool(Editor::CollisionGeometryAuthoring::Create(root, prepared->destination, prepared->source)));
        auto reused = Editor::PrepareModelCollisionAsset(root, model, mesh, source);
        check(reused && reused->existing && reused->source.key == prepared->source.key);
        check(reused && reused->destination == prepared->destination);
        auto changed = source;
        changed.points[0].x += 1;
        auto updated = Editor::PrepareModelCollisionAsset(root, model, mesh, changed);
        check(updated && !updated->existing && updated->destination != prepared->destination);
        check(updated && updated->source.key.asset != prepared->source.key.asset);

        const auto write = [&](const auto& path, const auto& bytes) {
            std::ofstream stream(path, std::ios::binary | std::ios::trunc);
            stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            stream.close();
            check(!stream.fail());
        };
        auto corrupt = prepared->source;
        corrupt.form = changed;
        auto encoded = ce::physics::CollisionGeometryCodec::Encode(corrupt);
        write(prepared->destination, *encoded);
        check(!Editor::PrepareModelCollisionAsset(root, model, mesh, source)); // Hash filename alone is insufficient.
        encoded = ce::physics::CollisionGeometryCodec::Encode(prepared->source);
        write(prepared->destination, *encoded);
        const auto meta = std::filesystem::path(prepared->destination.string() + ".meta");
        const auto badMeta = std::string("guid: ") + Uuid::ToString(mesh) + "\ngeometryRevision: 1\n";
        write(meta, badMeta);
        check(!Editor::PrepareModelCollisionAsset(root, model, mesh, source));
        const auto goodMeta =
            std::string("guid: ") + Uuid::ToString(prepared->source.key.asset) + "\ngeometryRevision: 1\n";
        write(meta, goodMeta);
        check(bool(Editor::PrepareModelCollisionAsset(root, model, mesh, source)));
        check(!Editor::PrepareModelCollisionAsset(root, {}, mesh, source));
        source.points[0].x = std::numeric_limits<float>::quiet_NaN();
        check(!Editor::PrepareModelCollisionAsset(root, model, mesh, source));
        std::cout << "PHYSICS_MODEL_COLLISION_OK checks=" << checks << '\n';
        return 0;
    }
    catch (const std::exception& failure)
    {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
