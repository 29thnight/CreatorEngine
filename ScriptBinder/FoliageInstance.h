#pragma once
#include "Core.Mathf.h"
#include "FoliageInstance.generated.h"

struct FoliageInstance
{
    [[Property]]
    Mathf::Vector3 m_position{};
    [[Property]]
    Mathf::Vector3 m_rotation{}; // Euler angles
    [[Property]]
    Mathf::Vector3 m_scale{ 1.f,1.f,1.f };
    [[Property]]
    uint32 m_foliageTypeID{ 0 }; // index of FoliageType
    bool m_isCulled{ false }; // whether this instance is culled or not

   ReflectFoliageInstance
    [[Serializable]]
	FoliageInstance() = default;
	~FoliageInstance() = default;
};
