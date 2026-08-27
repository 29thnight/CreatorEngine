#include <mathematics/mathematics.hpp>

#include "FrameCameraSnapshot.h"
#include "PhysicsMathAdapter.h"
#include "TransformStore.h"
#include "TweenManager.h"

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

math::matrix4x4 ReferencePerspectiveFovLH(
    float fov, float aspect, float near_plane, float far_plane) noexcept
{
    const float y_scale = 1.0f / std::tan(fov * 0.5f);
    const float x_scale = y_scale / aspect;
    const float depth_scale = far_plane / (far_plane - near_plane);
    return math::matrix4x4{
        x_scale, 0.0f, 0.0f, 0.0f,
        0.0f, y_scale, 0.0f, 0.0f,
        0.0f, 0.0f, depth_scale, 1.0f,
        0.0f, 0.0f, -near_plane * depth_scale, 0.0f};
}

math::matrix4x4 ReferenceOrthographicLH(
    float width, float height, float near_plane, float far_plane) noexcept
{
    const float inverse_depth = 1.0f / (far_plane - near_plane);
    return math::matrix4x4{
        2.0f / width, 0.0f, 0.0f, 0.0f,
        0.0f, 2.0f / height, 0.0f, 0.0f,
        0.0f, 0.0f, inverse_depth, 0.0f,
        0.0f, 0.0f, -near_plane * inverse_depth, 1.0f};
}

struct TweenProbeContext;
using TweenProbeManager = BasicTweenManager<TweenProbeContext>;

struct TweenProbeContext
{
    TweenProbeManager* manager{ nullptr };
    EntityHandle liveTarget{};
    float scalarValue{ 0.0f };
    math::vector3 vectorValue{};
    int scalarApplyCount{ 0 };
    int vectorApplyCount{ 0 };
    uint64_t lastApplyToken{ 0 };
    int completionCount{ 0 };
    std::array<TweenEndReason, 16> completionReasons{};
    std::array<uint64_t, 16> completionTokens{};
    bool targetAlive{ true };
    bool bindingAlive{ true };
    bool spawnOnApply{ false };
    bool spawnOnCompletion{ false };
    TweenHandle<float> spawned{};
};

TweenApplyResult ApplyProbeScalar(
    TweenProbeContext& context, EntityHandle target, const float& value,
    uint64_t token) noexcept
{
    if (!context.targetAlive || target != context.liveTarget)
        return TweenApplyResult::TargetLost;
    if (!context.bindingAlive) return TweenApplyResult::BindingLost;

    context.scalarValue = value;
    context.lastApplyToken = token;
    ++context.scalarApplyCount;
    if (context.spawnOnApply)
    {
        context.spawnOnApply = false;
        context.spawned = context.manager->Play<float>(
            context.liveTarget, math::make_tween(0.0f, 1.0f, 1.0f),
            &ApplyProbeScalar);
    }
    return TweenApplyResult::Applied;
}

TweenApplyResult ApplyProbeVector(
    TweenProbeContext& context, EntityHandle target,
    const math::vector3& value, uint64_t token) noexcept
{
    if (!context.targetAlive || target != context.liveTarget)
        return TweenApplyResult::TargetLost;
    if (!context.bindingAlive) return TweenApplyResult::BindingLost;

    context.vectorValue = value;
    context.lastApplyToken = token;
    ++context.vectorApplyCount;
    return TweenApplyResult::Applied;
}

void CompleteProbeScalar(
    TweenProbeContext& context, TweenHandle<float>, TweenEndReason reason,
    uint64_t token) noexcept
{
    if (context.completionCount <
        static_cast<int>(context.completionReasons.size()))
    {
        const size_t index = static_cast<size_t>(context.completionCount);
        context.completionReasons[index] = reason;
        context.completionTokens[index] = token;
    }
    ++context.completionCount;

    if (context.spawnOnCompletion)
    {
        context.spawnOnCompletion = false;
        context.spawned = context.manager->Play<float>(
            context.liveTarget, math::make_tween(0.0f, 1.0f, 1.0f),
            &ApplyProbeScalar);
    }
}

