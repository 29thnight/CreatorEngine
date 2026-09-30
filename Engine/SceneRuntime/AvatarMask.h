#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"
#include "BoneMask.h"

enum class BoneRegion;
class [[reflgen::reflect]] AvatarMask
{
   public:

public:
	AvatarMask() = default;
	~AvatarMask();
	//해당아바타가 해당 본 사용중인지
	bool IsBoneEnabled(BoneRegion region);
	void UseOnlyUpper() { useAll = false; useUpper = true;  useLower = false; }
	void UseOnlyLower() { useAll = false; useUpper = false; useLower = true; }


	void ReCreateMask(AvatarMask* _otherMask);

	[[reflgen::ignore]]
	BoneMask* RootMask{ nullptr };

	bool IsBoneEnabled(const std::string& name);
	std::vector<BoneMask*> m_BoneMasks;
	bool isHumanoid = true; 
	bool useAll = false;
	bool useUpper = true;
	bool useLower = true;
};


