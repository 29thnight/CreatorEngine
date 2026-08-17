#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"
#include "BoneMask.h"

class Skeleton;
class Bone;
enum class BoneRegion;
class AvatarMask
{
   public:
   static consteval auto reflect()
   {
       using Self = AvatarMask;
       return meta::schema<Self>(
           meta::field<&Self::m_BoneMasks>,
           meta::field<&Self::isHumanoid>,
           meta::field<&Self::useAll>,
           meta::field<&Self::useUpper>,
           meta::field<&Self::useLower>);
   }

public:
	AvatarMask() = default;
	~AvatarMask();
	//해당아바타가 해당 본 사용중인지
	bool IsBoneEnabled(BoneRegion region);
	void UseOnlyUpper() { useAll = false; useUpper = true;  useLower = false; }
	void UseOnlyLower() { useAll = false; useUpper = false; useLower = true; }


	void ReCreateMask(AvatarMask* _otherMask);
	BoneMask* RootMask{ nullptr };
	bool IsBoneEnabled(const std::string& name);
	BoneMask* MakeBoneMask(Bone* Bone);
	std::vector<BoneMask*> m_BoneMasks;
	bool isHumanoid = true; 
	bool useAll = false;
	bool useUpper = true;
	bool useLower = true;
};


