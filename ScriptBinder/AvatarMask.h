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

	//본이 해당아바타 사용중인지
	bool IsBoneEnabled(BoneRegion region);
	void UseOnlyUpper() { isUpper = true;  isLower = false; }
	void UseOnlyLower() { isUpper = false; isLower = true; }
	[[Property]]
	bool isUpper = true;
	[[Property]]
	bool isLower = true;
};


