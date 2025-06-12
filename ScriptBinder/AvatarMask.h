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
	~AvatarMask();
	//�ش�ƹ�Ÿ�� �ش� �� ���������
	bool IsBoneEnabled(BoneRegion region);
	void UseOnlyUpper() { useAll = false; useUpper = true;  useLower = false; }
	void UseOnlyLower() { useAll = false; useUpper = false; useLower = true; }

	BoneMask* RootMask{ nullptr };
	bool IsBoneEnabled(const std::string& name);
	BoneMask* MakeBoneMask(Bone* Bone);
	[[Property]]
	std::vector<BoneMask*> m_BoneMasks;
	[[Property]]
	bool isHumanoid = false;  //����ü �и��� ���Ÿ� üũ &&&&&
	[[Property]]
	bool useAll = true;
	[[Property]]
	bool useUpper = true;
	[[Property]]
	bool useLower = true;
};


