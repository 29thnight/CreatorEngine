#include "MaterialManager.h"
#include "pch.h"
#include "Material.h"
#include "MeshRenderer.h"

void MaterialManager::Start()
{
	SetGradationMaterials();
}

void MaterialManager::Update(float tick)
{
}

void MaterialManager::SetGradationMaterials()
{
	auto mat = GetOwner()->GetComponent<MeshRenderer>()->m_Material;

	mat->TrySetValue("TTBuffer", "pos1", &leftPosition, sizeof(float));
	mat->TrySetValue("TTBuffer", "pos2", &rightPosition, sizeof(float));
	mat->TrySetValue("TTBuffer", "color1", &leftColor, sizeof(Mathf::Vector3));
	mat->TrySetValue("TTBuffer", "color2", &rightColor, sizeof(Mathf::Vector3));
}

