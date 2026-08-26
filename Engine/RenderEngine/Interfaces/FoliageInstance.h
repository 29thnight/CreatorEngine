#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Mathf.h"
#include <mathematics/transform.hpp>
#include <type_traits>

struct FoliageInstance
{
   public:
   static consteval auto reflect()
   {
       using Self = FoliageInstance;
       return meta::schema<Self>(
           meta::field<&Self::m_position>,
           meta::field<&Self::m_rotation>,
           meta::field<&Self::m_scale>,
           meta::field<&Self::m_foliageTypeID>);
   }
    Mathf::Vector3 m_position{};
    Mathf::Vector3 m_rotation{}; // Euler angles
    Mathf::Vector3 m_scale{ 1.f,1.f,1.f };
    uint32 m_foliageTypeID{ 0 }; // index of FoliageType
    bool m_isCulled{ false }; // whether this instance is culled or not
	math::matrix4x4 m_worldMatrix{ math::matrix4x4::identity() };

	// position/rotation/scale은 foliage asset의 정본이고 world는 런타임 파생값이다.
	// Euler 적용 순서는 기존 SimpleMath S*Rx*Ry*Rz*T 규약을 그대로 보존한다.
	void RebuildWorldMatrix() noexcept
	{
		const math::vector3 position{ m_position.x, m_position.y, m_position.z };
		const math::vector3 rotation{ m_rotation.x, m_rotation.y, m_rotation.z };
		const math::vector3 scale{ m_scale.x, m_scale.y, m_scale.z };
		m_worldMatrix = math::scaling_matrix(scale) *
			math::rotation_x(math::radians(rotation.x)) *
			math::rotation_y(math::radians(rotation.y)) *
			math::rotation_z(math::radians(rotation.z)) *
			math::translation_matrix(position);
	}

	FoliageInstance() = default;
	~FoliageInstance() = default;
};

static_assert(std::is_same_v<decltype(FoliageInstance::m_worldMatrix),
	math::matrix4x4>);
