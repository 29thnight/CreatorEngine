#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Mathf.h"

struct FoliageInstance
{
    Mathf::Vector3 m_position{};
    Mathf::Vector3 m_rotation{}; // Euler angles
    Mathf::Vector3 m_scale{ 1.f,1.f,1.f };
    uint32 m_foliageTypeID{ 0 }; // index of FoliageType
    bool m_isCulled{ false }; // whether this instance is culled or not
	Mathf::xMatrix m_worldMatrix{ Mathf::Matrix::Identity };

   static consteval auto describe()
   {
       return meta::describe<FoliageInstance>(
           meta::member<&FoliageInstance::m_position>(),
           meta::member<&FoliageInstance::m_rotation>(),
           meta::member<&FoliageInstance::m_scale>(),
           meta::member<&FoliageInstance::m_foliageTypeID>());
   }
	FoliageInstance() = default;
	~FoliageInstance() = default;
};
