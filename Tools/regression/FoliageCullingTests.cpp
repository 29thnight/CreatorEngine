#include "../../Engine/SceneRuntime/FoliageCulling.h"
#include <iostream>
#include <cstdlib>
#include <new>
#include <stdexcept>
namespace { thread_local bool counting = false; thread_local size_t allocations = 0; }
void* operator new(size_t size)
{
    if (counting) ++allocations;
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }
namespace
{
// A borrowed sealed-pose consumer must not ask to rebuild authored transforms.
struct Pose
{
    uint32_t m_foliageTypeID = 0;
    bool m_isCulled = false;
    math::matrix4x4 m_worldMatrix = math::matrix4x4::identity();
    uint64_t m_temporalIdentity = 42;
    void RebuildWorldMatrix() = delete;
};
void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }

void TestWorldBoundsCache()
{
    foliage::WorldBoundsCache cache;
    static_assert(!std::is_copy_constructible_v<foliage::WorldBoundsCache>);
    std::vector<Pose> poses(10000);
    for (size_t i = 0; i < poses.size(); ++i)
    {
        poses[i].m_temporalIdentity = i + 1;
        poses[i].m_foliageTypeID = uint32_t(i % 2);
        poses[i].m_worldMatrix = math::translation_matrix({float(i % 40), 0, 5});
    }
    std::array<math::aabb, 2> local{
        math::aabb{{0, 0, 0}, {0.5f, 0.5f, 0.5f}}, math::aabb{{0, 0, 0}, {1, 2, 1}}};
    bool present = true;
    size_t resolves = 0;
    auto resolve = [&](uint32_t type) -> const math::aabb* {
        ++resolves;
        return type == 0 && !present ? nullptr : &local.at(type);
    };
    math::bounding_frustum camera;
    camera.far_plane = 20;
    std::optional<math::bounding_frustum> frustum = camera;
    auto cull = [&](bool playing = true) {
        return cache.Cull(std::span{poses}, local.size(), resolve, frustum, playing);
    };
    auto equivalent = [&] {
        auto expected = poses;
        foliage::CullSealedInstances(std::span{expected}, local.size(), resolve, frustum, true);
        for (size_t i = 0; i < poses.size(); ++i)
            Check(poses[i].m_isCulled == expected[i].m_isCulled &&
                poses[i].m_worldMatrix == expected[i].m_worldMatrix &&
                poses[i].m_temporalIdentity == expected[i].m_temporalIdentity,
                "cached result differs from uncached visibility/pose");
    };
    allocations = 0;
    counting = true;
    const auto cold = cull();
    counting = false;
    Check(cold == poses.size() && allocations == 1, "cold cache must use one contiguous allocation");
    equivalent();
    resolves = allocations = 0;
    size_t warm = 0;
    counting = true;
    for (int frame = 0; frame < 10; ++frame) warm += cull();
    counting = false;
    Check(warm == 0 && allocations == 0 && resolves == 0,
        "warm cache transformed, allocated or resolved asset bounds");
    poses[3].m_worldMatrix = math::translation_matrix({100, 0, 5});
    cache.Invalidate(3);
    allocations = 0;
    counting = true;
    const auto edit = cull();
    counting = false;
    Check(edit == 1 && allocations == 0, "single edit invalidated too much or allocated");
    equivalent();
    // A coherent replacement of one type must not invalidate other types.
    local[0].center = {-40, 0, 0};
    cache.InvalidateType(0, std::span{poses});
    Check(cull() == poses.size() / 2, "type replacement did not target matching instances");
    equivalent();
    present = false;
    cache.InvalidateType(0, std::span{poses});
    Check(cull() == 0, "missing bounds transformed an invalid source");
    equivalent();
    present = true;
    cache.InvalidateType(0, std::span{poses});
    Check(cull() == poses.size() / 2, "missing-to-ready asset did not invalidate");
    const auto savedBounds = local[0];
    local[0] = {};
    cache.InvalidateType(0, std::span{poses});
    Check(cull() == poses.size() / 2, "valid empty bounds were confused with a missing asset");
    equivalent();
    local[0] = savedBounds;
    cache.InvalidateType(0, std::span{poses});
    cull();
    // Camera changes only visibility, never world bounds.
    camera.origin = {100, 0, 0};
    frustum = camera;
    Check(cull() == 0, "camera movement rebuilt world bounds");
    equivalent();
    // Dirty edits must survive editor mode and absent camera frames.
    poses[4].m_worldMatrix = math::translation_matrix({100, 0, 5});
    cache.Invalidate(4);
    Check(cull(false) == 0, "editor mode built a world cache");
    frustum.reset();
    Check(cull() == 0, "no-camera mode built a world cache");
    frustum = camera;
    Check(cull() == 1, "editor/no-camera mode lost pending invalidation");
    equivalent();
    poses.erase(poses.begin() + 1);
    cache.InvalidateAll();
    Check(cull() == poses.size(), "removal retained stale slot bounds");
    equivalent();
    // Reuse the same slot count and even the same identity; Clear is required
    // for an asset reload and must retain its existing backing allocation.
    for (auto& pose : poses) pose.m_worldMatrix = math::translation_matrix({0, 0, 5});
    allocations = 0;
    counting = true;
    cache.Clear();
    const auto reloaded = cull();
    counting = false;
    Check(reloaded == poses.size() && allocations == 0, "reload retained stale values or discarded capacity");
    equivalent();
    // Identity tags independently guard shifted/replaced slots.
    poses[0].m_temporalIdentity = 999999;
    poses[0].m_worldMatrix = math::translation_matrix({100, 0, 5});
    Check(cull() == 1, "same-slot identity replacement used stale bounds");
    equivalent();
    // Type removal/reindex and brush compaction follow the structural path.
    std::erase_if(poses, [](const Pose& p) { return p.m_foliageTypeID == 0; });
    for (auto& pose : poses) pose.m_foliageTypeID = 0;
    cache.InvalidateAll();
    Check(cull() == poses.size(), "type removal/reindex did not rebuild shifted entries");
    equivalent();
    std::erase_if(poses, [](const Pose& p) { return p.m_temporalIdentity % 3 == 0; });
    cache.InvalidateAll();
    Check(cull() == poses.size(), "brush removal did not invalidate survivors");
    equivalent();
    Pose added;
    added.m_temporalIdentity = 1000001;
    added.m_worldMatrix = math::translation_matrix({100, 0, 5});
    poses.push_back(added);
    Check(cull() == 1, "new instance did not initialize exactly one cache entry");
    equivalent();
    // A fresh cache must not allocate merely to cull in the editor.
    foliage::WorldBoundsCache editorOnly;
    allocations = 0;
    counting = true;
    const auto editor = editorOnly.Cull(std::span{poses}, local.size(), resolve, frustum, false);
    counting = false;
    Check(editor == 0 && allocations == 0, "editor-only culling allocated cache storage");
    // Cache entries contain values, not owners or borrowed descriptor pointers.
    // An old descriptor can die immediately after a published replacement.
    foliage::WorldBoundsCache lifetime;
    camera.origin = {0, 0, 0};
    frustum = camera;
    std::array<Pose, 1> single;
    single[0].m_worldMatrix = math::translation_matrix({0, 0, 5});
    auto asset = std::make_shared<math::aabb>(math::vector3{0, 0, 0}, math::vector3{1, 1, 1});
    std::weak_ptr<math::aabb> oldAsset = asset;
    auto acquire = [&](uint32_t) { return asset.get(); };
    Check(lifetime.Cull(std::span{single}, 1, acquire, frustum, true) == 1 &&
        asset.use_count() == 1, "cache retained an asset owner");
    asset.reset();
    Check(oldAsset.expired(), "cache extended old descriptor lifetime");
    asset = std::make_shared<math::aabb>(math::vector3{100, 0, 0}, math::vector3{1, 1, 1});
    lifetime.InvalidateType(0, std::span{single});
    Check(lifetime.Cull(std::span{single}, 1, acquire, frustum, true) == 1 && single[0].m_isCulled,
        "replacement reused a retired descriptor/bounds");
    std::cout << "FOLIAGE_WORLD_BOUNDS_CACHE_OK initial=10000 cold_allocations=1 warm_transforms=0 warm_allocations=0 edit_transforms=1\n";
}
}
int main()
{
    try
    {
        TestWorldBoundsCache();
        std::array<Pose, 5> poses;
        poses[0].m_worldMatrix = math::translation_matrix({0, 0, 5});
        poses[1].m_worldMatrix = math::translation_matrix({100, 0, 5});
        poses[2].m_foliageTypeID = 1; // missing selected bounds
        poses[3].m_foliageTypeID = 2; // valid empty bounds
        poses[4].m_foliageTypeID = 3; // invalid type retains previous state
        poses[4].m_isCulled = true;
        const auto sealed = poses;
        math::aabb bounds{{0, 0, 0}, {0.5f, 0.5f, 0.5f}};
        const math::aabb empty;
        auto resolve = [&](uint32_t type) -> const math::aabb* {
            if (type == 0) return &bounds;
            if (type == 2) return &empty;
            return nullptr;
        };
        math::bounding_frustum camera;
        camera.far_plane = 20;
        std::optional<math::bounding_frustum> frustum = camera;
        auto cull = [&](bool playing) {
            foliage::CullSealedInstances(std::span{poses}, 3, resolve, frustum, playing);
        };
        cull(true);
        Check(!poses[0].m_isCulled && poses[1].m_isCulled && poses[2].m_isCulled &&
            !poses[3].m_isCulled && poses[4].m_isCulled, "playing visibility contract changed");
        cull(false);
        Check(!poses[0].m_isCulled && !poses[1].m_isCulled && poses[2].m_isCulled &&
            !poses[3].m_isCulled && poses[4].m_isCulled, "editor/missing bounds contract changed");
        frustum.reset();
        cull(true);
        Check(!poses[1].m_isCulled && poses[2].m_isCulled, "missing camera contract changed");
        for (size_t i = 0; i < poses.size(); ++i)
            Check(poses[i].m_temporalIdentity == sealed[i].m_temporalIdentity &&
                poses[i].m_worldMatrix == sealed[i].m_worldMatrix, "culling mutated sealed pose/identity");
        // Camera changes must still recull an unchanged pose.
        camera.origin = {100, 0, 0};
        frustum = camera;
        cull(true);
        Check(poses[0].m_isCulled && !poses[1].m_isCulled, "camera movement used stale visibility");
        // Owner-side edits publish a replacement matrix; no consumer cache may
        // retain the old pose or old mesh bounds across a replacement.
        poses[0].m_worldMatrix = poses[1].m_worldMatrix;
        cull(true);
        Check(!poses[0].m_isCulled, "edited matrix ignored");
        bounds.center = {-100, 0, 0};
        cull(true);
        Check(poses[0].m_isCulled, "replacement local bounds ignored");
        bounds.center = {0, 0, 0};
        camera.origin = {0, 0, 0};
        frustum = camera;
        const auto edited = math::scaling_matrix({-1, 2, 0.5f}) *
            math::rotation_x(math::radians(15.0f)) *
            math::rotation_y(math::radians(90.0f)) *
            math::rotation_z(math::radians(270.0f)) * math::translation_matrix({0, 0, 5});
        poses[0].m_worldMatrix = edited;
        cull(true);
        Check(!poses[0].m_isCulled && poses[0].m_worldMatrix == edited,
            "rotated/nonuniform/negative-scale pose was rebuilt or misculled");
        const auto beforeNoTypes = poses;
        foliage::CullSealedInstances(std::span{poses}, 0, resolve, frustum, true);
        for (size_t i = 0; i < poses.size(); ++i)
            Check(poses[i].m_isCulled == beforeNoTypes[i].m_isCulled, "zero types changed flags");
        allocations = 0;
        counting = true;
        for (int frame = 0; frame < 1000; ++frame) cull(true);
        counting = false;
        Check(allocations == 0, "sealed culling allocated");
        std::cout << "FOLIAGE_SEALED_CULLING_OK frames=1000 allocations=" << allocations << '\n';
        return 0;
    }
    catch (const std::exception& e) { counting = false; std::cerr << e.what() << '\n'; return 1; }
}
