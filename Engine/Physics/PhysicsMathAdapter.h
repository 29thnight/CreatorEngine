#pragma once

#include <mathematics/quaternion.hpp>
#include <mathematics/vector3.hpp>

#include <physx/foundation/PxQuat.h>
#include <physx/foundation/PxTransform.h>
#include <physx/foundation/PxVec3.h>

#include <type_traits>

namespace PhysicsMath
{
	[[nodiscard]] inline physx::PxVec3 ToPx(const math::vector3& value) noexcept
	{
		return { value.x, value.y, value.z };
	}

	[[nodiscard]] inline math::vector3 FromPx(const physx::PxVec3& value) noexcept
	{
		return { value.x, value.y, value.z };
	}

	[[nodiscard]] inline physx::PxQuat ToPx(const math::quaternion& value) noexcept
	{
		return { value.x, value.y, value.z, value.w };
	}

	[[nodiscard]] inline math::quaternion FromPx(const physx::PxQuat& value) noexcept
	{
		return { value.x, value.y, value.z, value.w };
	}

	[[nodiscard]] inline physx::PxTransform ToPxTransform(
		const math::vector3& position,
		const math::quaternion& rotation) noexcept
	{
		return { ToPx(position), ToPx(rotation) };
	}

	inline void FromPxTransform(
		const physx::PxTransform& transform,
		math::vector3& position,
		math::quaternion& rotation) noexcept
	{
		position = FromPx(transform.p);
		rotation = FromPx(transform.q);
	}
}

static_assert(sizeof(math::vector3) == sizeof(float) * 3);
static_assert(sizeof(math::quaternion) == sizeof(float) * 4);
static_assert(std::is_trivially_copyable_v<math::vector3>);
static_assert(std::is_trivially_copyable_v<math::quaternion>);
