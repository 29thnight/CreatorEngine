#pragma once
#include "Core.Minimal.h"
#include "Skeleton.h"

class AnimationLoader
{
public:
	std::optional<Animation> LoadAnimation(aiAnimation* _pAnimation);
	std::optional<NodeAnimation> LoadNodeAnimation(aiNodeAnim* _pNodeAnim);
};

