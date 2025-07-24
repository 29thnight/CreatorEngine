#pragma once
#include "Core.Minimal.h"
#include "Mesh.h"
#include "Material.h"

struct FoliageType
{
    Mesh* m_mesh{ nullptr };
    Material* m_material{ nullptr };
    bool m_castShadow{ true };
	std::string m_modelName{};
};

struct FoliageInstance
{
    Mathf::Vector3 m_position{};
    Mathf::Vector3 m_rotation{}; // Euler angles
    Mathf::Vector3 m_scale{ 1.f,1.f,1.f };
    uint32 m_foliageTypeID{ 0 }; // index of FoliageType
};