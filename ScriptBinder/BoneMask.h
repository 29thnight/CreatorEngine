#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"

class BoneMask
{
   public:
   static consteval auto reflect()
   {
       using Self = BoneMask;
       return meta::schema<Self>(
           meta::field<&Self::isEnabled>);
   }
public:
	BoneMask() = default;
	std::string boneName;
	std::vector<BoneMask*> m_children;
	bool isEnabled = true;
};
