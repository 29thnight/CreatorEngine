#pragma once
#include "Core.Minimal.h"
#include "AvatarMask.generated.h"
#include "BoneMask.h"

class Skeleton;
class Bone;
enum class BoneRegion;
class AvatarMask
{

public:
   ReflectAvatarMask
	[[Serializable]]
	AvatarMask() = default;

	//해당아바타가 해당 본 사용중인지
	bool IsBoneEnabled(BoneRegion region);
	void UseOnlyUpper() { useAll = false; useUpper = true;  useLower = false; }
	void UseOnlyLower() { useAll = false; useUpper = false; useLower = true; }

	bool IsBoneEnabled(const std::string& name);
	void MakeBoneMask(std::vector<Bone*> Bones);
	[[Property]]
	std::vector<BoneMask> m_BoneMasks;
	[[Property]]
	bool isHumanoid = false;
	[[Property]]
	bool useAll = true;
	[[Property]]
	bool useUpper = true;
	[[Property]]
	bool useLower = true;
};