void RunTweenManagerContracts(int& failures)
{
    TweenProbeManager manager;
    TweenProbeContext context;
    context.manager = &manager;
    context.liveTarget = EntityHandle{ 77u, 4u, 9u };

    const TweenHandle<float> invalid = manager.Play<float>(
        EntityHandle{}, math::make_tween(0.0f, 1.0f, 1.0f),
        &ApplyProbeScalar);
    Check(!invalid.IsValid(),
          "TweenManager rejects an invalid EntityHandle", failures);

    const TweenHandle<float> scalar = manager.Play<float>(
        context.liveTarget, math::make_tween(0.0f, 10.0f, 2.0f),
        &ApplyProbeScalar, &CompleteProbeScalar, 1001u);
    Check(scalar.IsValid() && manager.Contains(scalar) &&
              manager.ActiveCount<float>() == 1u &&
              manager.ActiveCount() == 1u,
          "TweenManager stores a scalar tween in its typed pool", failures);

    manager.Update(1.0f, context);
    const std::optional<float> midpoint = manager.Sample(scalar);
    Check(Near(context.scalarValue, 5.0f) && midpoint.has_value() &&
              Near(*midpoint, 5.0f) && context.scalarApplyCount == 1 &&
              context.lastApplyToken == 1001u && context.completionCount == 0,
          "TweenManager advances a live target with opaque binding data",
          failures);

    Check(manager.Pause(scalar) &&
              manager.State(scalar) == math::tween_state::paused,
          "TweenManager pauses a live handle", failures);
    manager.Update(0.5f, context);
    Check(context.scalarApplyCount == 1 && Near(context.scalarValue, 5.0f),
          "paused manager tracks do not reapply values", failures);
    Check(manager.Resume(scalar), "TweenManager resumes a live handle", failures);

    manager.Update(1.0f, context);
    Check(!manager.Contains(scalar) && manager.ActiveCount() == 0u &&
              Near(context.scalarValue, 10.0f) &&
              context.completionCount == 1 &&
              context.completionReasons[0] == TweenEndReason::Completed &&
              context.completionTokens[0] == 1001u,
          "completion applies the endpoint, sweeps, then dispatches", failures);

    const TweenHandle<float> cancelled = manager.Play<float>(
        context.liveTarget, math::make_tween(0.0f, 2.0f, 1.0f),
        &ApplyProbeScalar, &CompleteProbeScalar, 1002u);
    Check(cancelled.slot == scalar.slot &&
              cancelled.generation != scalar.generation,
          "reused TweenManager slots advance generation", failures);
    const int applyCountBeforeCancel = context.scalarApplyCount;
    Check(manager.Cancel(cancelled) && !manager.Contains(cancelled) &&
              context.completionCount == 1,
          "cancel marks a handle without firing inline callback", failures);
    manager.Update(0.0f, context);
    Check(context.scalarApplyCount == applyCountBeforeCancel &&
              context.completionCount == 2 &&
              context.completionReasons[1] == TweenEndReason::Cancelled &&
              context.completionTokens[1] == 1002u,
          "cancel callback is deferred until sweep", failures);

    context.targetAlive = false;
    const TweenHandle<float> lostTarget = manager.Play<float>(
        context.liveTarget, math::make_tween(0.0f, 1.0f, 1.0f),
        &ApplyProbeScalar, &CompleteProbeScalar, 1003u);
    manager.Update(0.25f, context);
    Check(!manager.Contains(lostTarget) && context.completionCount == 3 &&
              context.completionReasons[2] == TweenEndReason::TargetLost,
          "target loss retires a tween without storing a raw target", failures);

    context.targetAlive = true;
    context.bindingAlive = false;
    const TweenHandle<float> lostBinding = manager.Play<float>(
        context.liveTarget, math::make_tween(0.0f, 1.0f, 1.0f),
        &ApplyProbeScalar, &CompleteProbeScalar, 1004u);
    manager.Update(0.25f, context);
    Check(!manager.Contains(lostBinding) && context.completionCount == 4 &&
              context.completionReasons[3] == TweenEndReason::BindingLost,
          "component/property binding loss has a distinct terminal reason",
          failures);

    context.bindingAlive = true;
    const TweenHandle<math::vector3> vector = manager.Play<math::vector3>(
        context.liveTarget,
        math::make_tween(math::vector3{}, math::vector3{2.0f, 4.0f, 6.0f}, 2.0f),
        &ApplyProbeVector);
    manager.Update(1.0f, context);
    Check(vector.IsValid() && manager.Contains(vector) &&
              context.vectorValue == math::vector3{1.0f, 2.0f, 3.0f} &&
              manager.ActiveCount<math::vector3>() == 1u,
          "vector values use a separate typed pool", failures);
    manager.Update(1.0f, context);
    Check(!manager.Contains(vector) &&
              context.vectorValue == math::vector3{2.0f, 4.0f, 6.0f},
          "typed vector pool applies and retires its endpoint", failures);

    context.spawned = {};
    context.spawnOnApply = true;
    const int beforeReentrantApply = context.scalarApplyCount;
    const TweenHandle<float> reentrantParent = manager.Play<float>(
        context.liveTarget, math::make_tween(0.0f, 2.0f, 2.0f),
        &ApplyProbeScalar);
    manager.Update(1.0f, context);
    Check(context.spawned.IsValid() && manager.Contains(context.spawned) &&
              manager.Contains(reentrantParent) &&
              manager.ActiveCount<float>() == 2u &&
              context.scalarApplyCount == beforeReentrantApply + 1,
          "Play during apply is deferred to the next manager update", failures);
    Check(manager.Cancel(reentrantParent) && manager.Cancel(context.spawned),
          "reentrant tracks remain cancellable by generation handle", failures);
    manager.Update(0.0f, context);

    context.spawned = {};
    context.spawnOnCompletion = true;
    const int beforeCompletionSpawn = context.scalarApplyCount;
    const TweenHandle<float> completionParent = manager.Play<float>(
        context.liveTarget, math::make_tween(3.0f, 7.0f, 0.0f),
        &ApplyProbeScalar, &CompleteProbeScalar, 1005u);
    manager.Update(0.0f, context);
    Check(!manager.Contains(completionParent) && context.spawned.IsValid() &&
              manager.Contains(context.spawned) &&
              context.scalarApplyCount == beforeCompletionSpawn + 1 &&
              context.completionCount == 5 &&
              context.completionReasons[4] == TweenEndReason::Completed,
          "completion callback runs after sweep and may enqueue the next tween",
          failures);
    Check(manager.Cancel(context.spawned),
          "completion-spawned tween returns a live handle", failures);
    manager.Update(0.0f, context);

    const int completionsBeforeClear = context.completionCount;
    const TweenHandle<float> cleared = manager.Play<float>(
        context.liveTarget, math::make_tween(0.0f, 1.0f, 1.0f),
        &ApplyProbeScalar, &CompleteProbeScalar, 1006u);
    manager.Clear();
    manager.Update(0.0f, context);
    Check(!manager.Contains(cleared) && manager.ActiveCount() == 0u &&
              context.completionCount == completionsBeforeClear,
          "Clear invalidates handles without teardown callbacks", failures);

    const TweenHandle<float> afterClear = manager.Play<float>(
        context.liveTarget, math::make_tween(0.0f, 1.0f, 1.0f),
        &ApplyProbeScalar);
    Check(afterClear.IsValid() && afterClear.slot == cleared.slot &&
              afterClear.generation != cleared.generation,
          "Clear preserves slot generations against ABA", failures);
    const int beforeInvalidDelta = context.scalarApplyCount;
    manager.Update(-1.0f, context);
    Check(manager.Contains(afterClear) &&
              context.scalarApplyCount == beforeInvalidDelta,
          "invalid frame delta is a manager no-op", failures);
    manager.Clear();
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
static_assert(sizeof(math::easing_function) == sizeof(void*));
static_assert(std::is_trivially_copyable_v<math::easing_function>);
static_assert(TweenManagedValue<float>);
static_assert(TweenManagedValue<math::quaternion>);
static_assert(!TweenManagedValue<int>);
static_assert(!std::is_same_v<TweenHandle<float>, TweenHandle<math::vector3>>);
static_assert(math::linearly_interpolable<float>);
static_assert(math::linearly_interpolable<math::vector3>);
static_assert(math::linearly_interpolable<math::color>);
static_assert(math::linearly_interpolable<math::rect>);
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

static_assert(math::easing::linear(0.25f) == 0.25f);
static_assert(math::ease_clamped(-1.0f, math::easing::quadratic_in) == 0.0f);
static_assert(math::ease_clamped(2.0f, math::easing::quadratic_in) == 1.0f);
static_assert(math::tween_value(
                  math::vector3{0.0f, 2.0f, 4.0f},
                  math::vector3{2.0f, 4.0f, 6.0f}, 0.5f,
                  math::easing::smoothstep) ==
              math::vector3{1.0f, 3.0f, 5.0f});

constexpr bool StatefulTweenContract() noexcept
{
    auto track = math::make_tween(0.0f, 10.0f, 2.0f,
                                  math::easing::smoothstep);
    const auto midpoint = track.advance(1.0f);
    return midpoint.state == math::tween_state::playing &&
           midpoint.completed_cycles == 0u && midpoint.value == 5.0f &&
           !track.finished();
}

static_assert(StatefulTweenContract());

int main()
{
    int failures = 0;

    Check(Near(math::easing::elastic_in_out(0.5f), 0.5f),
          "elastic in-out uses the continuous Mathematics midpoint",
          failures);

    auto playback = math::make_tween(
        math::vector3{8.0f, 0.0f, -4.0f}, math::vector3{}, 2.0f,
        math::easing::quadratic_in);
    const auto playback_step = playback.advance(1.0f);
    Check(playback_step.state == math::tween_state::playing &&
              playback_step.completed_cycles == 0u &&
              math::near_equal(
                  playback_step.value,
                  math::vector3{6.0f, 0.0f, -3.0f}, epsilon),
          "stateful tween advances vector values with the selected easing",
          failures);

    RunTweenManagerContracts(failures);

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

    const math::matrix4x4 expected_world{
        -1.5874252f, 0.0968929f, 1.2127213f, 0.0f,
         0.1212997f, 0.4696636f, 0.1212538f, 0.0f,
         0.6972780f, -0.4244800f, 0.9466362f, 0.0f,
         8.0f, -4.0f, 2.0f, 1.0f};
    Check(math::near_equal(world, expected_world, epsilon),
          "row-major S*R*T compose preserves the migration golden", failures);
    Check(Near(world.m[3][0], translation.x) && Near(world.m[0][3], 0.0f),
          "translation is stored in row 3", failures);
    Check(math::near_equal(world.translation(), translation, epsilon),
          "matrix translation accessor preserves the draw origin", failures);
    Check(Near(math::length(world.right()), std::abs(scale.x)) &&
              Near(math::length(world.up()), std::abs(scale.y)) &&
              Near(math::length(world.forward()), std::abs(scale.z)),
          "matrix basis lengths preserve authored scale", failures);
    Check(math::near_equal(math::transpose(math::transpose(world)), world,
                           epsilon),
          "draw matrix transpose is an involution", failures);
    Check(math::near_equal(
              math::transpose(math::inverse(world)) * math::transpose(world),
              math::matrix4x4::identity(), 1.0e-3f),
          "transposed inverse-world closes to identity in GPU order", failures);

    const math::vector3 foliage_euler_degrees{17.0f, -63.0f, 121.0f};
    const math::matrix4x4 foliage_world =
        math::scaling_matrix(scale) *
        math::rotation_x(math::radians(foliage_euler_degrees.x)) *
        math::rotation_y(math::radians(foliage_euler_degrees.y)) *
        math::rotation_z(math::radians(foliage_euler_degrees.z)) *
        math::translation_matrix(translation);
    const math::vector3 point{1.0f, -2.0f, 0.5f};
    math::vector3 expected_foliage_point{point.x * scale.x,
                                         point.y * scale.y,
                                         point.z * scale.z};
    expected_foliage_point = math::rotate(
        expected_foliage_point,
        math::quaternion_from_axis_angle(
            math::vector3::unit_x(), math::radians(foliage_euler_degrees.x)));
    expected_foliage_point = math::rotate(
        expected_foliage_point,
        math::quaternion_from_axis_angle(
            math::vector3::unit_y(), math::radians(foliage_euler_degrees.y)));
    expected_foliage_point = math::rotate(
        expected_foliage_point,
        math::quaternion_from_axis_angle(
            math::vector3::unit_z(), math::radians(foliage_euler_degrees.z)));
    expected_foliage_point += translation;
    Check(math::near_equal(math::transform_point(point, foliage_world),
                           expected_foliage_point, epsilon),
          "foliage S*Rx*Ry*Rz*T rebuild preserves operation order", failures);
    Check(math::near_equal(
              math::transform_point(point, world),
              math::rotate(
                  math::vector3{point.x * scale.x, point.y * scale.y,
                                point.z * scale.z}, rotation) + translation,
              epsilon),
          "transform_point applies scale, rotation, then translation", failures);

    const math::quaternion qa =
        math::quaternion_from_axis_angle(math::vector3::unit_z(), 0.7f);
    const math::quaternion qb =
        math::quaternion_from_axis_angle(math::vector3::unit_x(), 0.4f);
    Check(math::near_equal(math::rotate(math::rotate(point, qa), qb),
                           math::rotate(point, qa * qb), epsilon),
          "quaternion composition reads left to right", failures);

    const math::aabb local_box{
        math::vector3{2.0f, -1.0f, 3.0f},
        math::vector3{1.5f, 0.75f, 2.25f}};
    const math::aabb transformed_box = math::transform(local_box, world);
    std::array<math::vector3, 8> transformed_box_corners{};
    for (std::size_t i = 0; i < transformed_box_corners.size(); ++i)
    {
        transformed_box_corners[i] = math::transform_point(
            local_box.corner(static_cast<int>(i)), world);
    }
    Check(math::near_equal(
              transformed_box,
              math::aabb_from_points(transformed_box_corners), epsilon),
          "AABB affine transform encloses all transformed corners", failures);

    const math::aabb pick_box{
        math::vector3{0.0f, 0.0f, 5.0f},
        math::vector3{1.0f, 1.0f, 1.0f}};
    const std::array<math::ray, 4> pick_rays{
        math::ray{math::vector3{0.0f, 0.0f, 0.0f},
                  math::vector3{0.0f, 0.0f, 1.0f}},
        math::ray{math::vector3{0.0f, 0.0f, 10.0f},
                  math::vector3{0.0f, 0.0f, -1.0f}},
        math::ray{math::vector3{2.0f, 0.0f, 0.0f},
                  math::vector3{0.0f, 0.0f, 1.0f}},
        math::ray{math::vector3{0.0f, 0.0f, 0.0f},
                  math::vector3{0.0f, 1.0f, 0.0f}}};
    const std::array<bool, 4> expected_hits{true, true, false, false};
    const std::array<float, 4> expected_distances{4.0f, 4.0f, 0.0f, 0.0f};
    for (std::size_t i = 0; i < pick_rays.size(); ++i)
    {
        float hit_distance = 0.0f;
        const bool hit = math::raycast(pick_rays[i], pick_box, hit_distance);
        Check(hit == expected_hits[i] &&
                  (!hit || Near(hit_distance, expected_distances[i])),
              "normalized ray-AABB picking preserves hit and distance policy",
              failures);
    }
    float inside_distance = -1.0f;
    Check(math::raycast(
              math::ray{pick_box.center, math::vector3::unit_x()}, pick_box,
              inside_distance) && Near(inside_distance, 0.0f),
          "ray starting inside an AABB uses zero-distance picking policy",
          failures);
    float invalid_ray_distance = -1.0f;
    Check(!math::raycast(
              math::ray{math::vector3{}, math::vector3{}}, pick_box,
              invalid_ray_distance),
          "zero-direction ray is rejected", failures);

    constexpr float fov = 1.1f;
    constexpr float aspect = 1.4f;
    constexpr float near_z = 0.5f;
    constexpr float far_z = 120.0f;
    const math::matrix4x4 projection =
        math::perspective_fov_lh(fov, aspect, near_z, far_z);
    Check(math::near_equal(
              projection,
              ReferencePerspectiveFovLH(fov, aspect, near_z, far_z), epsilon),
          "LH perspective projection matches the scalar reference", failures);

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

    Check(math::near_equal(
              math::transform_point(camera.eyePosition, camera.view),
              math::vector3{}, epsilon) &&
              math::near_equal(
                  math::transform_direction(camera.right, camera.view),
                  math::vector3::unit_x(), epsilon) &&
              math::near_equal(
                  math::transform_direction(camera.up, camera.view),
                  math::vector3::unit_y(), epsilon) &&
              math::near_equal(
                  math::transform_direction(camera.forward, camera.view),
                  math::vector3::unit_z(), epsilon),
          "FrameCameraSnapshot LH view maps the camera basis to canonical axes",
          failures);
    Check(math::near_equal(
              camera.projection,
              ReferencePerspectiveFovLH(
                  math::radians(camera.fov), aspect,
                  camera.nearPlane, camera.farPlane), epsilon),
          "FrameCameraSnapshot degree FOV uses the scalar LH projection",
          failures);
    const math::vector4 camera_test_point{1.0f, -0.5f, 12.0f, 1.0f};
    Check(math::near_equal(
              (camera_test_point * camera.view) * camera.projection,
              camera_test_point * (camera.view * camera.projection), epsilon),
          "FrameCameraSnapshot view-projection preserves row-vector order",
          failures);

    const math::vector4 clip_position{0.27f, -0.31f, 0.64f, 1.0f};
    const math::vector4 world_h = clip_position *
        math::inverse(camera.view * camera.projection);
    const math::vector4 reprojected = world_h *
        (camera.view * camera.projection);
    Check(math::near_equal(
              math::vector3{reprojected.x / reprojected.w,
                            reprojected.y / reprojected.w,
                            reprojected.z / reprojected.w},
              math::vector3{clip_position.x, clip_position.y, clip_position.z},
              1.0e-3f),
          "camera clip-to-world unprojection round-trips through view-projection",
          failures);

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

    Check(Near(math::length(rig_forward), 1.0f) &&
              Near(math::length(rig_up), 1.0f) &&
              Near(math::dot(rig_forward, rig_up), 0.0f) &&
              math::near_equal(math::cross(rig_up, rig_forward),
                               math::normalize(rig_right_axis), epsilon) &&
              math::near_equal(
                  math::rotate(math::vector3::unit_z(), rig_yaw_rotation),
                  math::vector3{std::sin(rig_yaw), 0.0f, std::cos(rig_yaw)},
                  epsilon),
          "editor camera yaw-pitch basis is normalized and left-handed",
          failures);

    Check(math::near_equal(camera.view * camera.inverseView,
                           math::matrix4x4::identity(), 1.0e-3f) &&
              math::near_equal(camera.projection * camera.inverseProjection,
                               math::matrix4x4::identity(), 1.0e-3f),
          "FrameCameraSnapshot precomputed inverses close to identity", failures);
    Check(math::near_equal(
              math::transpose(camera.inverseView) * math::transpose(camera.view),
              math::matrix4x4::identity(), 1.0e-3f) &&
              math::near_equal(
                  math::transpose(camera.inverseProjection) *
                      math::transpose(camera.projection),
                  math::matrix4x4::identity(), 1.0e-3f),
          "decal inverse camera constants preserve transposed inverse order",
          failures);
    Check(math::near_equal(
              math::transpose(camera.view * camera.projection),
              math::transpose(camera.projection) * math::transpose(camera.view),
              epsilon),
          "decal, sprite, and GBuffer staging obeys transpose product order",
          failures);
    Check(math::near_equal(
              math::transpose(math::matrix4x4::identity()),
              math::matrix4x4::identity(), epsilon),
          "GBuffer empty bone palette fallback uploads identity", failures);

    Check(math::near_equal(camera.view.translation(),
                           math::vector3{camera.view.m[3][0],
                                         camera.view.m[3][1],
                                         camera.view.m[3][2]}, epsilon) &&
              math::near_equal(camera.view.right(),
                               math::vector3{camera.view.m[0][0],
                                             camera.view.m[0][1],
                                             camera.view.m[0][2]}, epsilon),
          "camera matrix accessors preserve explicit row-major layout", failures);

    const math::matrix4x4 orthographic =
        math::orthographic_lh(18.0f, 10.0f, camera.nearPlane, camera.farPlane);
    Check(math::near_equal(
              orthographic,
              ReferenceOrthographicLH(
                  18.0f, 10.0f, camera.nearPlane, camera.farPlane), epsilon),
          "FrameCameraSnapshot LH orthographic projection matches the scalar reference",
          failures);

    const math::bounding_frustum frustum =
        math::bounding_frustum_from_projection_lh(projection);
    const float expected_top_slope = std::tan(fov * 0.5f);
    const float expected_right_slope = expected_top_slope * aspect;
    Check(Near(frustum.right_slope, expected_right_slope) &&
              Near(frustum.left_slope, -expected_right_slope) &&
              Near(frustum.top_slope, expected_top_slope) &&
              Near(frustum.bottom_slope, -expected_top_slope) &&
              Near(frustum.near_plane, near_z) &&
              Near(frustum.far_plane, far_z, 1.0e-2f),
          "frustum fields recover the analytic LH perspective volume", failures);

    const auto corners = frustum.corners();
    const std::array<math::vector3, 8> expected_corners{
        math::vector3{-expected_right_slope * near_z,
                       expected_top_slope * near_z, near_z},
        math::vector3{ expected_right_slope * near_z,
                       expected_top_slope * near_z, near_z},
        math::vector3{ expected_right_slope * near_z,
                      -expected_top_slope * near_z, near_z},
        math::vector3{-expected_right_slope * near_z,
                      -expected_top_slope * near_z, near_z},
        math::vector3{-expected_right_slope * far_z,
                       expected_top_slope * far_z, far_z},
        math::vector3{ expected_right_slope * far_z,
                       expected_top_slope * far_z, far_z},
        math::vector3{ expected_right_slope * far_z,
                      -expected_top_slope * far_z, far_z},
        math::vector3{-expected_right_slope * far_z,
                      -expected_top_slope * far_z, far_z}};
    for (std::size_t i = 0; i < corners.size(); ++i)
    {
        Check(math::near_equal(corners[i], expected_corners[i], 1.0e-2f),
              "frustum corner order preserves the engine contract", failures);
    }

    const std::array<math::aabb, 2> frustum_boxes{
        math::aabb{math::vector3{0.0f, 0.0f, 5.0f},
                   math::vector3{0.5f, 0.5f, 0.5f}},
        math::aabb{math::vector3{100.0f, 100.0f, 5.0f},
                   math::vector3{0.5f, 0.5f, 0.5f}}};
    Check(math::intersects(frustum, frustum_boxes[0]) &&
              !math::intersects(frustum, frustum_boxes[1]),
          "frustum AABB intersection accepts inside and rejects outside",
          failures);

    const std::array<math::sphere, 2> frustum_spheres{
        math::sphere{math::vector3{0.0f, 0.0f, 5.0f}, 0.75f},
        math::sphere{math::vector3{-100.0f, 50.0f, 5.0f}, 0.75f}};
    Check(math::intersects(frustum, frustum_spheres[0]) &&
              !math::intersects(frustum, frustum_spheres[1]),
          "frustum sphere intersection accepts inside and rejects outside",
          failures);

    const math::quaternion camera_rotation =
        math::quaternion_from_pitch_yaw_roll(0.2f, -0.4f, 0.1f);
    const math::vector3 camera_translation{4.0f, -2.0f, 7.0f};
    const math::matrix4x4 frustum_world = math::compose(
        math::vector3{2.0f, 2.0f, 2.0f}, camera_rotation,
        camera_translation);
    const math::bounding_frustum world_frustum =
        math::transform(frustum, frustum_world);
    const auto world_corners = world_frustum.corners();
    for (std::size_t i = 0; i < world_corners.size(); ++i)
    {
        Check(math::near_equal(
                  world_corners[i],
                  math::transform_point(corners[i], frustum_world),
                  2.0e-2f),
              "transformed frustum corners preserve uniform affine transform",
              failures);
    }
    Check(math::near_equal(world_frustum.origin, camera_translation, epsilon) &&
              math::same_rotation(world_frustum.orientation, camera_rotation,
                                  epsilon) &&
              Near(world_frustum.near_plane, near_z * 2.0f) &&
              Near(world_frustum.far_plane, far_z * 2.0f, 1.0e-2f),
          "transformed frustum preserves pose and uniform distance scale",
          failures);

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

    const float expected_cct_yaw = cct_current_yaw +
        (cct_target_yaw - cct_current_yaw) * cct_rotation_t;
    Check(Near(cct_current_yaw, -0.6f, 1.0e-3f) &&
              math::same_rotation(
                  cct_rotation,
                  math::quaternion_from_pitch_yaw_roll(
                      0.0f, expected_cct_yaw, 0.0f),
                  1.0e-3f),
          "CCT yaw-only auto rotation preserves shortest-path interpolation",
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

    std::puts("[MATHEMATICS CONTRACT] passed: layout, numeric/property conventions, easing and tween manager.");
    return 0;
}
