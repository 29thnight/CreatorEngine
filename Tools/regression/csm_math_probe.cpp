#include "../../Engine/RenderEngine/Render/Graph/ShadowMath.h"
#include <mathematics/transform.hpp>
#include <cassert>
#include <iostream>

int main()
{
    const auto perspective = math::perspective_fov_lh(1.f, 1.7f, .1f, 1000.f);
    const auto ortho = math::orthographic_off_center_lh(-3.f, 7.f, -2.f, 4.f, .1f, 1000.f);
    for (const auto projection : {perspective, ortho})
        for (float depth : {.1f, 10.f, 200.f})
            for (float x : {-1.f, 1.f})
                for (float y : {-1.f, 1.f})
                {
                    const auto p = shadow_math::ViewCorner(math::inverse(projection), x, y, depth);
                    const auto clip = math::vector4{p.x, p.y, p.z, 1} * projection;
                    assert(std::fabs(p.z - depth) < .001f);
                    assert(std::fabs(clip.x / clip.w - x) < .001f);
                    assert(std::fabs(clip.y / clip.w - y) < .001f);
                }
    const auto a = shadow_math::ViewCorner(math::inverse(ortho), 1, -1, 1);
    const auto b = shadow_math::ViewCorner(math::inverse(ortho), 1, -1, 100);
    assert(std::fabs(a.x - b.x) < 1e-5f && std::fabs(a.y - b.y) < 1e-5f);

    const auto direction = math::normalize(math::vector3{.3f, -.8f, .5f});
    const auto right = math::normalize(math::cross(math::vector3{0, 1, 0}, direction));
    const auto up = math::cross(direction, right);
    constexpr float texel = .125f;
    const auto center = shadow_math::SnapCenter({3.21f, 4.76f, -2.43f}, direction, texel);
    for (float offset : {-.24f, 0.f, .24f})
    {
        const auto snapped = shadow_math::SnapCenter(center + right * (offset * texel), direction, texel);
        assert(std::fabs(math::dot(snapped - center, right)) < 1e-5f);
        assert(std::fabs(math::dot(snapped - center, up)) < 1e-5f);
    }
    const auto stepped = shadow_math::SnapCenter(center + right * texel, direction, texel);
    assert(std::fabs(math::dot(stepped - center, right) - texel) < 1e-5f);

    const auto light = math::orthographic_off_center_lh(-5, 5, -5, 5, 0, 100);
    assert(shadow_math::IntersectsClip({{0, 0, 50}, 1}, light));
    assert(shadow_math::IntersectsClip({{5.5f, 0, 50}, 1}, light));
    assert(!shadow_math::IntersectsClip({{7, 0, 50}, 1}, light));
    assert(!shadow_math::IntersectsClip({{0, 0, -2}, 1}, light));
    assert(!shadow_math::IntersectsClip({{0, 0, 102}, 1}, light));
    assert(shadow_math::IntersectsClip({{1000, 0, 50}, 0}, light));

    // A caster outside the camera can still lie in the light's receiver volume.
    const shadow_math::Sphere caster{{-4, 0, 2}, .25f};
    assert(!shadow_math::IntersectsClip(caster, perspective));
    assert(shadow_math::IntersectsClip(caster, light));
    FrameCameraSnapshot camera;
    camera.isOrthographic = true;
    camera.nearPlane = -100; camera.farPlane = 100;
    camera.projection = math::orthographic_off_center_lh(-1, 1, -1, 1, -100, 100);
    camera.inverseView = math::matrix4x4::identity();
    const math::vector3 sideways{1, 0, 0};
    const auto receivers = shadow_math::ReceiverCascades(camera, sideways, 200, .15f);
    for (unsigned i = 0; i < 2; ++i)
        for (float z : {receivers[i].split - .01f, receivers[i].split + .01f,
                       shadow_math::BlendStart(receivers[i].split, .15f) + .01f})
        {
            // Both primary and blended cascades cover every transition receiver.
            const unsigned primary = z > receivers[i].split ? i + 1 : i;
            for (const unsigned selected : {primary, i + 1})
                for (float x : {-1.f, 1.f})
                    for (float y : {-1.f, 1.f})
                        assert(math::distance({x, y, z}, receivers[selected].bounds.center)
                            <= receivers[selected].bounds.radius + 1e-4f);
        }
    assert(math::distance({0, 0, -32}, receivers[1].bounds.center) <= receivers[1].bounds.radius);
    // A zero split has a zero-width blend; receivers on either side remain covered.
    assert(shadow_math::BlendStart(0.f, .15f) == 0.f);
    for (float nearDepth : {-30.f, -60.f})
    {
        camera.nearPlane = nearDepth; camera.farPlane = nearDepth + 90.f;
        camera.projection = math::orthographic_off_center_lh(-1, 1, -1, 1,
            camera.nearPlane, camera.farPlane);
        const auto zeroReceivers = shadow_math::ReceiverCascades(camera, sideways, 200, .15f);
        const unsigned boundary = nearDepth == -30.f ? 0u : 1u;
        assert(std::fabs(zeroReceivers[boundary].split) < 1e-5f);
        for (float z : {-.001f, 0.f, .001f})
        {
            const unsigned primary = z > 0 ? boundary + 1 : boundary;
            for (float x : {-1.f, 1.f})
                for (float y : {-1.f, 1.f})
                    assert(math::distance({x, y, z}, zeroReceivers[primary].bounds.center)
                        <= zeroReceivers[primary].bounds.radius + 1e-4f);
        }
    }
    unsigned selected = 0;
    for (unsigned i = 0; i < 4097; ++i)
    {
        const shadow_math::Sphere irrelevant{{0, 10000.f + float(i), 0}, 1};
        selected += shadow_math::RelevantToView(i == 0, irrelevant, receivers, sideways, true);
    }
    const shadow_math::Sphere upstream{receivers[0].bounds.center - sideways * 500.f, 1};
    assert(shadow_math::RelevantToView(false, upstream, receivers, sideways, true));
    assert(!shadow_math::RelevantToView(false, upstream, receivers, sideways, false));
    assert(selected == 1); // Unrelated offscreen geometry cannot exhaust the seal budget.
    std::cout << "CSM_MATH_OK perspective orthographic snapping clip offscreen\n";
}
