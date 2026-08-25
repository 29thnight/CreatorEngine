#include <mathematics/mathematics.hpp>

#include "FrameCameraSnapshot.h"
#include "MathematicsInterop.h"

#include <DirectXCollision.h>
#include <DirectXMath.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <type_traits>

namespace {

constexpr float epsilon = 2.0e-4f;

bool Near(float x, float y, float tolerance = epsilon) noexcept
{
    return std::fabs(x - y) <= tolerance;
}

void Check(bool condition, const char* contract, int& failures) noexcept
{
    if (condition)
    {
        return;
    }

    std::fprintf(stderr, "[MATHEMATICS CONTRACT] FAIL: %s\n", contract);
    ++failures;
}

DirectX::XMMATRIX ToDirectX(const math::matrix4x4& value) noexcept
{
    return DirectX::XMMATRIX(
        value.m[0][0], value.m[0][1], value.m[0][2], value.m[0][3],
        value.m[1][0], value.m[1][1], value.m[1][2], value.m[1][3],
        value.m[2][0], value.m[2][1], value.m[2][2], value.m[2][3],
        value.m[3][0], value.m[3][1], value.m[3][2], value.m[3][3]);
}

math::matrix4x4 FromDirectX(DirectX::FXMMATRIX value) noexcept
{
    DirectX::XMFLOAT4X4 stored{};
    DirectX::XMStoreFloat4x4(&stored, value);
    return math::matrix4x4{
        stored._11, stored._12, stored._13, stored._14,
        stored._21, stored._22, stored._23, stored._24,
        stored._31, stored._32, stored._33, stored._34,
        stored._41, stored._42, stored._43, stored._44};
}

math::vector3 FromDirectX3(DirectX::FXMVECTOR value) noexcept
{
    DirectX::XMFLOAT3 stored{};
    DirectX::XMStoreFloat3(&stored, value);
    return math::vector3{stored.x, stored.y, stored.z};
}

math::quaternion FromDirectXQuaternion(DirectX::FXMVECTOR value) noexcept
{
    DirectX::XMFLOAT4 stored{};
    DirectX::XMStoreFloat4(&stored, value);
    return math::quaternion{stored.x, stored.y, stored.z, stored.w};
}

} // namespace

static_assert(__cplusplus >= 202002L);

static_assert(sizeof(math::vector2) == 8);
static_assert(sizeof(math::vector3) == 12);
static_assert(sizeof(math::vector4) == 16);
static_assert(sizeof(math::quaternion) == 16);
static_assert(sizeof(math::matrix4x4) == 64);
static_assert(sizeof(math::color) == 16);
static_assert(sizeof(math::rect) == 16);
static_assert(sizeof(math::sphere) == 16);
static_assert(sizeof(math::aabb) == 24);
static_assert(sizeof(math::bounding_frustum) == 52);

static_assert(std::is_standard_layout_v<math::vector3>);
static_assert(std::is_standard_layout_v<math::matrix4x4>);
static_assert(std::is_standard_layout_v<math::color>);
static_assert(std::is_standard_layout_v<math::rect>);
static_assert(std::is_standard_layout_v<math::aabb>);
static_assert(std::is_standard_layout_v<math::bounding_frustum>);
static_assert(std::is_trivially_copyable_v<math::vector3>);
static_assert(std::is_trivially_copyable_v<math::matrix4x4>);
static_assert(std::is_trivially_copyable_v<math::color>);
static_assert(std::is_trivially_copyable_v<math::rect>);
static_assert(std::is_trivially_copyable_v<math::aabb>);
static_assert(std::is_trivially_copyable_v<math::bounding_frustum>);
static_assert(!std::is_same_v<math::color, math::vector4>);
static_assert(std::is_same_v<decltype(FrameCameraSnapshot::view), math::matrix4x4>);
static_assert(std::is_same_v<decltype(FrameCameraSnapshot::eyePosition), math::vector3>);
static_assert(std::is_standard_layout_v<FrameCameraSnapshot>);
static_assert(std::is_trivially_copyable_v<FrameCameraSnapshot>);

