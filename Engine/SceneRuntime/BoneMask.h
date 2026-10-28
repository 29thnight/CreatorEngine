#include "../Containers/ContainerAPI.h"
#pragma once
#include "../Utility_Framework/Reflection/Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "../Utility_Framework/Foundation/Core.Minimal.h"

class [[reflgen::reflect]] BoneMask
{
   public:
public:
	BoneMask() = default;
	std::string boneName;

	[[reflgen::ignore]]
	ce::dynamic_array<BoneMask*> m_children;

	bool isEnabled = true;
	float weight = 1.f;
};
