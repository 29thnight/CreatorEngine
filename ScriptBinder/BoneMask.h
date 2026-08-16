#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"

class BoneMask
{
public:
   static consteval auto describe()
   {
       return meta::describe<BoneMask>(
           meta::member<&BoneMask::isEnabled>());
   }
	BoneMask() = default;
	std::string boneName;
	std::vector<BoneMask*> m_children;
	bool isEnabled = true;
};