static_assert(offsetof(math::vector3, z) == 8);
static_assert(offsetof(math::color, a) == 12);
static_assert(offsetof(math::rect, height) == 12);
static_assert(offsetof(math::sphere, radius) == 12);
static_assert(offsetof(math::aabb, extents) == 12);
static_assert(offsetof(math::bounding_frustum, orientation) == 12);
static_assert(offsetof(math::bounding_frustum, near_plane) == 44);

constexpr math::matrix4x4 constexpr_translation =
    math::translation_matrix(math::vector3{10.0f, 20.0f, 30.0f});
static_assert(constexpr_translation.m[3][0] == 10.0f);
static_assert(constexpr_translation.m[0][3] == 0.0f);
static_assert(math::transform_point(math::vector3{1.0f, 2.0f, 3.0f},
                                    constexpr_translation) ==
              math::vector3{11.0f, 22.0f, 33.0f});
static_assert(math::aabb{}.is_empty());
static_assert(math::sphere{}.radius == 0.0f);
static_assert(math::color{}.a == 1.0f);

int main()
{
    int failures = 0;

    const math::vector3 axis = math::normalize(math::vector3{1.0f, 2.0f, -0.5f});
    const math::vector3 scale{-2.0f, 0.5f, 1.25f};
    const math::vector3 translation{8.0f, -4.0f, 2.0f};
    const math::quaternion rotation = math::quaternion_from_axis_angle(axis, 0.73f);
    const math::matrix4x4 world = math::compose(scale, rotation, translation);

    const DirectX::XMVECTOR dx_axis =
        DirectX::XMVectorSet(axis.x, axis.y, axis.z, 0.0f);
    const DirectX::XMVECTOR dx_rotation =
        DirectX::XMQuaternionRotationAxis(dx_axis, 0.73f);
    const DirectX::XMMATRIX dx_world = DirectX::XMMatrixAffineTransformation(
        DirectX::XMVectorSet(scale.x, scale.y, scale.z, 0.0f),
        DirectX::XMVectorZero(), dx_rotation,
        DirectX::XMVectorSet(translation.x, translation.y, translation.z, 0.0f));

    Check(math::near_equal(world, FromDirectX(dx_world), epsilon),
          "row-major S*R*T compose matches DirectXMath", failures);
    Check(Near(world.m[3][0], translation.x) && Near(world.m[0][3], 0.0f),
          "translation is stored in row 3", failures);

    const math::vector3 point{1.0f, -2.0f, 0.5f};
    const DirectX::XMVECTOR dx_point =
        DirectX::XMVectorSet(point.x, point.y, point.z, 1.0f);
    Check(math::near_equal(
              math::transform_point(point, world),
              FromDirectX3(DirectX::XMVector3TransformCoord(dx_point, dx_world)),
              epsilon),
          "transform_point matches XMVector3TransformCoord", failures);

    const math::quaternion qa =
        math::quaternion_from_axis_angle(math::vector3::unit_z(), 0.7f);
    const math::quaternion qb =
        math::quaternion_from_axis_angle(math::vector3::unit_x(), 0.4f);
    const DirectX::XMVECTOR dx_qa = DirectX::XMQuaternionRotationAxis(
        DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), 0.7f);
    const DirectX::XMVECTOR dx_qb = DirectX::XMQuaternionRotationAxis(
        DirectX::XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), 0.4f);
    Check(math::near_equal(
              qa * qb,
              FromDirectXQuaternion(DirectX::XMQuaternionMultiply(dx_qa, dx_qb)),
              epsilon),
          "quaternion multiplication order matches DirectXMath", failures);
    Check(math::near_equal(math::rotate(math::rotate(point, qa), qb),
                           math::rotate(point, qa * qb), epsilon),
          "quaternion composition reads left to right", failures);

    const math::aabb local_box{
        math::vector3{2.0f, -1.0f, 3.0f},
        math::vector3{1.5f, 0.75f, 2.25f}};
    const math::aabb transformed_box = math::transform(local_box, world);
    const DirectX::BoundingBox dx_local_box{
        DirectX::XMFLOAT3{local_box.center.x, local_box.center.y, local_box.center.z},
        DirectX::XMFLOAT3{local_box.extents.x, local_box.extents.y,
                          local_box.extents.z}};
    DirectX::BoundingBox dx_transformed_box{};
    dx_local_box.Transform(dx_transformed_box, dx_world);
    Check(math::near_equal(
              transformed_box,
              math::aabb{
                  math::vector3{dx_transformed_box.Center.x,
                                dx_transformed_box.Center.y,
                                dx_transformed_box.Center.z},
                  math::vector3{dx_transformed_box.Extents.x,
                                dx_transformed_box.Extents.y,
                                dx_transformed_box.Extents.z}},
              epsilon),
          "AABB affine transform matches BoundingBox::Transform", failures);

    constexpr float fov = 1.1f;
    constexpr float aspect = 1.4f;
    constexpr float near_z = 0.5f;
    constexpr float far_z = 120.0f;
    const math::matrix4x4 projection =
        math::perspective_fov_lh(fov, aspect, near_z, far_z);
    const DirectX::XMMATRIX dx_projection =
        DirectX::XMMatrixPerspectiveFovLH(fov, aspect, near_z, far_z);
    Check(math::near_equal(projection, FromDirectX(dx_projection), epsilon),
          "LH perspective projection matches DirectXMath", failures);

    FrameCameraSnapshot camera{};
    camera.eyePosition = math::vector3{3.0f, 2.0f, -7.0f};
    camera.forward = math::normalize(math::vector3{-0.2f, 0.1f, 1.0f});
    camera.right = math::normalize(math::cross(math::vector3::unit_y(), camera.forward));
    camera.up = math::cross(camera.forward, camera.right);
    camera.fov = 60.0f;
    camera.nearPlane = 0.2f;
    camera.farPlane = 350.0f;
    camera.view = math::look_to_lh(camera.eyePosition, camera.forward, camera.up);
    camera.projection = math::perspective_fov_lh(
        math::radians(camera.fov), aspect, camera.nearPlane, camera.farPlane);
    camera.inverseView = math::inverse(camera.view);
    camera.inverseProjection = math::inverse(camera.projection);

    const DirectX::XMVECTOR dx_camera_eye = DirectX::XMVectorSet(
        camera.eyePosition.x, camera.eyePosition.y, camera.eyePosition.z, 1.0f);
    const DirectX::XMVECTOR dx_camera_forward = DirectX::XMVectorSet(
        camera.forward.x, camera.forward.y, camera.forward.z, 0.0f);
    const DirectX::XMVECTOR dx_camera_up = DirectX::XMVectorSet(
        camera.up.x, camera.up.y, camera.up.z, 0.0f);
    const DirectX::XMMATRIX dx_camera_view = DirectX::XMMatrixLookToLH(
        dx_camera_eye, dx_camera_forward, dx_camera_up);
    const DirectX::XMMATRIX dx_camera_projection =
        DirectX::XMMatrixPerspectiveFovLH(
            DirectX::XMConvertToRadians(camera.fov), aspect,
            camera.nearPlane, camera.farPlane);

    Check(math::near_equal(camera.view, FromDirectX(dx_camera_view), epsilon),
          "FrameCameraSnapshot LH view matches DirectXMath", failures);
    Check(math::near_equal(camera.projection,
                           FromDirectX(dx_camera_projection), epsilon),
          "FrameCameraSnapshot degree FOV projection matches DirectXMath", failures);
    Check(math::near_equal(camera.view * camera.projection,
                           FromDirectX(DirectX::XMMatrixMultiply(
                               dx_camera_view, dx_camera_projection)), epsilon),
          "FrameCameraSnapshot view-projection order matches DirectXMath", failures);
    Check(math::near_equal(camera.view * camera.inverseView,
                           math::matrix4x4::identity(), 1.0e-3f) &&
              math::near_equal(camera.projection * camera.inverseProjection,
                               math::matrix4x4::identity(), 1.0e-3f),
          "FrameCameraSnapshot precomputed inverses close to identity", failures);

    const math::matrix4x4 bridge_round_trip = MathematicsInterop::FromDirectX(
        MathematicsInterop::ToDirectX(camera.view));
    Check(math::near_equal(camera.view, bridge_round_trip, epsilon),
          "camera matrix DirectX bridge round trip", failures);
    Check(Near(DirectX::XMVectorGetW(
                   MathematicsInterop::ToDirectXPoint(camera.eyePosition)), 1.0f) &&
              Near(DirectX::XMVectorGetW(
                   MathematicsInterop::ToDirectXDirection(camera.forward)), 0.0f),
          "camera vector bridge preserves point/direction w semantics", failures);

    const math::matrix4x4 orthographic =
        math::orthographic_lh(18.0f, 10.0f, camera.nearPlane, camera.farPlane);
    Check(math::near_equal(
              orthographic,
              FromDirectX(DirectX::XMMatrixOrthographicLH(
                  18.0f, 10.0f, camera.nearPlane, camera.farPlane)), epsilon),
          "FrameCameraSnapshot LH orthographic projection matches DirectXMath",
          failures);

    const math::bounding_frustum frustum =
        math::bounding_frustum_from_projection_lh(projection);
    DirectX::BoundingFrustum dx_frustum{};
    DirectX::BoundingFrustum::CreateFromMatrix(dx_frustum, dx_projection);
    Check(Near(frustum.right_slope, dx_frustum.RightSlope) &&
              Near(frustum.left_slope, dx_frustum.LeftSlope) &&
              Near(frustum.top_slope, dx_frustum.TopSlope) &&
              Near(frustum.bottom_slope, dx_frustum.BottomSlope) &&
              Near(frustum.near_plane, dx_frustum.Near) &&
              Near(frustum.far_plane, dx_frustum.Far, 1.0e-2f),
          "frustum projection fields match DirectXCollision", failures);

    const auto corners = frustum.corners();
    std::array<DirectX::XMFLOAT3, DirectX::BoundingFrustum::CORNER_COUNT>
        dx_corners{};
    dx_frustum.GetCorners(dx_corners.data());
    for (std::size_t i = 0; i < corners.size(); ++i)
    {
        Check(math::near_equal(
                  corners[i],
                  math::vector3{dx_corners[i].x, dx_corners[i].y,
                                dx_corners[i].z},
                  1.0e-2f),
              "frustum corner order matches DirectXCollision", failures);
    }

    const math::quaternion camera_rotation =
        math::quaternion_from_pitch_yaw_roll(0.2f, -0.4f, 0.1f);
    const math::vector3 camera_translation{4.0f, -2.0f, 7.0f};
    const math::matrix4x4 frustum_world = math::compose(
        math::vector3{2.0f, 2.0f, 2.0f}, camera_rotation,
        camera_translation);
    const math::bounding_frustum world_frustum =
        math::transform(frustum, frustum_world);
    DirectX::BoundingFrustum dx_world_frustum{};
    dx_frustum.Transform(dx_world_frustum, ToDirectX(frustum_world));
    const auto world_corners = world_frustum.corners();
    dx_world_frustum.GetCorners(dx_corners.data());
    for (std::size_t i = 0; i < world_corners.size(); ++i)
    {
        Check(math::near_equal(
                  world_corners[i],
                  math::vector3{dx_corners[i].x, dx_corners[i].y,
                                dx_corners[i].z},
                  2.0e-2f),
              "transformed frustum corners match DirectXCollision", failures);
    }

    Check(!math::try_bounding_frustum_from_projection_lh(math::matrix4x4{}),
          "singular projection is reported by the try API", failures);
    Check(math::contains(math::rect{0.0f, 0.0f, 10.0f, 10.0f},
                         math::vector2{0.0f, 0.0f}),
          "rect contains its minimum edge", failures);
    Check(!math::contains(math::rect{0.0f, 0.0f, 10.0f, 10.0f},
                          math::vector2{10.0f, 10.0f}),
          "rect excludes its maximum edge", failures);
    Check(math::pack_rgba8(math::color::red()) == 0xff0000ffu,
          "color RGBA8 packing is explicit", failures);

    if (failures != 0)
    {
        std::fprintf(stderr, "[MATHEMATICS CONTRACT] %d failure(s).\n", failures);
        return 1;
    }

    std::puts("[MATHEMATICS CONTRACT] passed: layout, conventions and DirectX parity.");
    return 0;
}
