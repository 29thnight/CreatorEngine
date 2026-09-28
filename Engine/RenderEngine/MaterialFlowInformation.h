#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"
#include <mathematics/vector2.hpp>
#include <mathematics/vector4.hpp>
#include <cstddef>

cbuffer [[reflgen::reflect]] MaterialFlowInformation
{
   public:
	MaterialFlowInformation() = default;
	~MaterialFlowInformation() = default;

	math::vector4         m_windVector{ 0.f, 0.f, 0.f, 0.f };
	math::vector2         m_uvScroll{ 0.f, 0.f };
	[[reflgen::ignore]] math::vector2         padding{ 0.f, 0.f };
};

static_assert(sizeof(MaterialFlowInformation) == 32);
static_assert(offsetof(MaterialFlowInformation, m_windVector) == 0);
static_assert(offsetof(MaterialFlowInformation, m_uvScroll) == 16);
static_assert(offsetof(MaterialFlowInformation, padding) == 24);
