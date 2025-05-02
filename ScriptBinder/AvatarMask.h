#pragma once
#include "Core.Minimal.h"
#include "../RenderEngine/Skeleton.h"
#include "AvatarMask.generated.h"
class Skeleton;
class Bone;
class AvatarMask
{

public:
   ReflectAvatarMask
	[[Serializable]]
	AvatarMask() = default;

	[[Property]]
	bool isupper = true;
};


