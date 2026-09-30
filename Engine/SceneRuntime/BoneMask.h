#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"

class [[reflgen::reflect]] BoneMask
{
   public:
public:
	BoneMask() = default;
	std::string boneName;

	[[reflgen::ignore]]
	std::vector<BoneMask*> m_children;

	bool isEnabled = true;
	float weight = 1.f;
};
