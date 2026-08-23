#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Mathf.h"

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
	Mathf::xMatrix m_worldMatrix{ Mathf::Matrix::Identity };

	FoliageInstance() = default;
	~FoliageInstance() = default;
};
