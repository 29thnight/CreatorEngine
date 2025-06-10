#pragma once
#include "Core.Minimal.h"
#include "AvatarMask.generated.h"

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
	[[Property]]
	bool useAll = true;
	[[Property]]
	bool useUpper = true;
	[[Property]]
	bool useLower = true;
};


