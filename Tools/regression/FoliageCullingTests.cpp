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
    uint64_t identity = 42;
    void RebuildWorldMatrix() = delete;
};
void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
}
int main()
{
    try
    {
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
            Check(poses[i].identity == sealed[i].identity &&
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
