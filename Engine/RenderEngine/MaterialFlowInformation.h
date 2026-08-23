#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"

cbuffer MaterialFlowInformation
{
   public:
   static consteval auto reflect()
   {
       using Self = MaterialFlowInformation;
       return meta::schema<Self>(
           meta::field<&Self::m_windVector>,
           meta::field<&Self::m_uvScroll>);
   }
	MaterialFlowInformation() = default;
	~MaterialFlowInformation() = default;

	Mathf::Vector4        m_windVector{ 0.f, 0.f, 0.f, 0.f };
	Mathf::Vector2        m_uvScroll{ 0.f, 0.f };
	float2				  padding{ 0.f, 0.f };
};
