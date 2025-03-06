#pragma once
#include "Core.Minimal.h"
#include "Skeleton.h"

class AnimationLoader
{
public:
	Animation LoadAnimation(aiAnimation* _pAnimation);
	NodeAnimation LoadNodeAnimation(aiNodeAnim* _pNodeAnim);
};

