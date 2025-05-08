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

	//���� �ش�ƹ�Ÿ ���������
	bool IsBoneEnabled(BoneRegion region);
	void UseOnlyUpper() { isUpper = true;  isLower = false; }
	void UseOnlyLower() { isUpper = false; isLower = true; }
	[[Property]]
	bool isUpper = true;
	[[Property]]
	bool isLower = true;
};


