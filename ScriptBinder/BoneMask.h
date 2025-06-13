#pragma once
#include "Core.Minimal.h"
#include "BoneMask.generated.h"


class BoneMask
{
public:
   ReflectBoneMask
	[[Serializable]]
	BoneMask() = default;
	std::string boneName;
	[[Property]]
	bool isEnabled = true;
	std::vector<BoneMask*> m_children;
};
