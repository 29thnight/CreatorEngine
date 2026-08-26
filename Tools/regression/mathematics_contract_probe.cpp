#include <mathematics/mathematics.hpp>

#include "FrameCameraSnapshot.h"
#include "MathematicsInterop.h"
#include "PhysicsMathAdapter.h"
#include "TransformStore.h"

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
static_assert(std::is_same_v<decltype(TransformStore::localMatrix),
                             std::vector<math::matrix4x4>>);
static_assert(std::is_same_v<decltype(TransformStore::worldMatrix),
                             std::vector<math::matrix4x4>>);
static_assert(std::is_same_v<decltype(TransformStore::worldPosition),
                             std::vector<math::vector4>>);

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

    TransformStore transform_store;
    transform_store.GrowOne();
    Check(transform_store.Size() == 1 &&
              transform_store.localMatrix[0] == math::matrix4x4::identity() &&
              transform_store.worldMatrix[0] == math::matrix4x4::identity() &&
              transform_store.dirty[0] == 1 &&
              transform_store.worldChanged[0] == 1 &&
              transform_store.worldScale[0] == math::vector4{1.0f, 1.0f, 1.0f, 1.0f} &&
              transform_store.worldQuaternion[0] == math::vector4{0.0f, 0.0f, 0.0f, 1.0f} &&
              transform_store.worldPosition[0] == math::vector4{0.0f, 0.0f, 0.0f, 1.0f},
          "TransformStore grow initializes Mathematics values", failures);
    transform_store.localMatrix[0] = math::translation_matrix(
        math::vector3{3.0f, 4.0f, 5.0f});
    transform_store.worldPosition[0] = math::vector4{3.0f, 4.0f, 5.0f, 0.0f};
    transform_store.dirty[0] = 0;
    transform_store.worldChanged[0] = 0;
    transform_store.ResetSlot(0);
    Check(transform_store.localMatrix[0] == math::matrix4x4::identity() &&
              transform_store.worldPosition[0] == math::vector4{0.0f, 0.0f, 0.0f, 1.0f} &&
              transform_store.dirty[0] == 1 &&
              transform_store.worldChanged[0] == 1,
          "TransformStore reset restores slot invariants", failures);
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
    Check(math::near_equal(world.translation(), translation, epsilon),
          "matrix translation accessor preserves the draw origin", failures);
    Check(Near(math::length(world.right()),
               DirectX::XMVectorGetX(DirectX::XMVector3Length(dx_world.r[0]))) &&
              Near(math::length(world.up()),
               DirectX::XMVectorGetX(DirectX::XMVector3Length(dx_world.r[1]))) &&
              Near(math::length(world.forward()),
               DirectX::XMVectorGetX(DirectX::XMVector3Length(dx_world.r[2]))),
          "matrix basis lengths match shadow culling scale", failures);
    Check(math::near_equal(math::transpose(world),
                           FromDirectX(DirectX::XMMatrixTranspose(dx_world)), epsilon),
          "transposed draw matrix matches GPU staging convention", failures);
    Check(math::near_equal(
              math::transpose(math::inverse(world)),
              FromDirectX(DirectX::XMMatrixTranspose(
                  DirectX::XMMatrixInverse(nullptr, dx_world))), epsilon),
          "transposed inverse-world matches decal GPU staging convention", failures);

    const math::vector3 foliage_euler_degrees{17.0f, -63.0f, 121.0f};
    const math::matrix4x4 foliage_world =
        math::scaling_matrix(scale) *
        math::rotation_x(math::radians(foliage_euler_degrees.x)) *
        math::rotation_y(math::radians(foliage_euler_degrees.y)) *
        math::rotation_z(math::radians(foliage_euler_degrees.z)) *
        math::translation_matrix(translation);
    const DirectX::XMMATRIX dx_foliage_world =
        DirectX::XMMatrixScaling(scale.x, scale.y, scale.z) *
        DirectX::XMMatrixRotationX(DirectX::XMConvertToRadians(foliage_euler_degrees.x)) *
        DirectX::XMMatrixRotationY(DirectX::XMConvertToRadians(foliage_euler_degrees.y)) *
        DirectX::XMMatrixRotationZ(DirectX::XMConvertToRadians(foliage_euler_degrees.z)) *
        DirectX::XMMatrixTranslation(translation.x, translation.y, translation.z);
    Check(math::near_equal(foliage_world, FromDirectX(dx_foliage_world), epsilon),
          "foliage S*Rx*Ry*Rz*T rebuild matches DirectXMath", failures);

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
    const math::aabb interop_local_box =
        MathematicsInterop::FromDirectX(dx_local_box);
    Check(math::near_equal(interop_local_box, local_box, epsilon),
          "BoundingBox to aabb bridge preserves center and extents", failures);
    const DirectX::BoundingBox interop_dx_box =
        MathematicsInterop::ToDirectX(interop_local_box);
    Check(Near(interop_dx_box.Center.x, dx_local_box.Center.x) &&
              Near(interop_dx_box.Center.y, dx_local_box.Center.y) &&
              Near(interop_dx_box.Center.z, dx_local_box.Center.z) &&
              Near(interop_dx_box.Extents.x, dx_local_box.Extents.x) &&
              Near(interop_dx_box.Extents.y, dx_local_box.Extents.y) &&
              Near(interop_dx_box.Extents.z, dx_local_box.Extents.z),
          "aabb to BoundingBox bridge preserves center and extents", failures);
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

    const math::vector4 clip_position{0.27f, -0.31f, 0.64f, 1.0f};
    const math::vector4 world_h = clip_position *
        math::inverse(camera.view * camera.projection);
    const math::vector3 unprojected{
        world_h.x / world_h.w,
        world_h.y / world_h.w,
        world_h.z / world_h.w};
    DirectX::XMVECTOR dx_world_h = DirectX::XMVector4Transform(
        DirectX::XMVectorSet(clip_position.x, clip_position.y,
                             clip_position.z, clip_position.w),
        DirectX::XMMatrixInverse(nullptr, DirectX::XMMatrixMultiply(
            dx_camera_view, dx_camera_projection)));
    dx_world_h = DirectX::XMVectorScale(
        dx_world_h, 1.0f / DirectX::XMVectorGetW(dx_world_h));
    Check(math::near_equal(unprojected, FromDirectX3(dx_world_h), 1.0e-3f),
          "camera clip-to-world unprojection matches DirectXMath", failures);

    constexpr float rig_yaw = 0.37f;
    constexpr float rig_pitch = -0.21f;
    const math::quaternion rig_yaw_rotation =
        math::quaternion_from_axis_angle(math::vector3::unit_y(), rig_yaw);
    const math::vector3 rig_right_axis =
        math::rotate(math::vector3::unit_x(), rig_yaw_rotation);
    const math::quaternion rig_pitch_rotation =
        math::quaternion_from_axis_angle(rig_right_axis, rig_pitch);
    const math::quaternion rig_rotation =
        math::normalize(rig_yaw_rotation * rig_pitch_rotation);
    const math::vector3 rig_forward = math::normalize(
        math::rotate(math::vector3::unit_z(), rig_rotation));
    const math::vector3 rig_up = math::normalize(
        math::rotate(math::vector3::unit_y(), rig_rotation));

    const DirectX::XMVECTOR dx_rig_yaw = DirectX::XMQuaternionRotationAxis(
        DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), rig_yaw);
    const DirectX::XMVECTOR dx_rig_right = DirectX::XMVector3Rotate(
        DirectX::XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), dx_rig_yaw);
    const DirectX::XMVECTOR dx_rig_pitch = DirectX::XMQuaternionRotationAxis(
        dx_rig_right, rig_pitch);
    const DirectX::XMVECTOR dx_rig_rotation = DirectX::XMQuaternionNormalize(
        DirectX::XMQuaternionMultiply(dx_rig_yaw, dx_rig_pitch));
    Check(math::near_equal(
              rig_forward,
              FromDirectX3(DirectX::XMVector3Normalize(DirectX::XMVector3Rotate(
                  DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f),
                  dx_rig_rotation))), epsilon) &&
              math::near_equal(
                  rig_up,
                  FromDirectX3(DirectX::XMVector3Normalize(DirectX::XMVector3Rotate(
                      DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f),
                      dx_rig_rotation))), epsilon),
          "editor camera yaw-pitch basis matches DirectXMath", failures);

    Check(math::near_equal(camera.view * camera.inverseView,
                           math::matrix4x4::identity(), 1.0e-3f) &&
              math::near_equal(camera.projection * camera.inverseProjection,
                               math::matrix4x4::identity(), 1.0e-3f),
          "FrameCameraSnapshot precomputed inverses close to identity", failures);
    Check(math::near_equal(
              math::transpose(camera.inverseView),
              FromDirectX(DirectX::XMMatrixTranspose(
                  DirectX::XMMatrixInverse(nullptr, dx_camera_view))), 1.0e-3f) &&
              math::near_equal(
                  math::transpose(camera.inverseProjection),
                  FromDirectX(DirectX::XMMatrixTranspose(
                      DirectX::XMMatrixInverse(nullptr, dx_camera_projection))), 1.0e-3f),
          "decal inverse camera constant staging matches DirectXMath", failures);
    Check(math::near_equal(
              math::transpose(camera.view * camera.projection),
              FromDirectX(DirectX::XMMatrixTranspose(
                  DirectX::XMMatrixMultiply(dx_camera_view, dx_camera_projection))),
              epsilon),
          "decal, sprite, and GBuffer view-projection staging matches DirectXMath",
          failures);
    Check(math::near_equal(
              math::transpose(math::matrix4x4::identity()),
              math::matrix4x4::identity(), epsilon),
          "GBuffer empty bone palette fallback uploads identity", failures);

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
    Check(!math::intersects(math::rect{0.0f, 0.0f, 10.0f, 10.0f},
                            math::rect{10.0f, 0.0f, 5.0f, 10.0f}),
          "rectangles sharing an edge do not overlap", failures);
    Check(!math::contains(math::rect{0.0f, 0.0f, 0.0f, 10.0f},
                          math::vector2{0.0f, 5.0f}) &&
              !math::contains(math::rect{10.0f, 0.0f, -10.0f, 10.0f},
                              math::vector2{5.0f, 5.0f}),
          "empty and negative-size rectangles reject hit tests", failures);
    Check(math::normalized(math::rect{10.0f, 10.0f, -4.0f, -6.0f}) ==
              math::rect{6.0f, 4.0f, 4.0f, 6.0f},
          "negative-size rect normalization is explicit", failures);
    Check(math::pack_rgba8(math::color::red()) == 0xff0000ffu,
          "color RGBA8 packing is explicit", failures);

    const math::vector3 physics_vector{-3.5f, 2.25f, 17.0f};
    const physx::PxVec3 px_vector = PhysicsMath::ToPx(physics_vector);
    const math::vector3 physics_vector_round_trip = PhysicsMath::FromPx(px_vector);
    Check(physics_vector_round_trip == physics_vector &&
              Near(px_vector.x, physics_vector.x) &&
              Near(px_vector.y, physics_vector.y) &&
              Near(px_vector.z, physics_vector.z),
          "PhysX vector adapter preserves xyz without raw copies", failures);

    const math::quaternion physics_rotation = math::normalize(
        math::quaternion{0.2f, -0.3f, 0.4f, 0.8f});
    const physx::PxTransform px_transform = PhysicsMath::ToPxTransform(
        physics_vector, physics_rotation);
    math::vector3 physics_position_round_trip{};
    math::quaternion physics_rotation_round_trip{};
    PhysicsMath::FromPxTransform(px_transform, physics_position_round_trip,
                                 physics_rotation_round_trip);
    Check(physics_position_round_trip == physics_vector &&
              Near(physics_rotation_round_trip.x, physics_rotation.x) &&
              Near(physics_rotation_round_trip.y, physics_rotation.y) &&
              Near(physics_rotation_round_trip.z, physics_rotation.z) &&
              Near(physics_rotation_round_trip.w, physics_rotation.w),
          "PhysX transform adapter preserves position and quaternion xyzw",
          failures);

    const math::vector3 actor_position{4.0f, -2.0f, 7.5f};
    const math::quaternion actor_rotation = math::normalize(
        math::quaternion_from_pitch_yaw_roll(0.3f, -0.7f, 0.2f));
    const math::vector3 collider_offset{0.5f, 1.25f, -0.75f};
    const math::quaternion collider_rotation_offset = math::normalize(
        math::quaternion_from_axis_angle(math::vector3::unit_y(), 0.4f));
    const math::vector3 collider_position = actor_position +
        math::rotate(collider_offset, actor_rotation);
    const math::quaternion collider_rotation =
        collider_rotation_offset * actor_rotation;

    const physx::PxTransform px_collider = PhysicsMath::ToPxTransform(
        collider_position, collider_rotation);
    math::vector3 collider_position_round_trip{};
    math::quaternion collider_rotation_round_trip{};
    PhysicsMath::FromPxTransform(px_collider, collider_position_round_trip,
                                 collider_rotation_round_trip);

    const math::quaternion recovered_actor_rotation =
        math::inverse(collider_rotation_offset) *
        collider_rotation_round_trip;
    const math::vector3 recovered_actor_position =
        collider_position_round_trip -
        math::rotate(collider_offset, recovered_actor_rotation);
    Check(math::near_equal(recovered_actor_position, actor_position, epsilon) &&
              math::same_rotation(recovered_actor_rotation, actor_rotation, epsilon),
          "rigid actor offset pose survives Mathematics-PhysX round trip",
          failures);

    const math::vector3 cct_position{128.25f, -3.5f, 2048.75f};
    const physx::PxExtendedVec3 px_cct_position =
        PhysicsMath::ToPxExtended(cct_position);
    Check(math::near_equal(PhysicsMath::FromPx(px_cct_position), cct_position,
                           epsilon),
          "CCT extended position adapter preserves float coordinates",
          failures);

    const math::quaternion cct_world_rotation =
        math::quaternion_from_pitch_yaw_roll(0.2f, -0.6f, 0.15f);
    const float cct_current_yaw = math::to_euler(cct_world_rotation).y;
    const float cct_target_yaw = 1.1f;
    const float cct_rotation_t = 0.35f;
    const math::quaternion cct_rotation = math::slerp(
        math::quaternion_from_pitch_yaw_roll(0.0f, cct_current_yaw, 0.0f),
        math::quaternion_from_pitch_yaw_roll(0.0f, cct_target_yaw, 0.0f),
        cct_rotation_t);

    const auto dx_cct_world_rotation =
        DirectX::XMQuaternionRotationRollPitchYaw(0.2f, -0.6f, 0.15f);
    const math::vector3 dx_cct_euler =
        math::to_euler(FromDirectXQuaternion(dx_cct_world_rotation));
    const auto dx_cct_rotation = DirectX::XMQuaternionSlerp(
        DirectX::XMQuaternionRotationRollPitchYaw(0.0f, dx_cct_euler.y, 0.0f),
        DirectX::XMQuaternionRotationRollPitchYaw(0.0f, cct_target_yaw, 0.0f),
        cct_rotation_t);
    Check(math::same_rotation(
              cct_rotation, FromDirectXQuaternion(dx_cct_rotation), epsilon),
          "CCT yaw-only auto rotation matches DirectX quaternion slerp",
          failures);

    const math::vector3 ragdoll_scale{1.25f, 0.8f, 1.1f};
    const math::quaternion ragdoll_rotation = math::normalize(
        math::quaternion_from_pitch_yaw_roll(-0.2f, 0.45f, 0.1f));
    const math::vector3 ragdoll_position{3.0f, 4.5f, -2.0f};
    const math::matrix4x4 ragdoll_world = math::compose(
        ragdoll_scale, ragdoll_rotation, ragdoll_position);
    math::vector3 preserved_scale{};
    math::quaternion ignored_rotation{};
    math::vector3 ignored_position{};
    const bool ragdoll_decomposed = math::decompose(
        ragdoll_world, preserved_scale, ignored_rotation, ignored_position);
    const physx::PxTransform px_ragdoll_pose =
        PhysicsMath::ToPxTransform(ragdoll_position, ragdoll_rotation);
    math::vector3 ragdoll_position_round_trip{};
    math::quaternion ragdoll_rotation_round_trip{};
    PhysicsMath::FromPxTransform(
        px_ragdoll_pose, ragdoll_position_round_trip,
        ragdoll_rotation_round_trip);
    const math::matrix4x4 ragdoll_world_round_trip = math::compose(
        preserved_scale, ragdoll_rotation_round_trip,
        ragdoll_position_round_trip);
    Check(ragdoll_decomposed &&
              math::near_equal(ragdoll_world_round_trip, ragdoll_world, epsilon),
          "ragdoll root pose preserves authored scale across PhysX round trip",
          failures);

    physx::PxTransform moved_ragdoll_pose = px_ragdoll_pose;
    moved_ragdoll_pose.p.x += 0.01f;
    physx::PxTransform sign_equivalent_pose = px_ragdoll_pose;
    sign_equivalent_pose.q = physx::PxQuat{
        -sign_equivalent_pose.q.x, -sign_equivalent_pose.q.y,
        -sign_equivalent_pose.q.z, -sign_equivalent_pose.q.w};
    Check(!PhysicsMath::IsTransformDifferent(
              px_ragdoll_pose, px_ragdoll_pose) &&
              PhysicsMath::IsTransformDifferent(
                  px_ragdoll_pose, moved_ragdoll_pose) &&
              !PhysicsMath::IsTransformDifferent(
                  px_ragdoll_pose, sign_equivalent_pose),
          "PhysX transform dirty check handles motion and quaternion sign",
          failures);

    const math::matrix4x4 parent_joint_global = math::compose(
        math::vector3::one(),
        math::quaternion_from_pitch_yaw_roll(0.1f, -0.3f, 0.2f),
        math::vector3{-1.0f, 2.0f, 0.5f});
    const math::matrix4x4 child_joint_global = math::compose(
        math::vector3::one(),
        math::quaternion_from_pitch_yaw_roll(-0.15f, 0.25f, 0.35f),
        math::vector3{1.5f, 3.0f, -0.75f});
    const math::matrix4x4 simulated_local =
        child_joint_global * math::inverse(parent_joint_global);
    Check(math::near_equal(
              simulated_local * parent_joint_global,
              child_joint_global, 1.0e-3f),
          "ragdoll simulated local transform preserves row-vector order",
          failures);

    if (failures != 0)
    {
        std::fprintf(stderr, "[MATHEMATICS CONTRACT] %d failure(s).\n", failures);
        return 1;
    }

    std::puts("[MATHEMATICS CONTRACT] passed: layout, conventions and DirectX parity.");
    return 0;
}
