#pragma once
#include "Core.Minimal.h"


struct BoneMask
{
	std::string boneName;
	bool isEnabled = true;
	std::vector<BoneMask*> m_children;
};