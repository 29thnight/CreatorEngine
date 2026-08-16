#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"

cbuffer MaterialFlowInformation
{
   static consteval auto describe()
   {
       return meta::describe<MaterialFlowInformation>(
           meta::member<&MaterialFlowInformation::m_windVector>(),
           meta::member<&MaterialFlowInformation::m_uvScroll>());
   }
	MaterialFlowInformation() = default;
	~MaterialFlowInformation() = default;

	Mathf::Vector4        m_windVector{ 0.f, 0.f, 0.f, 0.f };
	Mathf::Vector2        m_uvScroll{ 0.f, 0.f };
	float2				  padding{ 0.f, 0.f };
};
