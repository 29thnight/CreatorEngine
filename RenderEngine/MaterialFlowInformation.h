#pragma once
#include "Core.Minimal.h"
#include "MaterialFlowInformation.generated.h"

cbuffer MaterialFlowInformation
{
   ReflectMaterialFlowInformation
	[[Serializable]]
	MaterialFlowInformation() = default;
	~MaterialFlowInformation() = default;

	[[Property]]
	Mathf::Vector4        m_windVector{ 0.f, 0.f, 0.f, 0.f };
	[[Property]]
	Mathf::Vector2        m_uvScroll{ 0.f, 0.f };
	float2				  padding{ 0.f, 0.f };
};
